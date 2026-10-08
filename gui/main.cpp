#include "mainwindow.h"
#include "language.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QIcon>
#include <QSettings>
#include <QTranslator>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("Firo");
    QCoreApplication::setApplicationName("Firominer GUI");
    QCoreApplication::setApplicationVersion(FIROMINER_GUI_VERSION);
    QTranslator translator;
    installGuiLanguage(translator, QSettings().value("ui/language", "system").toString());
    QApplication::setWindowIcon(QIcon(":/firominer.ico"));
    MainWindow::installBrandFonts();

    QCommandLineParser parser;
    parser.setApplicationDescription(QObject::tr("Firominer desktop companion. Starts the command-line miner with your settings."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.process(app);

    MainWindow window;
    window.show();
    return app.exec();
}
