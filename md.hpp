#ifndef MD_HPP
#define MD_HPP

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <cstdint>
#include <cstdio>

#if defined(_MSC_VER)    // for _mm_mfence / sqrtf
#include <intrin.h>      // MSVC
#else
#include <x86intrin.h>   // GCC/MinGW
#endif

static_assert(sizeof(void*) == 8 && sizeof(DWORD_PTR) == 8,
			  "This library requires 64-bit Windows");

namespace myd {

	// 用户态 CPU 计时器
	class CPUTimer {
	public:
		CPUTimer() : start_tsc(0), elapsed_cpu_ms(0.0), freq_ns(0.0) {}
		void mark();
		double getCPUTimeMs() const { return elapsed_cpu_ms; }
	private:
		uint64_t start_tsc;
		double elapsed_cpu_ms;
		double freq_ns;
		void calibrate();
	};

	// 内存峰值统计器 (Private Bytes)
	class MemTracker {
	public:
		MemTracker() : peak_private_bytes(0) {}
		void mark();
		size_t getPeakPrivateBytes() const { return peak_private_bytes; }
	private:
		size_t peak_private_bytes;
		static size_t getCurrentPrivateBytes();
	};

	// CPU 亲和性管理
	class CoreBinder {
	public:
		explicit CoreBinder(int coreID = -1);
		~CoreBinder();
		CoreBinder(const CoreBinder&) = delete;
		CoreBinder& operator=(const CoreBinder&) = delete;
	private:
		DWORD_PTR oldMask;
		bool bound;
	};

	// ==================== OJ 归一化计时器 (E1: 离线指令锚定 + 多域几何平均) ====================
	// 方案说明:
	//   1. 内核基准固定, 每域的"每轮动态指令数"在开发期用 llvm-objdump 静态推导后硬编码为
	//      常量 kInstPerIter[4]。同一发布版二进制 ≡ 同一指令数, 与运行所在 CPU 完全无关,
	//      这正是"指令数 × 固定参考 IPC"的落地形态。
	//   2. 本地测量: 绑核 + GetThreadTimes(单线程用户态 CPU 时间), 避免 QPC 墙钟与判题
	//      GetProcessTimes 之间的单位错配。
	//   3. 参考刻度: refIPS = refIPC(单核吞吐指令/周期) × refGHz(主频) × 1e9, GUI 可配置。
	//   4. 因子: factor_d = ips_d / refIPS; 综合 = 4 域几何平均。toOJTime(t) = t × 综合因子。

	enum class OJDomain : int { IntALU = 0, FloatMath = 1, MemOps = 2, Branch = 3 };

	// 校准结果
	struct OJTimerResult {
		double speedFactor  = 1.0;             // 综合因子 (4 域几何平均)
		double refIPS       = 0.0;             // 参考机每秒指令数
		double domainFactor[4] = {1.0,1.0,1.0,1.0}; // 各域因子
		double domainIPS[4]    = {0.0,0.0,0.0,0.0}; // 各域实测每秒指令
		double domainCPUMs[4]  = {0.0,0.0,0.0,0.0}; // 各域采用的(最优)本地用户态CPU时间
		double calibWallMs  = 0.0;             // 校准总耗时(墙钟)
		bool   stable       = false;           // 是否得到有效因子
	};

	// 默认参考机: 3.0 IPC × 3.0 GHz = 9e9 指令/秒 (GUI 可改)
	inline constexpr double kDefaultRefIPC  = 3.0;
	inline constexpr double kDefaultRefGHz  = 3.0;

	// 离线锚定: 每循环轮数的动态机器指令数
	// (发布版二进制经 llvm-objdump 静态反汇编测得, 与 CPU 无关, 勿随意修改)
	//   IntALU   6.5   (O2 展开 ×4,   26 条指令/4轮)
	//   FloatMath 5.5  (O2 展开 ×4,   22 条指令/4轮)
	//   MemOps   8.0   (O2 展开 ×2,   16 条指令/2轮)
	//   Branch   10.0  (含真实不可预测条件跳转, 未展开)
	inline constexpr double kInstPerIter[4] = {
		6.5,   // IntALU
		5.5,   // FloatMath
		8.0,   // MemOps
		10.0,  // Branch
	};

	// 绑核校准的目标时长档位 (ms): 多时长交叉, 取最优(每指令耗时最小)消除 turbo 抖动
	inline constexpr double kCalibTargetMs[3] = { 80.0, 160.0, 320.0 };

	class OJTimer {
	public:
		static OJTimer& getInstance();

		// 校准: 返回完整结果并缓存综合因子
		// bindCore<0 表示不绑核; refIPC/refGHz 定义参考机
		OJTimerResult doCalibrate(int bindCore, double refIPC, double refGHz);

		double getSpeedFactor() const { return speedFactor; }
		double getRefIPS() const { return refIPS; }
		double toOJTime(double localTimeMs) const;

		// 4 域内核 (对 selftest 开放, 固定负载、无异常、无 asm)
		static uint64_t runIntALU(uint64_t iters);
		static uint64_t runFloatMath(uint64_t iters);
		static uint64_t runMemOps(uint64_t iters);
		static uint64_t runBranch(uint64_t iters);

	private:
		OJTimer() : speedFactor(1.0), refIPS(kDefaultRefIPC * kDefaultRefGHz * 1e9) {}
		double speedFactor;
		double refIPS;

		// 运行域 d 的 iters 轮, 返回单线程用户态 CPU 时间(ms)
		static double runDomainCPUMs(int d, uint64_t iters);
	};

} // namespace myd

#endif