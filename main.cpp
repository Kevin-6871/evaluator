#include <QApplication>
#include <QMainWindow>
#include <QPushButton>
#include <QLineEdit>
#include <QLabel>
#include <QTextEdit>
#include <QFileDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QWidget>
#include <QFrame>
#include <QShowEvent>
#include <QFontMetrics>
#include <QEvent>
#include <QColor>
#include <QPalette>
#include <QProcess>
#include <QDir>
#include <QFileInfo>
#include <QTextStream>
#include <QPainter>
#include <QPainterPath>
#include <QTemporaryDir>

#ifdef Q_OS_WIN
#include <windows.h>
#include <dwmapi.h>
#include <winreg.h>
#endif

#include "md.hpp"

// 工具链：默认指向用户环境的 llvm-mingw，找不到时回退到 PATH 中的 g++
#ifdef Q_OS_WIN
static const char* const COMPILER_CANDIDATES[] = {
	"C:/Qt/Tools/llvm-mingw1706_64/bin/g++.exe",
	"C:/Qt/Tools/mingw1310_64/bin/g++.exe",
	"C:/Qt/Tools/mingw810_64/bin/g++.exe",
};
#endif

// ==================== 1. DPI 自适应文本框 ====================
class CustomLineEdit : public QLineEdit {
  public:
	explicit CustomLineEdit(QWidget *parent = nullptr) : QLineEdit(parent) {
		updateHeight();
	}
  protected:
	void changeEvent(QEvent *e) override {
		if (e->type() == QEvent::FontChange) {
			updateHeight();
		}
		QLineEdit::changeEvent(e);
	}
  private:
	void updateHeight() {
		QFontMetrics fm(font());
		setFixedHeight(fm.height() + 8);
	}
};

// ==================== 2. 原生绘制的半透卡片 ====================
class CardWidget : public QFrame {
  public:
	using QFrame::QFrame;
  protected:
	void paintEvent(QPaintEvent *event) override {
		Q_UNUSED(event);
		QPainter painter(this);
		painter.setRenderHint(QPainter::Antialiasing);
		bool darkMode = false;
#ifdef Q_OS_WIN
		DWORD value = 1;
		if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"AppsUseLightTheme", RRF_RT_DWORD, NULL, &value, NULL) == ERROR_SUCCESS) {
			darkMode = (value == 0);
		}
#endif
		QColor bgColor = darkMode ? QColor(32, 32, 32, 128) : QColor(255, 255, 255, 128);
		QColor borderColor = darkMode ? QColor(80, 80, 80, 128) : QColor(180, 180, 180, 128);
		QPainterPath path;
		path.addRoundedRect(rect().adjusted(0, 0, 0, 0), 6, 6);
		painter.fillPath(path, bgColor);
		painter.setPen(borderColor);
		painter.drawPath(path);
	}
};

// ==================== 3. 核心评测逻辑（带底层调试信息） ====================
class EvaluatorCore {
  public:
	EvaluatorCore(const QString &exeDir) : m_exeDir(exeDir), m_tempDir("evaluator_") {
		if (m_tempDir.isValid()) {
			m_testDir = m_tempDir.path() + "/";
		} else {
			m_testDir = QDir::tempPath() + "/evaluator_fallback/";
			QDir().mkpath(m_testDir);
		}
	}

	bool compile(const QString &srcPath, const QString &flags, QString &output, const QString &outputBaseName = QString()) {
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

	bool run(int core, double &cpuTimeMs, size_t &peakMem, QString &output) {
		QString wrapperCode = QString(R"(
#include <windows.h>
#include <cstdio>
int main() {
	int core = %1;
	STARTUPINFO si={sizeof(si)}; PROCESS_INFORMATION pi;
	char cmd[]="source.exe";

	HANDLE job = CreateJobObjectW(NULL, NULL);
	JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli = {};
	jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
	if (job) SetInformationJobObject(job, JobObjectExtendedLimitInformation, &jeli, sizeof(jeli));

	DWORD flags = (core >= 0) ? CREATE_SUSPENDED : 0;
	if(!CreateProcess(NULL,cmd,NULL,NULL,FALSE,flags,NULL,NULL,&si,&pi)){
		FILE* f=fopen("time_result.txt","w");
		if(f){fprintf(f,"0.000 0");fclose(f);}
		if (job) CloseHandle(job);
		return 1;
	}
	if (job) AssignProcessToJobObject(job, pi.hProcess);
	if (core >= 0) {
		SetThreadAffinityMask(pi.hThread, 1ULL << core);
		ResumeThread(pi.hThread);
	}
	CloseHandle(pi.hThread);
	WaitForSingleObject(pi.hProcess,INFINITE);

	FILETIME c,e,k,u; GetProcessTimes(pi.hProcess,&c,&e,&k,&u);
	ULARGE_INTEGER ul; ul.LowPart=u.dwLowDateTime; ul.HighPart=u.dwHighDateTime;
	double t=ul.QuadPart/10000.0;
	CloseHandle(pi.hProcess);

	unsigned long long m = 0;
	if (job) {
		JOBOBJECT_EXTENDED_LIMIT_INFORMATION jinfo = {};
		if (QueryInformationJobObject(job, JobObjectExtendedLimitInformation, &jinfo, sizeof(jinfo), NULL))
			m = (unsigned long long)jinfo.PeakJobMemoryUsed;
		CloseHandle(job);
	}
	FILE* f=fopen("time_result.txt","w");
	if(f){fprintf(f,"%.3f %llu",t,m);fclose(f);}
	return 0;
}
)").arg(core);

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
			// 这里能捕获到“找不到文件”、“权限不足”等底层操作系统错误
			QString errorInfo = QString("QProcess::ProcessError: %1\n详细错误描述: %2")
									.arg(proc.error())
									.arg(proc.errorString());

			// 如果前面的管道重定向没生效，尝试强行读取 stderr 余量
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
			// 获取 exitCode 非 0 的情况
			QString stderrMsg = QString::fromLocal8Bit(proc.readAllStandardError());
			if (!stderrMsg.isEmpty()) {
				output += "程序报错信息 (stderr):\n" + stderrMsg + "\n";
			}
			return false;
		}

		QFile resultFile(m_testDir + "time_result.txt");
		if (!resultFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
			output += "错误：找不到 time_result.txt，评测可能因为路径或环境中断。\n";
			// 强行输出程序运行中实际产生的 stderr
			QString stderrMsg = QString::fromLocal8Bit(proc.readAllStandardError());
			if (!stderrMsg.isEmpty()) {
				output += "程序底层输出:\n" + stderrMsg + "\n";
			}
			return false;
		}
		QTextStream in(&resultFile);
		double t = 0; unsigned long long m = 0;
		in >> t >> m;
		cpuTimeMs = t;
		peakMem = (size_t)m;
		resultFile.close();

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

  public:
	QString resolveCompiler() {
#ifdef Q_OS_WIN
		for (const char* cand : COMPILER_CANDIDATES) {
			if (QFileInfo::exists(cand)) return QString::fromLocal8Bit(cand);
		}
#endif
		return QString("g++");
	}

  private:
	QString m_exeDir;
	QString m_testDir;
	QTemporaryDir m_tempDir;
};

// ==================== 4. Mica 核心窗口 ====================
class MicaWindow : public QMainWindow {
  public:
	MicaWindow(QWidget *parent = nullptr) : QMainWindow(parent) {
		setWindowTitle("MB 评测器 (Qt + Mica)");
		resize(780, 580);
		setAttribute(Qt::WA_TranslucentBackground);
		setAttribute(Qt::WA_OpaquePaintEvent);

		QWidget *central = new QWidget(this);
		setCentralWidget(central);
		central->setAttribute(Qt::WA_TranslucentBackground);

		QVBoxLayout *mainLayout = new QVBoxLayout(central);
		mainLayout->setContentsMargins(8, 8, 8, 8);
		mainLayout->setSpacing(4);

		CardWidget *cardWidget = new CardWidget(this);
		QVBoxLayout *cardLayout = new QVBoxLayout(cardWidget);
		cardLayout->setContentsMargins(10, 10, 10, 10);
		cardLayout->setSpacing(4);

		m_outputEdit = new QTextEdit(this);
		m_outputEdit->setReadOnly(true);
		m_outputEdit->setFont(QFont("Consolas", 10));

		QHBoxLayout *pathLayout = new QHBoxLayout();
		pathLayout->setSpacing(4);
		m_filePathEdit = new CustomLineEdit(this);
		m_filePathEdit->setPlaceholderText("请选择 .cpp 源文件");
		m_filePathEdit->setReadOnly(false);
		connect(m_filePathEdit, &CustomLineEdit::editingFinished, this, [this]() {
			QString text = m_filePathEdit->text().trimmed();
			if (QFileInfo::exists(text)) {
				m_currentSource = text;
				m_runBtn->setEnabled(true);
			}
		});
		m_browseBtn = new QPushButton("📁 浏览", this);
		connect(m_browseBtn, &QPushButton::clicked, this, [this]() {
			QString file = QFileDialog::getOpenFileName(this, "选择 C++ 源文件", "", "C++ 文件 (*.cpp);;所有文件 (*.*)");
			if (!file.isEmpty()) {
				m_filePathEdit->setText(file);
				m_runBtn->setEnabled(true);
				m_outputEdit->clear();
				m_currentSource = file;
			}
		});
		pathLayout->addWidget(m_filePathEdit, 1);
		pathLayout->addWidget(m_browseBtn);

		QHBoxLayout *paramLayout = new QHBoxLayout();
		paramLayout->setSpacing(4);
		QLabel *flagsLabel = new QLabel("编译参数:", this);
		m_flagsEdit = new CustomLineEdit(this);
		m_flagsEdit->setText("-std=c++17 -O2 -Wall");
		QLabel *coreLabel = new QLabel("核心:", this);
		m_coreEdit = new CustomLineEdit(this);
		m_coreEdit->setText("-1");
		m_coreEdit->setFixedWidth(60);

		m_runBtn = new QPushButton("开始评测", this);
		m_runBtn->setEnabled(false);
		connect(m_runBtn, &QPushButton::clicked, this, [this]() {
			if (m_currentSource.isEmpty()) return;
			QString flags = m_flagsEdit->text();
			if (flags.isEmpty()) flags = "-std=c++17 -O2 -Wall";
			int core = m_coreEdit->text().toInt();
			if (core < -1) core = -1;

			m_runBtn->setEnabled(false);
			m_outputEdit->clear();

			appendOutput("========== 评测开始 ==========\n");
			appendOutput("文件: " + m_currentSource + "\n");
			appendOutput("编译参数: " + flags + "\n");
			appendOutput("绑核: " + QString(core == -1 ? "不绑（自适应）" : QString::number(core)) + "\n");

			myd::OJTimer::getInstance().doCalibrate(core);
			double speedFactor = myd::OJTimer::getInstance().getSpeedFactor();
			appendOutput(QString("速度因子: %1\n").arg(speedFactor, 0, 'f', 3));

			QString compileOut;
			bool compileOk = m_core->compile(m_currentSource, flags, compileOut, "source");
			appendOutput(compileOut);
			if (!compileOk) {
				appendOutput("编译错误 (CE)\n");
				m_runBtn->setEnabled(true);
				return;
			}
			appendOutput("编译成功\n\n");

			double cpuTime = 0;
			size_t peakMem = 0;
			QString runOut;
			bool runOk = m_core->run(core, cpuTime, peakMem, runOut);
			appendOutput(runOut);
			if (!runOk) {
				appendOutput("运行失败\n");
				m_runBtn->setEnabled(true);
				return;
			}

			appendOutput(QString("用户态CPU时间: %1 ms\n").arg(cpuTime, 0, 'f', 3));
			double ojTime = myd::OJTimer::getInstance().toOJTime(cpuTime);
			appendOutput(QString("标准OJ环境预估用时: %1 ms\n").arg(ojTime, 0, 'f', 3));
			appendOutput(QString("峰值专用内存: %1 MB\n").arg(peakMem / (1024.0 * 1024.0), 0, 'f', 2));

			appendOutput("\n========== 评测结束 ==========\n");
			m_runBtn->setEnabled(true);
		});

		paramLayout->addWidget(flagsLabel);
		paramLayout->addWidget(m_flagsEdit, 1);
		paramLayout->addWidget(coreLabel);
		paramLayout->addWidget(m_coreEdit);
		paramLayout->addStretch();
		paramLayout->addWidget(m_runBtn);

		cardLayout->addWidget(m_outputEdit, 1);
		cardLayout->addLayout(pathLayout);
		cardLayout->addLayout(paramLayout);
		mainLayout->addWidget(cardWidget);

		applyPaletteTheme(isDarkMode());
		QString exeDir = QCoreApplication::applicationDirPath();
		m_core = new EvaluatorCore(exeDir);
	}

	~MicaWindow() { delete m_core; }

  protected:
	bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override {
#ifdef Q_OS_WIN
		if (eventType == "windows_generic_MSG") {
			MSG *msg = reinterpret_cast<MSG*>(message);
			if (msg->message == WM_ERASEBKGND) {
				*result = 1;
				return true;
			}
		}
#endif
		return QMainWindow::nativeEvent(eventType, message, result);
	}
	void showEvent(QShowEvent *event) override {
		QMainWindow::showEvent(event);
		enableMica();
	}

  private:
	CustomLineEdit *m_filePathEdit;
	QPushButton *m_browseBtn;
	CustomLineEdit *m_flagsEdit;
	CustomLineEdit *m_coreEdit;
	QPushButton *m_runBtn;
	QTextEdit *m_outputEdit;
	QString m_currentSource;
	EvaluatorCore *m_core;

	void appendOutput(const QString &text) {
		m_outputEdit->moveCursor(QTextCursor::End);
		m_outputEdit->insertPlainText(text);
		m_outputEdit->moveCursor(QTextCursor::End);
	}

	void applyPaletteTheme(bool darkMode) {
		QColor realTextColor = darkMode ? Qt::white : Qt::black;
		QColor placeholderColor = darkMode ? QColor(160,160,160) : QColor(100,100,100);
		QColor baseInputColor = darkMode ? QColor(32, 32, 32) : Qt::white;

		QList<QWidget*> widgets = {m_filePathEdit, m_flagsEdit, m_coreEdit};
		for(QWidget* w : widgets) {
			QPalette pal = w->palette();
			pal.setColor(QPalette::Text, realTextColor);
			pal.setColor(QPalette::PlaceholderText, placeholderColor);
			pal.setColor(QPalette::Base, baseInputColor);
			pal.setColor(QPalette::Highlight, QColor(0, 120, 212));
			pal.setColor(QPalette::HighlightedText, Qt::white);
			w->setPalette(pal);
		}
		QPalette outPal = m_outputEdit->palette();
		outPal.setColor(QPalette::Base, baseInputColor);
		outPal.setColor(QPalette::Text, realTextColor);
		outPal.setColor(QPalette::Highlight, QColor(0, 120, 212));
		outPal.setColor(QPalette::HighlightedText, Qt::white);
		m_outputEdit->setPalette(outPal);
	}

	bool isDarkMode() {
#ifdef Q_OS_WIN
		DWORD value = 1;
		DWORD size = sizeof(value);
		if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"AppsUseLightTheme", RRF_RT_DWORD, NULL, &value, &size) == ERROR_SUCCESS) {
			return (value == 0);
		}
#endif
		return false;
	}
	void enableMica() {
#ifdef Q_OS_WIN
		HWND hwnd = reinterpret_cast<HWND>(this->winId());
		if (!hwnd) return;
		MARGINS margins = { -1, -1, -1, -1 };
		DwmExtendFrameIntoClientArea(hwnd, &margins);
		int micaType = 2;
		DwmSetWindowAttribute(hwnd, 38, &micaType, sizeof(micaType));
		BOOL useDark = isDarkMode() ? TRUE : FALSE;
		DwmSetWindowAttribute(hwnd, 20, &useDark, sizeof(useDark));
		SetWindowPos(hwnd, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED);
#endif
	}
};

int main(int argc, char *argv[]) {
	QApplication app(argc, argv);

	// ==================== 6. 无头自测模式 ====================
	if (argc > 1 && QString(argv[1]) == "--selftest") {
		QString root = (argc > 2) ? QString(argv[2]) : QString("testcases");
		FILE* log = fopen("selftest_result.txt", "w");
		if (!log) return 2;

		fprintf(log, "========== 评测器可用性自测 ==========\n");
		QString compiler;
		{
			EvaluatorCore probe(QStringLiteral("."));
			compiler = probe.resolveCompiler();
		}
		fprintf(log, "工具链 g++: ");
		fflush(log);

		QProcess chk; chk.start(compiler, QStringList() << "--version");
		chk.waitForFinished(5000);
		QString ver = QString::fromLocal8Bit(chk.readAllStandardOutput());
		if (ver.isEmpty()) {
			fprintf(log, "不可用 (未找到，请确保 PATH 含工具链 bin)\n");
			fclose(log);
			return 1;
		}
		fprintf(log, "%s\n", ver.split("\n").first().toLocal8Bit().constData());

		myd::OJTimer::getInstance().doCalibrate();
		fprintf(log, "速度因子: %.3f\n\n", myd::OJTimer::getInstance().getSpeedFactor());

		QDir rootDir(root);
		QStringList folders = rootDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
		folders.sort();

		int pass = 0, total = 0;
		for (const QString &name : folders) {
			total++;
			QString folder = rootDir.filePath(name);
			QDir fdir(folder);
			QStringList cpps = fdir.entryList({ "*.cpp" }, QDir::Files);
			if (cpps.isEmpty()) {
				fprintf(log, "[%s] 无 .cpp 文件，跳过\n", name.toLocal8Bit().constData());
				total--;
				continue;
			}
			QString src = fdir.filePath(cpps.first());
			QString flags = "-std=c++17 -O2 -Wall";

			EvaluatorCore core(folder + "/");
			QString comOut;
			bool compileOk = core.compile(src, flags, comOut, "source");
			if (!compileOk) {
				bool isCE = comOut.contains("error:");
				fprintf(log, "用例 %-16s 编译失败 [%s]\n",
						name.toLocal8Bit().constData(), isCE ? "CE" : "编译0?");
				bool expectCE = (name == "CompileError");
				if (isCE == expectCE) pass++;
				continue;
			}
			if (name == "CompileError") {
				fprintf(log, "用例 %-16s 本应编译失败，却编译通过 [FAIL]\n", name.toLocal8Bit().constData());
				continue;
			}

			double cpu = 0; size_t mem = 0; QString runOut;
			bool runOk = core.run(-1, cpu, mem, runOut);
			QString verdict;
			if (runOut.contains("答案正确 (AC)")) verdict = "AC";
			else if (runOut.contains("答案错误 (WA)")) verdict = "WA";
			else verdict = "RUN?";

			fprintf(log, "用例 %-16s CPU=%8.3fms  内存=%8.2fMB  ->  %s  runOk=%d\n",
					name.toLocal8Bit().constData(), cpu, mem / (1024.0 * 1024.0),
					verdict.toLocal8Bit().constData(), (int)runOk);
			if (verdict == "RUN?") {
				fprintf(log, "  [runOut]:\n%s\n", runOut.toLocal8Bit().constData());
			}

			bool ok = (name == "AplusB"    && verdict == "AC") ||
					  (name == "WrongAns"  && verdict == "WA") ||
					  (name == "MemTest"   && verdict == "AC" && mem > 100LL * 1024 * 1024) ||
					  (name == "TimeTest"  && verdict == "AC" && cpu > 10.0);
			if (ok) pass++;
			else fprintf(log, "      ↑ 未达到预期状态\n");
		}

		fprintf(log, "\n========== 自测结果: %d/%d 通过 ==========\n", pass, total);
		fclose(log);
		return (pass == total) ? 0 : 1;
	}

	MicaWindow w;
	w.show();
	return app.exec();
}