#include "selftest.hpp"

#include "evaluator.hpp"
#include "md.hpp"

#include <QDir>
#include <QFileInfo>
#include <QProcess>

namespace selftest {

namespace {

QString verdictFromRunOut(const QString &runOut) {
	// 时限/内存超限优先于答案正确性 (同真实 OJ 判定优先级)
	if (runOut.contains("TLE-CPU")) return "TLE_CPU";
	if (runOut.contains("TLE-WALL")) return "TLE_WALL";
	if (runOut.contains("内存超限 (MLE)")) return "MLE";
	if (runOut.contains("答案正确 (AC)")) return "AC";
	if (runOut.contains("答案错误 (WA)")) return "WA";
	return "RUN?";
}

// 判定是否达到预期：
//  - CompileError 期望编译失败 (CE)
//  - MemTest 期望 AC 且峰值内存 > 100MB（验证 JobObject 峰值测量）
//  - TimeTest 期望 AC 且 CPU 时间 > 10ms（验证计时链路）
//  - TimeLimit 期望死循环被 CPU 软时限看门狗终止 (TLE-CPU)
//  - SleepTest 期望休眠程序被墙钟硬时限终止 (TLE-WALL)
//  - MemLimit 期望峰值内存超过限制被判定 MLE
//  - 其余用例期望正确运行并给出 AC/WA
bool isExpected(const QString &name, const QString &verdict, double cpu, size_t mem, bool multiGroup) {
	if (name == "CompileError")
		return verdict == "CE";
	if (name == "MemTest")
		return verdict == "AC" && mem > 100LL * 1024 * 1024;
	if (name == "TimeTest")
		return verdict == "AC" && cpu > 10.0;
	if (name == "TimeLimit")
		return verdict == "TLE_CPU";
	if (name == "SleepTest")
		return verdict == "TLE_WALL";
	if (name == "MemLimit")
		return verdict == "MLE";
	// 多组数据目录: 默认期望全部 AC; 名字带 WrongAns 时期望存在非 AC 组
	if (multiGroup)
		return name.contains("WrongAns", Qt::CaseInsensitive) ? (verdict == "NONAC") : (verdict == "AC");
	return verdict == "AC" || verdict == "WA";
}

} // anonymous namespace

int runAll(const QString &root, QString &report, int &casesFound) {
	casesFound = 0;
	report.clear();

	QDir rootDir(root);
	if (!rootDir.exists()) {
		report += QString("自测目录不存在: %1\n").arg(root);
		return 0;
	}

	QString compiler;
	{
		EvaluatorCore probe(rootDir.absolutePath());
		compiler = probe.resolveCompiler();
	}

	QProcess chk;
	chk.start(compiler, QStringList() << "--version");
	if (!chk.waitForFinished(5000)) {
		report += QString("工具链不可用: %1 (确保路径存在或加入 PATH)\n").arg(compiler);
		return 0;
	}
	QString ver = QString::fromLocal8Bit(chk.readAllStandardOutput());
	if (ver.isEmpty() || !ver.contains("version")) {
		report += QString("工具链未返回版本信息: %1\n").arg(compiler);
		return 0;
	}

	myd::OJTimerResult calib = myd::OJTimer::getInstance()
		.doCalibrate(-1, myd::kDefaultRefIPC, myd::kDefaultRefGHz);

	QStringList folders = rootDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
	folders.sort();

	QString header;
	header += "========== 评测器可用性自测 ==========\n";
	header += "工具链: " + compiler + "\n";
	header += "版本: " + ver.split("\n").first() + "\n";
	header += QString("参考机: %1 IPC × %2 GHz = %3 指令/秒\n")
		.arg(myd::kDefaultRefIPC, 0, 'f', 2).arg(myd::kDefaultRefGHz, 0, 'f', 2)
		.arg(calib.refIPS, 0, 'f', 2);
	static const char* const domName[4] = { "整型", "浮点", "内存", "分支" };
	for (int d = 0; d < 4; ++d) {
		header += QString("  %1: IPS=%2e9  因子=%3\n")
			.arg(domName[d])
			.arg(calib.domainIPS[d] / 1e9, 0, 'f', 4)
			.arg(calib.domainFactor[d], 0, 'f', 4);
	}
	header += QString("综合速度因子: %1\n").arg(calib.speedFactor, 0, 'f', 4);
	header += QString("校准耗时: %1 ms\n\n").arg(calib.calibWallMs, 0, 'f', 0);
	report += header;

	int pass = 0;
	int total = 0;
	for (const QString &name : folders) {
		QString folder = rootDir.filePath(name);
		QDir fdir(folder);
		QStringList cpps = fdir.entryList({ "*.cpp" }, QDir::Files);
		if (cpps.isEmpty()) continue;

		total++;
		casesFound++;
		QString src = fdir.filePath(cpps.first());
		QString flags = "-std=c++17 -O2 -Wall";

		EvaluatorCore core(folder + "/");
		QString line;
		QString verdict;
		double cpu = 0; double wall = 0; size_t mem = 0;

		// 多组数据目录 (base-N.in / base_N.in, N=1..100): 走新评测管线
		QStringList ins, ans;
		const int nGroups = core.findTestGroups(src, ins, ans);
		const bool multiGroup = (nGroups > 0);
		if (multiGroup) {
			QString comOut, tableOut;
			QVector<TestCaseResult> results;
			bool evalOk = core.evaluateGroups(src, flags, -1, 1000.0, 1.0, 1.5, 512,
											  comOut, tableOut, results);
			line += QString("用例 %1 (多组测试点 %2 个)\n").arg(name).arg(nGroups);
			if (!evalOk) {
				line += comOut + "\n";
				verdict = "CE";
			} else {
				line += comOut;
				line += tableOut;
				int ac = 0;
				for (const TestCaseResult &r : results) {
					if (r.verdict == JudgeVerdict::AC) ac++;
					cpu = qMax(cpu, r.cpuTimeMs);
					mem = qMax(mem, r.peakMemBytes);
				}
				verdict = (ac == results.size() && !results.isEmpty()) ? "AC" : "NONAC";
			}
		} else {
			QString comOut;
			bool compileOk = core.compile(src, flags, comOut, "source");
			if (!compileOk) {
				line += QString("用例 %1 编译失败\n").arg(name);
				line += comOut + "\n";
				verdict = "CE";
			} else {
				QString runOut;
				size_t memLimitMB = (name == "MemLimit") ? 100 : 512;
				int v = JudgeVerdict::RUN_ERR;
				bool runOk = core.run(-1, 1000.0, 1.0, 1.5, memLimitMB,
					cpu, wall, mem, runOut, v);
				verdict = verdictFromRunOut(runOut);
				QString status = runOk ? verdict : "RUNFAIL";
				QString fmt = QString("用例 %1  CPU=%2ms(E) 墙钟=%3ms  内存=%4MB  判读=%5 -> %6\n")
							  .arg(name)
							  .arg(cpu, 0, 'f', 3)
							  .arg(wall, 0, 'f', 3)
							  .arg(mem / (1024.0 * 1024.0), 0, 'f', 2)
							  .arg(v)
							  .arg(status);
				line += fmt;
				if (!runOk || verdict == "RUN?") {
					line += runOut.trimmed() + "\n";
				}
			}
		}

		if (isExpected(name, verdict, cpu, mem, multiGroup)) {
			pass++;
			line += QString("     → 通过\n");
		} else {
			line += QString("     → 未达到预期\n");
		}
		report += line;
	}

	report += QString("\n========== 自测结果: %1/%2 通过 ==========\n").arg(pass).arg(total);
	return pass;
}

} // namespace selftest