#pragma once

#include <deque>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>

#include <boost/array.hpp>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/bind.hpp>
#include <boost/lockfree/queue.hpp>

#include <json/json.h>

#include <libdevcore/FixedHash.h>
#include <libdevcore/Log.h>
#include <libethcore/Farm.h>
#include <libethcore/Miner.h>

#include "../PoolClient.h"

using namespace std;
using namespace dev;
using namespace dev::eth;

template <typename Verifier>
class verbose_verification
{
public:
    verbose_verification(Verifier verifier) : verifier_(verifier) {}

    bool operator()(bool preverified, boost::asio::ssl::verify_context& ctx)
    {
        char subject_name[256];
        X509* cert = X509_STORE_CTX_get_current_cert(ctx.native_handle());
        X509_NAME_oneline(X509_get_subject_name(cert), subject_name, 256);
        bool verified = verifier_(preverified, ctx);
#ifdef DEV_BUILD
        cnote << "Certificate: " << subject_name << " " << (verified ? "Ok" : "Failed");
#else
        if (!verified)
            cnote << "Certificate: " << subject_name << " "
                  << "Failed";
#endif
        return verified;
    }

private:
    Verifier verifier_;
};

class EthStratumClient : public PoolClient
{
public:
    enum StratumProtocol
    {
        STRATUM = 0,
        ETHPROXY,
        ETHEREUMSTRATUM,
        ETHEREUMSTRATUM2
    };

    EthStratumClient(int worktimeout, int responsetimeout);
    ~EthStratumClient() noexcept override;

    void init_socket();
    void connect() override;
    void disconnect() override;

    // Connected and Connection Statuses
    bool isConnected() override
    {
        bool _ret = PoolClient::isConnected();
        return _ret && !isPendingState();
    }
    bool isPendingState() override
    {
        return (m_connecting.load(std::memory_order_relaxed) ||
                m_disconnecting.load(std::memory_order_relaxed));
    }

    void submitHashrate(uint64_t const& rate, string const& id) override;
    void submitSolution(const Solution& solution) override;

    h256 currentHeaderHash() { return m_current.header; }
    bool current() { return static_cast<bool>(m_current); }

private:
    struct SocketState
    {
        std::shared_ptr<boost::asio::ssl::stream<boost::asio::ip::tcp::socket>> secure;
        std::shared_ptr<boost::asio::ip::tcp::socket> nonsecure;
        boost::asio::streambuf sendBuffer;
        boost::asio::streambuf recvBuffer;
    };

    struct CallbackState
    {
        explicit CallbackState(EthStratumClient* client) : client(client) {}

        std::recursive_mutex mutex;
        EthStratumClient* client;
    };

    template <typename Handler>
    auto guarded(Handler handler)
    {
        auto const state = m_callbackState;
        return [state, handler = std::move(handler)](auto&&... args) mutable {
            std::lock_guard<std::recursive_mutex> lock(state->mutex);
            if (state->client)
                std::invoke(handler, state->client, std::forward<decltype(args)>(args)...);
        };
    }

    template <typename Handler>
    auto guardedSocket(std::shared_ptr<SocketState> socketState, Handler handler)
    {
        auto const callbackState = m_callbackState;
        return [callbackState, socketState, handler = std::move(handler)](
                   auto&&... args) mutable {
            std::lock_guard<std::recursive_mutex> lock(callbackState->mutex);
            auto* client = callbackState->client;
            if (client && client->m_socketState == socketState)
                std::invoke(handler, client, std::forward<decltype(args)>(args)...);
        };
    }

    void startSession();
    void disconnect_finalize();
    void enqueue_response_plea(unsigned id = 0);
    std::chrono::milliseconds dequeue_response_plea(unsigned id);
    bool oldest_response_plea(std::chrono::steady_clock::time_point& oldest);
    void clear_response_pleas();
    void resolve_handler(
        const boost::system::error_code& ec, boost::asio::ip::tcp::resolver::iterator i);
    void start_connect();
    void connect_handler(const boost::system::error_code& ec);
    void handshake_handler(const boost::system::error_code& ec);
    void workloop_timer_elapsed(const boost::system::error_code& ec);

    void processResponse(Json::Value& responseObject);
    std::string processError(Json::Value& erroresponseObject);
    bool processExtranonce(
        std::string const& enonce, unsigned maxBytes = 4, bool allowEmpty = false);

    void recvSocketData();
    void onRecvSocketDataCompleted(
        const boost::system::error_code& ec, std::size_t bytes_transferred);
    void send(Json::Value const& jReq);
    void sendSocketData();
    void onSendSocketDataCompleted(const boost::system::error_code& ec);
    void onSSLShutdownCompleted(const boost::system::error_code& ec);

    std::atomic<bool> m_disconnecting = {false};
    std::atomic<bool> m_connecting = {false};
    std::atomic<bool> m_authpending = {false};
    std::shared_ptr<CallbackState> m_callbackState;

    // seconds to trigger a work_timeout (overwritten in constructor)
    int m_worktimeout;

    // seconds timeout for responses and connection (overwritten in constructor)
    int m_responsetimeout;

    // default interval for workloop timer (milliseconds)
    int m_workloop_interval = 1000;

    WorkPackage m_current;
    std::chrono::time_point<std::chrono::steady_clock> m_current_timestamp;

    boost::asio::io_service& m_io_service;  // The IO service reference passed in the constructor
    boost::asio::io_service::strand m_io_strand;
    std::shared_ptr<SocketState> m_socketState;
    boost::asio::ip::tcp::socket* m_socket;
    std::string m_message;  // The internal message string buffer
    bool m_newjobprocessed = false;

    Json::StreamWriterBuilder m_jSwBuilder;

    boost::asio::deadline_timer m_workloop_timer;

    std::mutex m_response_pleas_mutex;
    std::unordered_map<unsigned, std::deque<std::chrono::steady_clock::time_point>>
        m_response_plea_times;

    std::atomic<bool> m_txPending = {false};
    boost::lockfree::queue<std::string*> m_txQueue;

    boost::asio::ip::tcp::resolver m_resolver;
    std::queue<boost::asio::ip::basic_endpoint<boost::asio::ip::tcp>> m_endpoints;

    unsigned m_solution_submitted_max_id = 0;  // maximum json id we used to send a solution

    ///@brief Auxiliary function to make verbose_verification objects.
    template <typename Verifier>
    verbose_verification<Verifier> make_verbose_verification(Verifier verifier)
    {
        return verbose_verification<Verifier>(verifier);
    }
};
