#pragma once

#include "minercontroller.h"
#include <QDateTime>
#include <QList>
#include <QMainWindow>
#include <QStringList>
#include <memory>

class GpuGrid;
class HashrateChart;
class StatusPill;
class QBoxLayout;
class QComboBox;
class QFrame;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QStackedWidget;
class QSystemTrayIcon;
class QTableWidget;
struct GuiTheme;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    //! Load the bundled typefaces and make the brand body font the application default.
    static void installBrandFonts();

public slots:
    void updateStatistics(const QJsonObject& statistics);
    void setMiningState(const QString& state);

protected:
    void closeEvent(QCloseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    //! Totals from the last mining session, shown while the miner is stopped.
    struct Session
    {
        qint64 runtime = 0;
        qint64 accepted = 0;
        qint64 rejected = 0;
        double average = 0;
        QDateTime ended;
        bool solo = false;
    };

    QWidget* sidebar();
    QWidget* overviewPage();
    QWidget* heroPanel();
    QWidget* setupPage();
    QWidget* devicesPage();
    QWidget* activityPage();
    QTableWidget* deviceTable();
    MiningConfig configuration() const;
    //! Whether a solo setup is complete apart from its RPC password, which is never saved.
    bool needsRpcPassword() const;
    void loadSettings();
    bool saveSettings();
    void toggleMining();
    void appendActivity(const QString& line);
    void showFailure(const QString& message);
    void showNotice(const QString& message, const char* tone);
    void showSettings();
    void updateConnectionSummary();
    void updateMiningMode();
    void updateOverview();
    void clearReadings();
    void updateTheme();
    void refreshIcons();
    void setSidebarCompact(bool compact);
    void recordSession();

    MinerController controller_;
    std::unique_ptr<GuiTheme> colors_;
    QFrame* sidebar_ = nullptr;
    QLabel *brandName_ = nullptr, *sidebarFooter_ = nullptr;
    QListWidget* navigation_ = nullptr;
    QStringList navigationNames_;
    QPushButton *settingsButton_ = nullptr, *helpButton_ = nullptr;
    QStackedWidget* pages_ = nullptr;
    QLabel *title_ = nullptr, *runtime_ = nullptr, *notice_ = nullptr;
    StatusPill* state_ = nullptr;
    QPushButton* start_ = nullptr;
    QFrame* hero_ = nullptr;
    QBoxLayout *heroLayout_ = nullptr, *middleLayout_ = nullptr;
    QLabel *heroCaption_ = nullptr, *heroTitle_ = nullptr, *hashrate_ = nullptr, *hashrateUnit_ = nullptr, *gpuCount_ = nullptr;
    QWidget *liveStats_ = nullptr, *sessionStats_ = nullptr, *hashrateRow_ = nullptr;
    QLabel *acceptedLabel_ = nullptr, *accepted_ = nullptr, *shareDetail_ = nullptr;
    QLabel *lastShareLabel_ = nullptr, *lastShare_ = nullptr, *lastShareDetail_ = nullptr;
    QLabel *power_ = nullptr, *powerDetail_ = nullptr;
    QLabel *sessionRuntime_ = nullptr, *sessionEnded_ = nullptr, *sessionAcceptedLabel_ = nullptr;
    QLabel *sessionAccepted_ = nullptr, *sessionRejected_ = nullptr, *sessionAverage_ = nullptr;
    HashrateChart* chart_ = nullptr;
    QLabel* chartSubtitle_ = nullptr;
    QLabel *connectionTitle_ = nullptr, *endpointLabel_ = nullptr, *workerLabel_ = nullptr, *rewardLabel_ = nullptr;
    QLabel *pool_ = nullptr, *worker_ = nullptr, *wallet_ = nullptr;
    StatusPill* poolState_ = nullptr;
    QPushButton* copyAddress_ = nullptr;
    GpuGrid* gpuGrid_ = nullptr;
    QFrame* gpuEmpty_ = nullptr;
    QLabel* gpuSummary_ = nullptr;
    QList<QTableWidget*> deviceTables_;
    QLabel *setupHeading_ = nullptr, *setupIntro_ = nullptr, *nodeStatus_ = nullptr;
    QPushButton *poolMode_ = nullptr, *soloMode_ = nullptr, *testNode_ = nullptr, *saveSetup_ = nullptr, *nodeGuideButton_ = nullptr;
    QWidget *poolFields_ = nullptr, *soloFields_ = nullptr, *nodeGuide_ = nullptr, *devicesRow_ = nullptr;
    QLineEdit *poolInput_ = nullptr, *walletInput_ = nullptr, *workerInput_ = nullptr, *passwordInput_ = nullptr, *devicesInput_ = nullptr;
    QLineEdit *nodeInput_ = nullptr, *rpcUserInput_ = nullptr, *rpcPasswordInput_ = nullptr, *rewardInput_ = nullptr, *coinbaseMessageInput_ = nullptr;
    QComboBox* backendInput_ = nullptr;
    QPlainTextEdit* log_ = nullptr;
    QSystemTrayIcon* tray_ = nullptr;
    QString executable_;
    QString appearance_ = QStringLiteral("light");
    QString language_ = QStringLiteral("system");
    QString currentState_ = QStringLiteral("Stopped");
    qint64 acceptedCount_ = 0, rejectedCount_ = 0, runtimeSeconds_ = 0;
    //! Sum and count of this session's connected readings, for its average.
    double rateSum_ = 0;
    qint64 rateSamples_ = 0;
    int activeGpus_ = 0, totalGpus_ = 0;
    bool hasReadings_ = false;
    bool connectionLost_ = false; //!< The miner reported its pool or node connection down.
    Session lastSession_;
    bool hasSession_ = false;
    bool sidebarCompact_ = false;
    bool closing_ = false;
};
