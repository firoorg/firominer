#pragma once

#include <iostream>
#include <memory>
#include <mutex>
#include <string>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/asio.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/http.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/lockfree/queue.hpp>

#include <json/json.h>

#include "../PoolClient.h"

using namespace std;
using namespace dev;
using namespace eth;

class EthGetworkClient : public PoolClient
{
public:
    EthGetworkClient(int worktimeout, unsigned farmRecheckPeriod, const std::string &rewardAddress);
    ~EthGetworkClient();

    void connect() override;
    void disconnect() override;

    void submitHashrate(uint64_t const& rate, string const& id) override;
    void submitSolution(const Solution& solution) override;

private:
    // Async callbacks retain this state and lock it before touching the client.
    struct CallbackState
    {
        explicit CallbackState(EthGetworkClient* client) : client(client) {}

        std::recursive_mutex mutex;
        EthGetworkClient* client;
    };

    // Beast and Asio require operation storage to live through completion.
    struct HttpAttempt
    {
        std::string request;
        boost::beast::flat_buffer response;
        boost::beast::http::response_parser<boost::beast::http::string_body> parser;
    };

    unsigned m_farmRecheckPeriod = 500;  // In milliseconds

    void begin_connect();
    void handle_resolve(const boost::system::error_code& ec, boost::asio::ip::tcp::resolver::iterator i);
    void handle_connect(const boost::system::error_code& ec);
    void handle_write(const boost::system::error_code& ec, const std::shared_ptr<HttpAttempt>& attempt);
    void handle_read(const boost::system::error_code& ec, std::size_t bytes_transferred,
        const std::shared_ptr<HttpAttempt>& attempt);
    void arm_request_timer();
    void cancel_request_timer();
    void request_timer_elapsed(const boost::system::error_code& ec);
    void retry_endpoint();
    std::string processError(Json::Value& JRes);
    void processResponse(Json::Value& JRes);
    void send(Json::Value const& jReq);
    void send(std::string const& sReq);
    void getwork_timer_elapsed(const boost::system::error_code& ec);

    WorkPackage m_current;

    std::atomic<bool> m_connecting = {false};  // Whether or not socket is on first try connect
    std::atomic<bool> m_disconnecting = {false};
    std::atomic<bool> m_txPending = {false};   // Whether or not an async socket operation is pending
    std::shared_ptr<CallbackState> m_callbackState;
    std::atomic<uint64_t> m_operationGeneration = {0};
    std::atomic<uint64_t> m_requestTimerGeneration = {0};
    bool m_socketOperationPending = false;
    bool m_retryAfterCancel = false;
    boost::lockfree::queue<std::string*> m_txQueue;

    boost::asio::io_service::strand m_io_strand;

    boost::asio::ip::tcp::socket m_socket;
    boost::asio::ip::tcp::resolver m_resolver;
    std::queue<boost::asio::ip::basic_endpoint<boost::asio::ip::tcp>> m_endpoints;

    Json::StreamWriterBuilder m_jSwBuilder;
    std::string m_jsonGetWork;
    std::string m_pendingRequest;
    Json::Value m_pendingJReq;
    std::chrono::time_point<std::chrono::steady_clock> m_pending_tstamp;

    boost::asio::deadline_timer m_getwork_timer;  // The timer which triggers getWork requests
    boost::asio::deadline_timer m_request_timer;

    // seconds to trigger a work_timeout (overwritten in constructor)
    int m_worktimeout;
    std::chrono::time_point<std::chrono::steady_clock> m_current_tstamp;

    unsigned m_solution_submitted_max_id = 0;  // maximum json id we used to send a solution

    std::string m_base64_auth{};  // Used by firo for http authentication;
};
