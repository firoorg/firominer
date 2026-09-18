#include "mainwindow.h"
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QScrollArea>
#include <QStatusBar>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QtTest>

namespace
{
QJsonObject device(int index, const QString& name, const QString& mode, double rate,
    const QJsonArray& sensors, bool paused = false)
{
    return {{"_index", index}, {"_mode", mode},
        {"hardware", QJsonObject{{"name", name}, {"pci", "01:00.0"}, {"sensors", sensors}}},
        {"mining", QJsonObject{{"hashrate", "0x" + QString::number(quint64(rate * 1000000), 16)},
            {"paused", paused}, {"pause_reason", paused ? "Temperature limit" : ""}}}};
}

QJsonObject statistics()
{
    return {{"host", QJsonObject{{"runtime", 9240}}},
        {"connection", QJsonObject{{"connected", true}}},
        {"mining", QJsonObject{{"hashrate", "0x" + QString::number(112800000, 16)},
            {"shares", QJsonArray{1248, 2, 0, 12}}}},
        {"devices", QJsonArray{
            device(0, "NVIDIA GeForce RTX 4090", "CUDA", 71.2, {62, 58, 310}),
            device(1, "AMD Radeon RX 6900 XT", "OpenCL", 41.6, {65, 62, 180})}}};
}
}

class WindowTest : public QObject
{
    Q_OBJECT
private slots:
    void init() { QSettings().clear(); }

    void startsIdleAndRequiresConfiguration()
    {
        MainWindow window;
        QCOMPARE(window.findChild<QLabel*>("miningState")->text(), QString("Stopped"));
        QCOMPARE(window.findChild<QLabel*>("totalHashrate")->text(), QString("0.0 MH/s"));
        QVERIFY(!window.findChild<QLineEdit*>("devicesInput")->isEnabled());
        window.findChild<QPushButton*>("startMining")->click();
        QVERIFY(!window.findChild<QLabel*>("notice")->text().isEmpty());
        QCOMPARE(window.findChild<QListWidget*>("navigation")->currentRow(), 1);
        QCOMPARE(window.findChild<QLabel*>("miningState")->text(), QString("Stopped"));
    }

    void displaysActualUnitsAndClearsStoppedReadings()
    {
        MainWindow window;
        window.setMiningState("Mining");
        window.updateStatistics(statistics());
        QCOMPARE(window.findChild<QLabel*>("totalHashrate")->text(), QString("112.8 MH/s"));
        QCOMPARE(window.findChild<QLabel*>("totalPower")->text(), QString("490 W"));
        QCOMPARE(window.findChild<QLabel*>("acceptedShares")->text(), QLocale().toString(1248));
        const auto* table = window.findChild<QTableWidget*>("overviewDevices");
        QCOMPARE(table->rowCount(), 2);
        QCOMPARE(table->item(0, 1)->text(), QString("71.2 MH/s"));
        QCOMPARE(table->item(1, 2)->text(), QString::fromUtf8("65°C"));
        QCOMPARE(table->item(1, 5)->text(), QString("Mining"));
        QVERIFY(!window.findChild<QLineEdit*>("poolInput")->isEnabled());
        window.setMiningState("Stopped");
        QCOMPARE(window.findChild<QLabel*>("totalHashrate")->text(), QString("0.0 MH/s"));
        QCOMPARE(window.findChild<QLabel*>("totalPower")->text(), QString("Unavailable"));
        QCOMPARE(table->rowCount(), 1);
        QVERIFY(window.findChild<QLineEdit*>("poolInput")->isEnabled());
        window.updateStatistics(statistics());
        QCOMPARE(table->columnSpan(0, 0), 1);
    }

    void missingSensorsAndPauseRemainExplicit()
    {
        MainWindow window;
        auto stats = statistics();
        stats["devices"] = QJsonArray{
            device(0, "<b>Unavailable GPU</b>", "OpenCL", 0, {0, 0, 0}, true),
            device(1, "Available GPU", "CUDA", 50, {60, 0, 100})};
        window.updateStatistics(stats);
        const auto* table = window.findChild<QTableWidget*>("overviewDevices");
        QCOMPARE(table->item(0, 2)->text(), QString("Unavailable"));
        QCOMPARE(table->item(0, 3)->text(), QString("Unavailable"));
        QCOMPARE(table->item(0, 4)->text(), QString("Unavailable"));
        QCOMPARE(table->item(0, 5)->text(), QString("Paused"));
        QCOMPARE(table->item(0, 5)->toolTip(), QString("Temperature limit"));
        QCOMPARE(table->item(1, 3)->text(), QString("0%"));
        QCOMPARE(window.findChild<QLabel*>("totalPower")->text(), QString("100 W"));
        bool partial = false;
        for (const auto* text : window.findChildren<QLabel*>())
            partial |= text->text().startsWith("Partial");
        QVERIFY(partial);
    }

    void disconnectedStatisticsDoNotRestoreStaleReadings()
    {
        MainWindow window;
        window.setMiningState("Mining");
        window.updateStatistics(statistics());
        auto disconnected = statistics();
        disconnected["connection"] = QJsonObject{{"connected", false}};
        window.setMiningState("Reconnecting");
        window.updateStatistics(disconnected);
        QCOMPARE(window.findChild<QLabel*>("totalHashrate")->text(), QString("Unavailable"));
        QCOMPARE(window.findChild<QLabel*>("totalPower")->text(), QString("Unavailable"));
        const auto* table = window.findChild<QTableWidget*>("overviewDevices");
        QCOMPARE(table->item(0, 1)->text(), QString("Unavailable"));
        QCOMPARE(table->item(0, 5)->text(), QString("Waiting for statistics"));
        window.setMiningState("Mining");
        window.updateStatistics(statistics());
        QCOMPARE(window.findChild<QLabel*>("totalHashrate")->text(), QString("112.8 MH/s"));
        QCOMPARE(table->item(0, 5)->text(), QString("Mining"));
    }

    void failedSaveCanCancelCloseOrExplicitlyDiscard()
    {
        MainWindow window;
        window.show();
        auto* endpoint = window.findChild<QLineEdit*>("poolInput");
        endpoint->setText("stratum+tcp://user:secret@pool.example:3333");
        QTimer::singleShot(0, &window, [&window] {
            if (auto* prompt = window.findChild<QMessageBox*>())
                prompt->button(QMessageBox::Cancel)->click();
        });
        QVERIFY(!window.close());
        QVERIFY(window.isVisible());
        QVERIFY(endpoint->text().contains("secret"));
        QVERIFY(!QSettings().value("pool/endpoint").toString().contains("secret"));
        QTimer::singleShot(0, &window, [&window] {
            if (auto* prompt = window.findChild<QMessageBox*>())
                prompt->button(QMessageBox::Discard)->click();
        });
        QVERIFY(window.close());
        QVERIFY(!window.isVisible());
    }

    void shellScrollsOnSmallDisplays()
    {
        MainWindow window;
        window.resize(800, 500);
        window.show();
        QTest::qWait(20);
        QVERIFY(window.width() <= 800);
        QVERIFY(window.height() <= 500);
        auto* shell = window.findChild<QScrollArea*>("shellScroll");
        QVERIFY(shell);
        auto* start = window.findChild<QPushButton*>("startMining");
        shell->ensureWidgetVisible(start);
        const auto point = start->mapTo(shell->viewport(), start->rect().center());
        QVERIFY(shell->viewport()->rect().contains(point));
    }

    void systemPaletteChangesRemainReadable()
    {
        const auto original = QApplication::palette();
        MainWindow window;
        window.setMiningState("Mining");
        window.updateStatistics(statistics());
        QPalette dark = original;
        dark.setColor(QPalette::Window, Qt::black);
        dark.setColor(QPalette::WindowText, Qt::white);
        dark.setColor(QPalette::Base, Qt::black);
        dark.setColor(QPalette::Text, Qt::white);
        QApplication::setPalette(dark);
        QCoreApplication::processEvents();
        const auto color = window.findChild<QLabel*>("miningState")->palette().color(QPalette::WindowText);
        const bool fixedColors = window.styleSheet().contains("#f6f6f4");
        QApplication::setPalette(original);
        QCoreApplication::processEvents();
        QCOMPARE(color, QColor(Qt::white));
        QVERIFY(!fixedColors);
        QVERIFY(window.styleSheet().contains("#f6f6f4"));
    }

    void historyHasAccessibleTimestampedValues()
    {
        MainWindow window;
        window.updateStatistics(statistics());
        bool found = false;
        QTimer::singleShot(0, &window, [&] {
            auto* table = window.findChild<QTableWidget*>("hashrateHistory");
            found = table && table->rowCount() == 1 && !table->item(0, 0)->text().isEmpty() &&
                table->item(0, 1)->text() == "112.8";
            if (auto* dialog = window.findChild<QDialog*>())
                dialog->reject();
        });
        window.findChild<QPushButton*>("viewHistory")->click();
        QVERIFY(found);
    }

    void savesSetupWithoutPassword()
    {
        MainWindow window;
        window.findChild<QLineEdit*>("poolInput")->setText("stratum+tcp://pool.example:3333");
        window.findChild<QLineEdit*>("walletInput")->setText("test-payout-address");
        window.findChild<QLineEdit*>("passwordInput")->setText("do-not-persist-this");
        window.findChild<QComboBox*>("backendInput")->setCurrentIndex(1);
        QVERIFY(window.findChild<QLineEdit*>("devicesInput")->isEnabled());
        window.findChild<QPushButton*>("saveSetup")->click();
        QSettings settings;
        QCOMPARE(settings.value("pool/wallet").toString(), QString("test-payout-address"));
        for (const auto& key : settings.allKeys())
            QVERIFY(!settings.value(key).toString().contains("do-not-persist-this"));
        window.findChild<QLineEdit*>("poolInput")->setText("stratum+tcp://user:secret@pool.example:3333");
        window.findChild<QPushButton*>("saveSetup")->click();
        QCOMPARE(QSettings().value("pool/endpoint").toString(), QString("stratum+tcp://pool.example:3333"));
        MainWindow restored;
        QCOMPARE(restored.findChild<QLineEdit*>("walletInput")->text(), QString("test-payout-address"));
        QVERIFY(restored.findChild<QLineEdit*>("passwordInput")->text().isEmpty());
    }

    void failedSavePreventsStartingAndAllowsRetry()
    {
#ifdef Q_OS_WIN
        const auto helper = "gui-test-miner.exe";
#else
        const auto helper = "gui-test-miner";
#endif
        QSettings settings;
        settings.setValue("miner/executable", QDir(QCoreApplication::applicationDirPath()).filePath(helper));
        settings.setValue("pool/endpoint", "stratum+tcp://pool.example:3333");
        settings.setValue("pool/wallet", "test-account");
        settings.sync();
        QCOMPARE(settings.status(), QSettings::NoError);
        MainWindow window;
        auto* wallet = window.findChild<QLineEdit*>("walletInput");
        wallet->setText("edited-account");
        auto* controller = window.findChild<MinerController*>();
        auto* start = window.findChild<QPushButton*>("startMining");

        // A directory at the INI filename forces a write failure even as root.
        QVERIFY(QFile::remove(settings.fileName()));
        QVERIFY(QDir().mkdir(settings.fileName()));
        start->click();
        const bool started = controller->isRunning();
        QVERIFY(QDir().rmdir(settings.fileName()));
        QVERIFY(!started);
        QVERIFY(window.findChild<QLabel*>("notice")->text().contains("Could not save settings"));
        QCOMPARE(window.findChild<QLabel*>("miningState")->text(), QString("Stopped"));
        QCOMPARE(wallet->text(), QString("edited-account"));
        QVERIFY(wallet->isEnabled());

        start->click();
        QTRY_COMPARE_WITH_TIMEOUT(window.findChild<QLabel*>("miningState")->text(), QString("Mining"), 7000);
        QCOMPARE(QSettings().value("pool/wallet").toString(), QString("edited-account"));
        controller->stop();
        QTRY_VERIFY_WITH_TIMEOUT(!controller->isRunning(), 5000);
    }

    void closeCanCancelAndThenStopsOwnedMiner()
    {
#ifdef Q_OS_WIN
        const auto helper = "gui-test-miner.exe";
#else
        const auto helper = "gui-test-miner";
#endif
        QSettings settings;
        settings.setValue("miner/executable", QDir(QCoreApplication::applicationDirPath()).filePath(helper));
        settings.setValue("pool/endpoint", "stratum+tcp://pool.example:3333");
        settings.setValue("pool/wallet", "test-account");
        MainWindow window;
        window.show();
        window.findChild<QPushButton*>("startMining")->click();
        auto* controller = window.findChild<MinerController*>();
        QVERIFY(controller);
        QTRY_COMPARE_WITH_TIMEOUT(window.findChild<QLabel*>("miningState")->text(), QString("Mining"), 7000);
        QTimer::singleShot(0, &window, [&window] {
            auto* prompt = window.findChild<QMessageBox*>();
            if (prompt)
                prompt->button(QMessageBox::Cancel)->click();
        });
        window.close();
        QVERIFY(window.isVisible());
        QVERIFY(controller->isRunning());
        QTimer::singleShot(0, &window, [&window] {
            auto* prompt = window.findChild<QMessageBox*>();
            if (prompt)
                for (auto* button : prompt->buttons())
                    if (prompt->buttonRole(button) == QMessageBox::DestructiveRole)
                        button->click();
        });
        window.close();
        QTRY_VERIFY_WITH_TIMEOUT(!controller->isRunning(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!window.isVisible(), 1000);
    }

    void renderPreview()
    {
        const auto directory = qEnvironmentVariable("FIROMINER_GUI_SCREENSHOT_DIR");
        if (directory.isEmpty())
            return;
        QVERIFY(QDir().mkpath(directory));
        MainWindow window;
        window.resize(1400, 900);
        window.findChild<QLineEdit*>("poolInput")->setText("stratum+tcp://pool.example:3333");
        window.findChild<QLineEdit*>("walletInput")->setText("a7KpDesignPreviewAddress9mQ2");
        window.findChild<QLineEdit*>("workerInput")->setText("desktop-01");
        window.findChild<QPushButton*>("saveSetup")->click();
        window.setMiningState("Mining");
        window.updateStatistics(statistics());
        window.setWindowTitle("Firominer | Test fixture data");
        window.statusBar()->clearMessage();
        window.statusBar()->addPermanentWidget(new QLabel("Test fixture data"));
        window.show();
        QTest::qWait(100);
        QVERIFY(window.grab().save(QDir(directory).filePath("overview.png")));
        window.setMiningState("Stopped");
        window.findChild<QListWidget*>("navigation")->setCurrentRow(1);
        QTest::qWait(50);
        QVERIFY(window.grab().save(QDir(directory).filePath("setup.png")));
    }
};

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QApplication::setStyle("Fusion");
    QCoreApplication::setOrganizationName("FirominerGuiTests");
    QCoreApplication::setApplicationName("GuiTests");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QTemporaryDir settings;
    if (!settings.isValid())
        return 1;
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
    WindowTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_window.moc"
