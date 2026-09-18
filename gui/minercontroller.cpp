#include "minercontroller.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QTcpServer>
#include <QUrl>
#include <QUuid>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace
{
constexpr qsizetype MaxApiFrame = 1024 * 1024;
constexpr qsizetype MaxLogLine = 16 * 1024;

QString encodeCredential(const QString& value)
{
    // PoolURI splits on dots and colons BEFORE decoding. Encode dots too, even
    // though RFC 3986 ordinarily permits them unescaped in a username.
    return QString::fromLatin1(QUrl::toPercentEncoding(value, {}, "."));
}

QString poolArgument(const MiningConfig& config)
{
    const QUrl url(config.poolUrl.trimmed(), QUrl::StrictMode);
    QString user = encodeCredential(config.wallet);
    if (!config.worker.isEmpty())
        user += '.' + encodeCredential(config.worker);
    user += ':' + encodeCredential(config.password);
    // PoolURI finds the LAST @ in the whole URI, and decodes + as a space.
    QString path = url.path(QUrl::FullyEncoded);
    path.replace('@', QStringLiteral("%40"));
    path.replace('+', QStringLiteral("%2B"));
    return url.scheme() + "://" + user + '@' + url.host(QUrl::FullyEncoded) + ':' +
        QString::number(url.port()) + path;
}

QStringList deviceIds(const QString& devices)
{
    return devices.split(QRegularExpression("[,\\s]+"), Qt::SkipEmptyParts);
}
}

MinerController::MinerController(QObject* parent) : QObject(parent)
{
    m_process.setProcessChannelMode(QProcess::MergedChannels);
#ifdef Q_OS_WIN
    m_process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args) {
        args->flags |= CREATE_NO_WINDOW;
    });
#endif
    m_pollTimer.setInterval(2000);
    m_timeout.setSingleShot(true);
    m_timeout.setInterval(5000);
    m_killTimer.setSingleShot(true);
    m_killTimer.setInterval(3000);
    m_monitorTimer.setSingleShot(true);
    m_socket.setReadBufferSize(MaxApiFrame + 1);

    connect(&m_pollTimer, &QTimer::timeout, this, &MinerController::poll);
    connect(&m_timeout, &QTimer::timeout, this, [this] {
        resetConnection(tr("The miner API did not respond. Retrying."));
    });
    connect(&m_killTimer, &QTimer::timeout, &m_process, &QProcess::kill);
    connect(&m_monitorTimer, &QTimer::timeout, this, [this] {
        stop();
        emit failure(tr("The miner's statistics connection could not be established or restored. "
                        "Stopping the miner. Try starting again after it stops."));
    });
    connect(&m_process, &QProcess::started, this, [this] {
        if (m_stopping)
        {
            requestStop();
            return;
        }
        setState(QStringLiteral("Preparing GPUs"));
        // Allow GPU discovery to finish, but never leave a miner unmonitored indefinitely.
        m_monitorTimer.start(60000);
        m_pollTimer.start();
        poll();
    });
    connect(&m_process, &QProcess::readyReadStandardOutput, this, [this] { readLogs(); });
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
        {
            emit failure(tr("Could not launch the miner: %1").arg(m_process.errorString()));
            complete();
        }
    });
    connect(&m_process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        readLogs(true);
        if (!m_stopping && (code != 0 || status == QProcess::CrashExit))
            emit failure(tr("The miner exited unexpectedly (code %1). See the activity log.").arg(code));
        complete();
    });
    connect(&m_socket, &QTcpSocket::connected, this, [this] {
        request(QStringLiteral("api_authorize"), {{"psw", m_apiPassword}});
    });
    connect(&m_socket, &QTcpSocket::readyRead, this, &MinerController::readApi);
    connect(&m_socket, &QTcpSocket::disconnected, this, [this] { resetConnection(); });
    connect(&m_socket, &QTcpSocket::errorOccurred, this,
        [this](QAbstractSocket::SocketError) { resetConnection(); });
}

MinerController::~MinerController()
{
    m_process.disconnect(this);
    m_socket.disconnect(this);
    if (isRunning())
    {
        requestStop();
        if (!m_process.waitForFinished(1000))
        {
            m_process.kill();
            m_process.waitForFinished(2000);
        }
    }
#ifdef Q_OS_WIN
    if (m_shutdownEvent)
        CloseHandle(m_shutdownEvent);
#endif
}

QString MinerController::validate(const MiningConfig& config)
{
    const QFileInfo executable(config.executable);
    if (!executable.isFile() || !executable.isExecutable())
        return tr("Select an existing Firominer executable.");
    const QUrl url(config.poolUrl.trimmed(), QUrl::StrictMode);
    if (url.scheme() == "http" || url.scheme() == "getwork")
        return tr("This GUI supports Stratum pools. Use command-line Firominer for HTTP/getwork mining.");
    static const QRegularExpression schemes(
        "^(stratum[123]?\\+(tcp|tls|tls12|ssl)|stratum|stratums|stratumss)$");
    if (!url.isValid() || !schemes.match(url.scheme()).hasMatch() || url.host().isEmpty() ||
        url.port() < 1 || url.port() > 65535)
        return tr("Enter a pool URL with a supported scheme, hostname and port, for example "
                  "stratum+tcp://pool.example:3333.");
    if (!url.userInfo().isEmpty() || url.authority().contains('@') || url.hasQuery() || url.hasFragment())
        return tr("Keep the pool URL separate from wallet, worker and password, without a query or fragment.");
    // The CLI's PoolURI parser does not strip IPv6 brackets before DNS lookup.
    if (url.host().contains(':'))
        return tr("Use the pool's DNS name or IPv4 address.");
    if (url.host().compare("exit", Qt::CaseInsensitive) == 0)
        return tr("Enter a pool hostname, not the CLI exit directive.");
    if (config.wallet.trimmed().isEmpty())
        return tr("Enter your wallet address or pool account.");
    for (const auto& field : {config.wallet, config.worker, config.password})
        for (const QChar character : field)
            if (character.category() == QChar::Other_Control)
                return tr("Wallet, worker and password cannot contain control characters.");
    if (poolArgument(config).toUtf8().size() > 1024)
        return tr("The pool connection details exceed the miner's 1024-byte limit.");
    if (config.backend != "auto" && config.backend != "cuda" && config.backend != "opencl")
        return tr("Choose Automatic, CUDA or OpenCL for the mining backend.");
    if (!config.devices.trimmed().isEmpty())
    {
        if (config.backend == "auto")
            return tr("Choose CUDA or OpenCL before selecting device numbers.");
        const auto ids = deviceIds(config.devices);
        if (ids.isEmpty() || !QRegularExpression("^[0-9,\\s]+$").match(config.devices).hasMatch())
            return tr("Enter device numbers separated by spaces or commas, for example 0, 1.");
        for (const auto& id : ids)
        {
            bool valid = false;
            id.toUInt(&valid);
            if (!valid)
                return tr("A device number is out of range.");
        }
    }
    return {};
}

QStringList MinerController::arguments(const MiningConfig& config, quint16 port,
    const QString& apiPassword)
{
    QStringList args{"--nocolor", "--stdout", "--HWMON", "2", "--api-bind",
        QStringLiteral("127.0.0.1:-%1").arg(port), "--api-password", apiPassword,
        "-P", poolArgument(config)};
    if (config.backend == "cuda")
        args << "--cuda";
    else if (config.backend == "opencl")
        args << "--opencl";
    if (!config.devices.trimmed().isEmpty())
    {
        args << (config.backend == "cuda" ? "--cu-devices" : "--cl-devices");
        args << deviceIds(config.devices);
    }
    return args;
}

bool MinerController::start(const MiningConfig& config)
{
    if (isRunning())
        return false;
    const QString error = validate(config);
    if (!error.isEmpty())
    {
        emit failure(error);
        return false;
    }
    QTcpServer reservation;
    if (!reservation.listen(QHostAddress::LocalHost, 0))
    {
        emit failure(tr("Could not reserve a local port for miner statistics."));
        return false;
    }
    m_port = reservation.serverPort();
    reservation.close();
    m_apiPassword = QUuid::createUuid().toString(QUuid::Id128);
    auto args = arguments(config, m_port, m_apiPassword);
#ifdef Q_OS_WIN
    const auto eventName = QStringLiteral("Local\\FirominerStop-%1")
                               .arg(QUuid::createUuid().toString(QUuid::Id128));
    m_shutdownEvent = CreateEventW(nullptr, TRUE, FALSE,
        reinterpret_cast<LPCWSTR>(eventName.utf16()));
    if (!m_shutdownEvent || GetLastError() == ERROR_ALREADY_EXISTS)
    {
        if (m_shutdownEvent)
            CloseHandle(m_shutdownEvent);
        m_shutdownEvent = nullptr;
        emit failure(tr("Could not create the miner's shutdown event."));
        return false;
    }
    args << "--shutdown-event" << eventName;
#endif
    m_stopping = false;
    m_hadStats = false;
    m_logBuffer.clear();
    m_discardLogLine = false;
    resetConnection();
    setState(QStringLiteral("Starting"));
    m_process.setWorkingDirectory(QFileInfo(config.executable).absolutePath());
    m_process.start(QFileInfo(config.executable).absoluteFilePath(), args);
    return true;
}

void MinerController::stop()
{
    if (!isRunning() || m_stopping)
        return;
    m_stopping = true;
    m_pollTimer.stop();
    m_monitorTimer.stop();
    resetConnection();
    setState(QStringLiteral("Stopping"));
    requestStop();
    m_killTimer.start();
}

void MinerController::requestStop()
{
#ifdef Q_OS_WIN
    // The console miner observes this event in its normal cleanup loop.
    // WM_CLOSE from QProcess::terminate() cannot stop a CREATE_NO_WINDOW child.
    if (m_shutdownEvent)
        SetEvent(m_shutdownEvent);
#else
    m_process.terminate();
#endif
}

bool MinerController::isRunning() const
{
    return m_process.state() != QProcess::NotRunning;
}

void MinerController::setState(const QString& state)
{
    if (m_state == state)
        return;
    m_state = state;
    emit stateChanged(state);
}

void MinerController::poll()
{
    if (!isRunning() || m_stopping || m_pendingId)
        return;
    if (m_socket.state() == QAbstractSocket::UnconnectedState)
    {
        m_timeout.start();
        m_socket.connectToHost(QHostAddress::LocalHost, m_port);
    }
    else if (m_authenticated)
        request(QStringLiteral("miner_getstatdetail"));
}

void MinerController::request(const QString& method, const QJsonObject& params)
{
    m_pendingId = ++m_nextId;
    const QJsonObject request{{"jsonrpc", "2.0"}, {"id", m_pendingId},
        {"method", method}, {"params", params}};
    m_socket.write(QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n');
    m_timeout.start();
}

void MinerController::readApi()
{
    m_apiBuffer += m_socket.readAll();
    if (m_apiBuffer.size() > MaxApiFrame)
    {
        resetConnection(tr("The miner API response was too large. Retrying."));
        return;
    }
    qsizetype end = -1;
    while ((end = m_apiBuffer.indexOf('\n')) >= 0)
    {
        const QByteArray line = m_apiBuffer.left(end).trimmed();
        m_apiBuffer.remove(0, end + 1);
        if (line.isEmpty())
            continue;
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(line, &error);
        const auto response = document.object();
        if (error.error != QJsonParseError::NoError || !document.isObject() ||
            response.value("jsonrpc") != QJsonValue("2.0") || !m_pendingId ||
            !response.value("id").isDouble() || response.value("id").toDouble() != m_pendingId ||
            (!response.value("error").isUndefined() && !response.value("error").isNull()))
        {
            resetConnection(tr("The miner API returned an invalid response. Retrying."));
            return;
        }
        m_pendingId = 0;
        m_timeout.stop();
        if (!m_authenticated)
        {
            // Firominer returns only jsonrpc and id on successful authorization.
            if (response.contains("result") && !response.value("result").isNull() &&
                response.value("result") != QJsonValue(true))
            {
                resetConnection(tr("The miner API authorization response was invalid. Retrying."));
                return;
            }
            m_authenticated = true;
            request(QStringLiteral("miner_getstatdetail"));
            continue;
        }
        const auto stats = response.value("result").toObject();
        if (!stats.value("mining").isObject() || !stats.value("devices").isArray() ||
            !stats.value("connection").isObject() || !stats.value("host").isObject())
        {
            resetConnection(tr("The miner API returned incomplete statistics. Retrying."));
            return;
        }
        m_hadStats = true;
        m_monitorTimer.start(15000);
        const auto devices = stats.value("devices").toArray();
        bool allPaused = !devices.isEmpty();
        for (const auto& device : devices)
            allPaused = allPaused && device.toObject().value("mining").toObject().value("paused").toBool();
        const auto hashrate = stats.value("mining").toObject().value("hashrate").toString().toULongLong(nullptr, 16);
        setState(!stats.value("connection").toObject().value("connected").toBool() ? "Reconnecting" :
            allPaused ? "Paused" : hashrate ? "Mining" : "Preparing GPUs");
        emit statistics(stats);
    }
}

void MinerController::resetConnection(const QString& reason)
{
    m_timeout.stop();
    m_pendingId = 0;
    m_authenticated = false;
    m_apiBuffer.clear();
    // abort() may synchronously emit disconnected, so suppress that recursion.
    const QSignalBlocker blocker(&m_socket);
    m_socket.abort();
    if (!reason.isEmpty())
        emit logLine(reason);
    if (isRunning() && !m_stopping)
        setState(m_hadStats ? QStringLiteral("Reconnecting") : QStringLiteral("Preparing GPUs"));
}

void MinerController::readLogs(bool flush)
{
    auto output = [this](QByteArray line) {
        QString text = QString::fromUtf8(line).trimmed();
        // Never expose pool credentials or the session's API password in logs.
        text.replace(QRegularExpression("([a-zA-Z][a-zA-Z0-9+]*://)[^\\s/]*@"),
            QStringLiteral("\\1[credentials]@"));
        if (!m_apiPassword.isEmpty())
            text.replace(m_apiPassword, QStringLiteral("[redacted]"));
        if (!text.isEmpty())
            emit logLine(text);
        // ApiServer reports a bind collision without exiting the miner. Stop the
        // owned child so a lost ephemeral-port reservation cannot hide mining.
        if (!m_stopping && text.contains("Could not start API server on port:"))
        {
            stop();
            emit failure(tr("The miner could not open its local statistics port. "
                            "Stopping the miner. Try starting again after it stops."));
        }
    };
    while (m_process.bytesAvailable())
    {
        m_logBuffer += m_process.read(4096);
        qsizetype end;
        if (m_discardLogLine)
        {
            end = m_logBuffer.indexOf('\n');
            if (end < 0)
            {
                m_logBuffer.clear();
                continue;
            }
            m_logBuffer.remove(0, end + 1);
            m_discardLogLine = false;
        }
        while ((end = m_logBuffer.indexOf('\n')) >= 0)
        {
            if (end <= MaxLogLine)
                output(m_logBuffer.left(end));
            else
                emit logLine(tr("An oversized miner log line was omitted."));
            m_logBuffer.remove(0, end + 1);
        }
        if (m_logBuffer.size() > MaxLogLine)
        {
            emit logLine(tr("An oversized miner log line was omitted."));
            m_logBuffer.clear();
            m_discardLogLine = true;
        }
    }
    if (flush && !m_logBuffer.isEmpty())
    {
        output(m_logBuffer);
        m_logBuffer.clear();
    }
}

void MinerController::complete()
{
    m_pollTimer.stop();
    m_killTimer.stop();
    m_monitorTimer.stop();
    resetConnection();
    m_stopping = false;
#ifdef Q_OS_WIN
    if (m_shutdownEvent)
        CloseHandle(m_shutdownEvent);
    m_shutdownEvent = nullptr;
#endif
    setState(QStringLiteral("Stopped"));
    emit finished();
}
