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