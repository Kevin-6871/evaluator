#ifndef EVALUATOR_HPP
#define EVALUATOR_HPP

#include <QString>
#include <QTemporaryDir>
#include <cstddef>

// ==================== 判题结论定义 ====================
// 双时限模型 (DOMjudge 思路):
//   软时限(CPU): JobObject 用户态CPU时间限制, 到点系统自动终止 → TLE_CPU
//   硬时限(墙钟): wrapper 等待超时 → TerminateJobObject → TLE_WALL (防休眠/IO 卡死)
namespace JudgeVerdict {
enum Verdict {
	OK       = 0,  // 正常完成
	TLE_CPU  = 1,  // 用户态CPU时间超过软时限
	TLE_WALL = 2,  // 墙钟超过硬时限
	RUN_ERR  = 3,  // 运行层错误 (wrapper 启动失败等)
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
	// 内部把 OJ 时限换算成本地 CPU 限额: localLimit = ojLimit × languageFactor / speedFactor
	bool run(int core, double ojLimitMs, double languageFactor, double wallScale,
			 double &cpuTimeMs, size_t &peakMem, QString &output, int &verdict);

  private:
	QString m_exeDir;
	QString m_testDir;
	QTemporaryDir m_tempDir;
};

#endif // EVALUATOR_HPP