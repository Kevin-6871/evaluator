#include <QApplication>
#include <QMainWindow>
#include <QPushButton>
#include <QLineEdit>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QWidget>
#include <QShowEvent>
#include <QFontMetrics>
#include <QEvent>
#include <QColor>
#include <QPalette>

#ifdef Q_OS_WIN
#include <windows.h>
#include <dwmapi.h>
#include <winreg.h>
#endif

// -------- 1. 回归原生 QLineEdit（无手画，无 QProxyStyle） --------
class CustomLineEdit : public QLineEdit {
  public:
	explicit CustomLineEdit(QWidget *parent = nullptr) : QLineEdit(parent) {
		// 初始适配当前 DPI
		updateHeight();
	}

  protected:
	// 【核心修复：DPI 变化时自动撑开高度】
	void changeEvent(QEvent *e) override {
		if (e->type() == QEvent::FontChange) {
			updateHeight();
		}
		QLineEdit::changeEvent(e);
	}

  private:
	void updateHeight() {
		QFontMetrics fm(font());
		// 8px 为 QSS 中的上下边距，再加 2px 上下边框
		setFixedHeight(fm.height() + 8 + 8 + 2);
	}
};

// -------- 2. Mica 窗口 --------
class MicaWindow : public QMainWindow {
  public:
	MicaWindow(QWidget *parent = nullptr) : QMainWindow(parent) {
		setWindowTitle("Qt + Mica 原生光标版");
		resize(500, 300);

		setAttribute(Qt::WA_TranslucentBackground);
		setAttribute(Qt::WA_OpaquePaintEvent);

		QWidget *central = new QWidget(this);
		setCentralWidget(central);
		central->setAttribute(Qt::WA_TranslucentBackground);

		QVBoxLayout *mainLayout = new QVBoxLayout(central);
		mainLayout->setAlignment(Qt::AlignCenter);

		bool darkMode = isDarkMode();

			   // -------- 【UI 卡片】 Mica 上叠加 50% 白色块 --------
		QFrame *cardWidget = new QFrame(this);
		cardWidget->setObjectName("CardWidget");
		cardWidget->setFixedSize(400, 220);

		QVBoxLayout *cardLayout = new QVBoxLayout(cardWidget);
		cardLayout->setContentsMargins(24, 20, 24, 20);
		cardLayout->setSpacing(12);

		QLabel *titleLabel = new QLabel("自定义缩放", this);
		QFont titleFont = font();
		titleFont.setPointSize(16);
		titleFont.setBold(true);
		titleLabel->setFont(titleFont);

		QLabel *subLabel = new QLabel("输入 100% - 500% 之间的自定义缩放大小", this);
		QFont subFont = font();
		subFont.setPointSize(10);
		subLabel->setFont(subFont);

		QHBoxLayout *inputRowLayout = new QHBoxLayout();
		inputRowLayout->setSpacing(12);

			   // ---- 原生光标版文本框 ----
		CustomLineEdit *textBox = new CustomLineEdit(this);
		textBox->setPlaceholderText("100 - 500");
		textBox->setText("100 - 500");
		textBox->setMinimumWidth(200);
		textBox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

			   // 【重点：原生光标颜色控制】
		QPalette pal = textBox->palette();
		// 浅色模式下，文字和光标自动变为纯黑色；深色模式下，变为纯白色
		pal.setColor(QPalette::Text, darkMode ? Qt::white : Qt::black);
		pal.setColor(QPalette::Base, darkMode ? QColor(32, 32, 32) : Qt::white); // 输入框底色
		pal.setColor(QPalette::PlaceholderText, darkMode ? QColor(160,160,160) : QColor(100,100,100));
		pal.setColor(QPalette::Highlight, QColor(0, 120, 212));
		pal.setColor(QPalette::HighlightedText, Qt::white);
		textBox->setPalette(pal);

			   // 边框与底部粗线样式（复刻 Win11 输入框）
		if (darkMode) {
			textBox->setStyleSheet(
				"QLineEdit {"
				"   border: 1px solid #666666;"
				"   border-bottom: 2px solid #888888;"
				"   border-radius: 4px;"
				"   padding: 8px;"
				"}"
				"QLineEdit:focus {"
				"   border: 1px solid #b3b3b3;"
				"   border-bottom: 2px solid #0f6cbd;"
				"}"
				);
		} else {
			textBox->setStyleSheet(
				"QLineEdit {"
				"   border: 1px solid #d1d1d1;"
				"   border-bottom: 2px solid #616161;"
				"   border-radius: 4px;"
				"   padding: 8px;"
				"}"
				"QLineEdit:focus {"
				"   border: 1px solid #b3b3b3;"
				"   border-bottom: 2px solid #0f6cbd;"
				"}"
				);
		}

			   // ---- 确定按钮 ----
		QPushButton *button = new QPushButton("确定", this);
		button->setMinimumWidth(80);
		// 高度随 DPI 自动适配
		QFontMetrics btnFm(button->font());
		button->setFixedHeight(btnFm.height() + 16);
		connect(button, &QPushButton::clicked, []() {
			QApplication::quit();
		});

		inputRowLayout->addWidget(textBox);
		inputRowLayout->addWidget(button);

		cardLayout->addWidget(titleLabel);
		cardLayout->addWidget(subLabel);
		cardLayout->addLayout(inputRowLayout);
		cardLayout->addStretch();

		mainLayout->addWidget(cardWidget);

			   // -------- 【全局动态卡片样式表】 --------
		applyCardStyle(darkMode);
	}

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
	void applyCardStyle(bool darkMode) {
		QString cardBg = darkMode ? "rgba(32, 32, 32, 0.5)" : "rgba(255, 255, 255, 0.5)";
		QString cardBorder = darkMode ? "rgba(100, 100, 100, 0.5)" : "rgba(160, 160, 160, 0.5)";
		QString textColor = darkMode ? "white" : "black";

		QString qss = QString(
						  "#CardWidget {"
						  "   background-color: %1;"
						  "   border: 1px solid %2;"
						  "   border-radius: 8px;"
						  "}"
						  "QLabel { color: %3; font-family: 'Segoe UI'; }"
						  ).arg(cardBg, cardBorder, textColor);

		this->setStyleSheet(qss);
	}

	bool isDarkMode() {
#ifdef Q_OS_WIN
		DWORD value = 1;
		DWORD size = sizeof(value);
		if (RegGetValueW(HKEY_CURRENT_USER,
						 L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
						 L"AppsUseLightTheme", RRF_RT_DWORD, NULL, &value, &size) == ERROR_SUCCESS) {
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
	MicaWindow w;
	w.show();
	return app.exec();
}