#ifndef MD_HPP
#define MD_HPP

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <cstdint>
#include <cstdio>

#if defined(_MSC_VER)    // for _mm_mfence
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

	// OJ 算力模拟器 (全局单例)
	class OJTimer {
	public:
		static OJTimer& getInstance();
		void doCalibrate(int bindCore = -1);
		double getSpeedFactor() const;
		double toOJTime(double localTimeMs) const;
	private:
		OJTimer() : speedFactor(1.0) {}
		double speedFactor;
		static constexpr long long STANDARD_OPS = 100'000'000;
	};

} // namespace myd

#endif