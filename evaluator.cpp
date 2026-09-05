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

// ==================== 判词 → 短标签 ====================
namespace JudgeVerdict {
QString toString(int verdict) {
switch (verdict) {
case JudgeVerdict::AC:       return "AC";
case JudgeVerdict::WA:       return "WA";
case JudgeVerdict::TLE_CPU:  return "TLE-CPU";
case JudgeVerdict::TLE_WALL: return "TLE-WALL";
case JudgeVerdict::MLE:      return "MLE";
case JudgeVerdict::RUN_ERR:  return "RE";
case JudgeVerdict::CE:       return "CE";
case JudgeVerdict::OK:
default:                     return "OK";
}
}
}

// ==================== 表格自适应宽度工具 ====================
namespace {

// 计算字符串在等宽字体下的显示列宽 (CJK/全角字符按 2 列计)
int displayWidth(const QString &s) {
int w = 0;
for (const QChar &c : s) {
const ushort u = c.unicode();
if ((u >= 0x1100 && u <= 0x115F) ||
    (u >= 0x2E80 && u <= 0x303E) ||
    (u >= 0x3041 && u <= 0x33FF) ||
    (u >= 0x3400 && u <= 0x4DBF) ||
    (u >= 0x4E00 && u <= 0x9FFF) ||
    (u >= 0xA000 && u <= 0xA4CF) ||
    (u >= 0xAC00 && u <= 0xD7A3) ||
    (u >= 0xF900 && u <= 0xFAFF) ||
    (u >= 0xFE30 && u <= 0xFE4F) ||
    (u >= 0xFF00 && u <= 0xFF60) ||
    (u >= 0xFFE0 && u <= 0xFFE6))
    w += 2;
else
    w += 1;
}
return w;
}

// 把字符串填充到指定显示宽度 (rightAlign=true 右对齐, 否则左对齐)
QString padTo(const QString &s, int width, bool rightAlign) {
int w = displayWidth(s);
if (w >= width) return s;
const QString pad(width - w, QLatin1Char(' '));
return rightAlign ? pad + s : s + pad;
}

// 生成自适应宽度的评测结果表格
QString buildResultTable(const QVector<TestCaseResult> &results) {
const int cols = 8;
QStringList header;
header << "组号" << "输入文件" << "答案文件" << "CPU(ms)" << "OJ(ms)" << "墙钟(ms)" << "内存(MB)" << "结果";
const bool rightAlign[cols] = { true, false, false, true, true, true, true, false };

QVector<QStringList> grid;
grid.append(header);
for (const TestCaseResult &r : results) {
QStringList row;
row << QString::number(r.index)
    << r.inputFile
    << r.ansFile
    << QString::number(r.cpuTimeMs, 'f', 3)
    << QString::number(r.ojTimeMs, 'f', 3)
    << QString::number(r.wallTimeMs, 'f', 3)
    << QString::number(r.peakMemBytes / (1024.0 * 1024.0), 'f', 2)
    << JudgeVerdict::toString(r.verdict);
grid.append(row);
}

QVector<int> widths(cols, 2);
for (int c = 0; c < cols; ++c) {
int w = 2;
for (const QStringList &row : grid)
    w = qMax(w, displayWidth(row[c]));
widths[c] = w;
}

QString out;
for (int i = 0; i < grid.size(); ++i) {
const QStringList &row = grid[i];
QString line;
for (int c = 0; c < cols; ++c) {
    if (c > 0) line += " | ";
    line += padTo(row[c], widths[c], rightAlign[c]);
}
out += line + "\n";
if (i == 0) {
    QString sep;
    for (int c = 0; c < cols; ++c) {
        if (c > 0) sep += "-+-";
        sep += QString(widths[c], QLatin1Char('-'));
    }
    out += sep + "\n";
}
}
QString sep;
for (int c = 0; c < cols; ++c) {
if (c > 0) sep += "-+-";
sep += QString(widths[c], QLatin1Char('-'));
}
out += sep + "\n";
return out;
}

} // anonymous namespace

EvaluatorCore::EvaluatorCore(const QString &exeDir)
: m_exeDir(exeDir), m_tempDir("evaluator_"),
  m_wrapperReady(false), m_wrapperCore(-2),
  m_wrapperCpuLimitMs(-1.0), m_wrapperWallLimitMs(-1.0) {
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

// ==================== 源码文件流改写 ====================
// aaa.cpp 内 freopen("source.in", ...) → freopen("aaa.in", ...)
//            freopen("source.out", ...) → freopen("aaa.out", ...)
void EvaluatorCore::rewriteSourceStreams(const QString &destCpp, const QString &baseName) {
QFile f(destCpp);
if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return;
QString text = QString::fromUtf8(f.readAll());
f.close();

const QString newIn  = "\"" + baseName + ".in\"";
const QString newOut = "\"" + baseName + ".out\"";
bool changed = false;
if (text.contains("\"source.in\""))  { text.replace("\"source.in\"", newIn);  changed = true; }
if (text.contains("\"source.out\"")) { text.replace("\"source.out\"", newOut); changed = true; }
if (!changed) return;

QFile w(destCpp);
if (!w.open(QIODevice::WriteOnly | QIODevice::Text)) return;
w.write(text.toUtf8());
w.close();
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

// 把被评测程序内部的文件流名改为与文件名相同 (source.in/out → baseName.in/out)
rewriteSourceStreams(destCpp, baseName);

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
// ==================== wrapper 编译 (带缓存) ====================
bool EvaluatorCore::ensureWrapper(int core, double cpuLimitMs, double wallLimitMs, size_t memLimitBytes,
  const QString &exeName, QString &output) {
if (m_wrapperReady && m_wrapperCore == core &&
m_wrapperCpuLimitMs == cpuLimitMs && m_wrapperWallLimitMs == wallLimitMs &&
m_wrapperMemLimitBytes == memLimitBytes && m_wrapperExeName == exeName) {
return true;
}

QString wrapperCode = QString(R"(
#include <windows.h>
#include <cstdio>
int main() {
int core = %1;
double cpuLimitMs = %2;
double wallLimitMs = %3;
unsigned long long memLimitBytes = %4;
char cmd[] = "%5";

STARTUPINFO si = { sizeof(si) };
PROCESS_INFORMATION pi = {};
HANDLE job = CreateJobObjectW(NULL, NULL);
JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli = {};
jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;

// CPU 软限 → 进程级用户态时间硬限 (零开销, OS 到点即杀)
if (cpuLimitMs > 0.0) {
    jeli.BasicLimitInformation.LimitFlags |= JOB_OBJECT_LIMIT_PROCESS_TIME;
    jeli.BasicLimitInformation.PerProcessUserTimeLimit.QuadPart = (long long)(cpuLimitMs * 10000.0);
}
// 内存硬杀线 (零开销, OS 级提交内存上限)
if (memLimitBytes > 0) {
    jeli.BasicLimitInformation.LimitFlags |= JOB_OBJECT_LIMIT_JOB_MEMORY;
    jeli.JobMemoryLimit = (SIZE_T)memLimitBytes;
}
BOOL setOk = FALSE;
if (job)
    setOk = SetInformationJobObject(job, JobObjectExtendedLimitInformation, &jeli, sizeof(jeli));

DWORD flags = CREATE_SUSPENDED;
if (!CreateProcess(NULL, cmd, NULL, NULL, FALSE, flags, NULL, NULL, &si, &pi)) {
    FILE* f = fopen("time_result.txt", "w");
    if (f) { fprintf(f, "0.000 0.000 0 3"); fclose(f); }
    if (job) CloseHandle(job);
    return 1;
}

BOOL asgOk = FALSE;
if (job) asgOk = AssignProcessToJobObject(job, pi.hProcess);
if (core >= 0) {
    SetThreadAffinityMask(pi.hThread, 1ULL << core);
}
ResumeThread(pi.hThread);
CloseHandle(pi.hThread);

// ---- 零开销执行 ----
// TLE_CPU: JOB_OBJECT_LIMIT_PROCESS_TIME 由 OS 到点终止进程 (无轮询)
// TLE_WALL: 单次阻塞等待墙钟硬限 (防休眠/IO 卡死)
// 内存:   JOB_OBJECT_LIMIT_JOB_MEMORY 由 OS 限制提交内存 (无轮询)
DWORD runStart = GetTickCount();
int verdict = 0;
DWORD hardMs = (wallLimitMs > 0.0) ? (DWORD)wallLimitMs : INFINITE;
if (WaitForSingleObject(pi.hProcess, hardMs) == WAIT_TIMEOUT) {
    if (job) TerminateJobObject(job, 1);
    WaitForSingleObject(pi.hProcess, INFINITE);
    verdict = 2;
}

FILETIME c, e, k, u;
GetProcessTimes(pi.hProcess, &c, &e, &k, &u);
ULARGE_INTEGER tr;
tr.LowPart = u.dwLowDateTime;
tr.HighPart = u.dwHighDateTime;
double t = tr.QuadPart / 10000.0;
if (cpuLimitMs > 0.0 && t >= cpuLimitMs) {
    verdict = 1;   // TLE_CPU 优先: 进程用户态已超 CPU 软限 (即使墙钟先到)
}
double wallMs = (double)(DWORD)(GetTickCount() - runStart);
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
    fprintf(f, "%.3f %.3f %llu %d", t, wallMs, m, verdict);
    fclose(f);
}
return 0;
}
)").arg(core)
.arg(QString::number(cpuLimitMs, 'f', 3))
.arg(QString::number(wallLimitMs, 'f', 3))
.arg((qulonglong)memLimitBytes)
.arg(exeName);

QFile wrapperFile(m_testDir + "wrapper.cpp");
if (!wrapperFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
output += "无法写入包装程序源文件\n";
return false;
}
QTextStream out(&wrapperFile);
out << wrapperCode;
wrapperFile.close();

QString compileOut;
if (!compile(m_testDir + "wrapper.cpp", QString(), compileOut, "wrapper")) {
output += "包装程序编译失败:\n" + compileOut;
return false;
}

m_wrapperReady = true;
m_wrapperCore = core;
m_wrapperCpuLimitMs = cpuLimitMs;
m_wrapperWallLimitMs = wallLimitMs;
m_wrapperMemLimitBytes = memLimitBytes;
m_wrapperExeName = exeName;
return true;
}
// ==================== 运行 <exeBase>.exe 并测量 ====================
bool EvaluatorCore::runPrepared(const QString &exeBase, size_t memSoftMB,
double &cpuTimeMs, double &wallTimeMs, size_t &peakMem,
QString &output, int &verdict) {
verdict = JudgeVerdict::RUN_ERR;
wallTimeMs = 0.0;

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

output += "运行评测进程失败 (子进程启动失败或中断).\n";
output += "系统报告: " + errorInfo + "\n";
if (!stderrMsg.isEmpty()) {
output += "程序底层报错信息 (stderr):\n" + stderrMsg + "\n";
}
return false;
}

if (proc.exitCode() != 0) {
output += "评测子进程 (wrapper.exe) 异常退出, 退出码: " + QString::number(proc.exitCode()) + "\n";
QString stderrMsg = QString::fromLocal8Bit(proc.readAllStandardError());
if (!stderrMsg.isEmpty()) {
output += "程序报错信息 (stderr):\n" + stderrMsg + "\n";
}
return false;
}

QFile resultFile(m_testDir + "time_result.txt");
if (!resultFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
output += "错误: 找不到 time_result.txt, 评测可能因为路径或环境中断.\n";
QString stderrMsg = QString::fromLocal8Bit(proc.readAllStandardError());
if (!stderrMsg.isEmpty()) {
output += "程序底层输出:\n" + stderrMsg + "\n";
}
return false;
}
QTextStream in(&resultFile);
double t = 0; double wall = 0;
unsigned long long m = 0; int v = 0;
in >> t >> wall >> m >> v;
cpuTimeMs = t;
wallTimeMs = wall;
peakMem = (size_t)m;
verdict = (v >= JudgeVerdict::OK && v <= JudgeVerdict::RUN_ERR) ? v : JudgeVerdict::RUN_ERR;
resultFile.close();

// 内存限制 (测量口径): 峰值专用内存超过限制即 MLE
const size_t memLimitBytes = (memSoftMB > 0) ? (size_t)memSoftMB * 1024ull * 1024ull : 0;
if (memLimitBytes > 0 && peakMem > memLimitBytes && verdict == JudgeVerdict::OK)
verdict = JudgeVerdict::MLE;

if (verdict == JudgeVerdict::TLE_CPU)
output += "时间超限 (TLE-CPU)\n";
else if (verdict == JudgeVerdict::TLE_WALL)
output += "时间超限 (TLE-WALL)\n";
else if (verdict == JudgeVerdict::MLE)
output += "内存超限 (MLE)\n";
else if (verdict == JudgeVerdict::RUN_ERR)
output += "运行错误 (RE)\n";

return true;
}
// ==================== 旧单样例接口 (保持兼容, 供 selftest 使用) ====================
bool EvaluatorCore::run(int core, const JudgeLimits &limits,
double &cpuTimeMs, double &wallTimeMs, size_t &peakMem,
QString &output, int &verdict) {
verdict = JudgeVerdict::RUN_ERR;
wallTimeMs = 0.0;

// OJ 时限 → 本地 CPU 限额: 快机(因子>1)本地限额小, 慢机限额大
double speedFactor = 1.0;
{
const myd::OJTimer &tj = myd::OJTimer::getInstance();
double f = tj.getSpeedFactor();
if (f > 0.0) speedFactor = f;
}
const double localCpuLimitMs = (limits.ojLimitMs <= 0.0)
? 0.0
: (limits.ojLimitMs / speedFactor);
const double wallLimitMs = (localCpuLimitMs <= 0.0)
? 0.0
: (localCpuLimitMs * (limits.tleHardScale > 0.0 ? limits.tleHardScale : 1.0));
const size_t memHardBytes = (limits.memLimitMB > 0)
? (size_t)((double)limits.memLimitMB * 1024.0 * 1024.0 * (limits.memHardScale > 0.0 ? limits.memHardScale : 1.0)) : 0;

if (!ensureWrapper(core, localCpuLimitMs, wallLimitMs, memHardBytes, QStringLiteral("source.exe"), output))
return false;

if (!runPrepared(QStringLiteral("source"), limits.memLimitMB,
 cpuTimeMs, wallTimeMs, peakMem, output, verdict))
return false;

QFile outFile(m_testDir + "source.out");
QFile ansFile(m_testDir + "source.ans");
if (!outFile.exists()) {
output += "警告: 程序未生成输出文件\n";
return true;
}
if (!ansFile.exists()) {
output += "警告: 缺少标准答案文件\n";
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
// ==================== 多组探测 ====================
// 探测 <base>-N.in / <base>_N.in 与 <base>-N.out / <base>_N.out, N=1..100
int EvaluatorCore::findTestGroups(const QString &srcPath,
  QStringList &inputFiles, QStringList &ansFiles) const {
inputFiles.clear();
ansFiles.clear();

QFileInfo fi(srcPath);
const QString base = fi.baseName();
const QString dir = fi.absolutePath();

for (int i = 1; i <= 100; ++i) {
const QString num = QString::number(i);
QString inPath;
// 输入: aaa-i.in 或 aaa_i.in
{
const QString cand1 = dir + "/" + base + "-" + num + ".in";
const QString cand2 = dir + "/" + base + "_" + num + ".in";
if (QFileInfo::exists(cand1)) inPath = cand1;
else if (QFileInfo::exists(cand2)) inPath = cand2;
}
if (inPath.isEmpty()) continue;   // 该组无输入, 跳过 (允许编号不连续)

QString ansPath;
{
const QString cand1 = dir + "/" + base + "-" + num + ".out";
const QString cand2 = dir + "/" + base + "_" + num + ".out";
if (QFileInfo::exists(cand1)) ansPath = cand1;
else if (QFileInfo::exists(cand2)) ansPath = cand2;
}

inputFiles << inPath;
ansFiles << ansPath;   // 可能为空
}
return inputFiles.size();
}
// ==================== 多组完整评测 ====================
bool EvaluatorCore::evaluateGroups(const QString &srcPath, const QString &flags, int core,
   const JudgeLimits &limits,
   QString &compileOut, QString &tableText,
   QVector<TestCaseResult> &results) {
results.clear();
compileOut.clear();
tableText.clear();

QFileInfo fi(srcPath);
const QString base = fi.baseName();

// 1. 编译 (内部把 source.in/out 改写为 base.in/out)
if (!compile(srcPath, flags, compileOut, base)) {
tableText = "编译错误 (CE)\n";
return false;
}
compileOut += "编译成功\n";

// 2. 探测测试组
QStringList ins, ans;
int n = findTestGroups(srcPath, ins, ans);
if (n == 0) {
// 兼容未编号的单样例: 目录下 aaa.in / aaa.out 视为 1 组
QString singleIn = fi.absolutePath() + "/" + base + ".in";
QString singleAns = fi.absolutePath() + "/" + base + ".out";
if (QFileInfo::exists(singleIn) && QFileInfo::exists(singleAns)) {
ins << singleIn;
ans << singleAns;
n = 1;
} else {
tableText += "未找到任何测试数据组。\n";
tableText += "请将与源文件同名的数据按 aaa-N.in / aaa-N.out (或 aaa_N.in / aaa_N.out) 命名,\n";
tableText += "放在源文件同目录下 (N=1..100, 最多 100 组)。\n";
tableText += "综合评价: 未评测 (无测试数据)\n";
return true;
}
}

// 3. 本地时限换算 + wrapper 编译一次
double speedFactor = 1.0;
{
const myd::OJTimer &tj = myd::OJTimer::getInstance();
double f = tj.getSpeedFactor();
if (f > 0.0) speedFactor = f;
}
const double localCpuLimitMs = (limits.ojLimitMs <= 0.0)
? 0.0
: (limits.ojLimitMs / speedFactor);
const double wallLimitMs = (localCpuLimitMs <= 0.0)
? 0.0
: (localCpuLimitMs * (limits.tleHardScale > 0.0 ? limits.tleHardScale : 1.0));
const size_t memHardBytes = (limits.memLimitMB > 0)
? (size_t)((double)limits.memLimitMB * 1024.0 * 1024.0 * (limits.memHardScale > 0.0 ? limits.memHardScale : 1.0)) : 0;

QString wrapperOut;
if (!ensureWrapper(core, localCpuLimitMs, wallLimitMs, memHardBytes, base + ".exe", wrapperOut)) {
compileOut += wrapperOut;
tableText = "包装程序编译失败, 无法评测\n";
return false;
}

// 4. 逐组评测
int ac = 0, wa = 0, tle = 0, mle = 0, re = 0, noans = 0;
for (int i = 0; i < n; ++i) {
TestCaseResult r;
r.index = i + 1;
r.inputFile = QFileInfo(ins[i]).fileName();
r.ansFile = ans[i].isEmpty() ? QString("(缺)") : QFileInfo(ans[i]).fileName();

// 放置本组输入/答案: aaa-N.in → aaa.in, aaa-N.out → aaa.ans
QFile::remove(m_testDir + base + ".in");
QFile::remove(m_testDir + base + ".out");
QFile::remove(m_testDir + base + ".ans");
QFile::copy(ins[i], m_testDir + base + ".in");
if (!ans[i].isEmpty())
QFile::copy(ans[i], m_testDir + base + ".ans");

// 运行被评测程序 (读 aaa.in, 写 aaa.out)
QString runOut;
int v = JudgeVerdict::RUN_ERR;
bool ok = runPrepared(base, limits.memLimitMB, r.cpuTimeMs, r.wallTimeMs, r.peakMemBytes, runOut, v);

r.ojTimeMs = myd::OJTimer::getInstance().toOJTime(r.cpuTimeMs);

if (!ok) {
r.verdict = JudgeVerdict::RUN_ERR;
} else if (v == JudgeVerdict::OK) {
// 答案比对
QFile outF(m_testDir + base + ".out");
QFile ansF(m_testDir + base + ".ans");
if (!outF.exists()) {
r.verdict = JudgeVerdict::WA;   // 无输出
} else if (ans[i].isEmpty() || !ansF.exists()) {
r.verdict = JudgeVerdict::WA;   // 缺答案
noans++;
} else if (outF.open(QIODevice::ReadOnly | QIODevice::Text) &&
   ansF.open(QIODevice::ReadOnly | QIODevice::Text)) {
QTextStream os(&outF);
QTextStream as(&ansF);
QString outText = os.readAll().trimmed();
QString ansText = as.readAll().trimmed();
r.verdict = (outText == ansText) ? JudgeVerdict::AC : JudgeVerdict::WA;
} else {
r.verdict = JudgeVerdict::WA;
}
} else {
r.verdict = v;   // TLE_CPU / TLE_WALL / MLE / RUN_ERR
}

switch (r.verdict) {
case JudgeVerdict::AC: ac++; break;
case JudgeVerdict::WA: wa++; break;
case JudgeVerdict::TLE_CPU:
case JudgeVerdict::TLE_WALL: tle++; break;
case JudgeVerdict::MLE: mle++; break;
case JudgeVerdict::RUN_ERR: re++; break;
default: break;
}

// 非 AC 时附带本组运行诊断 (时间/内存超限等)
if (r.verdict != JudgeVerdict::AC && !runOut.trimmed().isEmpty())
tableText += QString("  测试点 %1: %2").arg(r.index).arg(runOut.trimmed()) + "\n";

results.append(r);
}

// 5. 表格输出 (自适应列宽)
tableText += "========== 评测结果 ==========\n";
tableText += QString("共 %1 组测试数据\n").arg(n);
tableText += buildResultTable(results);
// 6. 汇总 + 综合评价
double totalCpu = 0.0, totalOj = 0.0;
for (const TestCaseResult &r : results) {
totalCpu += r.cpuTimeMs;
totalOj += r.ojTimeMs;
}
tableText += QString("AC:%1  WA:%2  TLE:%3  MLE:%4  RE:%5  缺答案:%6\n")
.arg(ac).arg(wa).arg(tle).arg(mle).arg(re).arg(noans);
tableText += QString("总 CPU:%1 ms    总 OJ 预估:%2 ms\n")
.arg(totalCpu, 0, 'f', 1).arg(totalOj, 0, 'f', 1);
tableText += QString("通过率: %1/%2 = %3%\n")
.arg(ac).arg(n).arg(n > 0 ? (double)ac / n * 100.0 : 0.0, 0, 'f', 1);

if (ac == n) {
tableText += "综合评价: 全部通过 (AC)\n";
} else {
QStringList reasons;
if (wa > 0) reasons << QString("答案错误(WA)%1组").arg(wa);
if (tle > 0) reasons << QString("时间超限(TLE)%1组").arg(tle);
if (noans > 0) reasons << QString("缺答案%1组").arg(noans);
if (mle > 0) reasons << QString("内存超限(MLE)%1组").arg(mle);
if (re > 0) reasons << QString("运行错误(RE)%1组").arg(re);
tableText += QString("综合评价: 部分通过 (AC %1/%2) — %3\n")
.arg(ac).arg(n)
.arg(reasons.isEmpty() ? QString("未通过") : reasons.join(", "));
}

return true;
}