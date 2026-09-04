#include "EthGetworkClient.h"
#include "../stratum/utilstrencodings.h"

#include <chrono>
#include <sstream>

#include <boost/beast/core/detail/base64.hpp>
#include <libcrypto/ethash.hpp>

using namespace std;
using namespace dev;
using namespace eth;

using boost::asio::ip::tcp;

namespace
{
constexpr auto kHttpTimeoutSeconds = 30;
constexpr uint64_t kMaxHttpBodySize = 16 * 1024 * 1024;
}

EthGetworkClient::EthGetworkClient(int worktimeout, unsigned farmRecheckPeriod, const std::string &rewardAddress)
  : PoolClient(),
    m_farmRecheckPeriod(farmRecheckPeriod),
    m_callbackState(std::make_shared<CallbackState>(this)),
    m_txQueue(64),
    m_io_strand(g_io_service),
    m_socket(g_io_service),
    m_resolver(g_io_service),
    m_endpoints(),
    m_getwork_timer(g_io_service),
    m_request_timer(g_io_service),
    m_worktimeout(worktimeout)
{
    m_jSwBuilder.settings_["indentation"] = "";

    Json::Value jGetWork;
    jGetWork["id"] = unsigned(1);
    jGetWork["jsonrpc"] = "2.0";
    jGetWork["method"] = "getblocktemplate";

    Json::Value params = Json::Value(Json::arrayValue);
    params.append(Json::Value(Json::objectValue));
    params.append(rewardAddress);
    jGetWork["params"] = params;

    m_jsonGetWork = std::string(Json::writeString(m_jSwBuilder, jGetWork));
}

EthGetworkClient::~EthGetworkClient()
{
    std::lock_guard<std::recursive_mutex> lock(m_callbackState->mutex);
    m_callbackState->client = nullptr;
    m_txQueue.consume_all([](std::string* request) { delete request; });
    // Do not stop io service.
    // It's global
}

void EthGetworkClient::connect()
{
    // Build authentication
    m_base64_auth.clear();
    if (m_conn->User().size() || m_conn->Pass().size())
    {
        std::string authentication{m_conn->User() + ":" + m_conn->Pass()};
        size_t encoded_len{boost::beast::detail::base64::encoded_size(authentication.length())};
        m_base64_auth.resize(encoded_len, '\0');
        boost::beast::detail::base64::encode(m_base64_auth.data(), authentication.data(), authentication.length());
    }

    // Prevent unnecessary and potentially dangerous recursion
    bool expected = false;
    if (!m_connecting.compare_exchange_strong(expected, true, memory_order::memory_order_relaxed))
        return;

    // Reset status flags
    m_getwork_timer.cancel();

    // Initialize a new queue of end points
    m_endpoints = std::queue<boost::asio::ip::basic_endpoint<boost::asio::ip::tcp>>();
    m_endpoint = boost::asio::ip::basic_endpoint<boost::asio::ip::tcp>();

    if (m_conn->HostNameType() == dev::UriHostNameType::Dns || m_conn->HostNameType() == dev::UriHostNameType::Basic)
    {
        // Begin resolve all ips associated to hostname
        // calling the resolver each time is useful as most
        // load balancers will give Ips in different order
        m_resolver = boost::asio::ip::tcp::resolver(g_io_service);
        boost::asio::ip::tcp::resolver::query q(m_conn->Host(), toString(m_conn->Port()));

        // Start resolving async
        auto const generation = m_operationGeneration.fetch_add(1, std::memory_order_relaxed) + 1;
        arm_request_timer();
        auto const callbackState = m_callbackState;
        m_resolver.async_resolve(q,
            m_io_strand.wrap([callbackState, generation](const boost::system::error_code& ec,
                                 tcp::resolver::iterator i) {
                std::lock_guard<std::recursive_mutex> lock(callbackState->mutex);
                auto* client = callbackState->client;
                if (client && generation == client->m_operationGeneration.load(std::memory_order_relaxed))
                    client->handle_resolve(ec, i);
            }));
    }
    else
    {
        // No need to use the resolver if host is already an IP address
        m_endpoints.push(
            boost::asio::ip::tcp::endpoint(boost::asio::ip::address::from_string(m_conn->Host()), m_conn->Port()));
        send(m_jsonGetWork);
    }
}

void EthGetworkClient::disconnect()
{
    std::lock_guard<std::recursive_mutex> callbackLock(m_callbackState->mutex);
    bool expected = false;
    if (!m_disconnecting.compare_exchange_strong(expected, true, std::memory_order_relaxed))
        return;

    m_operationGeneration.fetch_add(1, std::memory_order_relaxed);

    // Release session
    m_connected.store(false, memory_order_relaxed);
    if (m_conn && m_session)
        m_conn->addDuration(m_session->duration());
    m_session = nullptr;

    m_connecting.store(false, std::memory_order_relaxed);
    m_txPending.store(false, std::memory_order_relaxed);
    m_retryAfterCancel = false;
    m_getwork_timer.cancel();
    cancel_request_timer();
    m_resolver.cancel();
    boost::system::error_code ignored;
    m_socket.close(ignored);

    m_txQueue.consume_all([](std::string* l) { delete l; });
    m_pendingRequest.clear();

    if (m_onDisconnected)
        m_onDisconnected();
}

void EthGetworkClient::begin_connect()
{
    if (m_disconnecting.load(std::memory_order_relaxed))
        return;

    if (!m_endpoints.empty())
    {
        // Pick the first endpoint in list.
        // Eventually endpoints get discarded on connection errors
        m_endpoint = m_endpoints.front();
        boost::system::error_code ignored;
        m_socket.close(ignored);
        auto const generation = m_operationGeneration.fetch_add(1, std::memory_order_relaxed) + 1;
        arm_request_timer();
        auto const callbackState = m_callbackState;
        m_socketOperationPending = true;
        m_socket.async_connect(m_endpoint,
            m_io_strand.wrap([callbackState, generation](const boost::system::error_code& ec) {
                std::lock_guard<std::recursive_mutex> lock(callbackState->mutex);
                auto* client = callbackState->client;
                if (client)
                {
                    client->m_socketOperationPending = false;
                    if (generation == client->m_operationGeneration.load(std::memory_order_relaxed))
                        client->handle_connect(ec);
                    if (client->m_retryAfterCancel &&
                        !client->m_disconnecting.load(std::memory_order_relaxed))
                    {
                        client->m_retryAfterCancel = false;
                        client->begin_connect();
                    }
                }
            }));
    }
    else
    {
        cwarn << "No more IP addresses to try for host: " << m_conn->Host();
        disconnect();
    }
}

void EthGetworkClient::handle_connect(const boost::system::error_code& ec)
{
    if (!ec && m_socket.is_open())
    {
        // If in "connecting" phase raise the proper event
        if (m_connecting.load(std::memory_order_relaxed))
        {
            // Initialize new session
            m_connected.store(true, memory_order_relaxed);
            m_session = unique_ptr<Session>(new Session);
            m_session->subscribed.store(true, memory_order_relaxed);
            m_session->authorized.store(true, memory_order_relaxed);

            m_connecting.store(false, std::memory_order_relaxed);

            if (m_onConnected)
                m_onConnected();
            m_current_tstamp = std::chrono::steady_clock::now();
        }

        // Retrieve 1st line waiting in the queue and submit
        // if other lines waiting they will be processed
        // at the end of the processed request
        if (m_pendingRequest.empty())
        {
            std::string* line;
            while (m_txQueue.pop(line))
            {
                if (line->size())
                {
                    m_pendingRequest = std::move(*line);
                    delete line;
                    break;
                }
                delete line;
            }
        }

        if (m_pendingRequest.empty())
        {
            m_txPending.store(false, std::memory_order_relaxed);
            if (!m_txQueue.empty())
            {
                bool expected = false;
                if (m_txPending.compare_exchange_strong(expected, true, std::memory_order_relaxed))
                    begin_connect();
            }
            return;
        }

        Json::Reader jRdr;
        jRdr.parse(m_pendingRequest, m_pendingJReq);
        m_pending_tstamp = std::chrono::steady_clock::now();

        auto const attempt = std::make_shared<HttpAttempt>();
        std::ostringstream os;
        string _path = (m_conn->Path().empty() ? "/" : m_conn->Path());
        os << "POST " << _path << " HTTP/1.0\r\n";
        os << "Host: " << m_conn->Host() << "\r\n";
        os << "Content-Type: application/json\r\n";
        os << "Content-Length: " << m_pendingRequest.length() << "\r\n";
        if (m_base64_auth.size())
            os << "Authorization: Basic " << m_base64_auth << "\r\n";
        os << "Connection: close\r\n\r\n";
        os << m_pendingRequest;
        attempt->request = os.str();

        if (g_logOptions & LOG_JSON)
            cnote << " >> " << m_pendingRequest;

        arm_request_timer();
        auto const generation = m_operationGeneration.load(std::memory_order_relaxed);
        auto const callbackState = m_callbackState;
        m_socketOperationPending = true;
        async_write(m_socket, boost::asio::buffer(attempt->request),
            m_io_strand.wrap([callbackState, generation, attempt](const boost::system::error_code& writeEc,
                                 std::size_t) {
                std::lock_guard<std::recursive_mutex> lock(callbackState->mutex);
                auto* client = callbackState->client;
                if (client)
                {
                    client->m_socketOperationPending = false;
                    if (generation == client->m_operationGeneration.load(std::memory_order_relaxed))
                        client->handle_write(writeEc, attempt);
                    if (client->m_retryAfterCancel &&
                        !client->m_disconnecting.load(std::memory_order_relaxed))
                    {
                        client->m_retryAfterCancel = false;
                        client->begin_connect();
                    }
                }
            }));
    }
    else
    {
        if (ec != boost::asio::error::operation_aborted)
        {
            // This endpoint does not respond
            // Pop it and retry
            cwarn << "Error connecting to " << m_conn->Host() << ":" << toString(m_conn->Port()) << " : "
                  << ec.message();
            retry_endpoint();
        }
    }
}

void EthGetworkClient::handle_write(
    const boost::system::error_code& ec, const std::shared_ptr<HttpAttempt>& attempt)
{
    if (ec == boost::asio::error::operation_aborted)
        return;

    if (!ec)
    {
        // Transmission succesfully sent.
        // Read the response async.
        attempt->parser.body_limit(kMaxHttpBodySize);
        arm_request_timer();
        auto const generation = m_operationGeneration.load(std::memory_order_relaxed);
        auto const callbackState = m_callbackState;
        m_socketOperationPending = true;
        boost::beast::http::async_read(m_socket, attempt->response, attempt->parser,
            m_io_strand.wrap([callbackState, generation, attempt](const boost::system::error_code& readEc,
                                 std::size_t bytesTransferred) {
                std::lock_guard<std::recursive_mutex> lock(callbackState->mutex);
                auto* client = callbackState->client;
                if (client)
                {
                    client->m_socketOperationPending = false;
                    if (generation == client->m_operationGeneration.load(std::memory_order_relaxed))
                        client->handle_read(readEc, bytesTransferred, attempt);
                    if (client->m_retryAfterCancel &&
                        !client->m_disconnecting.load(std::memory_order_relaxed))
                    {
                        client->m_retryAfterCancel = false;
                        client->begin_connect();
                    }
                }
            }));
    }
    else
    {
        cwarn << "Error writing to " << m_conn->Host() << ":" << toString(m_conn->Port()) << " : " << ec.message();
        retry_endpoint();
    }
}

void EthGetworkClient::handle_read(const boost::system::error_code& ec, std::size_t bytes_transferred,
    const std::shared_ptr<HttpAttempt>& attempt)
{
    if (ec == boost::asio::error::operation_aborted)
        return;

    cancel_request_timer();
    if (!ec)
    {
        // Close socket
        if (m_socket.is_open())
            m_socket.close();

        auto response = attempt->parser.release();
        auto const http_status_code = response.result_int();
        auto rx_message = std::move(response.body());
        bool const httpSuccess = http_status_code >= 200 && http_status_code < 300;

        // Firo returns ordinary JSON-RPC errors as HTTP 500. Other non-2xx
        // responses are transport failures and must not be treated as work.
        if (!httpSuccess && http_status_code != 500)
        {
            cwarn << m_conn->Host() << ":" << toString(m_conn->Port()) << " returned HTTP status "
                  << http_status_code;
            retry_endpoint();
            return;
        }

        if (rx_message.empty())
        {
            cwarn << m_conn->Host() << ":" << toString(m_conn->Port()) << " returned an empty response (status "
                  << http_status_code << ")";
            if (httpSuccess)
                disconnect();
            else
                retry_endpoint();
            return;
        }

        if (g_logOptions & LOG_JSON)
            cnote << " << " << rx_message;

        try
        {
            Json::Value jRes;
            Json::Reader jRdr;
            if (!jRdr.parse(rx_message, jRes))
            {
                string what = jRdr.getFormattedErrorMessages();
                boost::replace_all(what, "\n", " ");
                cwarn << "Got invalid Json message : " << what;
                if (httpSuccess)
                    disconnect();
                else
                    retry_endpoint();
                return;
            }

            if (!httpSuccess &&
                (!jRes.isObject() || !jRes.isMember("error") || jRes["error"].isNull()))
            {
                cwarn << m_conn->Host() << ":" << toString(m_conn->Port())
                      << " returned a non-error JSON body with HTTP status " << http_status_code;
                retry_endpoint();
                return;
            }

            m_pendingRequest.clear();

            processResponse(jRes);
        }
        catch (std::exception const& ex)
        {
            cwarn << "Invalid response from " << m_conn->Host() << ":" << toString(m_conn->Port())
                  << ": " << ex.what();
            disconnect();
            return;
        }

        // Release ownership before checking again so a concurrent sender
        // either schedules the next request or leaves it for this handler.
        m_txPending.store(false, std::memory_order_relaxed);
        if (!m_txQueue.empty())
        {
            bool expected = false;
            if (m_txPending.compare_exchange_strong(expected, true, std::memory_order_relaxed))
                begin_connect();
        }
    }
    else
    {
        cwarn << "Error reading from :" << m_conn->Host() << ":" << toString(m_conn->Port()) << " : "
              << ec.message() << " Bytes transferred " << bytes_transferred;
        retry_endpoint();
    }
}

void EthGetworkClient::arm_request_timer()
{
    auto const timerGeneration = m_requestTimerGeneration.fetch_add(1, std::memory_order_relaxed) + 1;
    m_request_timer.expires_from_now(boost::posix_time::seconds(kHttpTimeoutSeconds));
    auto const generation = m_operationGeneration.load(std::memory_order_relaxed);
    auto const callbackState = m_callbackState;
    m_request_timer.async_wait(m_io_strand.wrap(
        [callbackState, generation, timerGeneration](const boost::system::error_code& ec) {
            std::lock_guard<std::recursive_mutex> lock(callbackState->mutex);
            auto* client = callbackState->client;
            if (client && generation == client->m_operationGeneration.load(std::memory_order_relaxed) &&
                timerGeneration == client->m_requestTimerGeneration.load(std::memory_order_relaxed))
                client->request_timer_elapsed(ec);
        }));
}

void EthGetworkClient::cancel_request_timer()
{
    m_requestTimerGeneration.fetch_add(1, std::memory_order_relaxed);
    m_request_timer.cancel();
}

void EthGetworkClient::request_timer_elapsed(const boost::system::error_code& ec)
{
    if (!ec)
    {
        cwarn << "HTTP request to " << m_conn->Host() << ":" << toString(m_conn->Port()) << " timed out.";
        retry_endpoint();
    }
}

void EthGetworkClient::retry_endpoint()
{
    if (m_disconnecting.load(std::memory_order_relaxed))
        return;

    cancel_request_timer();
    boost::system::error_code ignored;
    m_socket.close(ignored);
    if (!m_endpoints.empty())
        m_endpoints.pop();

    if (m_socketOperationPending)
    {
        m_operationGeneration.fetch_add(1, std::memory_order_relaxed);
        m_retryAfterCancel = true;
        return;
    }
    begin_connect();
}

void EthGetworkClient::handle_resolve(const boost::system::error_code& ec, tcp::resolver::iterator i)
{
    if (ec == boost::asio::error::operation_aborted)
        return;

    if (!ec)
    {
        cancel_request_timer();
        while (i != tcp::resolver::iterator())
        {
            m_endpoints.push(i->endpoint());
            i++;
        }
        m_resolver.cancel();

        // Resolver has finished so invoke connection asynchronously
        send(m_jsonGetWork);
    }
    else
    {
        cwarn << "Could not resolve host " << m_conn->Host() << ", " << ec.message();
        disconnect();
    }
}

void EthGetworkClient::processResponse(Json::Value& JRes)
{
    unsigned _id = 0;         // This SHOULD be the same id as the request it is responding to
    bool _isSuccess = false;  // Whether or not this is a succesful or failed response
    string _errReason = "";   // Content of the error reason

    if (!JRes.isMember("id"))
    {
        throw std::invalid_argument("missing JSON-RPC id");
    }
    // We get the id from pending jrequest
    // It's not guaranteed we get response labelled with same id
    // For instance Dwarfpool always responds with "id":0
    _id = m_pendingJReq.get("id", unsigned(0)).asUInt();
    _isSuccess = !JRes.isMember("error") || JRes["error"].isNull();
    _errReason = (_isSuccess ? "" : processError(JRes));

    // We have only theese possible ids
    // 0 or 1 as job notification
    // 9 as response for eth_submitHashrate
    // 40+ for responses to mining submissions
    if (_id == 0 || _id == 1)
    {
        // Getwork might respond with an error to
        // a request. (eg. node is still syncing)
        // In such case delay further requests
        // by 30 seconds.
        // Otherwise resubmit another getwork request
        // with a delay of m_farmRecheckPeriod ms.
        if (!_isSuccess)
        {
            cwarn << "Got " << _errReason << " from " << m_conn->Host() << ":" << toString(m_conn->Port());
            m_getwork_timer.expires_from_now(boost::posix_time::seconds(30));
            auto const callbackState = m_callbackState;
            m_getwork_timer.async_wait(m_io_strand.wrap([callbackState](const boost::system::error_code& ec) {
                std::lock_guard<std::recursive_mutex> lock(callbackState->mutex);
                if (callbackState->client)
                    callbackState->client->getwork_timer_elapsed(ec);
            }));
        }
        else
        {
            if (!JRes.isMember("result") || !JRes["result"].isObject())
            {
                cwarn << "Missing result data for getblocktemplate request from " << m_conn->Host() << ":"
                      << toString(m_conn->Port());
            }
            else
            {
                Json::Value JPrm = JRes.get("result", Json::Value::null);

                // Sanity checks
                if (!JPrm.isMember("pprpcheader") || !JPrm.isMember("pprpcepoch") || !JPrm.isMember("height") ||
                    !JPrm.isMember("bits") || !JPrm.isMember("target"))
                {
                    cwarn << "Invalid/incomplete work package info from " << m_conn->Host() << ":"
                          << toString(m_conn->Port());
                }
                else
                {
                    auto const& headerValue = JPrm["pprpcheader"];
                    auto const& epochValue = JPrm["pprpcepoch"];
                    auto const& heightValue = JPrm["height"];
                    auto const& bitsValue = JPrm["bits"];
                    auto const& targetValue = JPrm["target"];

                    auto const header = headerValue.isString() ? headerValue.asString() : string{};
                    auto const target = targetValue.isString() ? targetValue.asString() : string{};
                    auto const bitsText = bitsValue.isString() ? bitsValue.asString() : string{};
                    uint32_t advertisedEpoch = 0;
                    uint32_t block = 0;
                    uint32_t bits = 0;

                    bool const validHeader =
                        (header.size() == 64 || (header.size() == 66 && header.compare(0, 2, "0x") == 0)) &&
                        IsHexNumber(header);
                    bool const validTarget =
                        (target.size() == 64 || (target.size() == 66 && target.compare(0, 2, "0x") == 0)) &&
                        IsHexNumber(target);
                    bool const validBits =
                        (bitsText.size() <= 8 ||
                            (bitsText.size() <= 10 && bitsText.compare(0, 2, "0x") == 0)) &&
                        IsHexNumber(bitsText) && ParseUInt32(bitsText, &bits, 16);
                    bool const validScalars = (epochValue.isString() || epochValue.isIntegral()) &&
                                              ParseUInt32(epochValue.asString(), &advertisedEpoch, 10) &&
                                              (heightValue.isString() || heightValue.isIntegral()) &&
                                              ParseUInt32(heightValue.asString(), &block, 10) && bitsValue.isString() &&
                                              validBits;

                    bool negative = false;
                    bool overflow = false;
                    auto const blockTarget = ethash::from_compact(bits, &negative, &overflow);
                    auto const parsedHeader = validHeader ? h256(header) : h256{};
                    auto const parsedTarget = validTarget ? h256(target) : h256{};
                    auto const compactTarget = h256(blockTarget.bytes, dev::h256::ConstructFromPointer);
                    if (!parsedHeader || !parsedTarget || !validScalars || negative || overflow || !compactTarget ||
                        parsedTarget < compactTarget)
                    {
                        cwarn << "Invalid work package data from " << m_conn->Host() << ":"
                              << toString(m_conn->Port());
                    }
                    else
                    {
                        WorkPackage newWp;
                        newWp.header = parsedHeader;
                        newWp.epoch = advertisedEpoch;
                        newWp.block_boundary = compactTarget;
                        newWp.boundary = parsedTarget;
                        newWp.block = block;
                        newWp.job = newWp.header.hex();

                        if (m_current.header != newWp.header)
                        {
                            m_current = newWp;
                            m_current_tstamp = std::chrono::steady_clock::now();

                            if (m_onWorkReceived)
                                m_onWorkReceived(m_current);
                        }
                    }
                }

            }

            m_getwork_timer.expires_from_now(boost::posix_time::milliseconds(m_farmRecheckPeriod));
            auto const callbackState = m_callbackState;
            m_getwork_timer.async_wait(m_io_strand.wrap([callbackState](const boost::system::error_code& ec) {
                std::lock_guard<std::recursive_mutex> lock(callbackState->mutex);
                if (callbackState->client)
                    callbackState->client->getwork_timer_elapsed(ec);
            }));
        }
    }
    else if (_id == 9)
    {
        // Response to hashrate submission
        // Actually don't do anything
    }
    else if (_id >= 40 && _id <= m_solution_submitted_max_id)
    {
        if (_isSuccess)
        {
            // Firo's pprpcsb currently returns "duplicate" on its normal
            // processed-block path. Every other BIP22 status is a rejection.
            if (!JRes.isMember("result"))
                _isSuccess = false;
            else
            {
                auto const& result = JRes["result"];
                _isSuccess = result.isNull() || (result.isBool() && result.asBool()) ||
                             (result.isString() && result.asString() == "duplicate");
                if (!_isSuccess && result.isString())
                    _errReason = result.asString();
            }
        }

        std::chrono::milliseconds _delay =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - m_pending_tstamp);

        const unsigned miner_index = _id - 40;
        if (_isSuccess)
        {
            if (m_onSolutionAccepted)
                m_onSolutionAccepted(_delay, miner_index, false);
        }
        else
        {
            cwarn << "Getwork submission rejected: " << (_errReason.empty() ? "invalid result" : _errReason);
            if (m_onSolutionRejected)
                m_onSolutionRejected(_delay, miner_index);
        }
    }
}

std::string EthGetworkClient::processError(Json::Value& JRes)
{
    std::string retVar;

    if (JRes.isMember("error") && !JRes.get("error", Json::Value::null).isNull())
    {
        if (JRes["error"].isConvertibleTo(Json::ValueType::stringValue))
        {
            retVar = JRes.get("error", "Unknown error").asString();
        }
        else if (JRes["error"].isConvertibleTo(Json::ValueType::arrayValue))
        {
            for (auto i : JRes["error"])
            {
                retVar += i.asString() + " ";
            }
        }
        else if (JRes["error"].isConvertibleTo(Json::ValueType::objectValue))
        {
            for (Json::Value::iterator i = JRes["error"].begin(); i != JRes["error"].end(); ++i)
            {
                Json::Value k = i.key();
                Json::Value v = (*i);
                retVar += (std::string)i.name() + ":" + v.asString() + " ";
            }
        }
    }
    else
    {
        retVar = "Unknown error";
    }

    return retVar;
}

void EthGetworkClient::send(Json::Value const& jReq)
{
    send(std::string(Json::writeString(m_jSwBuilder, jReq)));
}

void EthGetworkClient::send(std::string const& sReq)
{
    if (m_disconnecting.load(std::memory_order_relaxed))
        return;

    std::string* line = new std::string(sReq);
    if (!m_txQueue.push(line))
    {
        delete line;
        cwarn << "Getwork request queue is full";
        return;
    }

    bool ex = false;
    if (m_txPending.compare_exchange_strong(ex, true, std::memory_order_relaxed))
    {
        auto const callbackState = m_callbackState;
        g_io_service.post(m_io_strand.wrap([callbackState]() {
            std::lock_guard<std::recursive_mutex> lock(callbackState->mutex);
            if (callbackState->client)
                callbackState->client->begin_connect();
        }));
    }
}

void EthGetworkClient::submitHashrate(uint64_t const& rate, string const& id)
{
    // Just return as the node does not support it
    (void)rate;
    (void)id;
    return;

    //// No need to check for authorization
    // if (m_session)
    //{
    //    Json::Value jReq;
    //    jReq["id"] = unsigned(9);
    //    jReq["jsonrpc"] = "2.0";
    //    jReq["method"] = "eth_submitHashrate";
    //    jReq["params"] = Json::Value(Json::arrayValue);
    //    jReq["params"].append(toHex(rate, HexPrefix::Add));  // Already expressed as hex
    //    jReq["params"].append(id);                           // Already prefixed by 0x
    //    send(jReq);
    //}
}

void EthGetworkClient::submitSolution(const Solution& solution)
{
    if (m_session)
    {
        Json::Value jReq;
        string nonceHex = toHex(solution.nonce, dev::HexPrefix::Add);

        unsigned id = 40 + solution.midx;
        jReq["id"] = id;
        jReq["jsonrpc"] = "2.0";
        m_solution_submitted_max_id = max(m_solution_submitted_max_id, id);
        jReq["method"] = "pprpcsb";
        jReq["params"] = Json::Value(Json::arrayValue);
        jReq["params"].append(solution.work.header.hex());  // Don't prepend 0x (firo has a dictionary of hashes)
        jReq["params"].append(solution.mixHash.hex());
        jReq["params"].append(nonceHex);
        send(jReq);
    }
}

void EthGetworkClient::getwork_timer_elapsed(const boost::system::error_code& ec)
{
    // Triggers the resubmission of a getWork request
    if (!ec && !m_disconnecting.load(std::memory_order_relaxed))
    {
        // Check if last work is older than timeout
        std::chrono::seconds _delay =
            std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - m_current_tstamp);
        if (_delay.count() > m_worktimeout)
        {
            cwarn << "No new work received in " << m_worktimeout << " seconds.";
            if (!m_endpoints.empty())
                m_endpoints.pop();
            disconnect();
        }
        else
        {
            send(m_jsonGetWork);
        }
    }
}
