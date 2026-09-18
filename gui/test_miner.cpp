// A local-only process fixture. It never initializes GPUs or contacts a pool.
#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTextStream>
#include <QTimer>
#include <QUrl>
#include <atomic>
#include <csignal>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace
{
std::atomic<bool> stopRequested{false};
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const auto args = app.arguments();
    auto option = [&args](const QString& name) {
        const int index = args.indexOf(name);
        return index < 0 ? QString() : args.value(index + 1);
    };
    const QString bind = option("--api-bind");
    const QString password = option("--api-password");
    const QString pool = option("-P");
    const QString mode = QUrl(pool).path();
    if (args.contains("--shutdown-event"))
        return 10; // Older Firominer versions reject this option.
    if (mode.startsWith("/external") && QDir::currentPath() != app.applicationDirPath())
        return 11;
#ifdef Q_OS_WIN
    const auto eventName = qEnvironmentVariable("FIROMINER_SHUTDOWN_EVENT");
    const bool legacy = mode == "/external-legacy";
    HANDLE shutdownEvent = legacy ? nullptr : OpenEventW(SYNCHRONIZE, FALSE,
        reinterpret_cast<LPCWSTR>(eventName.utf16()));
    if (!legacy && !shutdownEvent)
        return 8;
#else
    std::signal(SIGTERM, [](int) { stopRequested.store(true); });
#endif
    QTimer shutdownTimer;
    QObject::connect(&shutdownTimer, &QTimer::timeout, &app, [&] {
#ifdef Q_OS_WIN
        const bool stopping = shutdownEvent && WaitForSingleObject(shutdownEvent, 0) == WAIT_OBJECT_0;
#else
        const bool stopping = stopRequested.load();
#endif
        if (stopping && mode != "/ignore-stop")
        {
            QTextStream(stdout) << "Graceful shutdown complete" << Qt::endl;
            app.quit();
        }
    });
    shutdownTimer.start(20);
    if (!bind.startsWith("127.0.0.1:-") || password.size() < 16)
        return 3;
    QTcpServer occupiedPort;
    if (mode == "/bind-failure" &&
        !occupiedPort.listen(QHostAddress::LocalHost, bind.mid(11).toUShort()))
        return 9;
    QTcpServer server;
    if (!server.listen(QHostAddress::LocalHost, bind.mid(11).toUShort()))
    {
        if (mode != "/bind-failure")
            return 4;
        QTextStream(stdout) << "Could not start API server on port: " << bind.mid(11) << Qt::endl;
    }
    QTextStream(stdout) << "Pool " << pool << '\n' << "API password " << password << Qt::endl;
    if (mode == "/fragmented")
        QTextStream(stdout) << QString(20 * 1024, 'x') << "private-password" << Qt::endl;
    if (mode == "/exit")
        QTimer::singleShot(200, &app, [] { QCoreApplication::exit(7); });
    QObject::connect(&server, &QTcpServer::newConnection, &app, [&] {
        while (server.hasPendingConnections())
        {
            auto socket = server.nextPendingConnection();
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [=, &server] {
                QByteArray buffer = socket->property("buffer").toByteArray() + socket->readAll();
                while (buffer.contains('\n'))
                {
                    const auto end = buffer.indexOf('\n');
                    const auto request = QJsonDocument::fromJson(buffer.left(end)).object();
                    buffer.remove(0, end + 1);
                    QJsonObject response{{"jsonrpc", "2.0"}, {"id", request.value("id")}};
                    if (request.value("method") == QJsonValue("api_authorize"))
                    {
                        if (request.value("params").toObject().value("psw").toString() != password)
                        {
                            QCoreApplication::exit(5);
                            return;
                        }
                        socket->setProperty("authorized", true);
                        if (mode == "/auth-error")
                            response["error"] = QJsonObject{{"code", -401}, {"message", "Invalid password"}};
                        // Mirror the actual API: successful auth has no result.
                    }
                    else
                    {
                        if (!socket->property("authorized").toBool() ||
                            request.value("method") != QJsonValue("miner_getstatdetail"))
                        {
                            QCoreApplication::exit(6);
                            return;
                        }
                        if (mode == "/malformed")
                        {
                            socket->write("{broken json}\n");
                            continue;
                        }
                        if (mode == "/oversized")
                        {
                            socket->write(QByteArray(1024 * 1024 + 2, 'x'));
                            continue;
                        }
                        if (mode == "/timeout")
                            continue;
                        if (mode == "/wrong-id")
                            response["id"] = -1;
                        const QJsonObject mining{{"hashrate", "0x02faf080"},
                            {"shares", QJsonArray{17, 1, 0, 4}}, {"paused", mode == "/paused"}};
                        response["result"] = QJsonObject{
                            {"host", QJsonObject{{"runtime", 123}, {"version", "Test fixture"}}},
                            {"connection", QJsonObject{{"connected", mode != "/disconnected"}}},
                            {"mining", mining},
                            {"devices", QJsonArray{QJsonObject{{"mining", mining},
                                {"hardware", QJsonObject{{"name", "Test GPU"},
                                    {"sensors", QJsonArray{61, 45, 120}}}}}}}};
                        if (mode == "/incomplete")
                            response["result"] = QJsonObject{{"mining", mining}};
                    }
                    const auto data = QJsonDocument(response).toJson(QJsonDocument::Compact) + '\n';
                    if (mode == "/fragmented")
                    {
                        socket->write(data.left(5));
                        QTimer::singleShot(20, socket, [socket, data] { socket->write(data.mid(5)); });
                    }
                    else
                        socket->write(data);
                    if (mode == "/lost-api" && response.contains("result"))
                        QTimer::singleShot(20, socket, [socket, &server] {
                            server.close();
                            socket->disconnectFromHost();
                        });
                }
                socket->setProperty("buffer", buffer);
            });
        }
    });
    const int result = app.exec();
#ifdef Q_OS_WIN
    if (shutdownEvent)
        CloseHandle(shutdownEvent);
#endif
    return result;
}
