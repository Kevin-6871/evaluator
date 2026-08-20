#include "md.hpp"
#include <windows.h>
#include <psapi.h>
#include <cstdlib>
#include <cmath>

namespace myd {

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

// ==================== OJTimer ====================
OJTimer& OJTimer::getInstance() {
    static OJTimer instance;
    return instance;
}

void OJTimer::doCalibrate(int bindCore) {
    CoreBinder binder(bindCore);
    long long counter = 0;

    LARGE_INTEGER freq, t1, t2;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t1);

    for (long long i = 0; i < STANDARD_OPS; ++i) {
        counter += 1;
        _mm_mfence();   // 阻止编译器优化掉循环
    }

    QueryPerformanceCounter(&t2);
    double avg = (t2.QuadPart - t1.QuadPart) * 1000.0 / freq.QuadPart;

    // 写入文件以防优化
    FILE* f = fopen("calibration_dump.txt", "w");
    if (f) { fprintf(f, "%lld", counter); fclose(f); }

    if (avg > 0.0 && avg < 100000.0) {
        speedFactor = 1000.0 / avg;
    } else {
        speedFactor = 1.0;
    }
}

double OJTimer::getSpeedFactor() const { return speedFactor; }

double OJTimer::toOJTime(double localTimeMs) const {
    return (localTimeMs < 0.0 || speedFactor <= 0.0) ? localTimeMs : localTimeMs * speedFactor;
}

} // namespace myd