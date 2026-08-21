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
#include "evaluator.hpp"
#include "selftest.hpp"

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

// ==================== 3. Mica 核心窗口 ====================
class MicaWindow : public QMainWindow {
  public:
	MicaWindow(QWidget *parent = nullptr) : QMainWindow(parent) {
		setWindowTitle("MB 评测器");
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
		m_browseBtn->setToolTip("使用文件资源管理器选择文件.");
		connect(m_browseBtn, &QPushButton::clicked, this, [this]() {
			QString file = QFileDialog::getOpenFileName(this, "选择 C++ 源文件", "", "C++ 文件 (*.cpp);;所有文件 (*.*)");
			if (!file.isEmpty()) {
				m_filePathEdit->setText(file);
				m_runBtn->setEnabled(true);
				m_currentSource = file;
			}
		});
		pathLayout->addWidget(m_filePathEdit, 1);
		pathLayout->addWidget(m_browseBtn);

		QHBoxLayout *paramLayout = new QHBoxLayout();
		paramLayout->setSpacing(4);
		QLabel *flagsLabel = new QLabel("编译参数:", this);
		flagsLabel->setToolTip("传给编译器的参数, 例如 -std=c++17 -O2 -Wall.");
		m_flagsEdit = new CustomLineEdit(this);
		m_flagsEdit->setText("-std=c++17 -O2 -Wall");
		m_flagsEdit->setToolTip(flagsLabel->toolTip());
		QLabel *coreLabel = new QLabel("核心:", this);
		coreLabel->setToolTip("把被评测程序绑定的 CPU 核心编号.\n-1 = 不绑定, 由系统调度.");
		m_coreEdit = new CustomLineEdit(this);
		m_coreEdit->setText("-1");
		m_coreEdit->setFixedWidth(60);
		m_coreEdit->setToolTip(coreLabel->toolTip());
		m_runBtn = new QPushButton("开始评测", this);
		m_runBtn->setToolTip("编译并运行当前 C++ 源文件, 按上方时限/内存/参考机参数评测.");
		m_runBtn->setEnabled(false);
		connect(m_runBtn, &QPushButton::clicked, this, [this]() {
			if (m_currentSource.isEmpty()) return;
			QString flags = m_flagsEdit->text();
			if (flags.isEmpty()) flags = "-std=c++17 -O2 -Wall";
			int core = m_coreEdit->text().toInt();
			if (core < -1) core = -1;

			m_runBtn->setEnabled(false);
			// 不清屏: 保留上次测评结果供对比

			appendOutput("==================== 评测开始 ====================\n");
			appendOutput("文件: " + m_currentSource + "\n");
			appendOutput("编译参数: " + flags + "\n");
			appendOutput("绑核: " + QString(core == -1 ? "不绑(自适应)" : QString::number(core)) + "\n");

			double refIPC = m_refIPCEdit->text().toDouble();
			double refGHz = m_refGHEdit->text().toDouble();
			if (refIPC != m_lastRefIPC || refGHz != m_lastRefGHz) {
				refreshCalibration();
			} else {
				appendOutput(QString("速度因子: %1  (参考机 %2 指令/秒)\n\n")
					.arg(myd::OJTimer::getInstance().getSpeedFactor(), 0, 'f', 4)
					.arg(myd::OJTimer::getInstance().getRefIPS(), 0, 'f', 2));
			}

			double ojLimitMs = m_ojLimitEdit->text().toDouble();
			double langFactor = m_langFactorEdit->text().toDouble();
			double wallScale = m_wallScaleEdit->text().toDouble();
			unsigned long long memLimitMB = m_memLimitEdit->text().toULongLong();
			if (ojLimitMs <= 0.0) ojLimitMs = 1000.0;
			if (langFactor <= 0.0) langFactor = 1.0;
			if (wallScale <= 0.0) wallScale = 1.5;
			if (memLimitMB == 0) memLimitMB = 256;
			appendOutput(QString("OJ时限: %1 ms   语言因子: %2   硬限比例: %3\n")
				.arg(ojLimitMs, 0, 'f', 0).arg(langFactor, 0, 'f', 2).arg(wallScale, 0, 'f', 2));
			appendOutput(QString("内存限制: %1 MB\n").arg(memLimitMB));

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
			double wallTime = 0;
			size_t peakMem = 0;
			QString runOut;
			int verdict = JudgeVerdict::RUN_ERR;
			bool runOk = m_core->run(core, ojLimitMs, langFactor, wallScale, memLimitMB,
				cpuTime, wallTime, peakMem, runOut, verdict);
			appendOutput(runOut);
			if (!runOk) {
				appendOutput("运行失败\n");
				m_runBtn->setEnabled(true);
				return;
			}

			appendOutput(QString("用户态CPU时间: %1 ms   墙钟用时: %2 ms\n")
				.arg(cpuTime, 0, 'f', 3).arg(wallTime, 0, 'f', 3));
			double ojTime = myd::OJTimer::getInstance().toOJTime(cpuTime);
			appendOutput(QString("标准OJ环境预估用时: %1 ms / 时限 %2 ms\n")
				.arg(ojTime, 0, 'f', 3).arg(ojLimitMs, 0, 'f', 0));
			if (verdict == JudgeVerdict::TLE_CPU)
				appendOutput("判定: 时间超限 (TLE-CPU)\n");
			else if (verdict == JudgeVerdict::TLE_WALL)
				appendOutput("判定: 时间超限 (TLE-WALL)\n");
			else if (verdict == JudgeVerdict::MLE)
				appendOutput("判定: 内存超限 (MLE)\n");
			appendOutput(QString("峰值专用内存: %1 MB\n").arg(peakMem / (1024.0 * 1024.0), 0, 'f', 2));

			appendOutput("\n==================== 评测结束 ====================\n\n");
			m_runBtn->setEnabled(true);
		});

		paramLayout->addWidget(flagsLabel);
		paramLayout->addWidget(m_flagsEdit, 1);
		paramLayout->addWidget(coreLabel);
		paramLayout->addWidget(m_coreEdit);
		paramLayout->addStretch();
		paramLayout->addWidget(m_runBtn);

		QHBoxLayout *limitLayout = new QHBoxLayout();
		limitLayout->setSpacing(4);
		QLabel *limitLabel = new QLabel("OJ时限(ms):", this);
		m_ojLimitEdit = new CustomLineEdit(this);
		m_ojLimitEdit->setText("1000");
		m_ojLimitEdit->setFixedWidth(64);
		QLabel *langLabel = new QLabel("语言因子:", this);
		m_langFactorEdit = new CustomLineEdit(this);
		m_langFactorEdit->setText("1.00");
		m_langFactorEdit->setFixedWidth(56);
		QLabel *wallLabel = new QLabel("硬限比例:", this);
		m_wallScaleEdit = new CustomLineEdit(this);
		m_wallScaleEdit->setText("1.5");
		m_wallScaleEdit->setFixedWidth(48);
		QLabel *memLabel = new QLabel("内存(MB):", this);
		m_memLimitEdit = new CustomLineEdit(this);
		m_memLimitEdit->setText("256");
		m_memLimitEdit->setFixedWidth(56);
		QLabel *refLabel = new QLabel("参考机:", this);
		m_refIPCEdit = new CustomLineEdit(this);
		m_refIPCEdit->setText("3.0");
		m_refIPCEdit->setFixedWidth(48);
		QLabel *ipcStar = new QLabel("IPC×", this);
		m_refGHEdit = new CustomLineEdit(this);
		m_refGHEdit->setText("3.0");
		m_refGHEdit->setFixedWidth(48);
		QLabel *ghzUnit = new QLabel("GHz", this);
		m_calibBtn = new QPushButton("重新校准", this);
		connect(m_calibBtn, &QPushButton::clicked, this, [this]() {
			refreshCalibration();
		});

		// 参数说明 (悬停提示)
		limitLabel->setToolTip("题目标准时限(ms), 以参考机刻度计.\n"
			"本地等效时限 = 时限 x 语言因子 / 速度因子,\n"
			"超过即判 TLE-CPU.");
		langLabel->setToolTip("语言速度因子: C/C++=1.00.\n"
			"解释型/慢语言请调大(如 Java=2, Python=5)以放宽时限.");
		wallLabel->setToolTip("墙钟硬时限 = 软时限(CPU) x 本比例(默认1.5).\n"
			"休眠或 IO 卡死的程序虽然不消耗 CPU, 也会被墙钟硬时限终止, 判 TLE-WALL.");
		memLabel->setToolTip("内存上限(MB). 评测结束时若峰值专用内存超过该值, 判 MLE.\n"
			"(测量口径, 不主动掐断分配)");
		refLabel->setToolTip("参考 OJ 机器单核算力: IPS = IPC x GHz x 10^9 条指令/秒.\n"
			"默认 3.0 IPC x 3.0 GHz = 9.0e9 指令/秒.\n"
			"本机各域指令吞吐与参考机之比即为该域因子.");
		ipcStar->setToolTip("参考机平均每周期完成的指令数(IPC).");
		ghzUnit->setToolTip("参考机主频(GHz).");
		m_calibBtn->setToolTip("用上方参考机参数重新测量本机 4 个域(整型/浮点/内存/分支)\n"
			"的指令吞吐并重算综合速度因子.");
		m_ojLimitEdit->setToolTip(limitLabel->toolTip());
		m_langFactorEdit->setToolTip(langLabel->toolTip());
		m_wallScaleEdit->setToolTip(wallLabel->toolTip());
		m_memLimitEdit->setToolTip(memLabel->toolTip());
		m_refIPCEdit->setToolTip(refLabel->toolTip());
		m_refGHEdit->setToolTip(refLabel->toolTip());

		limitLayout->addWidget(limitLabel);
		limitLayout->addWidget(m_ojLimitEdit);
		limitLayout->addWidget(langLabel);
		limitLayout->addWidget(m_langFactorEdit);
		limitLayout->addWidget(wallLabel);
		limitLayout->addWidget(m_wallScaleEdit);
		limitLayout->addWidget(memLabel);
		limitLayout->addWidget(m_memLimitEdit);
		limitLayout->addWidget(refLabel);
		limitLayout->addWidget(m_refIPCEdit);
		limitLayout->addWidget(ipcStar);
		limitLayout->addWidget(m_refGHEdit);
		limitLayout->addWidget(ghzUnit);
		limitLayout->addStretch();
		limitLayout->addWidget(m_calibBtn);

		cardLayout->addWidget(m_outputEdit, 1);
		cardLayout->addLayout(pathLayout);
		cardLayout->addLayout(limitLayout);
		cardLayout->addLayout(paramLayout);
		mainLayout->addWidget(cardWidget);

		applyPaletteTheme(isDarkMode());
		QString exeDir = QCoreApplication::applicationDirPath();
		m_core = new EvaluatorCore(exeDir);

		refreshCalibration();
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
	CustomLineEdit *m_ojLimitEdit;
	CustomLineEdit *m_langFactorEdit;
	CustomLineEdit *m_wallScaleEdit;
	CustomLineEdit *m_memLimitEdit;
	CustomLineEdit *m_refIPCEdit;
	CustomLineEdit *m_refGHEdit;
	QPushButton *m_calibBtn;
	QTextEdit *m_outputEdit;
	QString m_currentSource;
	double m_lastRefIPC = myd::kDefaultRefIPC;
	double m_lastRefGHz = myd::kDefaultRefGHz;
	EvaluatorCore *m_core;

	void refreshCalibration() {
		double refIPC = m_refIPCEdit->text().toDouble();
		double refGHz = m_refGHEdit->text().toDouble();
		if (refIPC <= 0.0) refIPC = myd::kDefaultRefIPC;
		if (refGHz <= 0.0) refGHz = myd::kDefaultRefGHz;
		int core = m_coreEdit->text().toInt();
		if (core < -1) core = -1;
		m_lastRefIPC = refIPC;
		m_lastRefGHz = refGHz;

		appendOutput("==================== CPU 校准 ====================\n");
		appendOutput(QString("绑核: %1\n").arg(core < 0 ? "不绑(自适应)" : QString::number(core)));
		myd::OJTimerResult res = myd::OJTimer::getInstance().doCalibrate(core, refIPC, refGHz);
		appendOutput(QString("参考机: %1 IPC × %2 GHz = %3 指令/秒\n")
			.arg(refIPC, 0, 'f', 3).arg(refGHz, 0, 'f', 3).arg(res.refIPS, 0, 'f', 3));
		static const char* const dom[] = { "整型", "浮点", "内存", "分支" };
		for (int d = 0; d < 4; ++d) {
			appendOutput(QString("  %1: IPS=%2e9  本地CPU=%3ms  因子=%4\n")
				.arg(dom[d])
				.arg(res.domainIPS[d] / 1e9, 0, 'f', 4)
				.arg(res.domainCPUMs[d], 0, 'f', 1)
				.arg(res.domainFactor[d], 0, 'f', 4));
		}
		appendOutput(QString("综合速度因子: %1\n")
			.arg(res.speedFactor, 0, 'f', 4));
		appendOutput(QString("校准耗时: %1 ms%2\n")
			.arg(res.calibWallMs, 0, 'f', 0)
			.arg(res.stable ? "" : "  (警告: 校准结果异常)"));
		appendOutput("\n==================== 结束校准 ====================\n\n");
	}

	void appendOutput(const QString &text) {
		m_outputEdit->moveCursor(QTextCursor::End);
		m_outputEdit->insertPlainText(text);
		m_outputEdit->moveCursor(QTextCursor::End);
	}

	void applyPaletteTheme(bool darkMode) {
		QColor realTextColor = darkMode ? Qt::white : Qt::black;
		QColor placeholderColor = darkMode ? QColor(160,160,160) : QColor(100,100,100);
		QColor baseInputColor = darkMode ? QColor(32, 32, 32) : Qt::white;

		QList<QWidget*> widgets = {
			m_filePathEdit, m_flagsEdit, m_coreEdit,
			m_ojLimitEdit, m_langFactorEdit, m_wallScaleEdit, m_memLimitEdit,
			m_refIPCEdit, m_refGHEdit
		};
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

	// 全局字体: Cascadia Code (回退 Consolas → 微软雅黑 UI)
	QFont appFont = app.font();
	appFont.setFamilies({"Cascadia Code", "Consolas", "Microsoft YaHei UI"});
	app.setFont(appFont);

	// ==================== 无头自测模式 ====================
	if (argc > 1 && QString(argv[1]) == "--selftest") {
		QString root = (argc > 2) ? QString(argv[2]) : QString("testcases");
		QString report;
		int casesFound = 0;
		int pass = selftest::runAll(root, report, casesFound);
		FILE* log = fopen("selftest_result.txt", "w");
		if (log) {
			fprintf(log, "%s", report.toLocal8Bit().constData());
			fclose(log);
		}
		return (casesFound > 0 && pass == casesFound) ? 0 : 1;
	}

	MicaWindow w;
	w.show();
	return app.exec();
}