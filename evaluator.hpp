#ifndef EVALUATOR_HPP
#define EVALUATOR_HPP

#include <QString>
#include <QTemporaryDir>
#include <cstddef>

// ==================== 编译+运行+判题核心声明 ====================
class EvaluatorCore {
  public:
	explicit EvaluatorCore(const QString &exeDir);

	// 解析可用的 C++ 编译器（优先用户工具链，回退 PATH 中 g++）
	QString resolveCompiler();

	// 编译 srcPath 到临时目录，比对/放置 .in/.out，产出 <baseName>.exe
	bool compile(const QString &srcPath, const QString &flags, QString &output,
				 const QString &outputBaseName = QString());

	// 用 wrapper 运行 source.exe，测量用户态 CPU 时间与峰值内存，并比对答案 (AC/WA)
	bool run(int core, double &cpuTimeMs, size_t &peakMem, QString &output);

  private:
	QString m_exeDir;
	QString m_testDir;
	QTemporaryDir m_tempDir;
};

#endif // EVALUATOR_HPP