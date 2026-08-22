#include "md.hpp"
#include <windows.h>
#include <psapi.h>
#include <immintrin.h>
#include <cstdlib>
#include <cmath>
#include <thread>

namespace myd {

#if defined(_MSC_VER)
#define MYD_NOINLINE __declspec(noinline)
#else
#define MYD_NOINLINE __attribute__((noinline))
#endif

// ==================== CPUTimer ====================
void CPUTimer::calibrate() {
    LARGE_INTEGER qpc_freq, qpc_start, qpc_end;
    QueryPerformanceFrequency(&qpc_freq);
    uint64_t tsc_start = __rdtsc();
    QueryPerformanceCounter(&qpc_start);
    LARGE_INTEGER qpc_target;
    qpc_target.QuadPart = qpc_start.QuadPart + (qpc_freq.QuadPart / 20);
    do { QueryPerformanceCounter(&qpc_end); } while (qpc_end.QuadPart < qpc_target.QuadPart);
    uint64_t tsc_end = __rdtsc();
    double elapsed_ns = (double)(qpc_end.QuadPart - qpc_start.QuadPart) / qpc_freq.QuadPart * 1e9;
    freq_ns = elapsed_ns / (tsc_end - tsc_start);
}

void CPUTimer::mark() {
    if (start_tsc == 0) {
        calibrate();
        start_tsc = __rdtsc();
        elapsed_cpu_ms = 0.0;
    } else {
        uint64_t end_tsc = __rdtsc();
        elapsed_cpu_ms = (end_tsc - start_tsc) * freq_ns / 1e6;
        start_tsc = end_tsc;
    }
}

// ==================== MemTracker ====================
size_t MemTracker::getCurrentPrivateBytes() {
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc))) {
        return pmc.PrivateUsage;
    }
    return 0;
}

void MemTracker::mark() {
    size_t current = getCurrentPrivateBytes();
    if (current > peak_private_bytes) peak_private_bytes = current;
}

// ==================== CoreBinder ====================
CoreBinder::CoreBinder(int coreID) : oldMask(0), bound(false) {
    if (coreID < 0 || coreID > 63) return;
    DWORD_PTR newMask = 1ULL << coreID;
    oldMask = SetThreadAffinityMask(GetCurrentThread(), newMask);
    bound = (oldMask != 0);
}
CoreBinder::~CoreBinder() { if (bound && oldMask) SetThreadAffinityMask(GetCurrentThread(), oldMask); }

// ==================== OJTimer: 4 域固定内核 ====================
// 每个内核都是"固定源码、单线程、无异常、无内联汇编"的 tight loop，
// 保证任何机器、任何次运行执行完全相同的机器指令序列。
// 每轮指令数常量 kInstPerIter[] 由发布二进制的循环体反汇编静态推导。

MYD_NOINLINE uint64_t OJTimer::runIntALU(uint64_t iters) {
    uint64_t v = 123456789ULL;
    volatile uint64_t sink = 0;
    for (uint64_t i = 0; i < iters; ++i) {
        v = v * 6364136223846793005ULL + 1442695040888963407ULL;
        v ^= (v >> 33);
        sink = v;
    }
    (void)sink;
    return iters;
}

MYD_NOINLINE uint64_t OJTimer::runFloatMath(uint64_t iters) {
    float x = 1.000001f, y = 0.999999f;
    volatile float sink = 0.0f;
    for (uint64_t i = 0; i < iters; ++i) {
        x = x * 1.000001f + y;
        y = y * 0.999999f;
        __m128 a = _mm_set_ss(x);
        a = _mm_sqrt_ss(a);
        x = _mm_cvtss_f32(a);
        sink = x;
    }
    (void)sink;
    return iters;
}

MYD_NOINLINE uint64_t OJTimer::runMemOps(uint64_t iters) {
    static volatile unsigned char s_buf[1u << 20]; // 1MB 缓存不友好
    const size_t mask = (1u << 20) - 1;
    size_t idx = 0;
    volatile unsigned char sink = 0;
    for (uint64_t i = 0; i < iters; ++i) {
        idx = (idx + 64) & mask;
        unsigned char v = (unsigned char)(s_buf[idx] + (idx >> 8));
        s_buf[idx] = v;
        sink = v;
    }
    (void)sink;
    return iters;
}

MYD_NOINLINE uint64_t OJTimer::runBranch(uint64_t iters) {
    static volatile unsigned char s_data[1u << 20]; // 预填伪随机使分支不可预测
    static volatile unsigned char s_taken = 0;
    static bool s_seeded = false;
    if (!s_seeded) {
        uint32_t s = 0x9E3779B9u;
        for (size_t i = 0; i < (1u << 20); ++i) {
            s = s * 1664525u + 1013904223u;
            s_data[i] = (unsigned char)(s >> 24);
        }
        s_seeded = true;
    }
    const size_t mask = (1u << 20) - 1;
    size_t idx = 0;
    uint64_t acc = 0;
    volatile uint64_t sink = 0;
    for (uint64_t i = 0; i < iters; ++i) {
        idx = (idx + 8) & mask;
        unsigned char c = s_data[idx];
        // 必须保留真实条件跳转: else 分支带有副作用(volatile 写), 编译器无法用 cmov 替代
        if (c < 128)
            acc += c;
        else {
            acc ^= c;
            s_taken ^= 1;
        }
        sink = acc;
    }
    (void)sink;
    (void)s_taken;
    return iters;
}

// ==================== OJTimer: 计时与校准 ====================
OJTimer& OJTimer::getInstance() {
    static OJTimer instance;
    return instance;
}

// 单线程用户态 CPU 时间 (ms)。与判题链路 GetProcessTimes 的用户态口径一致，
// 且校准与计分都使用"该线程真正在核心上执行"的时间，杜绝 QPC 墙钟单位错配。
static double threadUserCPUMs() {
    FILETIME c, e, k, u;
    GetThreadTimes(GetCurrentThread(), &c, &e, &k, &u);
    ULARGE_INTEGER ul;
    ul.LowPart = u.dwLowDateTime;
    ul.HighPart = u.dwHighDateTime;
    return ul.QuadPart / 10000.0;
}

double OJTimer::runDomainCPUMs(int d, uint64_t iters) {
    const double t0 = threadUserCPUMs();
    switch (static_cast<OJDomain>(d)) {
        case OJDomain::IntALU:    runIntALU(iters);    break;
        case OJDomain::FloatMath: runFloatMath(iters); break;
        case OJDomain::MemOps:    runMemOps(iters);    break;
        case OJDomain::Branch:    runBranch(iters);    break;
    }
    const double t1 = threadUserCPUMs();
    return t1 - t0;
}

OJTimerResult OJTimer::doCalibrate(int bindCore, double refIPC, double refGHz) {
    OJTimerResult res;
    // 先给 res.stable 赋值并由后续失败分支清零
    res.stable = true;

    if (refIPC <= 0.0) refIPC = kDefaultRefIPC;
    if (refGHz <= 0.0) refGHz = kDefaultRefGHz;
    res.refIPS = refIPC * refGHz * 1e9;   // 必须先于域循环, 因子计算依赖它
    refIPS = res.refIPS;

    const double instPerIter[4] = {
        (double)kInstPerIter[static_cast<int>(OJDomain::IntALU)],
        (double)kInstPerIter[static_cast<int>(OJDomain::FloatMath)],
        (double)kInstPerIter[static_cast<int>(OJDomain::MemOps)],
        (double)kInstPerIter[static_cast<int>(OJDomain::Branch)]
    };

    LARGE_INTEGER wf, wt1, wt2;
    QueryPerformanceFrequency(&wf);
    QueryPerformanceCounter(&wt1);

    {
        CoreBinder binder(bindCore);

        for (int d = 0; d < 4; ++d) {
            // 预热 (冷缓存/分支预测器/首次页缺失不纳入统计)
            runDomainCPUMs(d, 8192);

            // 逐步放大轮数, 保证计时超过系统时钟节拍 (GetThreadTimes 以节拍更新)
            uint64_t iters = 1u << 20;
            double ms = runDomainCPUMs(d, iters);
            int guard = 0;
            while (ms < 30.0 && guard++ < 12 && iters < (1ull << 40)) {
                iters *= 4;
                ms = runDomainCPUMs(d, iters);
            }
            if (!(ms > 0.0)) {
                res.stable = false;
                continue;
            }
            double itersPerMs = (double)iters / ms;

            double bestIPS = 0.0;
            double bestMs = 0.0;
            for (double target : kCalibTargetMs) {
                uint64_t it = (uint64_t)(itersPerMs * target) + 8;
                if (it < 64) it = 64;
                double m = runDomainCPUMs(d, it);
                if (m < 20.0) {   // 单次测量分辨率兜底 (至少跨一个时钟节拍)
                    it *= 8;
                    m = runDomainCPUMs(d, it);
                }
                if (!(m > 0.0) || instPerIter[d] <= 0.0) continue;
                double ips = ((double)it * instPerIter[d]) * 1000.0 / m;
                if (ips > bestIPS) { bestIPS = ips; bestMs = m; }
            }

            res.domainIPS[d]   = bestIPS;
            res.domainCPUMs[d] = bestMs;
            res.domainFactor[d] = (res.refIPS > 0.0) ? bestIPS / res.refIPS : 1.0;
            if (!(res.domainFactor[d] > 0.0)) res.stable = false;
        }
    }

double g = 1.0;
	int valid = 0;
	for (int d = 0; d < 4; ++d) {
		if (res.domainFactor[d] > 0.0 && std::isfinite(res.domainFactor[d])) {
			g *= res.domainFactor[d];
			++valid;
		}
	}
	double gmean = (valid > 0) ? pow(g, 1.0 / (double)valid) : 1.0;
	if (!(gmean > 0.0) || !std::isfinite(gmean)) gmean = 1.0;
	res.speedFactor = gmean;
	speedFactor = gmean;
	if (valid < 4) res.stable = false;   // 任一域失败即视为校准不稳定

    QueryPerformanceCounter(&wt2);
    res.calibWallMs = (wt2.QuadPart - wt1.QuadPart) * 1000.0 / (double)wf.QuadPart;

    // 测量当前实际频率 (100ms 采样)
    {
        LARGE_INTEGER t1, t2;
        QueryPerformanceCounter(&t1);
        uint64_t tsc1 = __rdtsc();
        Sleep(120);
        uint64_t tsc2 = __rdtsc();
        QueryPerformanceCounter(&t2);
        double sec = (double)(t2.QuadPart - t1.QuadPart) / (double)wf.QuadPart;
        if (sec > 0.0) res.actualGHz = (tsc2 - tsc1) / sec / 1.0e9;
    }

    return res;
}

double OJTimer::toOJTime(double localTimeMs) const {
    return (localTimeMs < 0.0 || speedFactor <= 0.0) ? localTimeMs : localTimeMs * speedFactor;
}

} // namespace myd