#include "mainwindow.h"
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QDialogButtonBox>
#include <QFile>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QScopeGuard>
#include <QScrollArea>
#include <QScrollBar>
#include <QStatusBar>
#include <QTableWidget>
#include <QTcpServer>
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

bool chooseTheme(MainWindow& window, const QString& value, QDialogButtonBox::StandardButton action)
{
    bool selected = false;
    QTimer::singleShot(0, &window, [&] {
        auto* dialog = window.findChild<QDialog*>("settingsDialog");
        if (!dialog)
            return;
        auto* theme = dialog->findChild<QComboBox*>("themeInput");
        auto* buttons = dialog->findChild<QDialogButtonBox*>();
        if (!theme || !buttons)
        {
            dialog->reject();
            return;
        }
        theme->setCurrentIndex(theme->findData(value));
        selected = theme->currentData().toString() == value;
        buttons->button(action)->click();
    });
    window.findChild<QPushButton*>("settingsButton")->click();
    return selected;
}
}

class WindowTest : public QObject
{
    Q_OBJECT
private slots:
    void init() { QSettings().clear(); }

    void soloDefaultsHelpAndSettingsStaySeparate()
    {
        MainWindow window;
        window.resize(1120, 800);
        window.show();
        window.findChild<QListWidget*>("navigation")->setCurrentRow(1);
        auto* pool = window.findChild<QLineEdit*>("poolInput");
        auto* node = window.findChild<QLineEdit*>("nodeInput");
        auto* rpcUser = window.findChild<QLineEdit*>("rpcUserInput");
        auto* rpcPassword = window.findChild<QLineEdit*>("rpcPasswordInput");
        auto* reward = window.findChild<QLineEdit*>("rewardInput");
        auto* solo = window.findChild<QPushButton*>("soloMode");
        auto* gpu = window.findChild<QComboBox*>("backendInput");
        auto* guide = window.findChild<QPushButton*>("nodeGuideButton");
        QVERIFY(!solo->isChecked());
        QCOMPARE(node->text(), QString("http://127.0.0.1:8888"));
        QCOMPARE(rpcUser->text(), QString("miner"));
        QVERIFY(rpcPassword->text().isEmpty());
        QVERIFY(reward->text().isEmpty());
        QVERIFY(node->toolTip().contains("server=1"));
        QVERIFY(node->toolTip().contains("rpcallowip=127.0.0.1"));
        QVERIFY(rpcPassword->toolTip().contains("Restart Firo Core"));
        QVERIFY(reward->toolTip().contains("Spark"));
        QVERIFY(!node->isVisible());
        pool->setText("stratum+tcp://pool.example:3333");
        QTest::qWait(20);
        const int poolGpuY = gpu->mapTo(&window, QPoint()).y();
        solo->click();
        QTest::qWait(20);
        QVERIFY(node->isVisible());
        QVERIFY(!pool->isVisible());
        QVERIFY(!window.findChild<QLineEdit*>("devicesInput")->isVisible());
        const int soloGpuY = gpu->mapTo(&window, QPoint()).y();
        QVERIFY(soloGpuY > poolGpuY); // Hidden solo rows consume no space in pool mode.
        guide->click();
        QTest::qWait(20);
        QVERIFY(gpu->mapTo(&window, QPoint()).y() > soloGpuY);
        guide->click();
        QTest::qWait(20);
        QCOMPARE(gpu->mapTo(&window, QPoint()).y(), soloGpuY);
        node->setText("http://127.0.0.1:8382");
        reward->setText("solo-reward-address");
        rpcPassword->setText("never-persist-rpc-password");
        window.findChild<QPushButton*>("saveSetup")->click();
        for (const auto& key : QSettings().allKeys())
            QVERIFY(!QSettings().value(key).toString().contains("never-persist-rpc-password"));
        window.findChild<QPushButton*>("poolMode")->click();
        QCOMPARE(pool->text(), QString("stratum+tcp://pool.example:3333"));
        solo->click();
        QCOMPARE(rpcPassword->text(), QString("never-persist-rpc-password"));
        MainWindow restored;
        QVERIFY(restored.findChild<QPushButton*>("soloMode")->isChecked());
        QCOMPARE(restored.findChild<QLineEdit*>("nodeInput")->text(), QString("http://127.0.0.1:8382"));
        QCOMPARE(restored.findChild<QLineEdit*>("rewardInput")->text(), QString("solo-reward-address"));
        QVERIFY(restored.findChild<QLineEdit*>("rpcPasswordInput")->text().isEmpty());
        node->setText("http://user:secret@127.0.0.1:8888");
        window.findChild<QPushButton*>("poolMode")->click();
        window.findChild<QPushButton*>("saveSetup")->click();
        QCOMPARE(QSettings().value("solo/endpoint").toString(), QString("http://127.0.0.1:8382"));
        QVERIFY(solo->isChecked()); // Surface the invalid field even when its mode was hidden.
        QCOMPARE(window.findChild<QListWidget*>("navigation")->currentRow(), 1);
    }

    void soloControlsAndStatistics()
    {
        MainWindow window;
        auto* solo = window.findChild<QPushButton*>("soloMode");
        solo->click();
        window.setMiningState("Checking node");
        QVERIFY(!solo->isEnabled());
        QVERIFY(!window.findChild<QPushButton*>("testNode")->isEnabled());
        QVERIFY(!window.findChild<QLineEdit*>("rpcPasswordInput")->isEnabled());
        QCOMPARE(window.findChild<QPushButton*>("startMining")->text(), QString("Cancel check"));
        QCOMPARE(window.findChild<QListWidget*>("navigation")->currentRow(), 1);
        window.setMiningState("Mining");
        auto stats = statistics();
        auto mining = stats["mining"].toObject();
        mining["shares"] = QJsonArray{0, 0, 0, 0};
        stats["mining"] = mining;
        window.updateStatistics(stats);
        QCOMPARE(window.findChild<QLabel*>("acceptedLabel")->text(), QString("Blocks accepted"));
        bool healthy = false;
        for (auto* text : window.findChildren<QLabel*>())
            healthy |= text->text() == "Mining normally · no block found yet";
        QVERIFY(healthy);
        window.setMiningState("Stopped");
        QVERIFY(solo->isEnabled());
        window.findChild<QPushButton*>("poolMode")->click();
        QCOMPARE(window.findChild<QLabel*>("acceptedLabel")->text(), QString("Accepted shares"));
    }

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

    void closingDuringNodeCheckCancelsWithoutMiningPrompt()
    {
        QTcpServer node;
        QVERIFY(node.listen(QHostAddress::LocalHost, 0));
        MainWindow window;
        window.show();
        window.findChild<QPushButton*>("soloMode")->click();
        window.findChild<QLineEdit*>("nodeInput")->setText(QString("http://127.0.0.1:%1").arg(node.serverPort()));
        window.findChild<QLineEdit*>("rpcPasswordInput")->setText("session-secret");
        window.findChild<QLineEdit*>("rewardInput")->setText("test-reward-address");
        window.findChild<QPushButton*>("testNode")->click();
        auto* controller = window.findChild<MinerController*>();
        QVERIFY(controller->isRunning());
        QVERIFY(window.close());
        QVERIFY(!window.isVisible());
        QVERIFY(!controller->isRunning());
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

    void layoutAtDifferentSizes_data()
    {
        QTest::addColumn<QSize>("size");
        QTest::addColumn<QString>("theme");
        QTest::addColumn<int>("fontSize");
        QTest::newRow("small") << QSize(640, 480) << "light" << 0;
        QTest::newRow("laptop") << QSize(800, 600) << "light" << 0;
        QTest::newRow("before-reflow") << QSize(999, 700) << "light" << 0;
        QTest::newRow("after-reflow") << QSize(1000, 700) << "light" << 0;
        QTest::newRow("default") << QSize(1120, 800) << "light" << 0;
        QTest::newRow("desktop") << QSize(1920, 1080) << "light" << 0;
        QTest::newRow("small-dark") << QSize(640, 480) << "dark" << 0;
        QTest::newRow("desktop-dark") << QSize(1920, 1080) << "dark" << 0;
        QTest::newRow("large-text") << QSize(800, 600) << "light" << 18;
    }

    void layoutAtDifferentSizes()
    {
        QFETCH(QSize, size);
        QFETCH(QString, theme);
        QFETCH(int, fontSize);
        QSettings().setValue("appearance/theme", theme);
        const auto original = QApplication::font();
        const auto restore = qScopeGuard([original] { QApplication::setFont(original); });
        if (fontSize)
        {
            auto font = original;
            font.setPointSize(fontSize);
            QApplication::setFont(font);
        }
        MainWindow window;
        window.resize(size.width() < 1000 ? QSize(1120, 800) : QSize(640, 480));
        window.show();
        window.resize(size); // Exercise reflow in both directions after showing the window.
        window.findChild<QPushButton*>("soloMode")->click();
        window.statusBar()->addPermanentWidget(new QLabel("Test fixture data"));
        auto* navigation = window.findChild<QListWidget*>("navigation");
        // Revisit setup in Pool mode and with the node guide expanded.
        for (int state = 0; state < 6; ++state)
        {
            const int page = state < 4 ? state : 1;
            if (state == 4)
                window.findChild<QPushButton*>("poolMode")->click();
            if (state == 5)
            {
                window.findChild<QPushButton*>("soloMode")->click();
                window.findChild<QPushButton*>("nodeGuideButton")->click();
            }
            window.setMiningState(page == 0 || page == 2 ? "Mining" : "Stopped");
            if (page == 0 || page == 2)
                window.updateStatistics(statistics());
            navigation->setCurrentRow(page);
            QTest::qWait(30);
            const auto directory = qEnvironmentVariable("FIROMINER_GUI_SCREENSHOT_DIR");
            if (!directory.isEmpty())
            {
                QVERIFY(QDir().mkpath(directory));
                QVERIFY(window.grab().save(QDir(directory).filePath(QString("%1-%2.png").arg(QTest::currentDataTag()).arg(state))));
            }
            QCOMPARE(window.size(), size);
            auto* shell = window.findChild<QScrollArea*>("shellScroll");
            QCOMPARE(shell->verticalScrollBar()->maximum(), 0);
            auto* start = window.findChild<QPushButton*>("startMining");
            QVERIFY(shell->viewport()->rect().contains(QRect(start->mapTo(shell->viewport(), QPoint()), start->size())));
            for (auto* scroll : window.findChildren<QScrollArea*>())
                if (scroll->isVisible())
                {
                    QCOMPARE(scroll->horizontalScrollBar()->maximum(), 0);
                    if (page == 1 && scroll != shell)
                        for (auto* input : scroll->findChildren<QLineEdit*>())
                            if (input->isVisible())
                            {
                                QVERIFY(input->width() >= 200);
                                scroll->ensureWidgetVisible(input);
                                QVERIFY(scroll->viewport()->rect().contains(QRect(input->mapTo(scroll->viewport(), QPoint()), input->size())));
                            }
                }
        }
    }

    void systemPaletteChangesRemainReadable()
    {
        QSettings().setValue("appearance/theme", "system");
        const auto original = QApplication::palette();
        const auto restore = qScopeGuard([original] { QApplication::setPalette(original); });
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
        QCOMPARE(window.palette().color(QPalette::Window), original.color(QPalette::Window));
    }

    void explicitThemesOverrideSystemColorsAndPersist_data()
    {
        QTest::addColumn<bool>("systemDark");
        QTest::addColumn<QString>("theme");
        QTest::newRow("light-on-dark-system") << true << "light";
        QTest::newRow("dark-on-light-system") << false << "dark";
    }

    void explicitThemesOverrideSystemColorsAndPersist()
    {
        QFETCH(bool, systemDark);
        QFETCH(QString, theme);
        const auto original = QApplication::palette();
        const auto restore = qScopeGuard([original] { QApplication::setPalette(original); });
        QPalette system = original;
        for (const auto role : {QPalette::Window, QPalette::Base, QPalette::Button})
            system.setColor(role, systemDark ? Qt::black : Qt::white);
        for (const auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
            system.setColor(role, systemDark ? Qt::white : Qt::black);
        QApplication::setPalette(system);

        MainWindow window;
        // A fresh install keeps the approved light design even on a dark OS.
        QCOMPARE(window.palette().color(QPalette::Window), QColor("#f6f6f4"));
        QVERIFY(chooseTheme(window, theme, QDialogButtonBox::Save));
        const QColor background(theme == "dark" ? "#1b1d21" : "#f6f6f4");
        const QColor text(theme == "dark" ? "#ededf0" : "#24262b");
        QCOMPARE(window.palette().color(QPalette::Window), background);
        QCOMPARE(window.findChild<QLabel*>("miningState")->palette().color(QPalette::WindowText), text);
        QCOMPARE(window.findChild<QLineEdit*>("poolInput")->palette().color(QPalette::Text), text);
        QCOMPARE(QSettings().value("appearance/theme").toString(), theme);
        QCOMPARE(QApplication::palette().color(QPalette::Window), system.color(QPalette::Window));

        const auto directory = qEnvironmentVariable("FIROMINER_GUI_SCREENSHOT_DIR");
        if (!directory.isEmpty())
        {
            QVERIFY(QDir().mkpath(directory));
            window.show();
            QTest::qWait(50);
            QVERIFY(window.grab().save(QDir(directory).filePath(QString(QTest::currentDataTag()) + ".png")));
        }

        MainWindow restored;
        QCOMPARE(restored.palette().color(QPalette::Window), background);
        QVERIFY(chooseTheme(restored, "system", QDialogButtonBox::Save));
        QCOMPARE(restored.palette().color(QPalette::Window), system.color(QPalette::Window));
        QCOMPARE(restored.findChild<QLabel*>("miningState")->palette().color(QPalette::WindowText), system.color(QPalette::WindowText));
        QCOMPARE(restored.findChild<QLineEdit*>("poolInput")->palette().color(QPalette::Base), system.color(QPalette::Base));
        QCOMPARE(QSettings().value("appearance/theme").toString(), QString("system"));
    }

    void cancellingThemeChangeKeepsSavedChoice()
    {
        MainWindow window;
        QVERIFY(chooseTheme(window, "dark", QDialogButtonBox::Save));
        const auto savedPalette = window.palette();
        const auto savedSheet = window.styleSheet();
        QVERIFY(chooseTheme(window, "light", QDialogButtonBox::Cancel));
        QCOMPARE(window.palette(), savedPalette);
        QCOMPARE(window.styleSheet(), savedSheet);
        QCOMPARE(QSettings().value("appearance/theme").toString(), QString("dark"));
        MainWindow restored;
        QCOMPARE(restored.palette().color(QPalette::Window), QColor("#1b1d21"));
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
        window.resize(1120, 800);
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
        QVERIFY(chooseTheme(window, "dark", QDialogButtonBox::Save));
        QTest::qWait(50);
        QVERIFY(window.grab().save(QDir(directory).filePath("overview-dark.png")));
        bool settingsCaptured = false;
        QTimer::singleShot(0, &window, [&] {
            if (auto* dialog = window.findChild<QDialog*>("settingsDialog"))
            {
                QTest::qWait(50);
                settingsCaptured = dialog->grab().save(QDir(directory).filePath("settings-dark.png"));
                dialog->reject();
            }
        });
        window.findChild<QPushButton*>("settingsButton")->click();
        QVERIFY(settingsCaptured);
        QVERIFY(chooseTheme(window, "light", QDialogButtonBox::Save));
        window.setMiningState("Stopped");
        window.findChild<QListWidget*>("navigation")->setCurrentRow(1);
        QTest::qWait(50);
        QVERIFY(window.grab().save(QDir(directory).filePath("setup.png")));
        window.findChild<QPushButton*>("soloMode")->click();
        QTest::qWait(50);
        QVERIFY(window.grab().save(QDir(directory).filePath("solo-setup.png")));
        window.findChild<QPushButton*>("nodeGuideButton")->click();
        QTest::qWait(50);
        QVERIFY(window.grab().save(QDir(directory).filePath("solo-config.png")));
        window.findChild<QPushButton*>("nodeGuideButton")->click();
        QVERIFY(chooseTheme(window, "dark", QDialogButtonBox::Save));
        QTest::qWait(50);
        QVERIFY(window.grab().save(QDir(directory).filePath("solo-dark.png")));
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
