#ifndef EVALUATOR_HPP
#define EVALUATOR_HPP

#include <QString>
#include <QTemporaryDir>
#include <cstddef>

// ==================== 判题结论定义 ====================
// 双时限模型 (DOMjudge 思路):
//   软时限(CPU): 看门狗轮询 GetProcessTimes 用户态时间, 到点终结 → TLE_CPU
//   硬时限(墙钟): wrapper 墙钟超时 → TerminateJobObject → TLE_WALL (防休眠/IO 卡死)
//   内存限制: 运行后比对峰值专用内存 (PeakJobMemoryUsed) > 限制 → MLE
namespace JudgeVerdict {
enum Verdict {
	OK       = 0,  // 正常完成
	TLE_CPU  = 1,  // 用户态CPU时间超过软时限
	TLE_WALL = 2,  // 墙钟超过硬时限
	RUN_ERR  = 3,  // 运行层错误 (wrapper 启动失败等)
	MLE      = 4,  // 峰值专用内存超过内存限制
};
}

// ==================== 编译+运行+判题核心声明 ====================
class EvaluatorCore {
  public:
	explicit EvaluatorCore(const QString &exeDir);

	// 解析可用的 C++ 编译器（优先用户工具链，回退 PATH 中 g++）
	QString resolveCompiler();

	// 编译 srcPath 到临时目录，比对/放置 .in/.out，产出 <baseName>.exe
	bool compile(const QString &srcPath, const QString &flags, QString &output,
				 const QString &outputBaseName = QString());

	// 用 wrapper 运行 source.exe，测量用户态 CPU 时间与峰值内存，并比对答案 (AC/WA)。
	// 双时限:
	//   ojLimitMs       = OJ 标准时限 (参考机刻度, ms)
	//   languageFactor  = 语言因子 (C++ = 1.0)
	//   wallScale       = 硬时限 / 软时限 (默认 1.5)
	//   memLimitMB      = 内存限制 (MB); >0 时峰值专用内存超过即 MLE
	// 内部把 OJ 时限换算成本地 CPU 限额: localLimit = ojLimit × languageFactor / speedFactor
	// 输出: cpuTimeMs=用户态CPU时间(ms), wallTimeMs=墙钟时长(ms), peakMem=峰值专用内存
	bool run(int core, double ojLimitMs, double languageFactor, double wallScale,
			 size_t memLimitMB,
			 double &cpuTimeMs, double &wallTimeMs, size_t &peakMem, QString &output, int &verdict);

  private:
	QString m_exeDir;
	QString m_testDir;
	QTemporaryDir m_tempDir;
};

#endif // EVALUATOR_HPP