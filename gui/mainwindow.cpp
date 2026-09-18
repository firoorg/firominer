#include "mainwindow.h"

#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QHeaderView>
#include <QHostInfo>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScreen>
#include <QSettings>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyleHints>
#include <QSystemTrayIcon>
#include <QTableWidget>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
#include <QAccessibilityHints>
#elif defined(Q_OS_WIN)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace
{
bool highContrastEnabled()
{
    bool highContrast = false;
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    highContrast = QGuiApplication::styleHints()->accessibility()->contrastPreference() == Qt::ContrastPreference::HighContrast;
#elif defined(Q_OS_WIN)
    HIGHCONTRAST settings{};
    settings.cbSize = sizeof(settings);
    highContrast = SystemParametersInfo(SPI_GETHIGHCONTRAST, sizeof(settings), &settings, 0) &&
        (settings.dwFlags & HCF_HIGHCONTRASTON);
#endif
    return highContrast;
}

QLabel* label(const QString& text, const char* role = nullptr)
{
    auto* result = new QLabel(text);
    result->setTextFormat(Qt::PlainText);
    if (role)
        result->setProperty("role", role);
    return result;
}

QFrame* panel()
{
    auto* frame = new QFrame;
    frame->setObjectName("panel");
    return frame;
}

QPushButton* button(const QString& text, const char* role = nullptr)
{
    auto* result = new QPushButton(text);
    result->setCursor(Qt::PointingHandCursor);
    if (role)
        result->setProperty("role", role);
    return result;
}

QWidget* scrollPage(QWidget* content)
{
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(content);
    return scroll;
}

double hashValue(const QJsonValue& value)
{
    bool ok = false;
    const double result = value.isString() ? value.toString().toULongLong(&ok, 16) : value.toDouble();
    return (value.isString() && !ok) || !std::isfinite(result) || result < 0 ? 0 : result / 1000000.;
}

QString sensor(const QJsonValue& value, const QString& unit, bool allowZero = false)
{
    if (!value.isDouble() || !std::isfinite(value.toDouble()) || value.toDouble() < 0 ||
        (!allowZero && value.toDouble() == 0))
        return QStringLiteral("Unavailable");
    return QString::number(value.toDouble(), 'f', 0) + unit;
}

QString abbreviated(const QString& text)
{
    return text.size() > 22 ? text.left(10) + QString::fromUtf8("…") + text.right(8) : text;
}

QString runtimeText(qint64 seconds)
{
    return QString("Running for %1h %2m").arg(seconds / 3600).arg((seconds / 60) % 60);
}
}

// The chart needs only a short, bounded session history, not a charting dependency.
class HashrateChart : public QWidget
{
public:
    explicit HashrateChart(QWidget* parent = nullptr) : QWidget(parent)
    {
        setMinimumHeight(210);
        setAccessibleName("Local hashrate history");
        setAccessibleDescription("Use View history for timestamped hashrate readings.");
    }
    void add(double value)
    {
        const auto now = QDateTime::currentSecsSinceEpoch();
        if (points_.isEmpty() || now - points_.last().x() >= 5)
            points_.append(QPointF(now, value));
        else
            points_.last().setY(value);
        while (!points_.isEmpty() && (points_.first().x() < now - 21600 || points_.size() > 4321))
            points_.removeFirst();
        update();
    }
    void reset() { points_.clear(); update(); }
    void setRange(int seconds) { range_ = seconds; update(); }
    void showHistory()
    {
        QDialog dialog(this);
        dialog.setPalette(palette());
        dialog.setWindowTitle("Hashrate history");
        dialog.resize(440, 360);
        auto* layout = new QVBoxLayout(&dialog);
        auto* table = new QTableWidget(0, 2);
        table->setObjectName("hashrateHistory");
        table->setAccessibleName("Timestamped hashrate readings");
        table->setHorizontalHeaderLabels({"Time", "Hashrate (MH/s)"});
        table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        const auto earliest = QDateTime::currentSecsSinceEpoch() - range_;
        for (const auto& point : points_)
        {
            if (point.x() < earliest)
                continue;
            const int row = table->rowCount();
            table->insertRow(row);
            table->setItem(row, 0, new QTableWidgetItem(QDateTime::fromSecsSinceEpoch(qint64(point.x())).toString("HH:mm:ss")));
            table->setItem(row, 1, new QTableWidgetItem(QString::number(point.y(), 'f', 1)));
        }
        layout->addWidget(table);
        auto* close = new QDialogButtonBox(QDialogButtonBox::Close);
        connect(close, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        layout->addWidget(close);
        dialog.exec();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const auto colors = window()->palette();
        const QRectF plot(43, 18, width() - 57, height() - 53);
        if (plot.width() <= 0 || plot.height() <= 0)
            return;
        const auto now = QDateTime::currentSecsSinceEpoch();
        double maximum = 10;
        for (const auto& point : points_)
            if (point.x() >= now - range_)
                maximum = std::max(maximum, point.y() * 1.15);
        maximum = std::ceil(maximum / 10) * 10;
        QFont axisFont = font();
        axisFont.setPointSize(9);
        painter.setFont(axisFont);
        for (int i = 0; i <= 3; ++i)
        {
            const auto y = plot.bottom() - i * plot.height() / 3;
            painter.setPen(colors.color(QPalette::Mid));
            painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
            painter.setPen(colors.color(QPalette::WindowText));
            painter.drawText(QRectF(0, y - 10, 35, 20), Qt::AlignRight | Qt::AlignVCenter,
                QString::number(maximum * i / 3, 'f', 0));
        }
        for (int i = 0; i <= 4; ++i)
        {
            const auto x = plot.left() + i * plot.width() / 4;
            const auto time = now - range_ + i * range_ / 4;
            painter.setPen(colors.color(QPalette::WindowText));
            painter.drawText(QRectF(x - 25, plot.bottom() + 10, 50, 20), Qt::AlignCenter,
                QDateTime::fromSecsSinceEpoch(time).toString("HH:mm"));
        }
        if (points_.isEmpty())
        {
            painter.setPen(colors.color(QPalette::WindowText));
            painter.drawText(plot, Qt::AlignCenter, "Hashrate history appears when mining starts");
            return;
        }
        QPainterPath line;
        bool first = true;
        double previousTime = 0;
        for (const auto& point : points_)
        {
            if (point.x() < now - range_)
                continue;
            const QPointF p(plot.right() - (now - point.x()) / range_ * plot.width(),
                plot.bottom() - point.y() / maximum * plot.height());
            if (first || point.x() - previousTime > 15)
                line.moveTo(p);
            else
                line.lineTo(p);
            first = false;
            previousTime = point.x();
        }
        painter.setClipRect(plot.adjusted(-1, -1, 1, 1));
        painter.setPen(QPen(highContrastEnabled() || colors.color(QPalette::Window).lightness() < 128 ?
            colors.color(QPalette::Highlight) : QColor("#9b1c2e"), 2));
        painter.drawPath(line);
        if (points_.size() == 1)
            painter.drawEllipse(line.currentPosition(), 2, 2);
    }
private:
    QList<QPointF> points_;
    int range_ = 3600;
};

void MainWindow::updateTheme()
{
    if (highContrastEnabled() || theme_ == "system")
    {
        // A user's high-contrast setting always takes precedence over the theme.
        setStyleSheet(R"(
            QLabel[role="heading"] { font-size: 29px; font-weight: 650; }
            QLabel[role="section"] { font-size: 17px; font-weight: 650; }
            QLabel[role="metric"] { font-size: 35px; font-weight: 650; }
            QLabel#brand { font-size: 24px; font-weight: 650; }
            QLabel#notice { padding: 12px; }
            QListWidget#navigation::item { height: 48px; padding-left: 15px; margin-bottom: 6px; }
            QPushButton { padding: 10px 16px; }
            QPushButton[role="range"] { padding: 5px 10px; }
            QLineEdit, QComboBox { padding: 9px; min-height: 20px; }
        )");
        setPalette(QApplication::palette());
        return;
    }
    const bool dark = theme_ == "dark";
    QPalette colors = QApplication::palette();
    auto color = [&](QPalette::ColorRole role, const char* light, const char* darkColor) {
        colors.setColor(role, QColor(dark ? darkColor : light));
    };
    color(QPalette::Window, "#f6f6f4", "#1b1d21");
    color(QPalette::WindowText, "#24262b", "#ededf0");
    color(QPalette::Base, "#ffffff", "#26282d");
    color(QPalette::AlternateBase, "#f6f6f4", "#303239");
    color(QPalette::Text, "#24262b", "#ededf0");
    color(QPalette::Button, "#ffffff", "#26282d");
    color(QPalette::ButtonText, "#24262b", "#ededf0");
    color(QPalette::BrightText, "#ffffff", "#ffffff");
    color(QPalette::Highlight, "#9b1c2e", "#ec9caa");
    color(QPalette::HighlightedText, "#ffffff", "#24262b");
    color(QPalette::Link, "#9b1c2e", "#ec9caa");
    color(QPalette::LinkVisited, "#682331", "#dbb1c4");
    color(QPalette::ToolTipBase, "#ffffff", "#26282d");
    color(QPalette::ToolTipText, "#24262b", "#ededf0");
    color(QPalette::PlaceholderText, "#606570", "#b5b8c2");
    color(QPalette::Light, "#ffffff", "#4d5059");
    color(QPalette::Midlight, "#ececef", "#3b3e46");
    color(QPalette::Mid, "#dedfe3", "#42454e");
    color(QPalette::Dark, "#94979e", "#16171a");
    color(QPalette::Shadow, "#727681", "#101114");
    for (const auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        colors.setColor(QPalette::Disabled, role, QColor(dark ? "#9397a2" : "#81848c"));
    QString sheet = R"(
        QMainWindow, QWidget#workspace, QScrollArea, QScrollArea > QWidget > QWidget { background: #f6f6f4; }
        QDialog, QMenu, QMessageBox { background: #f6f6f4; }
        QWidget { color: #24262b; }
        QLabel { background: transparent; }
        QLabel[role="muted"] { color: #606570; }
        QLabel[role="heading"] { font-size: 29px; font-weight: 650; }
        QLabel[role="section"] { font-size: 17px; font-weight: 650; }
        QLabel[role="metric"] { font-size: 35px; font-weight: 650; }
        QLabel#notice { background: #fff0e9; color: #81321b; padding: 12px; border-radius: 6px; }
        QFrame#panel { background: white; border: 1px solid #dedfe3; border-radius: 8px; }
        QFrame#sidebar { background: #242527; }
        QFrame#sidebar QLabel { color: white; }
        QLabel#brand { font-size: 24px; font-weight: 650; }
        QListWidget#navigation { background: transparent; border: none; outline: none; color: #e7e8eb; }
        QListWidget#navigation::item { height: 48px; border-radius: 6px; padding-left: 15px; margin-bottom: 6px; }
        QListWidget#navigation::item:selected { background: #682331; color: white; }
        QListWidget#navigation::item:hover:!selected { background: #343538; }
        QPushButton { background: white; border: 1px solid #d8d9dd; border-radius: 6px; padding: 10px 16px; }
        QPushButton:hover { background: #f0f0f1; }
        QPushButton:focus { border: 2px solid #9b1c2e; }
        QPushButton:disabled { color: #94979e; background: #eeeeef; }
        QPushButton[role="primary"] { background: #9b1c2e; border-color: #9b1c2e; color: white; font-weight: 600; }
        QPushButton[role="primary"]:hover { background: #801526; }
        QPushButton[role="primary"]:disabled { background: #b58089; border-color: #b58089; }
        QPushButton[role="link"] { color: #9b1c2e; background: transparent; border: none; text-align: left; padding-left: 0; }
        QPushButton[role="sidebar"] { color: #e1e2e5; background: transparent; border: none; text-align: left; }
        QPushButton[role="sidebar"]:hover { background: #343538; }
        QPushButton[role="range"] { padding: 5px 10px; }
        QPushButton[role="range"]:checked { background: #9b1c2e; color: white; border-color: #9b1c2e; }
        QLineEdit, QComboBox { background: white; border: 1px solid #d7d9de; border-radius: 5px; padding: 9px; min-height: 20px; }
        QLineEdit:focus, QComboBox:focus { border: 1px solid #9b1c2e; }
        QLineEdit:disabled, QComboBox:disabled { background: #f0f0f1; color: #81848c; }
        QTableWidget { border: none; background: white; gridline-color: #ececef; selection-background-color: #f8e9ec; selection-color: #24262b; }
        QHeaderView::section { background: white; color: #606570; border: none; border-bottom: 1px solid #e5e6e9; padding: 8px 5px; text-align: left; }
        QPlainTextEdit { background: white; border: 1px solid #dedfe3; border-radius: 6px; padding: 10px; font-family: "Consolas", monospace; font-size: 12px; }
        QComboBox QAbstractItemView { background: white; color: #24262b; selection-background-color: #9b1c2e; selection-color: white; }
        QStatusBar { background: #f6f6f4; color: #606570; }
    )";
    if (dark)
    {
        sheet.replace("#f6f6f4", "#1b1d21");
        sheet.replace("background: white", "background: #26282d");
        sheet.replace("#24262b", "#ededf0");
        sheet.replace("#606570", "#b5b8c2");
        for (const auto* border : {"#dedfe3", "#d8d9dd", "#d7d9de", "#e5e6e9", "#ececef"})
            sheet.replace(border, "#42454e");
        sheet.replace("#f0f0f1", "#34363d");
        sheet.replace("#eeeeef", "#34363d");
        sheet.replace("#f8e9ec", "#682331");
        sheet.replace("#fff0e9", "#493328");
        sheet.replace("#81321b", "#ffcfac");
        sheet.replace("color: #9b1c2e; background: transparent", "color: #ec9caa; background: transparent");
        sheet.replace("border: 2px solid #9b1c2e", "border: 2px solid #ec9caa");
        sheet.replace("border: 1px solid #9b1c2e", "border: 1px solid #ec9caa");
    }
    setStyleSheet(sheet);
    setPalette(colors);
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == qApp && event->type() == QEvent::ApplicationPaletteChange)
        // Let Qt finish propagating the application palette before repolishing
        // our stylesheet; otherwise Qt 6.2 can restore a child's old colors.
        QTimer::singleShot(0, this, &MainWindow::updateTheme);
    return QMainWindow::eventFilter(watched, event);
}

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent), controller_(this)
{
    setWindowTitle("Firominer");
    setWindowIcon(QIcon(":/firominer.ico"));
    resize(1400, 900);
    const QSize available = screen()->availableGeometry().size() - QSize(40, 80);
    setMinimumSize(QSize(640, 400).boundedTo(available));
    updateTheme();
    qApp->installEventFilter(this);
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    connect(QGuiApplication::styleHints()->accessibility(), &QAccessibilityHints::contrastPreferenceChanged,
        this, [this] { updateTheme(); });
#endif

    auto* shell = new QWidget;
    auto* outer = new QHBoxLayout(shell);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    auto* sidebar = new QFrame;
    sidebar->setObjectName("sidebar");
    sidebar->setFixedWidth(210);
    auto* side = new QVBoxLayout(sidebar);
    side->setContentsMargins(14, 25, 14, 20);
    auto* brand = new QHBoxLayout;
    auto* icon = label("");
    icon->setPixmap(windowIcon().pixmap(42, 42));
    auto* name = label("firominer");
    name->setObjectName("brand");
    brand->addWidget(icon);
    brand->addWidget(name);
    side->addLayout(brand);
    side->addSpacing(25);
    navigation_ = new QListWidget;
    navigation_->setObjectName("navigation");
    navigation_->setAccessibleName("Navigation");
    navigation_->addItems({"Overview", "Mining setup", "Devices", "Activity"});
    navigation_->setMinimumHeight(220);
    side->addWidget(navigation_, 1);
    auto* settings = button("Settings", "sidebar");
    settings->setObjectName("settingsButton");
    auto* help = button("Help", "sidebar");
    side->addWidget(settings);
    side->addWidget(help);
    outer->addWidget(sidebar);

    auto* workspace = new QWidget;
    workspace->setObjectName("workspace");
    auto* content = new QVBoxLayout(workspace);
    content->setContentsMargins(27, 25, 27, 12);
    content->setSpacing(18);
    auto* header = new QHBoxLayout;
    auto* heading = new QVBoxLayout;
    subtitle_ = label("THIS COMPUTER", "muted");
    title_ = label("Mining overview", "heading");
    heading->addWidget(subtitle_);
    heading->addWidget(title_);
    header->addLayout(heading, 1);
    auto* running = new QVBoxLayout;
    state_ = label("Stopped", "section");
    state_->setObjectName("miningState");
    runtime_ = label("Ready when you are", "muted");
    running->addWidget(state_);
    running->addWidget(runtime_);
    header->addLayout(running);
    header->addSpacing(20);
    start_ = button("Start mining", "primary");
    start_->setObjectName("startMining");
    start_->setMinimumWidth(160);
    header->addWidget(start_);
    content->addLayout(header);
    notice_ = label("");
    notice_->setObjectName("notice");
    notice_->setWordWrap(true);
    notice_->hide();
    content->addWidget(notice_);
    pages_ = new QStackedWidget;
    pages_->addWidget(overviewPage());
    pages_->addWidget(setupPage());
    pages_->addWidget(devicesPage());
    pages_->addWidget(activityPage());
    content->addWidget(pages_, 1);
    outer->addWidget(workspace, 1);
    auto* shellScroll = scrollPage(shell);
    shellScroll->setObjectName("shellScroll");
    setCentralWidget(shellScroll);
    statusBar()->setSizeGripEnabled(true);
    statusBar()->addWidget(label("FiroPoW  |  Mainnet", "muted"));
    statusBar()->addPermanentWidget(label("Local miner", "muted"));

    connect(navigation_, &QListWidget::currentRowChanged, this, [this](int row) {
        pages_->setCurrentIndex(row);
        const QStringList titles{"Mining overview", "Mining setup", "Your GPUs", "Activity"};
        title_->setText(titles.value(row));
    });
    navigation_->setCurrentRow(0);
    connect(start_, &QPushButton::clicked, this, &MainWindow::toggleMining);
    connect(settings, &QPushButton::clicked, this, &MainWindow::showSettings);
    connect(help, &QPushButton::clicked, this, [this] {
        QMessageBox::information(this, "Using Firominer",
            "1. Open Mining setup and enter your pool endpoint and payout address.\n"
            "2. Choose your GPU backend, or leave Automatic selected.\n"
            "3. Click Start mining.\n\n"
            "The GUI runs firominer beside it. Select another executable in Settings if needed. "
            "The miner needs a compatible GPU driver.\n\n"
            "Minimizing this window keeps mining. Closing while mining asks whether to stop "
            "or keep mining in the system tray, when available.\n\n"
            "Command-line users can still run firominer directly. This GUI currently configures "
            "mainnet pool mining; solo mining and advanced options remain available in the CLI.");
    });
    connect(&controller_, &MinerController::statistics, this, &MainWindow::updateStatistics);
    connect(&controller_, &MinerController::stateChanged, this, &MainWindow::setMiningState);
    connect(&controller_, &MinerController::logLine, this, &MainWindow::appendActivity);
    connect(&controller_, &MinerController::failure, this, &MainWindow::showFailure);
    connect(&controller_, &MinerController::finished, this, [this] {
        if (closing_)
            QTimer::singleShot(0, this, &QWidget::close);
    });

    tray_ = new QSystemTrayIcon(windowIcon(), this);
    tray_->setToolTip("Firominer - stopped");
    auto* trayMenu = new QMenu(this);
    trayMenu->addAction("Show Firominer", this, [this] { showNormal(); raise(); activateWindow(); });
    trayMenu->addAction("Stop mining", &controller_, &MinerController::stop);
    trayMenu->addSeparator();
    trayMenu->addAction("Quit", this, [this] { showNormal(); close(); });
    tray_->setContextMenu(trayMenu);
    connect(tray_, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick)
        { showNormal(); raise(); activateWindow(); }
    });
    if (QSystemTrayIcon::isSystemTrayAvailable())
        tray_->show();
    loadSettings();
    resize(size().boundedTo(available));
    clearReadings();
}

QWidget* MainWindow::overviewPage()
{
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(16);
    auto* metrics = panel();
    auto* metricLayout = new QHBoxLayout(metrics);
    metricLayout->setContentsMargins(23, 18, 23, 18);
    auto addMetric = [&](const QString& text, QLabel*& value, QLabel*& detail, const char* objectName) {
        auto* group = new QVBoxLayout;
        group->addWidget(label(text, "muted"));
        value = label("Unavailable", "metric");
        value->setObjectName(objectName);
        group->addWidget(value);
        detail = label("", "muted");
        group->addWidget(detail);
        metricLayout->addLayout(group, 1);
    };
    addMetric("Total hashrate", hashrate_, gpuCount_, "totalHashrate");
    addMetric("Accepted shares", accepted_, shareDetail_, "acceptedShares");
    addMetric("GPU power", power_, powerDetail_, "totalPower");
    layout->addWidget(metrics);

    auto* middle = new QHBoxLayout;
    middle->setSpacing(16);
    auto* chartPanel = panel();
    auto* chartLayout = new QVBoxLayout(chartPanel);
    chartLayout->setContentsMargins(18, 17, 18, 12);
    auto* chartTop = new QHBoxLayout;
    auto* chartTitles = new QVBoxLayout;
    chartTitles->addWidget(label("Hashrate", "section"));
    chartTitles->addWidget(label("Local hashrate · MH/s", "muted"));
    chartTop->addLayout(chartTitles, 1);
    QList<QPushButton*> ranges;
    for (const auto& text : {"15m", "1h", "6h"})
    {
        auto* range = button(text, "range");
        range->setCheckable(true);
        range->setAutoExclusive(true);
        range->setChecked(QString(text) == "1h");
        ranges.append(range);
        chartTop->addWidget(range);
    }
    chartLayout->addLayout(chartTop);
    chart_ = new HashrateChart;
    chartLayout->addWidget(chart_, 1);
    auto* history = button("View history", "link");
    history->setObjectName("viewHistory");
    chartLayout->addWidget(history, 0, Qt::AlignLeft);
    connect(history, &QPushButton::clicked, chart_, &HashrateChart::showHistory);
    const QList<int> intervals{900, 3600, 21600};
    for (int i = 0; i < ranges.size(); ++i)
        connect(ranges[i], &QPushButton::clicked, chart_, [this, intervals, i] { chart_->setRange(intervals[i]); });
    middle->addWidget(chartPanel, 3);
    auto* connection = panel();
    connection->setMinimumWidth(225);
    connection->setMaximumWidth(300);
    auto* poolLayout = new QVBoxLayout(connection);
    poolLayout->setContentsMargins(20, 18, 20, 15);
    poolLayout->setSpacing(6);
    poolLayout->addWidget(label("Pool connection", "section"));
    poolState_ = label("Not connected");
    poolState_->setObjectName("poolState");
    poolLayout->addWidget(poolState_);
    auto addField = [&](const QString& text, QLabel*& value) {
        poolLayout->addWidget(label(text, "muted"));
        value = label("");
        value->setWordWrap(true);
        value->setTextInteractionFlags(Qt::TextSelectableByMouse);
        poolLayout->addWidget(value);
    };
    addField("Pool", pool_);
    addField("Worker", worker_);
    addField("Payout address", wallet_);
    wallet_->setObjectName("payoutSummary");
    auto* copy = button("Copy", "link");
    copy->setToolTip("Copy payout address");
    copy->setAccessibleName("Copy payout address");
    poolLayout->removeWidget(wallet_);
    auto* payoutRow = new QHBoxLayout;
    payoutRow->addWidget(wallet_, 1);
    payoutRow->addWidget(copy);
    poolLayout->addLayout(payoutRow);
    connect(copy, &QPushButton::clicked, this, [this] {
        QApplication::clipboard()->setText(walletInput_->text().trimmed());
        statusBar()->showMessage("Payout address copied", 3000);
    });
    poolLayout->addStretch();
    auto* edit = button("Edit mining setup", "link");
    connect(edit, &QPushButton::clicked, this, [this] { navigation_->setCurrentRow(1); });
    poolLayout->addWidget(edit);
    middle->addWidget(connection, 1);
    layout->addLayout(middle, 1);

    auto* devices = panel();
    auto* deviceLayout = new QVBoxLayout(devices);
    deviceLayout->setContentsMargins(18, 15, 18, 8);
    deviceLayout->addWidget(label("Your GPUs", "section"));
    auto* table = deviceTable();
    table->setFixedHeight(164);
    deviceLayout->addWidget(table);
    layout->addWidget(devices);

    auto* recent = panel();
    auto* recentLayout = new QHBoxLayout(recent);
    recentLayout->setContentsMargins(18, 5, 18, 5);
    lastActivity_ = label("Ready to start mining", "muted");
    lastActivity_->setWordWrap(true);
    recentLayout->addWidget(lastActivity_, 1);
    auto* view = button("View activity →", "link");
    connect(view, &QPushButton::clicked, this, [this] { navigation_->setCurrentRow(3); });
    recentLayout->addWidget(view);
    layout->addWidget(recent);
    return scrollPage(page);
}

QWidget* MainWindow::setupPage()
{
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* card = panel();
    auto* body = new QVBoxLayout(card);
    body->setContentsMargins(25, 23, 25, 23);
    body->setSpacing(20);
    body->addWidget(label("Connect to a mining pool", "section"));
    auto* intro = label("Enter the endpoint supplied by your pool and the Firo address where it should pay you.", "muted");
    intro->setWordWrap(true);
    body->addWidget(intro);
    auto* form = new QFormLayout;
    form->setVerticalSpacing(17);
    auto field = [&](const QString& caption, const char* name, const QString& placeholder) {
        auto* input = new QLineEdit;
        input->setObjectName(name);
        input->setAccessibleName(caption);
        input->setPlaceholderText(placeholder);
        input->setMaxLength(1024);
        form->addRow(caption, input);
        return input;
    };
    poolInput_ = field("Pool endpoint", "poolInput", "stratum+tcp://pool.example:3333");
    walletInput_ = field("Payout address / pool account", "walletInput", "Your Firo payout address or pool username");
    workerInput_ = field("Worker name", "workerInput", "Optional, for example desktop-01");
    passwordInput_ = field("Pool password", "passwordInput", "x (unless your pool specifies another password)");
    passwordInput_->setEchoMode(QLineEdit::Password);
    passwordInput_->setToolTip("Kept only for this session. It is not saved to disk.");
    backendInput_ = new QComboBox;
    backendInput_->setObjectName("backendInput");
    backendInput_->addItem("Automatic · all compatible GPUs", "auto");
    backendInput_->addItem("NVIDIA CUDA", "cuda");
    backendInput_->addItem("OpenCL", "opencl");
    form->addRow("GPU backend", backendInput_);
    devicesInput_ = field("Device numbers", "devicesInput", "All devices, or numbers such as 0, 1");
    devicesInput_->setToolTip("Device numbers from firominer --list-devices for the selected backend.");
    connect(backendInput_, &QComboBox::currentIndexChanged, this, [this] {
        devicesInput_->setEnabled(!controller_.isRunning() && backendInput_->currentData().toString() != "auto");
    });
    body->addLayout(form);
    auto* note = label("Settings apply to the next mining session. Pool passwords are not saved. "
        "Choose a specific backend to select device numbers; Automatic uses all compatible GPUs.", "muted");
    note->setWordWrap(true);
    body->addWidget(note);
    auto* save = button("Save setup", "primary");
    save->setObjectName("saveSetup");
    connect(save, &QPushButton::clicked, this, [this] {
        if (saveSettings())
        {
            updateConnectionSummary();
            statusBar()->showMessage("Mining setup saved", 4000);
        }
    });
    body->addWidget(save, 0, Qt::AlignLeft);
    layout->addWidget(card);
    layout->addStretch();
    return scrollPage(page);
}

QTableWidget* MainWindow::deviceTable()
{
    auto* table = new QTableWidget(0, 6);
    table->setObjectName(deviceTables_.isEmpty() ? "overviewDevices" : "allDevices");
    table->setAccessibleName("GPU statistics");
    table->setHorizontalHeaderLabels({"Device", "Hashrate", "Temp", "Fan", "Power", "Status"});
    table->verticalHeader()->hide();
    table->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    const QList<int> widths{140, 100, 85, 100, 130};
    for (int col = 1; col < 6; ++col)
    {
        table->horizontalHeader()->setSectionResizeMode(col, QHeaderView::Fixed);
        table->setColumnWidth(col, widths[col - 1]);
    }
    table->horizontalHeader()->setMinimumSectionSize(65);
    table->setShowGrid(false);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setFocusPolicy(Qt::StrongFocus);
    table->setWordWrap(true);
    deviceTables_.append(table);
    return table;
}

QWidget* MainWindow::devicesPage()
{
    auto* page = panel();
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(20, 20, 20, 20);
    auto* text = label("Live readings from your active mining session. GPU sensors depend on driver support. "
        "Use Mining setup to select the GPUs for your next session.", "muted");
    text->setWordWrap(true);
    layout->addWidget(text);
    layout->addSpacing(12);
    layout->addWidget(deviceTable(), 1);
    auto* setup = button("Choose GPUs in mining setup", "link");
    connect(setup, &QPushButton::clicked, this, [this] { navigation_->setCurrentRow(1); });
    layout->addWidget(setup, 0, Qt::AlignLeft);
    return page;
}

QWidget* MainWindow::activityPage()
{
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* toolbar = new QHBoxLayout;
    toolbar->addWidget(label("Session activity · most recent 2,000 lines", "muted"), 1);
    auto* exportLog = button("Save log");
    auto* clear = button("Clear");
    toolbar->addWidget(exportLog);
    toolbar->addWidget(clear);
    layout->addLayout(toolbar);
    log_ = new QPlainTextEdit;
    log_->setObjectName("activityLog");
    log_->setReadOnly(true);
    log_->setMaximumBlockCount(2000);
    layout->addWidget(log_, 1);
    connect(clear, &QPushButton::clicked, log_, &QPlainTextEdit::clear);
    connect(exportLog, &QPushButton::clicked, this, [this] {
        const auto path = QFileDialog::getSaveFileName(this, "Save activity log", "firominer.log", "Log files (*.log);;All files (*)");
        if (path.isEmpty())
            return;
        QFile file(path);
        const auto bytes = log_->toPlainText().toUtf8();
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(bytes) != bytes.size())
            showFailure("Could not save the activity log: " + file.errorString());
        else
            statusBar()->showMessage("Activity log saved", 4000);
    });
    return page;
}

MiningConfig MainWindow::configuration() const
{
    return {executable_, poolInput_->text().trimmed(), walletInput_->text().trimmed(),
        workerInput_->text().trimmed(), passwordInput_->text().isEmpty() ? QStringLiteral("x") : passwordInput_->text(),
        backendInput_->currentData().toString(),
        backendInput_->currentData().toString() == "auto" ? QString() : devicesInput_->text().trimmed()};
}

void MainWindow::loadSettings()
{
    QSettings settings;
    theme_ = settings.value("appearance/theme", "light").toString();
    if (theme_ != "light" && theme_ != "dark" && theme_ != "system")
        theme_ = "light";
    updateTheme();
#ifdef Q_OS_WIN
    const auto minerName = QStringLiteral("firominer.exe");
#else
    const auto minerName = QStringLiteral("firominer");
#endif
    executable_ = settings.value("miner/executable", QDir(QCoreApplication::applicationDirPath()).filePath(minerName)).toString();
    poolInput_->setText(settings.value("pool/endpoint").toString());
    walletInput_->setText(settings.value("pool/wallet").toString());
    workerInput_->setText(settings.value("pool/worker", QHostInfo::localHostName().section('.', 0, 0)).toString());
    backendInput_->setCurrentIndex(std::max(0, backendInput_->findData(settings.value("miner/backend", "auto").toString())));
    devicesInput_->setText(settings.value("miner/devices").toString());
    devicesInput_->setEnabled(backendInput_->currentData().toString() != "auto");
    restoreGeometry(settings.value("window/geometry").toByteArray());
    updateConnectionSummary();
}

bool MainWindow::saveSettings()
{
    const QUrl endpoint(poolInput_->text().trimmed());
    if (!endpoint.userInfo().isEmpty() || endpoint.authority().contains('@'))
    {
        showFailure("Keep credentials out of the pool endpoint. Enter them in the separate account and password fields.");
        return false;
    }
    QSettings settings;
    settings.setValue("appearance/theme", theme_);
    settings.setValue("miner/executable", executable_);
    settings.setValue("pool/endpoint", poolInput_->text().trimmed());
    settings.setValue("pool/wallet", walletInput_->text().trimmed());
    settings.setValue("pool/worker", workerInput_->text().trimmed());
    settings.setValue("miner/backend", backendInput_->currentData());
    settings.setValue("miner/devices", devicesInput_->text().trimmed());
    settings.setValue("window/geometry", saveGeometry());
    settings.sync();
    if (settings.status() != QSettings::NoError)
    {
        showFailure("Could not save settings. Check that your user settings folder is writable.");
        return false;
    }
    return true;
}

void MainWindow::updateConnectionSummary()
{
    const QUrl endpoint(poolInput_->text().trimmed());
    const auto port = endpoint.port();
    pool_->setText(endpoint.host().isEmpty() ? "Not configured" : endpoint.host() + (port > 0 ? ":" + QString::number(port) : QString()));
    worker_->setText(workerInput_->text().trimmed().isEmpty() ? "Default" : workerInput_->text().trimmed());
    wallet_->setText(walletInput_->text().trimmed().isEmpty() ? "Not configured" : abbreviated(walletInput_->text().trimmed()));
    wallet_->setToolTip(walletInput_->text().trimmed());
}

void MainWindow::toggleMining()
{
    if (controller_.isRunning())
    {
        controller_.stop();
        return;
    }
    notice_->hide();
    const auto config = configuration();
    const auto error = MinerController::validate(config);
    if (!error.isEmpty())
    {
        navigation_->setCurrentRow(1);
        showFailure(error);
        return;
    }
    if (!saveSettings())
        return;
    updateConnectionSummary();
    clearReadings();
    chart_->reset();
    if (controller_.start(config))
        navigation_->setCurrentRow(0);
}

void MainWindow::clearReadings()
{
    hashrate_->setText("0.0 MH/s");
    gpuCount_->setText("No GPUs mining");
    accepted_->setText("0");
    shareDetail_->setText("0 rejected · 0 failed");
    power_->setText("Unavailable");
    powerDetail_->setText("Reported by devices");
    poolState_->setText("Not connected");
    lastActivity_->setText("Ready to start mining");
    for (auto* table : deviceTables_)
    {
        table->clearSpans();
        table->clearContents();
        table->setRowCount(1);
        table->setSpan(0, 0, 1, 6);
        table->setItem(0, 0, new QTableWidgetItem("GPU details appear when mining starts"));
        table->setRowHeight(0, 65);
    }
}

void MainWindow::setMiningState(const QString& state)
{
    currentState_ = state;
    state_->setText(state);
    const bool running = state != "Stopped";
    start_->setText(running ? "Stop mining" : "Start mining");
    start_->setEnabled(state != "Stopping");
    for (auto* field : {poolInput_, walletInput_, workerInput_, passwordInput_})
        field->setEnabled(!running);
    backendInput_->setEnabled(!running);
    devicesInput_->setEnabled(!running && backendInput_->currentData().toString() != "auto");
    if (state == "Stopped")
    {
        clearReadings();
        runtime_->setText("Ready when you are");
    }
    else if (state == "Starting" || state == "Preparing GPUs")
        runtime_->setText("Waiting for miner statistics");
    else if (state == "Reconnecting")
    {
        hashrate_->setText("Unavailable");
        power_->setText("Unavailable");
        gpuCount_->setText("Waiting for statistics");
        poolState_->setText("Reconnecting");
        runtime_->setText("Waiting for fresh statistics");
        lastActivity_->setText("Waiting for fresh share statistics");
        for (auto* table : deviceTables_)
            if (table->columnSpan(0, 0) == 1)
                for (int row = 0; row < table->rowCount(); ++row)
                    for (int col = 1; col < 6; ++col)
                        if (auto* item = table->item(row, col))
                            item->setText(col == 5 ? "Waiting for statistics" : "Unavailable");
    }
    if (tray_)
        tray_->setToolTip("Firominer - " + state.toLower());
}

void MainWindow::updateStatistics(const QJsonObject& statistics)
{
    const auto mining = statistics.value("mining").toObject();
    const auto shares = mining.value("shares").toArray();
    const auto devices = statistics.value("devices").toArray();
    const bool connected = statistics.value("connection").toObject().value("connected").toBool();
    const auto count = shares.at(0).toInteger();
    accepted_->setText(QLocale().toString(count));
    shareDetail_->setText(QString("%1 rejected · %2 failed").arg(shares.at(1).toInteger()).arg(shares.at(2).toInteger()));
    lastActivity_->setText(count + shares.at(1).toInteger() + shares.at(2).toInteger() > 0 ?
        QString("Last share · %1 seconds ago").arg(shares.at(3).toInteger()) : "Waiting for the first share");
    if (!connected)
    {
        // A pool disconnect can leave the API's previous hashrate and sensors populated.
        setMiningState("Reconnecting");
        gpuCount_->setText("Waiting for pool");
        return;
    }
    const auto rate = hashValue(mining.value("hashrate"));
    hashrate_->setText(QString::number(rate, 'f', 1) + " MH/s");
    chart_->add(rate);
    runtime_->setText(runtimeText(statistics.value("host").toObject().value("runtime").toInteger()));
    poolState_->setText("Connected");
    int active = 0;
    double totalPower = 0;
    int powerReadings = 0;
    for (auto* table : deviceTables_)
    {
        table->clearSpans();
        table->setRowCount(devices.size());
    }
    for (int row = 0; row < devices.size(); ++row)
    {
        const auto device = devices[row].toObject();
        const auto hardware = device.value("hardware").toObject();
        const auto sensors = hardware.value("sensors").toArray();
        const auto info = device.value("mining").toObject();
        const bool paused = info.value("paused").toBool();
        const auto deviceRate = hashValue(info.value("hashrate"));
        if (!paused && deviceRate > 0)
            ++active;
        if (sensors.at(2).toDouble() > 0 && std::isfinite(sensors.at(2).toDouble()))
        { totalPower += sensors.at(2).toDouble(); ++powerReadings; }
        const QString status = paused ? "Paused" : deviceRate > 0 ? "Mining" : "Preparing";
        const QStringList values{
            hardware.value("name").toString() + "\nGPU " + QString::number(device.value("_index").toInt()) + " · " + device.value("_mode").toString(),
            QString::number(deviceRate, 'f', 1) + " MH/s", sensor(sensors.at(0), "°C"),
            sensor(sensors.at(1), "%", sensors.at(0).toDouble() > 0), sensor(sensors.at(2), " W"), status};
        for (auto* table : deviceTables_)
        {
            table->setRowHeight(row, 58);
            for (int col = 0; col < values.size(); ++col)
            {
                auto* item = new QTableWidgetItem(values[col]);
                if (col == 5)
                {
                    item->setToolTip(info.value("pause_reason").toString());
                }
                else if (col == 0)
                    item->setToolTip(hardware.value("name").toString() + "\nPCI: " + hardware.value("pci").toString());
                table->setItem(row, col, item);
            }
        }
    }
    gpuCount_->setText(QString("%1 of %2 GPUs mining").arg(active).arg(devices.size()));
    power_->setText(powerReadings ? QString::number(totalPower, 'f', 0) + " W" : "Unavailable");
    powerDetail_->setText(powerReadings && powerReadings != devices.size() ?
        QString("Partial · %1 of %2 devices").arg(powerReadings).arg(devices.size()) : "Reported by devices");
}

void MainWindow::appendActivity(const QString& line)
{
    log_->appendPlainText(QTime::currentTime().toString("HH:mm:ss") + "  " + line);
}

void MainWindow::showFailure(const QString& message)
{
    notice_->setText(message);
    notice_->show();
    appendActivity(message);
    if (!isVisible())
    { showNormal(); raise(); }
}

void MainWindow::showSettings()
{
    QDialog dialog(this);
    dialog.setObjectName("settingsDialog");
    dialog.setPalette(palette());
    dialog.setWindowTitle("Firominer settings");
    dialog.resize(650, 320);
    auto* layout = new QVBoxLayout(&dialog);
    layout->setSpacing(16);
    auto* appearance = new QFormLayout;
    auto* theme = new QComboBox;
    theme->setObjectName("themeInput");
    theme->setAccessibleName("Color theme");
    theme->addItem("Light", "light");
    theme->addItem("Dark", "dark");
    theme->addItem("System", "system");
    theme->setCurrentIndex(theme->findData(theme_));
    appearance->addRow("Color theme", theme);
    layout->addLayout(appearance);
    auto* themeNote = label("System follows your computer's colors. High-contrast settings always take priority.", "muted");
    themeNote->setWordWrap(true);
    layout->addWidget(themeNote);
    layout->addWidget(label("Miner executable", "section"));
    auto* row = new QHBoxLayout;
    auto* path = new QLineEdit(executable_);
    path->setObjectName("minerExecutableInput");
    path->setAccessibleName("Miner executable path");
    auto* browse = button("Browse…");
    row->addWidget(path, 1);
    row->addWidget(browse);
    layout->addLayout(row);
    auto* note = label("Use the firominer executable included in your downloaded package. "
        "Keep it with its companion libraries. Changes apply to the next session.", "muted");
    note->setWordWrap(true);
    layout->addWidget(note);
    connect(browse, &QPushButton::clicked, &dialog, [path, &dialog] {
        const auto selected = QFileDialog::getOpenFileName(&dialog, "Choose firominer", path->text());
        if (!selected.isEmpty())
            path->setText(selected);
    });
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        const QFileInfo file(path->text().trimmed());
        if (path->text().trimmed() != executable_ && (!file.isFile() || !file.isExecutable()))
        {
            QMessageBox::warning(&dialog, "Miner not found", "Select a valid firominer executable.");
            return;
        }
        const auto previous = executable_;
        const auto previousTheme = theme_;
        executable_ = file.absoluteFilePath();
        theme_ = theme->currentData().toString();
        if (saveSettings())
        {
            updateTheme();
            dialog.accept();
        }
        else
        {
            executable_ = previous;
            theme_ = previousTheme;
        }
    });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialog.exec();
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (!controller_.isRunning())
    {
        if (!saveSettings())
        {
            const auto choice = QMessageBox::warning(this, "Settings were not saved",
                "Your changes could not be saved. Stay in Firominer to correct them, or discard them and quit.",
                QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel);
            if (choice != QMessageBox::Discard)
            {
                closing_ = false;
                event->ignore();
                return;
            }
        }
        event->accept();
        return;
    }
    event->ignore();
    if (closing_)
        return;
    QMessageBox prompt(QMessageBox::Question, "Mining is running", "What should Firominer do?", QMessageBox::NoButton, this);
    auto* stop = prompt.addButton("Stop mining and quit", QMessageBox::DestructiveRole);
    QPushButton* keep = nullptr;
    if (QSystemTrayIcon::isSystemTrayAvailable())
        keep = prompt.addButton("Keep mining in tray", QMessageBox::AcceptRole);
    auto* cancel = prompt.addButton(QMessageBox::Cancel);
    prompt.setDefaultButton(cancel);
    prompt.setEscapeButton(cancel);
    prompt.exec();
    if (prompt.clickedButton() == stop)
    {
        closing_ = true;
        if (controller_.isRunning())
            controller_.stop();
        else
            QTimer::singleShot(0, this, &QWidget::close);
    }
    else if (keep && prompt.clickedButton() == keep)
        hide();
}
