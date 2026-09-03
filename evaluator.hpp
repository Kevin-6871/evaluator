#ifndef EVALUATOR_HPP
#define EVALUATOR_HPP

#include <QString>
#include <QStringList>
#include <QVector>
#include <QTemporaryDir>
#include <cstddef>

// ==================== 判题结论定义 ====================
// 双时限模型 (DOMjudge 思路):
//   软时限(CPU): 看门狗轮询 GetProcessTimes 用户态时间, 到点终结 → TLE_CPU
//   硬时限(墙钟): wrapper 墙钟超时 → TerminateJobObject → TLE_WALL (防休眠/IO 卡死)
//   内存限制: 运行后比对峰值专用内存 (PeakJobMemoryUsed) > 限制 → MLE
namespace JudgeVerdict {
enum Verdict {
	OK       = 0,  // 正常完成 (尚未比对答案)
	TLE_CPU  = 1,  // 用户态CPU时间超过软时限
	TLE_WALL = 2,  // 墙钟超过硬时限
	RUN_ERR  = 3,  // 运行层错误 (wrapper 启动失败等)
	MLE      = 4,  // 峰值专用内存超过内存限制
	AC       = 5,  // 答案正确 (比对通过)
	WA       = 6,  // 答案错误 / 缺少输出 / 缺少答案
	CE       = 7,  // 编译错误
};

// 判词 → 表格展示用短标签
QString toString(int verdict);
}

// ==================== 单组测试结果 ====================
struct TestCaseResult {
	int    index         = 0;    // 组号 (1..N)
	QString inputFile;           // 输入文件名 (如 aaa-1.in / aaa_2.in)
	QString ansFile;             // 答案文件名 (如 aaa-1.out / aaa_3.out)
	double cpuTimeMs     = 0.0;  // 本地用户态 CPU 时间 (ms)
	double ojTimeMs      = 0.0;  // 折算 OJ 预估用时 (ms) = cpu × 速度因子
	double wallTimeMs    = 0.0;  // 墙钟用时 (ms)
	size_t peakMemBytes  = 0;    // 峰值专用内存 (bytes)
	int    verdict       = JudgeVerdict::OK; // JudgeVerdict
};

// ==================== 编译+运行+判题核心声明 ====================
class EvaluatorCore {
  public:
	explicit EvaluatorCore(const QString &exeDir);

	// 解析可用的 C++ 编译器（优先用户工具链，回退 PATH 中 g++）
	QString resolveCompiler();

	// 编译 srcPath 到临时目录，产出 <baseName>.exe。
	// 编译前会把复制到临时目录的源码里的文件流名改写为与源文件同名:
	//   aaa.cpp 内 freopen("source.in", ...) → freopen("aaa.in", ...)
	//   aaa.cpp 内 freopen("source.out", ...) → freopen("aaa.out", ...)
	// (当 baseName == "source" 时改写为空操作, 保持旧单样例流程兼容)
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

	// ---------- 多组评测 (新) ----------
	// 探测 <base>-N.in / <base>_N.in 与 <base>-N.out / <base>_N.out (N=1..100)。
	// inputFiles/ansFiles 依次存放每组输入/答案的绝对路径; 若某组只有输入没有答案,
	// 对应 ansFiles 为空字符串。返回找到的组数。
	int findTestGroups(const QString &srcPath, QStringList &inputFiles, QStringList &ansFiles) const;

	// 完整多组评测:
	//   1. 编译 (自动把源码文件流改写为与源文件同名)
	//   2. 探测测试组 (aaa-1.in / aaa_2.out, 最多 100 组)
	//   3. 逐组: 复制 aaa-N.in → aaa.in, aaa-N.out → aaa.ans,
	//            运行被评测程序 (读 aaa.in, 写 aaa.out), 与 aaa.ans 比对
	//   4. 汇总成表格输出到 tableText, 每组成绩写入 results
	// compileOut 返回编译过程/结果文本; 返回 false 表示编译失败 (CE)。
	bool evaluateGroups(const QString &srcPath, const QString &flags, int core,
						double ojLimitMs, double languageFactor, double wallScale,
						size_t memLimitMB,
						QString &compileOut, QString &tableText,
						QVector<TestCaseResult> &results);

  private:
	// 编译 wrapper (带缓存: 同一实例参数不变时只编译一次)
	bool ensureWrapper(int core, double cpuLimitMs, double wallLimitMs,
					   const QString &exeName, QString &output);

	// 运行 <exeBase>.exe (须已 ensureWrapper), 测量 CPU/墙钟/峰值内存, 返回 wrapper 层判词 (0..4)
	bool runPrepared(const QString &exeBase, size_t memLimitMB,
					 double &cpuTimeMs, double &wallTimeMs, size_t &peakMem,
					 QString &output, int &verdict);

	// 把 destCpp 内 "source.in"/"source.out" 字符串改写为 "<baseName>.in"/"<baseName>.out"
	static void rewriteSourceStreams(const QString &destCpp, const QString &baseName);

	QString m_exeDir;
	QString m_testDir;
	QTemporaryDir m_tempDir;

	// wrapper 缓存
	bool   m_wrapperReady;
	int    m_wrapperCore;
	double m_wrapperCpuLimitMs;
	double m_wrapperWallLimitMs;
	QString m_wrapperExeName;
};

#endif // EVALUATOR_HPP