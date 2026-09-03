#include "ApiServer.h"

#include <firominer/buildinfo.h>

#include <libethcore/Farm.h>

#ifndef HOST_NAME_MAX
#define HOST_NAME_MAX 255
#endif

// Define grayscale palette
#define HTTP_HDR0_COLOR "#e8e8e8"
#define HTTP_HDR1_COLOR "#f0f0f0"
#define HTTP_ROW0_COLOR "#f8f8f8"
#define HTTP_ROW1_COLOR "#ffffff"
#define HTTP_ROWRED_COLOR "#f46542"

static std::string redactUriUserInfo(std::string uri)
{
    auto userInfo = uri.find("://");
    auto at = uri.rfind('@');
    if (userInfo != std::string::npos && at != std::string::npos && at > userInfo + 3)
        uri.replace(userInfo + 3, at - userInfo - 3, "***");
    return uri;
}


/* helper functions getting values from a JSON request */
static bool getRequestValue(const char* membername, bool& refValue, Json::Value& jRequest,
    bool optional, Json::Value& jResponse)
{
    if (!jRequest.isMember(membername))
    {
        if (!optional)
        {
            jResponse["error"]["code"] = -32602;
            jResponse["error"]["message"] =
                std::string("Missing '") + std::string(membername) + std::string("'");
        }
        return optional;
    }
    if (!jRequest[membername].isBool())
    {
        jResponse["error"]["code"] = -32602;
        jResponse["error"]["message"] =
            std::string("Invalid type of value '") + std::string(membername) + std::string("'");
        return false;
    }
    if (jRequest[membername].empty())
    {
        jResponse["error"]["code"] = -32602;
        jResponse["error"]["message"] =
            std::string("Empty '") + std::string(membername) + std::string("'");
        return false;
    }
    refValue = jRequest[membername].asBool();
    return true;
}

static bool getRequestValue(const char* membername, unsigned& refValue, Json::Value& jRequest,
    bool optional, Json::Value& jResponse)
{
    if (!jRequest.isMember(membername))
    {
        if (!optional)
        {
            jResponse["error"]["code"] = -32602;
            jResponse["error"]["message"] =
                std::string("Missing '") + std::string(membername) + std::string("'");
        }
        return optional;
    }
    if (!jRequest[membername].isUInt())
    {
        jResponse["error"]["code"] = -32602;
        jResponse["error"]["message"] =
            std::string("Invalid type of value '") + std::string(membername) + std::string("'");
        return false;
    }
    if (jRequest[membername].empty())
    {
        jResponse["error"]["code"] = -32602;
        jResponse["error"]["message"] =
            std::string("Empty '") + std::string(membername) + std::string("'");
        return false;
    }
    refValue = jRequest[membername].asUInt();
    return true;
}

static bool getRequestValue(const char* membername, uint64_t& refValue, Json::Value& jRequest,
    bool optional, Json::Value& jResponse)
{
    if (!jRequest.isMember(membername))
    {
        if (!optional)
        {
            jResponse["error"]["code"] = -32602;
            jResponse["error"]["message"] =
                std::string("Missing '") + std::string(membername) + std::string("'");
        }
        return optional;
    }
    /* as there is no isUInt64() function we can not check the type */
    if (jRequest[membername].empty())
    {
        jResponse["error"]["code"] = -32602;
        jResponse["error"]["message"] =
            std::string("Empty '") + std::string(membername) + std::string("'");
        return false;
    }
    try
    {
        refValue = jRequest[membername].asUInt64();
    }
    catch (...)
    {
        jRequest["error"]["code"] = -32602;
        jResponse["error"]["message"] =
            std::string("Bad value in '") + std::string(membername) + std::string("'");
        return false;
    }
    return true;
}

static bool getRequestValue(const char* membername, Json::Value& refValue, Json::Value& jRequest,
    bool optional, Json::Value& jResponse)
{
    if (!jRequest.isMember(membername))
    {
        if (!optional)
        {
            jResponse["error"]["code"] = -32602;
            jResponse["error"]["message"] =
                std::string("Missing '") + std::string(membername) + std::string("'");
        }
        return optional;
    }
    if (!jRequest[membername].isObject())
    {
        jResponse["error"]["code"] = -32602;
        jResponse["error"]["message"] =
            std::string("Invalid type of value '") + std::string(membername) + std::string("'");
        return false;
    }
    if (jRequest[membername].empty())
    {
        jResponse["error"]["code"] = -32602;
        jResponse["error"]["message"] =
            std::string("Empty '") + std::string(membername) + std::string("'");
        return false;
    }
    refValue = jRequest[membername];
    return true;
}

static bool getRequestValue(const char* membername, std::string& refValue, Json::Value& jRequest,
    bool optional, Json::Value& jResponse)
{
    if (!jRequest.isMember(membername))
    {
        if (!optional)
        {
            jResponse["error"]["code"] = -32602;
            jResponse["error"]["message"] =
                std::string("Missing '") + std::string(membername) + std::string("'");
        }
        return optional;
    }
    if (!jRequest[membername].isString())
    {
        jResponse["error"]["code"] = -32602;
        jResponse["error"]["message"] =
            std::string("Invalid type of value '") + std::string(membername) + std::string("'");
        return false;
    }
    if (jRequest[membername].empty())
    {
        jResponse["error"]["code"] = -32602;
        jResponse["error"]["message"] =
            std::string("Empty '") + std::string(membername) + std::string("'");
        return false;
    }
    refValue = jRequest[membername].asString();
    return true;
}

static bool checkApiWriteAccess(bool is_read_only, Json::Value& jResponse)
{
    if (is_read_only)
    {
        jResponse["error"]["code"] = -32601;
        jResponse["error"]["message"] = "Method not available";
    }
    return !is_read_only;
}

static bool parseRequestId(Json::Value& jRequest, Json::Value& jResponse)
{
    const char* membername = "id";

    // NOTE: all errors have the same code (-32600) indicating this is an invalid request

    // be sure id is there and it's not empty, otherwise raise an error
    if (!jRequest.isMember(membername) || jRequest[membername].empty())
    {
        jResponse[membername] = Json::nullValue;
        jResponse["error"]["code"] = -32600;
        jResponse["error"]["message"] = "Invalid Request (missing or empty id)";
        return false;
    }

    // try to parse id as Uint
    if (jRequest[membername].isUInt())
    {
        jResponse[membername] = jRequest[membername].asUInt();
        return true;
    }

    // try to parse id as String
    if (jRequest[membername].isString())
    {
        jResponse[membername] = jRequest[membername].asString();
        return true;
    }

    // id has invalid type
    jResponse[membername] = Json::nullValue;
    jResponse["error"]["code"] = -32600;
    jResponse["error"]["message"] = "Invalid Request (id has invalid type)";
    return false;
}

ApiServer::ApiServer(string address, int portnum, string password)
  : m_password(std::move(password)),
    m_address(address),
    m_acceptor(g_io_service),
    m_io_strand(g_io_service)
{
    if (portnum < 0)
    {
        m_portnumber = -portnum;
        m_readonly = true;
    }
    else
    {
        m_portnumber = portnum;
        m_readonly = false;
    }
}

void ApiServer::start()
{
    // cnote << "ApiServer::start";
    if (m_portnumber == 0)
        return;

    tcp::endpoint endpoint(boost::asio::ip::address::from_string(m_address), m_portnumber);

    // Try to bind to port number
    // if exception occurs it may be due to the fact that
    // requested port is already in use by another service
    try
    {
        m_acceptor.open(endpoint.protocol());
        m_acceptor.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));
        m_acceptor.bind(endpoint);
        m_acceptor.listen(64);
    }
    catch (const std::exception&)
    {
        cwarn << "Could not start API server on port: " +
                     to_string(m_acceptor.local_endpoint().port());
        cwarn << "Ensure port is not in use by another service";
        return;
    }

    cnote << "Api server listening on port " + to_string(m_acceptor.local_endpoint().port())
          << (m_password.empty() ? "." : ". Authentication needed.");
    auto lifetime = std::make_shared<Lifetime>(this);
    m_lifetime = lifetime;
    m_running.store(true, std::memory_order_relaxed);
    begin_accept(lifetime);
}

void ApiServer::stop()
{
    // Exit if not started
    if (!m_running.exchange(false, std::memory_order_relaxed))
        return;

    auto lifetime = std::move(m_lifetime);
    std::vector<std::shared_ptr<ApiConnection>> sessions;
    {
        std::lock_guard<std::mutex> lock(lifetime->mutex);
        lifetime->server = nullptr;

        boost::system::error_code ec;
        m_acceptor.cancel(ec);
        m_acceptor.close(ec);
        sessions.swap(m_sessions);
    }

    for (auto const& session : sessions)
    {
        if (session->m_io_strand.running_in_this_thread())
        {
            session->m_onDisconnected = {};
            session->disconnect();
            continue;
        }
        std::lock_guard<std::mutex> lock(session->m_mutex);
        session->m_onDisconnected = {};
        session->disconnect();
    }
}

void ApiServer::begin_accept(std::shared_ptr<Lifetime> const& state)
{
    if (!isRunning())
        return;

    auto session =
        std::make_shared<ApiConnection>(m_io_strand, ++lastSessionId, m_readonly, m_password);
    std::weak_ptr<Lifetime> lifetime = state;
    m_acceptor.async_accept(session->socket(), m_io_strand.wrap(
        [lifetime, session](const boost::system::error_code& ec) {
            if (auto state = lifetime.lock())
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                if (state->server)
                    state->server->handle_accept(session, ec, state);
            }
        }));
}

void ApiServer::handle_accept(std::shared_ptr<ApiConnection> session, boost::system::error_code ec,
    std::shared_ptr<Lifetime> const& state)
{
    // Start new connection
    // cnote << "ApiServer::handle_accept";
    if (!ec)
    {
        std::weak_ptr<Lifetime> lifetime = state;
        session->onDisconnected([lifetime](int id) {
            auto state = lifetime.lock();
            if (!state)
                return;
            std::lock_guard<std::mutex> lock(state->mutex);
            if (!state->server)
                return;

            // Destroy pointer to session
            auto& sessions = state->server->m_sessions;
            auto it = find_if(sessions.begin(), sessions.end(),
                [&id](const std::shared_ptr<ApiConnection> session) {
                    return session->getId() == id;
                });
            if (it != sessions.end())
                sessions.erase(it);
        });
        m_sessions.push_back(session);
        cnote << "New API session from " << session->socket().remote_endpoint();
        session->start();
    }
    else
    {
        session.reset();
    }

    // Resubmit new accept
    begin_accept(state);
}

void ApiConnection::disconnect()
{
    // cnote << "ApiConnection::disconnect";

    if (m_disconnected)
        return;
    m_disconnected = true;

    // Cancel pending operations
    boost::system::error_code ec;
    m_socket.cancel(ec);

    if (m_socket.is_open())
    {
        m_socket.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
        m_socket.close(ec);
    }

    auto onDisconnected = std::move(m_onDisconnected);
    if (onDisconnected)
        onDisconnected(this->getId());
}

ApiConnection::ApiConnection(
    boost::asio::io_service::strand& _strand, int id, bool readonly, string password)
  : m_sessionId(id),
    m_socket(g_io_service),
    m_io_strand(_strand),
    m_recvBuffer(c_maxRequestSize),
    m_readonly(readonly),
    m_password(std::move(password))
{
    m_jSwBuilder.settings_["indentation"] = "";
    if (!m_password.empty())
        m_is_authenticated = false;
}

void ApiConnection::start()
{
    // cnote << "ApiConnection::start";
    recvSocketData();
}

void ApiConnection::processRequest(Json::Value& jRequest, Json::Value& jResponse)
{
    jResponse["jsonrpc"] = "2.0";

    // Strict sanity checks over jsonrpc v2
    if (!parseRequestId(jRequest, jResponse))
        return;

    std::string jsonrpc;
    std::string _method;
    if (!getRequestValue("jsonrpc", jsonrpc, jRequest, false, jResponse) || jsonrpc != "2.0" ||
        !getRequestValue("method", _method, jRequest, false, jResponse))
    {
        jResponse["error"]["code"] = -32600;
        jResponse["error"]["message"] = "Invalid Request";
        return;
    }

    // Check authentication
    if (!m_is_authenticated || _method == "api_authorize")
    {
        if (_method != "api_authorize")
        {
            // Use error code like http 403 Forbidden
            jResponse["error"]["code"] = -403;
            jResponse["error"]["message"] = "Authorization needed";
            return;
        }

        m_is_authenticated =
            false; /* we allow api_authorize method even if already authenticated */

        Json::Value jRequestParams;
        if (!getRequestValue("params", jRequestParams, jRequest, false, jResponse))
            return;

        std::string psw;
        if (!getRequestValue("psw", psw, jRequestParams, false, jResponse))
            return;

        unsigned result = psw.size() == m_password.size() ? 0 : 1;
        for (size_t i = 0; i < m_password.size(); ++i)
            result |= static_cast<unsigned char>(m_password[i]) ^
                      static_cast<unsigned char>(i < psw.size() ? psw[i] : 0);

        if (result == 0)
        {
            m_is_authenticated = true;
        }
        else
        {
            // Use error code like http 401 Unauthorized
            jResponse["error"]["code"] = -401;
            jResponse["error"]["message"] = "Invalid password";
            cerr << "API : Invalid password provided.";
            // Should we close the connection in the outer function after invalid password ?
        }
        /*
         * possible wait here a fixed time of eg 10s before respond after 5 invalid
           authentications were submitted to prevent brute force password attacks.
        */
        return;
    }

    assert(m_is_authenticated);
    cnote << "API : Method " << _method << " requested";
    if (_method == "miner_getstat1")
    {
        jResponse["result"] = getMinerStat1();
    }

    else if (_method == "miner_getstatdetail")
    {
        jResponse["result"] = getMinerStatDetail();
    }

    else if (_method == "miner_shuffle")
    {
        // Gives nonce scrambler a new range
        jResponse["result"] = true;
        Farm::f().shuffle();
    }

    else if (_method == "miner_ping")
    {
        // Replies back to (check for liveness)
        jResponse["result"] = "pong";
    }

    else if (_method == "miner_restart")
    {
        // Send response to client of success
        // and invoke an async restart
        // to prevent locking
        if (!checkApiWriteAccess(m_readonly, jResponse))
            return;
        jResponse["result"] = true;
        Farm::f().restart_async();
    }

    else if (_method == "miner_reboot")
    {
        if (!checkApiWriteAccess(m_readonly, jResponse))
            return;

        jResponse["result"] = Farm::f().reboot({{"api_miner_reboot"}});
    }

    else if (_method == "miner_getconnections")
    {
        // Returns a list of configured pools
        jResponse["result"] = PoolManager::p().getConnectionsJson();
    }

    else if (_method == "miner_addconnection")
    {
        if (!checkApiWriteAccess(m_readonly, jResponse))
            return;

        Json::Value jRequestParams;
        if (!getRequestValue("params", jRequestParams, jRequest, false, jResponse))
            return;

        std::string sUri;
        if (!getRequestValue("uri", sUri, jRequestParams, false, jResponse))
            return;

        try
        {
            // If everything ok then add this new uri
            PoolManager::p().addConnection(sUri);
            jResponse["result"] = true;
        }
        catch (...)
        {
            jResponse["error"]["code"] = -422;
            jResponse["error"]["message"] = "Bad URI : " + sUri;
        }
    }

    else if (_method == "miner_setactiveconnection")
    {
        if (!checkApiWriteAccess(m_readonly, jResponse))
            return;

        Json::Value jRequestParams;
        if (!getRequestValue("params", jRequestParams, jRequest, false, jResponse))
            return;
        if (jRequestParams.isMember("index"))
        {
            unsigned index;
            if (getRequestValue("index", index, jRequestParams, false, jResponse))
            {
                try
                {
                    PoolManager::p().setActiveConnection(index);
                }
                catch (const std::exception& _ex)
                {
                    std::string what = _ex.what();
                    jResponse["error"]["code"] = -422;
                    jResponse["error"]["message"] = what;
                    return;
                }
            }
            else
            {
                jResponse["error"]["code"] = -422;
                jResponse["error"]["message"] = "Invalid index";
                return;
            }
        }
        else
        {
            string uri;
            if (getRequestValue("URI", uri, jRequestParams, false, jResponse))
            {
                try
                {
                    PoolManager::p().setActiveConnection(uri);
                }
                catch (const std::exception& _ex)
                {
                    std::string what = _ex.what();
                    jResponse["error"]["code"] = -422;
                    jResponse["error"]["message"] = what;
                    return;
                }
            }
            else
            {
                jResponse["error"]["code"] = -422;
                jResponse["error"]["message"] = "Invalid index";
                return;
            }
        }
        jResponse["result"] = true;
    }

    else if (_method == "miner_removeconnection")
    {
        if (!checkApiWriteAccess(m_readonly, jResponse))
            return;

        Json::Value jRequestParams;
        if (!getRequestValue("params", jRequestParams, jRequest, false, jResponse))
            return;

        unsigned index;
        if (!getRequestValue("index", index, jRequestParams, false, jResponse))
            return;

        try
        {
            PoolManager::p().removeConnection(index);
            jResponse["result"] = true;
        }
        catch (const std::exception& _ex)
        {
            std::string what = _ex.what();
            jResponse["error"]["code"] = -422;
            jResponse["error"]["message"] = what;
            return;
        }
    }

    else if (_method == "miner_getscramblerinfo")
    {
        jResponse["result"] = Farm::f().get_nonce_scrambler_json();
    }

    else if (_method == "miner_setscramblerinfo")
    {
        if (!checkApiWriteAccess(m_readonly, jResponse))
            return;

        Json::Value jRequestParams;
        if (!getRequestValue("params", jRequestParams, jRequest, false, jResponse))
            return;

        bool any_value_provided = false;
        uint64_t nonce = Farm::f().get_nonce_scrambler();
        unsigned exp = Farm::f().get_segment_width();

        if (jRequestParams.isMember("noncescrambler"))
        {
            string nonceHex;

            any_value_provided = true;

            nonceHex = jRequestParams["noncescrambler"].asString();
            if (nonceHex.substr(0, 2) == "0x")
            {
                try
                {
                    nonce = std::stoul(nonceHex, nullptr, 16);
                }
                catch (const std::exception&)
                {
                    jResponse["error"]["code"] = -422;
                    jResponse["error"]["message"] = "Invalid nonce";
                    return;
                }
            }
            else
            {
                // as we already know there is a "noncescrambler" element we can use optional=false
                if (!getRequestValue("noncescrambler", nonce, jRequestParams, false, jResponse))
                    return;
            }
        }

        if (jRequestParams.isMember("segmentwidth"))
        {
            any_value_provided = true;
            if (!getRequestValue("segmentwidth", exp, jRequestParams, false, jResponse))
                return;
        }

        if (!any_value_provided)
        {
            jResponse["error"]["code"] = -32602;
            jResponse["error"]["message"] = "Missing parameters";
            return;
        }

        if (exp < 10)
            exp = 10;  // Not below
        if (exp > 50)
            exp = 40;  // Not above
        Farm::f().set_nonce_scrambler(nonce);
        Farm::f().set_nonce_segment_width(exp);
        jResponse["result"] = true;
    }

    else if (_method == "miner_pausegpu")
    {
        if (!checkApiWriteAccess(m_readonly, jResponse))
            return;

        Json::Value jRequestParams;
        if (!getRequestValue("params", jRequestParams, jRequest, false, jResponse))
            return;

        unsigned index;
        if (!getRequestValue("index", index, jRequestParams, false, jResponse))
            return;

        bool pause;
        if (!getRequestValue("pause", pause, jRequestParams, false, jResponse))
            return;

        auto const& miner = Farm::f().getMiner(index);
        if (miner)
        {
            if (pause)
                miner->pause(MinerPauseEnum::PauseDueToAPIRequest);
            else
                miner->resume(MinerPauseEnum::PauseDueToAPIRequest);

            jResponse["result"] = true;
        }
        else
        {
            jResponse["error"]["code"] = -422;
            jResponse["error"]["message"] = "Index out of bounds";
            return;
        }
    }

    else if (_method == "miner_setverbosity")
    {
        if (!checkApiWriteAccess(m_readonly, jResponse))
            return;

        Json::Value jRequestParams;
        if (!getRequestValue("params", jRequestParams, jRequest, false, jResponse))
            return;

        unsigned verbosity;
        if (!getRequestValue("verbosity", verbosity, jRequestParams, false, jResponse))
            return;

        if (verbosity >= LOG_NEXT)
        {
            jResponse["error"]["code"] = -422;
            jResponse["error"]["message"] =
                "Verbosity out of bounds (0-" + to_string(LOG_NEXT - 1) + ")";
            return;
        }
        cnote << "Setting verbosity level to " << verbosity;
        g_logOptions = verbosity;
        jResponse["result"] = true;
    }

    else
    {
        // Any other method not found
        jResponse["error"]["code"] = -32601;
        jResponse["error"]["message"] = "Method not found";
    }
}

void ApiConnection::recvSocketData()
{
    if (m_disconnected || !m_socket.is_open())
        return;

    auto self = shared_from_this();
    boost::asio::async_read(m_socket, m_recvBuffer, boost::asio::transfer_at_least(1),
        m_io_strand.wrap([self](const boost::system::error_code& ec, std::size_t bytesTransferred) {
            std::lock_guard<std::mutex> lock(self->m_mutex);
            self->onRecvSocketDataCompleted(ec, bytesTransferred);
        }));
}

void ApiConnection::onRecvSocketDataCompleted(
    const boost::system::error_code& ec, std::size_t bytes_transferred)
{
    /*
    Standard http request detection pattern
    1st group : any UPPERCASE word
    2nd group : the path
    3rd group : HTTP version
    */
    static std::regex http_pattern("^([A-Z]{1,6}) (\\/[\\S]*) (HTTP\\/1\\.[0-9]{1})");
    std::smatch http_matches;

    if (!ec && bytes_transferred > 0)
    {
        // Extract received message and free the buffer
        std::string rx_message(
            boost::asio::buffer_cast<const char*>(m_recvBuffer.data()), bytes_transferred);
        m_recvBuffer.consume(bytes_transferred);
        m_message.append(rx_message);

        if (m_message.size() > c_maxRequestSize)
        {
            disconnect();
            return;
        }

        std::string line;
        std::string linedelimiter;
        std::size_t linedelimiteroffset;

        if (m_message.size() < 4 && m_message.find('\n') == std::string::npos)
        {
            recvSocketData();
            return;  // Wait for other data to come in
        }

        if (std::regex_search(
                m_message, http_matches, http_pattern, std::regex_constants::match_default))
        {
            // We got an HTTP request
            std::string http_method = http_matches[1].str();
            std::string http_path = http_matches[2].str();
            std::string http_ver = http_matches[3].str();

            if (!m_is_authenticated)
            {
                std::string what = "Authorization needed";
                std::stringstream ss;
                ss << http_ver << " 401 Unauthorized\r\n"
                   << "Server: " << firominer_get_buildinfo()->project_name_with_version << "\r\n"
                   << "Content-Type: text/plain\r\n"
                   << "Content-Length: " << what.size() << "\r\n\r\n"
                   << what;
                sendSocketData(ss.str(), true);
                m_message.clear();
                return;
            }

            // Do we support method ?
            if (http_method != "GET")
            {
                std::string what = "Method " + http_method + " not allowed";
                std::stringstream ss;
                ss << http_ver << " "
                   << "405 Method not allowed\r\n"
                   << "Server: " << firominer_get_buildinfo()->project_name_with_version << "\r\n"
                   << "Content-Type: text/plain\r\n"
                   << "Content-Length: " << what.size() << "\r\n\r\n"
                   << what << "\r\n";
                sendSocketData(ss.str(), true);
                m_message.clear();
                return;
            }

            // Do we support path ?
            if (http_path != "/" && http_path != "/getstat1")
            {
                std::string what =
                    "The requested resource " + http_path + " not found on this server";
                std::stringstream ss;
                ss << http_ver << " "
                   << "404 Not Found\r\n"
                   << "Server: " << firominer_get_buildinfo()->project_name_with_version << "\r\n"
                   << "Content-Type: text/plain\r\n"
                   << "Content-Length: " << what.size() << "\r\n\r\n"
                   << what << "\r\n";
                sendSocketData(ss.str(), true);
                m_message.clear();
                return;
            }

            //// Get all the lines - we actually don't care much
            //// until we support other http methods or paths
            //// Keep this for future use (if any)
            //// Remember to #include <boost/algorithm/string.hpp>
            // std::vector<std::string> lines;
            // boost::split(lines, m_message, [](char _c) { return _c == '\n'; });

            std::stringstream ss;  // Builder of the response

            if (http_method == "GET" && (http_path == "/" || http_path == "/getstat1"))
            {
                try
                {
                    std::string body = getHttpMinerStatDetail();
                    ss.clear();
                    ss << http_ver << " "
                       << "200 Ok Error\r\n"
                       << "Server: " << firominer_get_buildinfo()->project_name_with_version
                       << "\r\n"
                       << "Content-Type: text/html; charset=utf-8\r\n"
                       << "Content-Length: " << body.size() << "\r\n\r\n"
                       << body << "\r\n";
                }
                catch (const std::exception& _ex)
                {
                    std::string what = "Internal error : " + std::string(_ex.what());
                    ss.clear();
                    ss << http_ver << " "
                       << "500 Internal Server Error\r\n"
                       << "Server: " << firominer_get_buildinfo()->project_name_with_version
                       << "\r\n"
                       << "Content-Type: text/plain\r\n"
                       << "Content-Length: " << what.size() << "\r\n\r\n"
                       << what << "\r\n";
                }
            }

            sendSocketData(ss.str(), true);
            m_message.clear();
        }
        else
        {
            // We got a Json request
            // Process each line in the transmission
            linedelimiter = "\n";

            linedelimiteroffset = m_message.find(linedelimiter);
            while (linedelimiteroffset != string::npos)
            {
                if (linedelimiteroffset > 0)
                {
                    line = m_message.substr(0, linedelimiteroffset);
                    boost::trim(line);

                    if (!line.empty())
                    {
                        // Test validity of chunk and process
                        Json::Value jMsg;
                        Json::Value jRes;
                        Json::Reader jRdr;
                        if (jRdr.parse(line, jMsg))
                        {
                            try
                            {
                                // Run in sync so no 2 different async reads may overlap
                                processRequest(jMsg, jRes);
                            }
                            catch (const std::exception& _ex)
                            {
                                jRes = Json::Value();
                                jRes["jsonrpc"] = "2.0";
                                jRes["id"] = Json::Value::null;
                                jRes["error"]["errorcode"] = "500";
                                jRes["error"]["message"] = _ex.what();
                            }
                        }
                        else
                        {
                            jRes = Json::Value();
                            jRes["jsonrpc"] = "2.0";
                            jRes["id"] = Json::Value::null;
                            jRes["error"]["errorcode"] = "-32700";
                            string what = jRdr.getFormattedErrorMessages();
                            boost::replace_all(what, "\n", " ");
                            cwarn << "API : Got invalid Json message " << what;
                            jRes["error"]["message"] = "Json parse error : " + what;
                        }

                        // Send response to client
                        sendSocketData(jRes);
                    }
                }

                // Next line (if any)
                m_message.erase(0, linedelimiteroffset + 1);
                linedelimiteroffset = m_message.find(linedelimiter);
            }

            // Eventually keep reading from socket
            if (m_socket.is_open() && m_sendQueue.empty())
                recvSocketData();
        }
    }
    else
    {
        disconnect();
    }
}

void ApiConnection::sendSocketData(Json::Value const& jReq, bool _disconnect)
{
    if (!m_socket.is_open())
        return;
    std::stringstream line;
    line << Json::writeString(m_jSwBuilder, jReq) << std::endl;
    sendSocketData(line.str(), _disconnect);
}

void ApiConnection::sendSocketData(std::string const& _s, bool _disconnect)
{
    if (m_disconnected || !m_socket.is_open())
        return;

    bool idle = m_sendQueue.empty();
    m_sendQueue.emplace_back(_s, _disconnect);
    if (idle)
        startSend();
}

void ApiConnection::startSend()
{
    if (m_disconnected || m_sendQueue.empty())
        return;

    auto self = shared_from_this();
    boost::asio::async_write(m_socket, boost::asio::buffer(m_sendQueue.front().first),
        m_io_strand.wrap([self](const boost::system::error_code& ec, std::size_t) {
            std::lock_guard<std::mutex> lock(self->m_mutex);
            self->onSendSocketDataCompleted(ec);
        }));
}

void ApiConnection::onSendSocketDataCompleted(const boost::system::error_code& ec)
{
    if (ec)
    {
        disconnect();
        return;
    }

    bool disconnectAfter = m_sendQueue.front().second;
    m_sendQueue.pop_front();
    if (disconnectAfter)
        disconnect();
    else if (!m_sendQueue.empty())
        startSend();
    else
        recvSocketData();
}

Json::Value ApiConnection::getMinerStat1()
{
    auto connection = PoolManager::p().getActiveConnection();
    TelemetryType t = Farm::f().Telemetry();
    auto runningTime =
        std::chrono::duration_cast<std::chrono::minutes>(steady_clock::now() - t.start);


    ostringstream totalMhEth;
    ostringstream totalMhDcr;
    ostringstream detailedMhEth;
    ostringstream detailedMhDcr;
    ostringstream tempAndFans;
    ostringstream poolAddresses;
    ostringstream invalidStats;

    totalMhEth << std::fixed << std::setprecision(0) << t.farm.hashrate / 1000.0f << ";"
               << t.farm.solutions.accepted << ";" << t.farm.solutions.rejected;
    totalMhDcr << "0;0;0";                            // DualMining not supported
    invalidStats << t.farm.solutions.failed << ";0";  // Invalid + Pool switches
    if (connection)
        poolAddresses << connection->Host() << ':' << connection->Port();
    invalidStats << ";0;0";  // DualMining not supported

    int gpuIndex;
    int numGpus = t.miners.size();

    for (gpuIndex = 0; gpuIndex < numGpus; gpuIndex++)
    {
        detailedMhEth << std::fixed << std::setprecision(0)
                      << t.miners.at(gpuIndex).hashrate / 1000.0f
                      << (((numGpus - 1) > gpuIndex) ? ";" : "");
        detailedMhDcr << "off"
                      << (((numGpus - 1) > gpuIndex) ? ";" : "");  // DualMining not supported
    }

    for (gpuIndex = 0; gpuIndex < numGpus; gpuIndex++)
    {
        tempAndFans << t.miners.at(gpuIndex).sensors.tempC << ";"
                    << t.miners.at(gpuIndex).sensors.fanP
                    << (((numGpus - 1) > gpuIndex) ? ";" : "");  // Fetching Temp and Fans
    }

    Json::Value jRes;

    jRes[0] = firominer_get_buildinfo()->project_name_with_version;  // miner version.
    jRes[1] = toString(runningTime.count());                        // running time, in minutes.
    jRes[2] = totalMhEth.str();  // total ETH hashrate in MH/s, number of ETH shares, number of ETH
                                 // rejected shares.
    jRes[3] = detailedMhEth.str();  // detailed ETH hashrate for all GPUs.
    jRes[4] = totalMhDcr.str();  // total DCR hashrate in MH/s, number of DCR shares, number of DCR
                                 // rejected shares.
    jRes[5] = detailedMhDcr.str();  // detailed DCR hashrate for all GPUs.
    jRes[6] = tempAndFans.str();    // Temperature and Fan speed(%) pairs for all GPUs.
    jRes[7] =
        poolAddresses.str();  // current mining pool. For dual mode, there will be two pools here.
    jRes[8] = invalidStats.str();  // number of ETH invalid shares, number of ETH pool switches,
                                   // number of DCR invalid shares, number of DCR pool switches.

    return jRes;
}

Json::Value ApiConnection::getMinerStatDetailPerMiner(
    const TelemetryType& _t, std::shared_ptr<Miner> _miner)
{
    unsigned _index = _miner->Index();
    std::chrono::steady_clock::time_point _now = std::chrono::steady_clock::now();

    Json::Value jRes;
    DeviceDescriptor minerDescriptor = _miner->getDescriptor();

    jRes["_index"] = _index;
    jRes["_mode"] =
        (minerDescriptor.subscriptionType == DeviceSubscriptionTypeEnum::Cuda ? "CUDA" : "OpenCL");

    /* Hardware Info */
    Json::Value hwinfo;
    hwinfo["pci"] = minerDescriptor.uniqueId;
    hwinfo["type"] =
        (minerDescriptor.type == DeviceTypeEnum::Gpu ?
                "GPU" :
                (minerDescriptor.type == DeviceTypeEnum::Accelerator ? "ACCELERATOR" : "CPU"));
    ostringstream ss;
    ss << (minerDescriptor.clDetected ? minerDescriptor.clName : minerDescriptor.cuName) << " "
       << dev::getFormattedMemory((double)minerDescriptor.totalMemory);
    hwinfo["name"] = ss.str();

    /* Hardware Sensors*/
    Json::Value sensors = Json::Value(Json::arrayValue);

    sensors.append(_t.miners.at(_index).sensors.tempC);
    sensors.append(_t.miners.at(_index).sensors.fanP);
    sensors.append(_t.miners.at(_index).sensors.powerW);

    hwinfo["sensors"] = sensors;

    /* Mining Info */
    Json::Value mininginfo;
    Json::Value jshares = Json::Value(Json::arrayValue);
    Json::Value jsegment = Json::Value(Json::arrayValue);
    jshares.append(_t.miners.at(_index).solutions.accepted);
    jshares.append(_t.miners.at(_index).solutions.rejected);
    jshares.append(_t.miners.at(_index).solutions.failed);

    auto solution_lastupdated = std::chrono::duration_cast<std::chrono::seconds>(
        _now - _t.miners.at(_index).solutions.tstamp);
    jshares.append(uint64_t(solution_lastupdated.count()));  // interval in seconds from last found
                                                             // share

    mininginfo["shares"] = jshares;
    mininginfo["paused"] = _miner->paused();
    mininginfo["pause_reason"] = _miner->paused() ? _miner->pausedString() : Json::Value::null;

    /* Nonce infos */
    auto segment_width = Farm::f().get_segment_width();
    uint64_t gpustartnonce = Farm::f().get_nonce_scrambler() + ((uint64_t)_index << segment_width);
    jsegment.append(toHex(uint64_t(gpustartnonce), HexPrefix::Add));
    jsegment.append(toHex(uint64_t(gpustartnonce + (1LL << segment_width)), HexPrefix::Add));
    mininginfo["segment"] = jsegment;

    /* Hash & Share infos */
    mininginfo["hashrate"] = toHex((uint32_t)_t.miners.at(_index).hashrate, HexPrefix::Add);

    jRes["hardware"] = hwinfo;
    jRes["mining"] = mininginfo;

    return jRes;
}

std::string ApiConnection::getHttpMinerStatDetail()
{
    Json::Value jStat = getMinerStatDetail();
    uint64_t durationSeconds = jStat["host"]["runtime"].asUInt64();
    int hours = (int)(durationSeconds / 3600);
    durationSeconds -= (hours * 3600);
    int minutes = (int)(durationSeconds / 60);
    int hoursSize = (hours > 9 ? (hours > 99 ? 3 : 2) : 1);

    /* Build up header*/
    std::stringstream _ret;
    _ret << "<!doctype html>"
         << "<html lang=en>"
         << "<head>"
         << "<meta charset=utf-8>"
         << "<meta http-equiv=\"refresh\" content=\"30\">"
         << "<title>" << jStat["host"]["name"].asString() << "</title>"
         << "<style>"
         << "body{font-family:-apple-system,BlinkMacSystemFont,\"Segoe UI\",Roboto,"
         << "\"Helvetica Neue\",Helvetica,Arial,sans-serif;font-size:16px;line-height:1.5;"
         << "text-align:center;}"
         << "table,td,th{border:1px inset #000;}"
         << "table{border-spacing:0;}"
         << "td,th{padding:3px;}"
         << "tbody tr:nth-child(even){background-color:" << HTTP_ROW0_COLOR << ";}"
         << "tbody tr:nth-child(odd){background-color:" << HTTP_ROW1_COLOR << ";}"
         << ".mx-auto{margin-left:auto;margin-right:auto;}"
         << ".bg-header1{background-color:" << HTTP_HDR1_COLOR << ";}"
         << ".bg-header0{background-color:" << HTTP_HDR0_COLOR << ";}"
         << ".bg-red{color:" << HTTP_ROWRED_COLOR << ";}"
         << ".right{text-align: right;}"
         << "</style>"
         << "<meta http-equiv=refresh content=30>"
         << "</head>"
         << "<body>"
         << "<table class=mx-auto>"
         << "<thead>"
         << "<tr class=bg-header1>"
         << "<th colspan=9>" << jStat["host"]["version"].asString() << " - " << setw(hoursSize)
         << hours << ":" << setw(2) << setfill('0') << fixed << minutes
         << "<br>Pool: " << redactUriUserInfo(jStat["connection"]["uri"].asString())
         << "</th>"
         << "</tr>"
         << "<tr class=bg-header0>"
         << "<th>PCI</th>"
         << "<th>Device</th>"
         << "<th>Mode</th>"
         << "<th>Paused</th>"
         << "<th class=right>Hash Rate</th>"
         << "<th class=right>Solutions</th>"
         << "<th class=right>Temp.</th>"
         << "<th class=right>Fan %</th>"
         << "<th class=right>Power</th>"
         << "</tr>"
         << "</thead><tbody>";

    /* Loop miners */
    double total_hashrate = 0;
    double total_power = 0;
    unsigned int total_solutions = 0;

    for (Json::Value::ArrayIndex i = 0; i != jStat["devices"].size(); i++)
    {
        Json::Value device = jStat["devices"][i];
        double hashrate = std::stoul(device["mining"]["hashrate"].asString(), nullptr, 16);
        double power = device["hardware"]["sensors"][2].asDouble();
        unsigned int solutions = device["mining"]["shares"][0].asUInt();
        total_hashrate += hashrate;
        total_power += power;
        total_solutions += solutions;

        _ret << "<tr" << (device["mining"]["paused"].asBool() ? " class=\"bg-red\"" : "")
             << ">";  // Open row

        _ret << "<td>" << device["hardware"]["pci"].asString() << "</td>";
        _ret << "<td>" << device["hardware"]["name"].asString() << "</td>";
        _ret << "<td>" << device["_mode"].asString() << "</td>";

        _ret << "<td>"
             << (device["mining"]["paused"].asBool() ? device["mining"]["pause_reason"].asString() :
                                                       "No")
             << "</td>";

        _ret << "<td class=right>" << dev::getFormattedHashes(hashrate) << "</td>";

        _ret << "<td class=right>" << device["mining"]["shares"][0].asString() << "</td>";
        _ret << "<td class=right>" << device["hardware"]["sensors"][0].asString() << "</td>";
        _ret << "<td class=right>" << device["hardware"]["sensors"][1].asString() << "</td>";
        _ret << "<td class=right>" << device["hardware"]["sensors"][2].asString() << "</td>";

        _ret << "</tr>";  // Close row
    }
    _ret << "</tbody>";

    /* Summarize */
    _ret << "<tfoot><tr class=bg-header0><td colspan=4 class=right>Total</td><td class=right>"
         << dev::getFormattedHashes(total_hashrate) << "</td><td class=right>" << total_solutions
         << "</td><td colspan=3 class=right>" << setprecision(2) << total_power << "</td></tfoot>";

    _ret << "</table></body></html>";
    return _ret.str();
}

/**
 * @brief Return a total and per GPU detailed list of current status
 * As we return here difficulty and share counts (which are not getting resetted if we
 * switch pool) the results may "lie".
 * Eg: Calculating runtime, (current) difficulty and submitted shares must not match the hashrate.
 * Inspired by Andrea Lanfranchi comment on issue 1232:
 *    https://github.com/gangnamtestnet/firominer/pull/1232#discussion_r193995891
 * @return The json result
 */
Json::Value ApiConnection::getMinerStatDetail()
{
    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    TelemetryType t = Farm::f().Telemetry();

    auto runningTime = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - t.start);

    // ostringstream version;
    Json::Value devices = Json::Value(Json::arrayValue);
    Json::Value jRes;

    /* Host Info */
    Json::Value hostinfo;
    hostinfo["version"] = firominer_get_buildinfo()->project_name_with_version;  // miner version.
    hostinfo["runtime"] = uint64_t(runningTime.count());  // running time, in seconds.

    {
        // Even the client should know which host was queried
        char hostName[HOST_NAME_MAX + 1];
        if (!gethostname(hostName, HOST_NAME_MAX + 1))
            hostinfo["name"] = hostName;
        else
            hostinfo["name"] = Json::Value::null;
    }


    /* Connection info */
    Json::Value connectioninfo;
    auto connection = PoolManager::p().getActiveConnection();
    connectioninfo["uri"] = connection ? Json::Value(connection->str()) : Json::Value::null;
    connectioninfo["connected"] = PoolManager::p().isConnected();
    connectioninfo["switches"] = PoolManager::p().getConnectionSwitches();

    /* Mining Info */
    Json::Value mininginfo;
    Json::Value sharesinfo = Json::Value(Json::arrayValue);

    mininginfo["hashrate"] = toHex(uint32_t(t.farm.hashrate), HexPrefix::Add);
    mininginfo["epoch"] = PoolManager::p().getCurrentEpoch();
    mininginfo["epoch_changes"] = PoolManager::p().getEpochChanges();
    mininginfo["difficulty"] = PoolManager::p().getCurrentDifficulty();

    sharesinfo.append(t.farm.solutions.accepted);
    sharesinfo.append(t.farm.solutions.rejected);
    sharesinfo.append(t.farm.solutions.failed);
    auto solution_lastupdated =
        std::chrono::duration_cast<std::chrono::seconds>(now - t.farm.solutions.tstamp);
    sharesinfo.append(uint64_t(solution_lastupdated.count()));  // interval in seconds from last
                                                                // found share
    mininginfo["shares"] = sharesinfo;

    /* Monitors Info */
    Json::Value monitorinfo;
    auto tstop = Farm::f().get_tstop();
    if (tstop)
    {
        Json::Value tempsinfo = Json::Value(Json::arrayValue);
        tempsinfo.append(Farm::f().get_tstart());
        tempsinfo.append(tstop);
        monitorinfo["temperatures"] = tempsinfo;
    }

    /* Devices related info */
    for (shared_ptr<Miner> miner : Farm::f().getMiners())
        devices.append(getMinerStatDetailPerMiner(t, miner));

    jRes["devices"] = devices;

    jRes["monitors"] = monitorinfo;
    jRes["connection"] = connectioninfo;
    jRes["host"] = hostinfo;
    jRes["mining"] = mininginfo;

    return jRes;
}
