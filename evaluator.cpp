#include "evaluator.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTextStream>

#include "md.hpp"

// 工具链候选路径：优先用户环境的 llvm-mingw，找不到时回退到 PATH 中的 g++
#ifdef Q_OS_WIN
static const char* const COMPILER_CANDIDATES[] = {
	"C:/Qt/Tools/llvm-mingw1706_64/bin/g++.exe",
	"C:/Qt/Tools/mingw1310_64/bin/g++.exe",
	"C:/Qt/Tools/mingw810_64/bin/g++.exe",
};
#endif

EvaluatorCore::EvaluatorCore(const QString &exeDir)
	: m_exeDir(exeDir), m_tempDir("evaluator_") {
	if (m_tempDir.isValid()) {
		m_testDir = m_tempDir.path() + "/";
	} else {
		m_testDir = QDir::tempPath() + "/evaluator_fallback/";
		QDir().mkpath(m_testDir);
	}
}

QString EvaluatorCore::resolveCompiler() {
#ifdef Q_OS_WIN
	for (const char* cand : COMPILER_CANDIDATES) {
		if (QFileInfo::exists(cand)) return QString::fromLocal8Bit(cand);
	}
#endif
	return QString("g++");
}

bool EvaluatorCore::compile(const QString &srcPath, const QString &flags,
							QString &output, const QString &outputBaseName) {
	QFileInfo fi(srcPath);
	QString baseName = outputBaseName.isEmpty() ? fi.baseName() : outputBaseName;

	QString destCpp = m_testDir + baseName + ".cpp";

	if (QFileInfo(srcPath).canonicalFilePath() != QFileInfo(destCpp).canonicalFilePath()) {
		QFile::remove(destCpp);
		QFile::copy(srcPath, destCpp);
	}

	if (baseName == "source") {
		QString dir = fi.absolutePath();
		QString userBaseName = fi.baseName();
		QFile::copy(dir + "/" + userBaseName + ".in", m_testDir + "source.in");
		QFile::copy(dir + "/" + userBaseName + ".out", m_testDir + "source.ans");
	}

	QString destExe = m_testDir + baseName + ".exe";
	QStringList args;
	if (!flags.isEmpty()) {
		args << flags.split(" ", Qt::SkipEmptyParts);
	}
	args << destCpp;
	args << "-o";
	args << destExe;

	QString compiler = resolveCompiler();
	output += "编译命令: " + compiler + " " + args.join(" ") + "\n";

	QProcess proc;
	proc.setWorkingDirectory(m_testDir);
	proc.start(compiler, args);
	proc.waitForFinished(-1);
	QString stdOut = proc.readAllStandardOutput();
	QString stdErr = proc.readAllStandardError();
	output += stdOut + stdErr;

	return (proc.exitCode() == 0);
}

bool EvaluatorCore::run(int core, double ojLimitMs, double languageFactor, double wallScale,
						double &cpuTimeMs, size_t &peakMem, QString &output, int &verdict) {
	verdict = JudgeVerdict::RUN_ERR;

	// OJ 时限 → 本地 CPU 限额: 快机(因子>1)本地限额小, 慢机限额大
	double speedFactor = 1.0;
	{
		const myd::OJTimer &tj = myd::OJTimer::getInstance();
		double f = tj.getSpeedFactor();
		if (f > 0.0) speedFactor = f;
	}
	const double localCpuLimitMs = (ojLimitMs <= 0.0)
		? 0.0
		: (ojLimitMs * languageFactor / speedFactor);
	const double wallLimitMs = (localCpuLimitMs <= 0.0)
		? 0.0
		: (localCpuLimitMs * (wallScale > 0.0 ? wallScale : 1.0));

	QString wrapperCode = QString(R"(
#include <windows.h>
#include <cstdio>
int main() {
	int core = %1;
	double cpuLimitMs = %2;
	double wallLimitMs = %3;

	STARTUPINFO si = { sizeof(si) };
	PROCESS_INFORMATION pi = {};
	char cmd[] = "source.exe";

	HANDLE job = CreateJobObjectW(NULL, NULL);
	JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli = {};
	jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
	if (job)
		SetInformationJobObject(job, JobObjectExtendedLimitInformation, &jeli, sizeof(jeli));

	DWORD flags = (core >= 0) ? CREATE_SUSPENDED : 0;
	if (!CreateProcess(NULL, cmd, NULL, NULL, FALSE, flags, NULL, NULL, &si, &pi)) {
		FILE* f = fopen("time_result.txt", "w");
		if (f) { fprintf(f, "0.000 0.000 0 3"); fclose(f); }
		if (job) CloseHandle(job);
		return 1;
	}
	if (job) AssignProcessToJobObject(job, pi.hProcess);
	if (core >= 0) {
		SetThreadAffinityMask(pi.hThread, 1ULL << core);
		ResumeThread(pi.hThread);
	}
	CloseHandle(pi.hThread);

	// ---- 双时限执行 ----
	// 软时限(TLE_CPU): 看门狗轮询 GetProcessTimes 用户态时间(与计分同源时钟), 超限终结
	// 硬时限(TLE_WALL): 墙钟爬满 wallLimitMs(软限×比例)终结, 防休眠/IO 卡死
	int verdict = 0;
	if (cpuLimitMs <= 0.0) {
		DWORD hardMs = (wallLimitMs > 0.0) ? (DWORD)wallLimitMs : INFINITE;
		if (WaitForSingleObject(pi.hProcess, hardMs) == WAIT_TIMEOUT) {
			if (job) TerminateJobObject(job, 1);
			WaitForSingleObject(pi.hProcess, INFINITE);
			verdict = 2;   // TLE_WALL
		}
	} else {
		DWORD wallStart = GetTickCount();
		bool done = false;
		while (!done) {
			DWORD elapsed = GetTickCount() - wallStart;
			if (wallLimitMs > 0.0 && (double)elapsed >= wallLimitMs) {
				if (job) TerminateJobObject(job, 1);
				WaitForSingleObject(pi.hProcess, INFINITE);
				verdict = 2;   // TLE_WALL
				done = true;
				break;
			}
			DWORD remain = (wallLimitMs > 0.0 && wallLimitMs > (double)elapsed)
				? (DWORD)(wallLimitMs - elapsed) : 25;
			DWORD slice = (remain > 25) ? 25 : (remain < 5 ? 5 : remain);
			if (WaitForSingleObject(pi.hProcess, slice) == WAIT_OBJECT_0) {
				done = true;   // 正常退出
				break;
			}
			FILETIME tc, te, tk, tu;
			GetProcessTimes(pi.hProcess, &tc, &te, &tk, &tu);
			ULARGE_INTEGER ul;
			ul.LowPart = tu.dwLowDateTime;
			ul.HighPart = tu.dwHighDateTime;
			if ((double)ul.QuadPart / 10000.0 >= cpuLimitMs) {
				if (job) TerminateJobObject(job, 1);
				WaitForSingleObject(pi.hProcess, INFINITE);
				verdict = 1;   // TLE_CPU
				done = true;
			}
		}
	}

	FILETIME c, e, k, u;
	GetProcessTimes(pi.hProcess, &c, &e, &k, &u);
	ULARGE_INTEGER tr;
	tr.LowPart = u.dwLowDateTime;
	tr.HighPart = u.dwHighDateTime;
	double t = tr.QuadPart / 10000.0;
	CloseHandle(pi.hProcess);

	unsigned long long m = 0;
	if (job) {
		JOBOBJECT_EXTENDED_LIMIT_INFORMATION jinfo = {};
		if (QueryInformationJobObject(job, JobObjectExtendedLimitInformation,
				&jinfo, sizeof(jinfo), NULL))
			m = (unsigned long long)jinfo.PeakJobMemoryUsed;
		CloseHandle(job);
	}

	FILE* f = fopen("time_result.txt", "w");
	if (f) {
		fprintf(f, "%.3f %.3f %llu %d", t, t, m, verdict);
		fclose(f);
	}
	return 0;
}
)").arg(core)
		.arg(QString::number(localCpuLimitMs, 'f', 3))
		.arg(QString::number(wallLimitMs, 'f', 3));

	QFile wrapperFile(m_testDir + "wrapper.cpp");
	if (!wrapperFile.open(QIODevice::WriteOnly | QIODevice::Text))
		return false;
	QTextStream out(&wrapperFile);
	out << wrapperCode;
	wrapperFile.close();

	QString compileOut;
	if (!compile(m_testDir + "wrapper.cpp", QString(), compileOut, "wrapper")) {
		output += "包装程序编译失败:\n" + compileOut;
		return false;
	}

	QProcess proc;
	proc.setWorkingDirectory(m_testDir);
	proc.setStandardOutputFile(QProcess::nullDevice());
	proc.setStandardErrorFile(QProcess::nullDevice());
	proc.start(m_testDir + "wrapper.exe");

	// 【核心调试】捕获底层运行失败的原因
	if (!proc.waitForFinished(-1)) {
		QString errorInfo = QString("QProcess::ProcessError: %1\n详细错误描述: %2")
								.arg(proc.error())
								.arg(proc.errorString());
		QString stderrMsg = QString::fromLocal8Bit(proc.readAllStandardError());

		output += "运行评测进程失败 (子进程启动失败或中断)。\n";
		output += "系统报告: " + errorInfo + "\n";
		if (!stderrMsg.isEmpty()) {
			output += "程序底层报错信息 (stderr):\n" + stderrMsg + "\n";
		}
		return false;
	}

	if (proc.exitCode() != 0) {
		output += "评测子进程 (wrapper.exe) 异常退出，退出码: " + QString::number(proc.exitCode()) + "\n";
		QString stderrMsg = QString::fromLocal8Bit(proc.readAllStandardError());
		if (!stderrMsg.isEmpty()) {
			output += "程序报错信息 (stderr):\n" + stderrMsg + "\n";
		}
		return false;
	}

	QFile resultFile(m_testDir + "time_result.txt");
	if (!resultFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
		output += "错误：找不到 time_result.txt，评测可能因为路径或环境中断。\n";
		QString stderrMsg = QString::fromLocal8Bit(proc.readAllStandardError());
		if (!stderrMsg.isEmpty()) {
			output += "程序底层输出:\n" + stderrMsg + "\n";
		}
		return false;
	}
	QTextStream in(&resultFile);
	double t = 0; double wallT = 0;
	unsigned long long m = 0; int v = 0;
	in >> t >> wallT >> m >> v;
	cpuTimeMs = t;
	peakMem = (size_t)m;
	verdict = (v >= JudgeVerdict::OK && v <= JudgeVerdict::RUN_ERR) ? v : JudgeVerdict::RUN_ERR;
	resultFile.close();

	if (verdict == JudgeVerdict::TLE_CPU)
		output += "时间超限 (TLE-CPU)\n";
	else if (verdict == JudgeVerdict::TLE_WALL)
		output += "时间超限 (TLE-WALL)\n";

	QFile outFile(m_testDir + "source.out");
	QFile ansFile(m_testDir + "source.ans");
	if (!outFile.exists()) {
		output += "警告：程序未生成输出文件\n";
		return true;
	}
	if (!ansFile.exists()) {
		output += "警告：缺少标准答案文件\n";
		return true;
	}
	if (outFile.open(QIODevice::ReadOnly | QIODevice::Text) &&
		ansFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
		QTextStream outStream(&outFile);
		QTextStream ansStream(&ansFile);
		QString outText = outStream.readAll().trimmed();
		QString ansText = ansStream.readAll().trimmed();
		if (outText == ansText)
			output += "答案正确 (AC)\n";
		else
			output += "答案错误 (WA)\n";
	}

	return true;
}