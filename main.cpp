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
#include <QScrollBar>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDateTime>
#include <QDialog>
#include <QGroupBox>
#include <QFormLayout>
#include <QDoubleSpinBox>
#include <QKeySequenceEdit>
#include <QShortcut>
#include <QCheckBox>
#include <QSlider>
#include <QListWidget>
#include <QStackedWidget>

#ifdef Q_OS_WIN
#include <windows.h>
#include <dwmapi.h>
#include <winreg.h>
#endif

#include "md.hpp"
#include "evaluator.hpp"

// ==================== 1. DPI 自适应文本框 ====================
class CustomLineEdit :public QLineEdit {
  public:
	explicit CustomLineEdit(QWidget *parent = nullptr) :QLineEdit(parent) {
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
		setFixedHeight(fm.height() + 6);
	}
};

// ==================== 2. 原生绘制的半透卡片 ====================
class CardWidget :public QFrame {
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
		if (RegGetValueW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",L"AppsUseLightTheme",RRF_RT_DWORD,NULL,&value,NULL) == ERROR_SUCCESS) {
			darkMode = (value == 0);
		}
#endif
		QColor bgColor = darkMode ? QColor(32,32,32,128) :QColor(255,255,255,128);
		QColor borderColor = darkMode ? QColor(80,80,80,128) :QColor(180,180,180,128);
		QPainterPath path;
		path.addRoundedRect(rect().adjusted(0,0,0,0),6,6);
		painter.fillPath(path,bgColor);
		painter.setPen(borderColor);
		painter.drawPath(path);
	}
};

// ==================== 3. 应用设置 (跨重启持久化: ./ 优先, AppData 回退, 双写同步) ====================
struct SettingsData {
	double refFactor      = 3.0;    // 参考机因子 IPC×GHz (参考指令数 = 因子 × 1e8)
	double tleHardScale   = 1.5;    // TLE 硬线比例 (墙钟 = CPU 软限 × 此值)
	double memHardScale   = 1.5;    // MEM 硬线比例 (内存硬杀线 = 软限 × 此值)
	QString runHotkey     = "F5";
	QString browseHotkey  = "Ctrl+O";
	QString settingsHotkey = "Ctrl+,";
};

static QString settingsPortablePath() { return QDir::currentPath() + "/mboj_config.json"; }
static QString settingsAppDataPath() {
	return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/mboj_config.json";
}

static double settingsSavedAtOf(const QString &path) {
	QFile f(path);
	if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return -1.0;
	QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
	f.close();
	if (!doc.isObject()) return -1.0;
	return doc.object().value("savedAt").toDouble(-1.0);
}

static SettingsData loadSettings() {
	SettingsData d;
	QStringList cands;
	const QString p = settingsPortablePath();
	const QString a = settingsAppDataPath();
	if (QFileInfo::exists(p)) cands << p;
	if (QFileInfo::exists(a)) cands << a;
	QString best; double bestT = -1.0;
	for (const QString &c : cands) {
		const double t = settingsSavedAtOf(c);
		if (t > bestT) { bestT = t; best = c; }
	}
	if (best.isEmpty()) return d;
	QFile f(best);
	if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return d;
	QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
	f.close();
	d.refFactor      = o.value("refFactor").toDouble(3.0);
	d.tleHardScale   = o.value("tleHardScale").toDouble(1.5);
	d.memHardScale   = o.value("memHardScale").toDouble(1.5);
	d.runHotkey      = o.value("runHotkey").toString("F5");
	d.browseHotkey   = o.value("browseHotkey").toString("Ctrl+O");
	d.settingsHotkey = o.value("settingsHotkey").toString("Ctrl+,");
	if (d.refFactor <= 0.0) d.refFactor = 3.0;
	if (d.tleHardScale <= 0.0) d.tleHardScale = 1.5;
	if (d.memHardScale <= 0.0) d.memHardScale = 1.5;
	return d;
}

static void saveSettings(const SettingsData &d) {
	QJsonObject o;
	o["refFactor"]      = d.refFactor;
	o["tleHardScale"]   = d.tleHardScale;
	o["memHardScale"]   = d.memHardScale;
	o["runHotkey"]      = d.runHotkey;
	o["browseHotkey"]   = d.browseHotkey;
	o["settingsHotkey"] = d.settingsHotkey;
	o["savedAt"]        = (double)QDateTime::currentMSecsSinceEpoch();
	const QByteArray ba = QJsonDocument(o).toJson(QJsonDocument::Indented);

	// 主写 ./ ; 失败回退 AppData
	bool primaryOk = false;
	QFile pf(settingsPortablePath());
	if (pf.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
		pf.write(ba); pf.close(); primaryOk = true;
	}
	if (!primaryOk) {
		QFile af(settingsAppDataPath());
		QDir().mkpath(QFileInfo(settingsAppDataPath()).absolutePath());
		if (af.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
			af.write(ba); af.close();
		}
	} else {
		// 双写镜像, 保证只读目录下次启动也能读到最新值
		QFile af(settingsAppDataPath());
		QDir().mkpath(QFileInfo(settingsAppDataPath()).absolutePath());
		if (af.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
			af.write(ba); af.close();
		}
	}
}

// ==================== 5. 设置子窗口 (仅关闭按钮, 固定大小, 非模态) ====================
class SettingsDialog : public QDialog {
  public:
explicit SettingsDialog(SettingsData *data, QWidget *parent = nullptr)
: QDialog(parent), m_data(data) {
setWindowTitle("设置");
setWindowFlags(Qt::Dialog | Qt::WindowCloseButtonHint);
setWindowModality(Qt::NonModal);

QVBoxLayout *root = new QVBoxLayout(this);
root->setContentsMargins(10,10,10,8);
root->setSpacing(6);

// 现代风格: 左侧导航 + 右侧内容
QHBoxLayout *body = new QHBoxLayout();
body->setSpacing(8);
m_navList = new QListWidget(this);
m_navList->setFixedWidth(140);
m_navList->addItem("评测参数");
m_navList->addItem("热键");
body->addWidget(m_navList);

m_stack = new QStackedWidget(this);
QWidget *page1 = new QWidget(this);
QFormLayout *f1 = new QFormLayout(page1);
f1->setContentsMargins(8,6,8,6);
m_refFactorSpin = new QDoubleSpinBox(page1);
m_refFactorSpin->setRange(0.1, 100.0); m_refFactorSpin->setDecimals(2);
m_refFactorSpin->setValue(m_data->refFactor);
f1->addRow("参考机因子 (IPC×GHz):", m_refFactorSpin);
m_tleHardSpin = new QDoubleSpinBox(page1);
m_tleHardSpin->setRange(0.1, 10.0); m_tleHardSpin->setDecimals(2);
m_tleHardSpin->setValue(m_data->tleHardScale);
f1->addRow("TLE 硬线比例:", m_tleHardSpin);
m_memHardSpin = new QDoubleSpinBox(page1);
m_memHardSpin->setRange(0.1, 10.0); m_memHardSpin->setDecimals(2);
m_memHardSpin->setValue(m_data->memHardScale);
f1->addRow("MEM 硬线比例:", m_memHardSpin);
m_stack->addWidget(page1);

QWidget *page2 = new QWidget(this);
QFormLayout *f2 = new QFormLayout(page2);
f2->setContentsMargins(8,6,8,6);
m_runHotEdit = new QKeySequenceEdit(page2);
m_runHotEdit->setKeySequence(QKeySequence(m_data->runHotkey));
f2->addRow("开始评测:", m_runHotEdit);
m_browseHotEdit = new QKeySequenceEdit(page2);
m_browseHotEdit->setKeySequence(QKeySequence(m_data->browseHotkey));
f2->addRow("浏览文件:", m_browseHotEdit);
m_settingsHotEdit = new QKeySequenceEdit(page2);
m_settingsHotEdit->setKeySequence(QKeySequence(m_data->settingsHotkey));
f2->addRow("打开设置:", m_settingsHotEdit);
m_stack->addWidget(page2);

body->addWidget(m_stack, 1);
root->addLayout(body);

QHBoxLayout *btns = new QHBoxLayout();
QPushButton *defBtn = new QPushButton("恢复默认", this);
QPushButton *closeBtn = new QPushButton("关闭", this);
btns->addWidget(defBtn); btns->addStretch(); btns->addWidget(closeBtn);
root->addLayout(btns);

setFixedSize(640, 380);
m_navList->setCurrentRow(0);
connect(m_navList, &QListWidget::currentRowChanged, m_stack, &QStackedWidget::setCurrentIndex);
connect(defBtn, &QPushButton::clicked, this, [this]() {
SettingsData d;
m_refFactorSpin->setValue(d.refFactor);
m_tleHardSpin->setValue(d.tleHardScale);
m_memHardSpin->setValue(d.memHardScale);
m_runHotEdit->setKeySequence(QKeySequence(d.runHotkey));
m_browseHotEdit->setKeySequence(QKeySequence(d.browseHotkey));
m_settingsHotEdit->setKeySequence(QKeySequence(d.settingsHotkey));
});
connect(closeBtn, &QPushButton::clicked, this, [this]() {
commit(); done(QDialog::Accepted);
});
}

void commit() {
m_data->refFactor      = m_refFactorSpin->value();
m_data->tleHardScale   = m_tleHardSpin->value();
m_data->memHardScale   = m_memHardSpin->value();
m_data->runHotkey      = m_runHotEdit->keySequence().toString();
m_data->browseHotkey   = m_browseHotEdit->keySequence().toString();
m_data->settingsHotkey = m_settingsHotEdit->keySequence().toString();
if (m_data->runHotkey.isEmpty())      m_data->runHotkey = "F5";
if (m_data->browseHotkey.isEmpty())   m_data->browseHotkey = "Ctrl+O";
if (m_data->settingsHotkey.isEmpty()) m_data->settingsHotkey = "Ctrl+,";
saveSettings(*m_data);
}

  protected:
void reject() override {
commit(); QDialog::reject();
}

  private:
SettingsData *m_data;
QListWidget *m_navList;
QStackedWidget *m_stack;
QDoubleSpinBox *m_refFactorSpin;
QDoubleSpinBox *m_tleHardSpin;
QDoubleSpinBox *m_memHardSpin;
QKeySequenceEdit *m_runHotEdit;
QKeySequenceEdit *m_browseHotEdit;
QKeySequenceEdit *m_settingsHotEdit;
};
// ==================== 6. Mica 核心窗口 ====================
class MicaWindow :public QMainWindow {
  public:
	MicaWindow(QWidget *parent = nullptr) :QMainWindow(parent) {
		setWindowTitle("MB 评测器");
		resize(920,680);
		setAttribute(Qt::WA_TranslucentBackground);
		setAttribute(Qt::WA_OpaquePaintEvent);
		m_settings = loadSettings();

		QWidget *central = new QWidget(this);
		setCentralWidget(central);
		central->setAttribute(Qt::WA_TranslucentBackground);

		QVBoxLayout *mainLayout = new QVBoxLayout(central);
		mainLayout->setContentsMargins(8,8,8,8);
		mainLayout->setSpacing(4);

		m_cardWidget = new CardWidget(this);
		QVBoxLayout *cardLayout = new QVBoxLayout(m_cardWidget);
		cardLayout->setContentsMargins(10,8,10,8);
		cardLayout->setSpacing(4);

		m_outputEdit = new QTextEdit(this);
		m_outputEdit->setReadOnly(true);
		// 结果区按行滚动 (匹配 Windows 默认 3 行/格)
		m_outputEdit->verticalScrollBar()->setSingleStep(m_outputEdit->fontMetrics().height());

		QHBoxLayout *pathLayout = new QHBoxLayout();
		pathLayout->setSpacing(4);
		QLabel *srcLabel = new QLabel("源文件:",this);
		pathLayout->addWidget(srcLabel);
		m_filePathEdit = new CustomLineEdit(this);
		m_filePathEdit->setPlaceholderText("请选择 .cpp 源文件");
		m_filePathEdit->setReadOnly(false);
		connect(m_filePathEdit,&CustomLineEdit::editingFinished,this,[this]() {
			QString text = m_filePathEdit->text().trimmed();
			if (QFileInfo::exists(text)) {
				m_currentSource = text;
				m_runBtn->setEnabled(true);
			}
		});
		m_browseBtn = new QPushButton("浏览",this);
		m_browseBtn->setToolTip("使用文件资源管理器选择文件.");
		connect(m_browseBtn,&QPushButton::clicked,this,[this]() {
			QString file = QFileDialog::getOpenFileName(this,"选择 C++ 源文件","","C++ 文件 (*.cpp);;所有文件 (*.*)");
			if (!file.isEmpty()) {
				m_filePathEdit->setText(file);
				m_runBtn->setEnabled(true);
				m_currentSource = file;
			}
		});
		pathLayout->addWidget(m_filePathEdit,1);
		pathLayout->addWidget(m_browseBtn);

		QHBoxLayout *paramLayout = new QHBoxLayout();
		paramLayout->setSpacing(4);
		QLabel *flagsLabel = new QLabel("编译参数:",this);
		flagsLabel->setToolTip("传给编译器的参数,例如 -std=c++17 -O2 -Wall.");
		m_flagsEdit = new CustomLineEdit(this);
		m_flagsEdit->setText("-std=c++17 -O2 -Wall");
		m_flagsEdit->setToolTip(flagsLabel->toolTip());
		QLabel *coreLabel = new QLabel("核心:",this);
		coreLabel->setToolTip("把被评测程序绑定的 CPU 核心编号.\n"
			"输入 1~N 对应第 1~N 个逻辑处理器.\n"
			"-1 = 不绑定,由系统调度.");
		m_coreEdit = new CustomLineEdit(this);
		m_coreEdit->setText("-1");
		m_coreEdit->setFixedWidth(60);
		m_coreEdit->setToolTip(coreLabel->toolTip());
		m_runBtn = new QPushButton("开始评测",this);
		m_runBtn->setToolTip("编译并运行当前 C++ 源文件,自动探测同目录下与源文件同名的测试组\n"
			"(aaa-N.in / aaa-N.out 或 aaa_N.in / aaa_N.out, N=1..100),\n"
			"逐个评测并输出汇总表格与综合评价.");
		m_runBtn->setEnabled(false);
		m_settingsBtn = new QPushButton("设置",this);
		m_settingsBtn->setToolTip("打开设置 (参考机因子/硬线比例/热键).");
		connect(m_settingsBtn,&QPushButton::clicked,this,[this]() { openSettings(); });
		connect(m_runBtn,&QPushButton::clicked,this,[this]() {
			if (m_currentSource.isEmpty()) return;
			QString flags = m_flagsEdit->text();
			if (flags.isEmpty()) flags = "-std=c++17 -O2 -Wall";
			int rawCore = m_coreEdit->text().toInt();   // 用户输入:1 起
			int core = rawCore;
			if (core > 0) core -= 1;           // 内部:0 起
			if (core < -1) core = -1;

			m_runBtn->setEnabled(false);
			// 不清屏:保留上次测评结果供对比

			appendOutput("==================== 评测开始 ====================\n");
			appendOutput("文件:" + m_currentSource + "\n");
			appendOutput("编译参数:" + flags + "\n");
			appendOutput("绑核:" + QString(rawCore == -1 ? "不绑(自适应)" :QString::number(rawCore)) + "\t");

			double refFactor = m_refFactorEdit->text().toDouble();
			if (refFactor <= 0.0) refFactor = myd::kDefaultRefFactor;
			if (refFactor != m_lastRefFactor) {
				refreshCalibration();
			} else {
				appendOutput(QString("速度因子:%1  (参考机 %2 指令/秒)\n")
					.arg(myd::OJTimer::getInstance().getSpeedFactor(),0,'f',4)
					.arg(myd::OJTimer::getInstance().getRefIPS(),0,'f',2));
			}

			JudgeLimits limits;
			limits.ojLimitMs     = m_ojLimitEdit->text().toDouble();
			limits.memLimitMB    = m_memLimitEdit->text().toULongLong();
			limits.tleHardScale  = m_settings.tleHardScale;
			limits.memHardScale  = m_settings.memHardScale;
			if (limits.ojLimitMs <= 0.0) limits.ojLimitMs = 1000.0;
			if (limits.memLimitMB == 0) limits.memLimitMB = 256;
			appendOutput(QString("OJ时限:%1 ms\t内存限制:%2 MB\n")
				.arg(limits.ojLimitMs,0,'f',0).arg(limits.memLimitMB));

			// 多组评测: 探测 aaa-1.in/aaa_2.out 等 (最多 100 组), 逐组运行并汇总表格
			QVector<TestCaseResult> results;
			QString compileOut, tableOut;
			bool evalOk = m_core->evaluateGroups(m_currentSource,flags,core,
				limits,
				compileOut,tableOut,results);
			appendOutput(compileOut);
			if (!evalOk) {
				appendOutput("编译错误 (CE)\n");
				m_runBtn->setEnabled(true);
				return;
			}
			appendOutput(tableOut);

			appendOutput("\n==================== 评测结束 ====================\n\n");
			m_runBtn->setEnabled(true);
		});

		paramLayout->addWidget(flagsLabel);
		paramLayout->addWidget(m_flagsEdit,1);
		paramLayout->addWidget(coreLabel);
		paramLayout->addWidget(m_coreEdit);
		paramLayout->addStretch();

		QHBoxLayout *limitLayout = new QHBoxLayout();
		limitLayout->setSpacing(4);
		QLabel *limitLabel = new QLabel("OJ时限(ms):",this);
		m_ojLimitEdit = new CustomLineEdit(this);
		m_ojLimitEdit->setText("1000");
		m_ojLimitEdit->setFixedWidth(64);
		QLabel *memLabel = new QLabel("内存(MB):",this);
		m_memLimitEdit = new CustomLineEdit(this);
		m_memLimitEdit->setText("256");
		m_memLimitEdit->setFixedWidth(56);
		QLabel *refLabel = new QLabel("参考机因子:",this);
		m_refFactorEdit = new CustomLineEdit(this);
		m_refFactorEdit->setText("3");
		m_refFactorEdit->setFixedWidth(48);
		m_calibBtn = new QPushButton("重新校准",this);
		connect(m_calibBtn,&QPushButton::clicked,this,[this]() {
			refreshCalibration();
		});

		// 参数说明 (悬停提示)
		limitLabel->setToolTip("题目标准时限(ms),以参考机刻度计.\n"
			"本地等效时限 = 时限 / 速度因子,\n"
			"超过即判 TLE-CPU.");
		memLabel->setToolTip("内存软限(MB). 峰值专用内存超过该值判 MLE.\n"
			"内存硬杀线 = 软限 × MEM硬线比例(设置中).");
		refLabel->setToolTip("参考机因子 = IPC × GHz, 参考指令数 = 因子 × 1e8 条/秒.\n"
			"默认 3 (等价 IPC=1 × GHz=3).");
		m_calibBtn->setToolTip("用上方参考机因子重新测量本机 4 个域(整型/浮点/内存/分支)\n"
			"的指令吞吐并重算综合速度因子.");
		m_ojLimitEdit->setToolTip(limitLabel->toolTip());
		m_memLimitEdit->setToolTip(memLabel->toolTip());
		m_refFactorEdit->setToolTip(refLabel->toolTip());

		limitLayout->addWidget(limitLabel);
		limitLayout->addWidget(m_ojLimitEdit);
		limitLayout->addWidget(memLabel);
		limitLayout->addWidget(m_memLimitEdit);
		limitLayout->addWidget(refLabel);
		limitLayout->addWidget(m_refFactorEdit);
		limitLayout->addStretch();

		cardLayout->addLayout(pathLayout);
		cardLayout->addLayout(paramLayout);
		cardLayout->addLayout(limitLayout);
		cardLayout->addWidget(m_outputEdit,1);

		// 操作按钮聚拢右下角 (次要动作在左, 主动作在右)
		QHBoxLayout *buttonRow = new QHBoxLayout();
		buttonRow->setSpacing(8);
		buttonRow->addStretch();
		buttonRow->addWidget(m_calibBtn);
		buttonRow->addWidget(m_settingsBtn);
		buttonRow->addWidget(m_runBtn);
		cardLayout->addLayout(buttonRow);
		mainLayout->addWidget(m_cardWidget);

		QString exeDir = QCoreApplication::applicationDirPath();
		m_core = new EvaluatorCore(exeDir);
		applySettings();
		refreshCalibration();
	}

	~MicaWindow() { delete m_core; }

  protected:
	bool nativeEvent(const QByteArray &eventType,void *message,qintptr *result) override {
#ifdef Q_OS_WIN
		if (eventType == "windows_generic_MSG") {
			MSG *msg = reinterpret_cast<MSG*>(message);
			if (msg->message == WM_ERASEBKGND) {
				*result = 1;
				return true;
			}
		}
#endif
		return QMainWindow::nativeEvent(eventType,message,result);
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
	CustomLineEdit *m_memLimitEdit;
	CustomLineEdit *m_refFactorEdit;
	QPushButton *m_calibBtn;
	QTextEdit *m_outputEdit;
	QPushButton *m_settingsBtn;
	QShortcut *m_runShortcut = nullptr;
	QShortcut *m_browseShortcut = nullptr;
	QShortcut *m_settingsShortcut = nullptr;
	CardWidget *m_cardWidget = nullptr;
	SettingsData m_settings;
	QString m_currentSource;
	double m_lastRefFactor = myd::kDefaultRefFactor;
	EvaluatorCore *m_core;

	void refreshCalibration() {
		double refFactor = m_refFactorEdit->text().toDouble();
		if (refFactor <= 0.0) refFactor = myd::kDefaultRefFactor;
		int rawCore = m_coreEdit->text().toInt();
		int core = rawCore;
		if (core > 0) core -= 1;           // 1起→0起
		if (core < -1) core = -1;
		m_lastRefFactor = refFactor;

		appendOutput("==================== CPU校准 ====================\n");
		appendOutput(QString("绑核:%1\n").arg(rawCore < 0 ? "不绑(自适应)" :QString::number(rawCore)));
		myd::OJTimerResult res = myd::OJTimer::getInstance().doCalibrate(core,refFactor);
		appendOutput(QString("参考机因子:%1 (IPC×GHz), 参考指令数 = %2 指令/秒\n")
			.arg(refFactor,0,'f',3).arg(res.refIPS,0,'f',3));
		static const char* const dom[] = { "整型","浮点","内存","分支" };
		for (int d = 0; d < 4; ++d) {
			appendOutput(QString("  %1:IPS=%2e9  本地CPU=%3ms  因子=%4\n")
				.arg(dom[d])
				.arg(res.domainIPS[d] / 1e9,0,'f',4)
				.arg(res.domainCPUMs[d],0,'f',1)
				.arg(res.domainFactor[d],0,'f',4));
		}
		appendOutput(QString("综合速度因子:%1\t")
			.arg(res.speedFactor,0,'f',4));
		appendOutput(QString("校准耗时:%1 ms%2\t")
			.arg(res.calibWallMs,0,'f',0)
			.arg(res.stable ? "" :"  (警告:校准结果异常)"));
		appendOutput(QString("实测综合 IPS:%1e9\t")
			.arg(res.refIPS * res.speedFactor / 1e9, 0, 'f', 4));
		appendOutput("\n==================== 校准结束 ====================\n\n");
	}

	void applySettings() {
		applyPaletteTheme(isDarkMode());
		applyShortcuts();
	}

	void applyShortcuts() {
		if (!m_runShortcut) {
			m_runShortcut = new QShortcut(this);
			connect(m_runShortcut,&QShortcut::activated,this,[this](){ if (m_runBtn->isEnabled()) m_runBtn->click(); });
		}
		if (!m_browseShortcut) {
			m_browseShortcut = new QShortcut(this);
			connect(m_browseShortcut,&QShortcut::activated,this,[this](){ m_browseBtn->click(); });
		}
		if (!m_settingsShortcut) {
			m_settingsShortcut = new QShortcut(this);
			connect(m_settingsShortcut,&QShortcut::activated,this,[this](){ openSettings(); });
		}
		m_runShortcut->setKey(QKeySequence(m_settings.runHotkey));
		m_browseShortcut->setKey(QKeySequence(m_settings.browseHotkey));
		m_settingsShortcut->setKey(QKeySequence(m_settings.settingsHotkey));
	}

	void openSettings() {
		SettingsDialog *dlg = new SettingsDialog(&m_settings, this);
		connect(dlg, &QDialog::finished, this, [this](int) { applySettings(); });
		dlg->setAttribute(Qt::WA_DeleteOnClose);
		dlg->show();
	}

	void appendOutput(const QString &text) {
		m_outputEdit->moveCursor(QTextCursor::End);
		m_outputEdit->insertPlainText(text);
		m_outputEdit->moveCursor(QTextCursor::End);
	}

	void applyPaletteTheme(bool darkMode) {
		QColor realTextColor = darkMode ? Qt::white : Qt::black;
		QColor placeholderColor = darkMode ? QColor(160,160,160) : QColor(100,100,100);
		QColor baseInputColor = darkMode ? QColor(32,32,32) : Qt::white;

		// 输入框/结果区: 回滚到透明蒙版前的纯色外观 (无 QSS 边框, 无蓝色底边)
		QList<QWidget*> widgets = {
			m_filePathEdit,m_flagsEdit,m_coreEdit,
			m_ojLimitEdit,m_memLimitEdit,m_refFactorEdit,
			m_outputEdit
		};
		for(QWidget* w : widgets) {
			w->setStyleSheet(QString());
			QPalette pal = w->palette();
			pal.setColor(QPalette::Text,realTextColor);
			pal.setColor(QPalette::PlaceholderText,placeholderColor);
			pal.setColor(QPalette::Base,baseInputColor);
			pal.setColor(QPalette::Highlight,QColor(0,120,212));
			pal.setColor(QPalette::HighlightedText,Qt::white);
			w->setPalette(pal);
		}
	}

	bool isDarkMode() {
#ifdef Q_OS_WIN
		DWORD value = 1;
		DWORD size = sizeof(value);
		if (RegGetValueW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",L"AppsUseLightTheme",RRF_RT_DWORD,NULL,&value,&size) == ERROR_SUCCESS) {
			return (value == 0);
		}
#endif
		return false;
	}
	void enableMica() {
		if (QWidget *c = centralWidget()) c->setAutoFillBackground(false);
#ifdef Q_OS_WIN
		HWND hwnd = reinterpret_cast<HWND>(this->winId());
		if (!hwnd) return;
		MARGINS margins = { -1,-1,-1,-1 };
		DwmExtendFrameIntoClientArea(hwnd,&margins);
		int micaType = 2;
		DwmSetWindowAttribute(hwnd,38,&micaType,sizeof(micaType));
		BOOL useDark = isDarkMode() ? TRUE :FALSE;
		DwmSetWindowAttribute(hwnd,20,&useDark,sizeof(useDark));
		SetWindowPos(hwnd,NULL,0,0,0,0,SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED);
#endif
	}
};

int main(int argc,char *argv[]) {
	QApplication app(argc,argv);

	// 全局字体:Cascadia Code (回退 Consolas → 微软雅黑 UI)
	QFont appFont = app.font();
	appFont.setFamilies({"Cascadia Code","Consolas","Microsoft YaHei UI"});
	app.setFont(appFont);

	MicaWindow w;
	w.show();
	return app.exec();
}