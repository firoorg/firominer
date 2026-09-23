#include "minercontroller.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QProcessEnvironment>
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
constexpr qsizetype MaxNodeResponse = 16 * 1024 * 1024;

QString encodeCredential(const QString& value)
{
    // PoolURI splits on dots and colons BEFORE decoding. Encode dots too, even
    // though RFC 3986 ordinarily permits them unescaped in a username.
    return QString::fromLatin1(QUrl::toPercentEncoding(value, {}, "."));
}

QString connectionArgument(const MiningConfig& config)
{
    const QUrl url((config.solo ? config.nodeUrl : config.poolUrl).trimmed(), QUrl::StrictMode);
    QString user = encodeCredential(config.solo ? config.rpcUser : config.wallet);
    if (!config.solo && !config.worker.isEmpty())
        user += '.' + encodeCredential(config.worker);
    user += ':' + encodeCredential(config.solo ? config.rpcPassword : config.password);
    // PoolURI finds the LAST @ in the whole URI, and decodes + as a space.
    QString path = url.path(QUrl::FullyEncoded);
    // Getwork sends PoolURI's decoded path verbatim as the HTTP request target.
    if (config.solo)
        path.replace('%', "%25");
    path.replace('@', QStringLiteral("%40"));
    path.replace('+', QStringLiteral("%2B"));
    return url.scheme() + "://" + user + '@' + url.host(QUrl::FullyEncoded) + ':' +
        QString::number(url.port()) + path;
}

QStringList deviceIds(const QString& devices)
{
    return devices.split(QRegularExpression("[,\\s]+"), Qt::SkipEmptyParts);
}

QString connectionError(const MiningConfig& config)
{
    const QString endpoint = (config.solo ? config.nodeUrl : config.poolUrl).trimmed();
    const QUrl url(endpoint, QUrl::StrictMode);
    static const QRegularExpression poolSchemes(
        "^(stratum[123]?\\+(tcp|tls|tls12|ssl)|stratum|stratums|stratumss)$");
    const bool scheme = config.solo ? url.scheme() == "http" || url.scheme() == "getwork" :
        poolSchemes.match(url.scheme()).hasMatch();
    if (!url.isValid() || !scheme || url.host().isEmpty() || url.port() < 1 || url.port() > 65535)
        return config.solo ? QObject::tr("Enter an HTTP/getwork node endpoint with a hostname and RPC port, for example http://127.0.0.1:8888.") :
            QObject::tr("Enter a Stratum pool endpoint with a hostname and port, for example stratum+tcp://pool.example:3333.");
    if (!url.userInfo().isEmpty() || url.authority().contains('@') || url.hasQuery() || url.hasFragment())
        return QObject::tr("Keep login details in the separate fields, not in the endpoint URL. Remove any query or fragment.");
    // The CLI's PoolURI parser does not strip IPv6 brackets before DNS lookup.
    if (url.host().contains(':') || url.host().compare("exit", Qt::CaseInsensitive) == 0)
        return QObject::tr("Use a node or pool DNS name or IPv4 address.");
    if (endpoint.contains(QRegularExpression("\\s")) || url.path().contains(QRegularExpression("[\\s\\x00-\\x1f\\x7f]")))
        return QObject::tr("The endpoint cannot contain whitespace or control characters.");
    const QStringList fields = config.solo ?
        QStringList{config.rpcUser, config.rpcPassword, config.rewardAddress} :
        QStringList{config.wallet, config.worker, config.password};
    for (const auto& field : fields)
        for (const QChar character : field)
            if (character.category() == QChar::Other_Control)
                return QObject::tr("Connection details cannot contain control characters.");
    if (config.solo)
    {
        if (config.rpcUser.trimmed().isEmpty() || config.rpcPassword.isEmpty())
            return QObject::tr("Enter the RPC username and password from your node's firo.conf.");
        if (config.rpcUser.contains(':'))
            return QObject::tr("RPC usernames cannot contain a colon. Update rpcuser in firo.conf and restart Firo Core.");
        if (config.rewardAddress.isEmpty() || config.rewardAddress.startsWith('-') ||
            config.rewardAddress.size() > 1024 || config.rewardAddress.contains(QRegularExpression("\\s")))
            return QObject::tr("Enter a transparent Firo reward address. Spark addresses cannot receive solo block rewards.");
    }
    else if (config.wallet.trimmed().isEmpty())
        return QObject::tr("Enter your wallet address or pool account.");
    if (connectionArgument(config).toUtf8().size() > 1024)
        return QObject::tr("The connection details exceed the miner's 1024-byte limit.");
    return {};
}
}

MinerController::MinerController(QObject* parent) : QObject(parent)
{
    m_nodeNetwork.setProxy(QNetworkProxy::NoProxy);
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
    if (m_nodeReply)
    {
        m_nodeReply->disconnect(this);
        m_nodeReply->abort();
    }
    m_process.disconnect(this);
    m_socket.disconnect(this);
    if (m_process.state() != QProcess::NotRunning)
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
    const auto error = connectionError(config);
    if (!error.isEmpty())
        return error;
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
        "-P", connectionArgument(config)};
    if (config.solo)
        args << "--reward-address" << config.rewardAddress;
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
    if (config.solo)
    {
        beginNodeCheck(config, true);
        return true;
    }
    return launch(config);
}

bool MinerController::launch(const MiningConfig& config)
{
    QTcpServer reservation;
    if (!reservation.listen(QHostAddress::LocalHost, 0))
    {
        emit failure(tr("Could not reserve a local port for miner statistics."));
        return false;
    }
    m_port = reservation.serverPort();
    reservation.close();
    m_apiPassword = QUuid::createUuid().toString(QUuid::Id128);
    m_rpcPassword = config.solo ? config.rpcPassword : QString();
    const auto args = arguments(config, m_port, m_apiPassword);
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
    // Older miners ignore this environment variable instead of rejecting a new CLI option.
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("FIROMINER_SHUTDOWN_EVENT"), eventName);
    m_process.setProcessEnvironment(environment);
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

bool MinerController::testNode(const MiningConfig& config)
{
    if (isRunning())
        return false;
    const auto error = config.solo ? connectionError(config) : tr("Select Solo to test your node.");
    if (!error.isEmpty())
    {
        emit nodeChecked(false, error);
        return false;
    }
    beginNodeCheck(config, false);
    return true;
}

void MinerController::beginNodeCheck(const MiningConfig& config, bool startWhenReady)
{
    m_nodeConfig = config;
    m_startAfterNodeCheck = startWhenReady;
    requestNode(0);
    setState(QStringLiteral("Checking node"));
}

void MinerController::requestNode(int stage)
{
    QUrl url(m_nodeConfig.nodeUrl.trimmed());
    url.setScheme("http");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setRawHeader("Authorization", "Basic " +
        (m_nodeConfig.rpcUser + ':' + m_nodeConfig.rpcPassword).toUtf8().toBase64());
    const QJsonObject body{{"jsonrpc", "2.0"}, {"id", stage + 1},
        {"method", stage == 0 ? "getblockchaininfo" : "getblocktemplate"},
        {"params", stage == 0 ? QJsonArray{} : QJsonArray{QJsonObject{}, m_nodeConfig.rewardAddress}}};
    m_nodeBuffer.clear();
    auto* reply = m_nodeNetwork.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    m_nodeReply = reply;
    reply->setReadBufferSize(MaxNodeResponse + 1);
    // Bound total request time, including a node that sends an endless slow response.
    QTimer::singleShot(10000, reply, [reply] { reply->abort(); });
    connect(reply, &QNetworkReply::readyRead, this, [this, reply] {
        m_nodeBuffer += reply->readAll();
        if (m_nodeBuffer.size() > MaxNodeResponse)
            reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, stage] {
        if (reply->isOpen())
            m_nodeBuffer += reply->readAll();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QJsonParseError parseError;
        const auto document = QJsonDocument::fromJson(m_nodeBuffer, &parseError);
        const auto response = document.object();
        const auto rpcError = response.value("error");
        const auto result = response.value("result").toObject();
        QString error;
        if (m_nodeBuffer.size() > MaxNodeResponse)
            error = tr("The node response was too large. Check your node endpoint.");
        else if (status == 401)
            error = tr("RPC login failed. Match rpcuser and rpcpassword in firo.conf, then restart Firo Core.");
        else if (status == 403)
            error = tr("RPC access denied. Check rpcallowip in firo.conf and restart Firo Core.");
        else if (status >= 300 && status < 400)
            error = tr("The node redirected the request. Enter its direct RPC endpoint; redirects are not followed.");
        else if (!status || reply->error() == QNetworkReply::OperationCanceledError)
            error = tr("Cannot reach the node or the request timed out. Check server=1, the RPC port, and that Firo Core is running.");
        else if (parseError.error != QJsonParseError::NoError || !document.isObject() ||
            response.value("id") != QJsonValue(stage + 1))
            error = tr("The endpoint did not return a valid Firo RPC response.");
        else if (!rpcError.isNull() && !rpcError.isUndefined())
        {
            const int code = rpcError.toObject().value("code").toInt();
            if (code == -10 || code == -28)
                error = tr("The node is starting or syncing. Wait for Firo Core to finish, then test again.");
            else if (code == -9)
                error = tr("The node has no network peers. Let Firo Core connect, then test again.");
            else if (code == -5 && stage == 1)
                error = tr("Invalid reward address. Enter a transparent Mainnet Firo address; Spark addresses are not supported.");
            else
                error = tr("The node rejected the mining check. Use a current Firo Core node with RPC enabled.");
        }
        else if (reply->error() != QNetworkReply::NoError || status != 200 || !response.value("result").isObject())
            error = tr("The node could not complete the mining check. Check its RPC configuration.");
        else if (stage == 0)
        {
            if (result.value("chain") != QJsonValue("main"))
                error = tr("Network mismatch. This GUI mines Mainnet; connect to a Mainnet Firo node.");
            else if (!result.value("blocks").isDouble() || !result.value("headers").isDouble())
                error = tr("The node returned incomplete sync information.");
            else if (result.value("blocks").toDouble() < result.value("headers").toDouble())
                error = tr("The node is syncing. Wait for Firo Core to finish, then test again.");
        }
        else
        {
            // getblocktemplate validates the transparent reward address and refuses work
            // while Firo's blockchain or masternode sync is incomplete.
            for (const auto* field : {"pprpcheader", "pprpcepoch", "height", "bits", "target"})
                if (!result.contains(field) || result.value(field).isNull())
                    error = tr("The node did not supply FiroPoW mining work. Use a current Firo Core node.");
        }
        reply->deleteLater();
        m_nodeReply = nullptr;
        if (error.isEmpty() && stage == 0)
            requestNode(1);
        else
            finishNodeCheck(error);
    });
}

void MinerController::finishNodeCheck(const QString& error)
{
    const auto config = m_nodeConfig;
    const bool launchAfterCheck = m_startAfterNodeCheck && error.isEmpty();
    m_startAfterNodeCheck = false;
    m_nodeConfig = {};
    m_nodeBuffer.clear();
    if (!launchAfterCheck || !launch(config))
        setState(QStringLiteral("Stopped"));
    emit nodeChecked(error.isEmpty(), error.isEmpty() ?
        tr("Node ready · Mainnet · Synced · Reward address valid") : error);
}

void MinerController::stop()
{
    if (m_nodeReply)
    {
        m_nodeReply->disconnect(this);
        m_nodeReply->abort();
        m_nodeReply->deleteLater();
        m_nodeReply = nullptr;
        finishNodeCheck(tr("Node check cancelled."));
        emit finished();
        return;
    }
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
    return m_nodeReply || m_process.state() != QProcess::NotRunning;
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
        const bool apiBindFailed = text.contains("Could not start API server on port:");
        // Never expose connection credentials or the session's API password in logs.
        text.replace(QRegularExpression("([a-zA-Z][a-zA-Z0-9+]*://)[^\\s/]*@"),
            QStringLiteral("\\1[credentials]@"));
        if (!m_apiPassword.isEmpty())
            text.replace(m_apiPassword, QStringLiteral("[redacted]"));
        if (!m_rpcPassword.isEmpty())
        {
            text.replace(encodeCredential(m_rpcPassword), QStringLiteral("[redacted]"));
            text.replace(m_rpcPassword, QStringLiteral("[redacted]"));
        }
        if (!text.isEmpty())
            emit logLine(text);
        // ApiServer reports a bind collision without exiting the miner. Stop the
        // owned child so a lost ephemeral-port reservation cannot hide mining.
        if (!m_stopping && apiBindFailed)
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
