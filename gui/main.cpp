#include "mainwindow.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QIcon>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("Firo");
    QCoreApplication::setApplicationName("Firominer GUI");
    QCoreApplication::setApplicationVersion(FIROMINER_GUI_VERSION);
    QApplication::setWindowIcon(QIcon(":/firominer.ico"));

    QCommandLineParser parser;
    parser.setApplicationDescription("Firominer desktop companion. Starts the command-line miner with your settings.");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.process(app);

    MainWindow window;
    window.show();
    return app.exec();
}
