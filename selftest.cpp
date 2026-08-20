#include "selftest.hpp"

#include "evaluator.hpp"
#include "md.hpp"

#include <QDir>
#include <QFileInfo>
#include <QProcess>

namespace selftest {

namespace {

QString verdictFromRunOut(const QString &runOut) {
	if (runOut.contains("答案正确 (AC)")) return "AC";
	if (runOut.contains("答案错误 (WA)")) return "WA";
	return "RUN?";
}

// 判定是否达到预期：
//  - CompileError 期望编译失败 (CE)
//  - MemTest 期望 AC 且峰值内存 > 100MB（验证 JobObject 峰值测量）
//  - TimeTest 期望 AC 且 CPU 时间 > 10ms（验证计时链路）
//  - 其余用例期望正确运行并给出 AC/WA
bool isExpected(const QString &name, const QString &verdict, double cpu, size_t mem) {
	if (name == "CompileError")
		return verdict == "CE";
	if (name == "MemTest")
		return verdict == "AC" && mem > 100LL * 1024 * 1024;
	if (name == "TimeTest")
		return verdict == "AC" && cpu > 10.0;
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

	myd::OJTimer::getInstance().doCalibrate();

	QStringList folders = rootDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
	folders.sort();

	QString header;
	header += "========== 评测器可用性自测 ==========\n";
	header += "工具链: " + compiler + "\n";
	header += "版本: " + ver.split("\n").first() + "\n";
	header += QString("速度因子: %1\n\n").arg(
		myd::OJTimer::getInstance().getSpeedFactor(), 0, 'f', 3);
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
		QString comOut;
		bool compileOk = core.compile(src, flags, comOut, "source");

		QString line;
		QString verdict;
		double cpu = 0; size_t mem = 0;

		if (!compileOk) {
			line += QString("用例 %1 编译失败\n").arg(name);
			line += comOut + "\n";
			verdict = "CE";
		} else {
			QString runOut;
			bool runOk = core.run(-1, cpu, mem, runOut);
			verdict = verdictFromRunOut(runOut);
			QString status = runOk ? verdict : "RUNFAIL";
			QString fmt = QString("用例 %1  CPU=%2ms  内存=%3MB  -> %4\n")
							  .arg(name)
							  .arg(cpu, 0, 'f', 3)
							  .arg(mem / (1024.0 * 1024.0), 0, 'f', 2)
							  .arg(status);
			line += fmt;
			if (!runOk || verdict == "RUN?") {
				line += runOut.trimmed() + "\n";
			}
		}

		if (isExpected(name, verdict, cpu, mem)) {
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