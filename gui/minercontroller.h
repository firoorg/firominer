#pragma once

#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QStringList>
#include <QTcpSocket>
#include <QTimer>

struct MiningConfig
{
    QString executable;
    QString poolUrl;
    QString wallet;
    QString worker;
    QString password;
    QString backend = QStringLiteral("auto");
    QString devices;
};

class MinerController : public QObject
{
    Q_OBJECT

public:
    explicit MinerController(QObject* parent = nullptr);
    ~MinerController() override;

    bool start(const MiningConfig& config);
    void stop();
    bool isRunning() const;
    static QString validate(const MiningConfig& config);
    static QStringList arguments(const MiningConfig& config, quint16 port,
        const QString& apiPassword);

signals:
    void statistics(const QJsonObject& stats);
    void stateChanged(const QString& state);
    void logLine(const QString& line);
    void failure(const QString& message);
    void finished();

private:
    void setState(const QString& state);
    void poll();
    void request(const QString& method, const QJsonObject& params = {});
    void readApi();
    void resetConnection(const QString& reason = {});
    void readLogs(bool flush = false);
    void complete();
    void requestStop();

    QProcess m_process;
    QTcpSocket m_socket;
    QTimer m_pollTimer;
    QTimer m_timeout;
    QTimer m_killTimer;
    QTimer m_monitorTimer;
    QByteArray m_apiBuffer;
    QByteArray m_logBuffer;
    QString m_apiPassword;
    QString m_state = QStringLiteral("Stopped");
    quint16 m_port = 0;
    int m_nextId = 0;
    int m_pendingId = 0;
    bool m_authenticated = false;
    bool m_stopping = false;
    bool m_hadStats = false;
    bool m_discardLogLine = false;
#ifdef Q_OS_WIN
    void* m_shutdownEvent = nullptr;
#endif
};
