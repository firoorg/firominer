#pragma once

#include "minercontroller.h"
#include <QMainWindow>
#include <QList>

class HashrateChart;
class QBoxLayout;
class QLabel;
class QLineEdit;
class QComboBox;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QStackedWidget;
class QSystemTrayIcon;
class QTableWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

public slots:
    void updateStatistics(const QJsonObject& statistics);
    void setMiningState(const QString& state);

protected:
    void closeEvent(QCloseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QWidget* overviewPage();
    QWidget* setupPage();
    QWidget* devicesPage();
    QWidget* activityPage();
    QTableWidget* deviceTable();
    MiningConfig configuration() const;
    void loadSettings();
    bool saveSettings();
    void toggleMining();
    void appendActivity(const QString& line);
    void showFailure(const QString& message);
    void showSettings();
    void updateConnectionSummary();
    void updateMiningMode();
    void clearReadings();
    void updateTheme();

    MinerController controller_;
    QListWidget* navigation_;
    QStackedWidget* pages_;
    QLabel *title_, *state_, *runtime_, *notice_;
    QLabel *hashrate_, *gpuCount_, *accepted_, *shareDetail_, *power_, *powerDetail_;
    QLabel *poolState_, *pool_, *worker_, *wallet_, *lastActivity_;
    QLabel *acceptedLabel_, *connectionTitle_, *endpointLabel_, *workerLabel_, *rewardLabel_, *modeStatus_;
    QLabel *setupHeading_, *setupIntro_, *nodeStatus_;
    QPushButton* start_;
    QPushButton *poolMode_, *soloMode_, *testNode_, *saveSetup_, *nodeGuideButton_;
    QWidget *poolFields_, *soloFields_, *nodeGuide_, *devicesRow_;
    HashrateChart* chart_;
    QBoxLayout *metricsLayout_, *overviewLayout_;
    QList<QTableWidget*> deviceTables_;
    QLineEdit *poolInput_, *walletInput_, *workerInput_, *passwordInput_, *devicesInput_;
    QLineEdit *nodeInput_, *rpcUserInput_, *rpcPasswordInput_, *rewardInput_;
    QComboBox* backendInput_;
    QPlainTextEdit* log_;
    QSystemTrayIcon* tray_ = nullptr;
    QString executable_;
    QString theme_ = QStringLiteral("light");
    QString currentState_ = QStringLiteral("Stopped");
    bool closing_ = false;
};
