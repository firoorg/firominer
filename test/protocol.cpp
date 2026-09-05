#include <atomic>
#include <chrono>
#include <cstdint>
#include <future>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <thread>

#include <boost/asio.hpp>

#include <libcrypto/hash_types.hpp>
#include <libpoolprotocols/PoolManager.h>
#include <libpoolprotocols/PoolURI.h>
#include <libpoolprotocols/stratum/EthStratumClient.h>
#include <libpoolprotocols/stratum/utilstrencodings.h>

boost::asio::io_service g_io_service;
bool g_exitOnError = false;

// Exercise the production JSON parsers directly without sockets or public test APIs.
struct ProtocolTest
{
    static Json::Value json(std::string const& text)
    {
        Json::Value value;
        Json::Reader reader;
        if (!reader.parse(text, value))
            throw std::runtime_error("Invalid protocol fixture");
        return value;
    }

    static void require(bool condition, char const* message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    static void rejectHandshake(unsigned mode, Json::Value reply)
    {
        EthStratumClient client(60, 1);
        auto uri = std::make_shared<dev::URI>("stratum://127.0.0.1:1");
        client.setConnection(uri);
        uri->SetStratumMode(mode, true);
        reply["error"]["code"] = 20;
        reply["error"]["message"] = "subscription denied";
        client.processResponse(reply);
        require(!client.m_session && uri->IsUnrecoverable(),
            "Stratum accepted a subscription result accompanied by an RPC error");
    }

    static void networkMismatch()
    {
        dev::eth::PoolSettings settings;
        settings.coinbaseMessage = "solo tag";
        settings.connections.push_back(std::make_shared<dev::URI>("stratum://127.0.0.1:1"));
        bool poolRejected = false;
        try { dev::eth::PoolManager unsupported(settings); }
        catch (std::invalid_argument const&) { poolRejected = true; }
        require(poolRejected, "pool manager accepted coinbase messages with Stratum");
        settings.connections.clear();
        dev::eth::PoolManager manager(settings);
        poolRejected = false;
        try { manager.addConnection("stratum://127.0.0.1:1"); }
        catch (std::invalid_argument const&) { poolRejected = true; }
        require(poolRejected, "API-added Stratum connection ignored configured coinbase message");
        auto uri = std::make_shared<dev::URI>("getwork://127.0.0.1:8888");
        auto client = std::make_unique<EthGetworkClient>(60, 1000, "reward");
        client->setConnection(uri);
        client->m_pendingJReq["id"] = 1u;
        auto* getwork = client.get();
        manager.p_client = std::move(client);
        manager.setClientHandlers();
        // Suppress failover so this test isolates the consensus-network check.
        manager.m_stopping.store(true);
        auto response = json(R"({"id":1,"error":null,"result":{
            "pprpcheader":"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
            "pprpcepoch":1,"height":2600,"bits":"1d00ffff",
            "target":"00000000ffff0000000000000000000000000000000000000000000000000000"}})");
        getwork->processResponse(response);
        require(uri->IsUnrecoverable() && !manager.m_currentWp && !getwork->getConnection(),
            "getwork daemon/network mismatch was silently mined with a different epoch");
    }

    static void run()
    {
        auto response = json(R"({"id":1,"error":null,"result":{
            "pprpcheader":"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
            "pprpcepoch":1,"height":1300,"bits":"1d00ffff",
            "target":"00000000ffff0000000000000000000000000000000000000000000000000000"}})");
        const auto header = response["result"]["pprpcheader"].asString();
        const auto target = response["result"]["target"].asString();
        const auto seed = std::string(64, '0');
        {
            EthGetworkClient plain(60, 1000, "reward");
            require(!json(plain.m_jsonGetWork)["params"][0].isMember("coinbase_message"),
                "empty coinbase message changed stock daemon requests");
            const std::string message = "Mined by \"Firo\" / caf\xc3\xa9";
            EthGetworkClient tagged(60, 1000, "reward", message);
            auto request = json(tagged.m_jsonGetWork);
            require(request["params"][0]["coinbase_message"].asString() == message &&
                        request["params"][1].asString() == "reward",
                "coinbase message or reward did not survive JSON encoding");
            EthGetworkClient maximum(60, 1000, "reward", std::string(80, 'x'));
            std::string unicode;
            for (unsigned i = 0; i < 40; ++i)
                unicode += "\xc3\xa9";
            EthGetworkClient unicodeMaximum(60, 1000, "reward", unicode);
            bool oversizedRejected = false;
            try { EthGetworkClient oversized(60, 1000, "reward", unicode + "\xc3\xa9"); }
            catch (std::invalid_argument const&) { oversizedRejected = true; }
            require(oversizedRejected, "oversized coinbase message was accepted");

            for (auto const& acknowledgement : {Json::Value(), Json::Value("wrong"), Json::Value(message)})
            {
                EthGetworkClient client(60, 1000, "reward", message);
                auto uri = std::make_shared<dev::URI>("getwork://127.0.0.1:8888");
                client.setConnection(uri);
                client.m_pendingJReq["id"] = 1u;
                unsigned jobs = 0;
                client.onWorkReceived([&](dev::eth::WorkPackage&) { ++jobs; });
                auto reply = response;
                reply["result"]["coinbase_message"] = acknowledgement;
                client.processResponse(reply);
                const bool confirmed = acknowledgement == Json::Value(message);
                require(jobs == unsigned(confirmed) && uri->IsUnrecoverable() != confirmed,
                    "tagged work did not require an exact daemon acknowledgement");
            }
        }
        {
            EthGetworkClient client(60, 1000, "reward");
            client.setConnection(std::make_shared<dev::URI>("getwork://127.0.0.1:8888"));
            client.m_pendingJReq["id"] = 1u;
            unsigned jobs = 0;
            dev::eth::WorkPackage work;
            client.onWorkReceived([&](dev::eth::WorkPackage& received) { ++jobs; work = received; });
            client.processResponse(response);
            require(jobs == 1 && work.header == dev::h256(header) && work.block == 1300 &&
                        work.epoch == 1 && work.boundary == dev::h256(target) &&
                        work.block_boundary == work.boundary,
                "getblocktemplate integer fields did not produce expected work");

            response["result"]["height"] = "001301";
            response["result"]["pprpcepoch"] = "0001";
            response["result"]["pprpcheader"] = std::string(64, '2');
            response["result"]["target"] = "00" + std::string(62, 'f');
            client.processResponse(response);
            require(jobs == 2 && work.block == 1301 && work.epoch == 1 &&
                        work.boundary > work.block_boundary,
                "getblocktemplate decimal strings or separate share target failed");
            response["result"]["target"] = std::string(63, '0') + "1";
            response["result"]["pprpcheader"] = std::string(64, '3');
            client.processResponse(response);
            require(jobs == 2, "getblocktemplate accepted target below block target");

            unsigned accepted = 0, rejected = 0;
            client.onSolutionAccepted([&](std::chrono::milliseconds const&, unsigned const& index, bool) {
                require(index == 0, "incorrect getwork accepted miner index");
                ++accepted;
            });
            client.onSolutionRejected([&](std::chrono::milliseconds const&, unsigned const& index) {
                require(index == 0, "incorrect getwork rejected miner index");
                ++rejected;
            });
            client.m_pendingJReq["id"] = 40u;
            client.m_solution_submitted_max_id = 40;
            client.m_pending_tstamp = std::chrono::steady_clock::now();
            for (auto const* result : {"null", "true", "\"duplicate\"", "false",
                     "\"duplicate-inconclusive\"", "\"bad-mixhash\""})
            {
                auto reply = json(std::string("{\"id\":40,\"error\":null,\"result\":") + result + "}");
                client.processResponse(reply);
            }
            require(accepted == 3 && rejected == 3, "pprpcsb status classification failed");
        }

        auto notify = json(std::string(R"({"method":"mining.notify","params":["job",")") + header + "\",\"" + seed + "\",\"" + target + "\",true,1300,\"1d00ffff\"]}");
        // Standard Stratum and both eth-proxy notification envelopes share the same job fields.
        for (unsigned mode : {0u, 1u})
        {
            EthStratumClient client(60, 1);
            auto uri = std::make_shared<dev::URI>("stratum://127.0.0.1:1");
            client.setConnection(uri);
            uri->SetStratumMode(mode, true);
            client.startSession();
            client.m_session->subscribed.store(true);
            client.processResponse(notify);
            require(client.m_newjobprocessed && client.m_current.block == 1300 &&
                        client.m_current.header == dev::h256(header) && !client.m_current.epoch &&
                        client.m_current.block_boundary == dev::h256(target),
                "legacy mining.notify did not produce expected work");
            client.m_newjobprocessed = false;
            auto invalid = notify;
            invalid["params"][4] = 1;
            client.processResponse(invalid);
            require(!client.m_newjobprocessed, "mining.notify accepted non-boolean clean_jobs");
            invalid = notify;
            invalid["params"][2] = "";
            client.processResponse(invalid);
            require(!client.m_newjobprocessed, "mining.notify accepted missing seed");
            if (mode == 1)
            {
                Json::Value proxy;
                proxy["id"] = 0u;
                proxy["result"] = Json::Value(Json::arrayValue);
                for (Json::ArrayIndex i = 1; i < notify["params"].size(); ++i)
                    proxy["result"].append(notify["params"][i]);
                client.processResponse(proxy);
                require(client.m_newjobprocessed && client.m_current.header == dev::h256(header),
                    "eth-proxy result notification failed");
            }
        }
        {
            EthStratumClient client(60, 1);
            auto uri = std::make_shared<dev::URI>("stratum2+tcp://127.0.0.1:1");
            client.setConnection(uri);
            uri->SetStratumMode(2, true);
            auto subscription = json(R"({"id":1,"result":[["mining.notify","session","EthereumStratum/1.0.0"]],"error":null})");
            rejectHandshake(2, subscription);
            client.processResponse(subscription);
            require(client.isSubscribed(), "EthereumStratum/1.0.0 subscription failed");
            auto difficulty = json(R"({"method":"mining.set_difficulty","params":[2]})");
            client.processResponse(difficulty);
            auto extranonce = json(R"({"method":"mining.set_extranonce","params":["00ab"]})");
            client.processResponse(extranonce);
            auto legacy = json(std::string(R"({"method":"mining.notify","params":["height-job",")") + seed + "\",\"" + header + "\",\"001300\"]}");
            client.processResponse(legacy);
            require(client.m_newjobprocessed && client.m_current.block == 1300 &&
                        client.m_current.startNonce == 0x00ab000000000000ULL &&
                        client.m_current.exSizeBytes == 4 &&
                        client.m_current.boundary == dev::h256(dev::getTargetFromDiff(2)),
                "height-bearing EthereumStratum/1.0.0 job failed");
            difficulty["params"][0] = 0;
            client.processResponse(difficulty);
            require(client.m_session->nextWorkBoundary == client.m_current.boundary,
                "invalid mining difficulty changed session state");
        }
        {
            EthStratumClient client(60, 1);
            auto uri = std::make_shared<dev::URI>("stratum3+tcp://127.0.0.1:1");
            client.setConnection(uri);
            uri->SetStratumMode(3, true);
            auto hello = json(R"({"id":1,"result":{"proto":"EthereumStratum/2.0.0","encoding":"json",
                "resume":false,"timeout":"1e","maxerrors":"a","node":"fixture"},"error":null})");
            rejectHandshake(3, hello);
            client.processResponse(hello);
            auto set = json(std::string(R"({"method":"mining.set","params":{"epoch":"1","target":")") +
                target + R"(","algo":"progpow","extranonce":"123456789abc"}})");
            client.processResponse(set);
            auto job = json(std::string(R"({"method":"mining.notify","params":["job","514",")") + header + "\",\"0\"]}");
            client.processResponse(job);
            require(client.m_newjobprocessed && client.m_current.block == 1300 &&
                        client.m_current.epoch == 1 && client.m_current.exSizeBytes == 12 &&
                        client.m_current.startNonce == 0x123456789abc0000ULL &&
                        client.m_current.boundary == dev::h256(target),
                "EthereumStratum/2.0.0 set/notify failed");
            require(!client.processExtranonce("123456789abcde", 6) &&
                        !client.processExtranonce("0x00gg", 6) &&
                        client.m_session->extraNonce == 0x123456789abc0000ULL,
                "invalid extranonce changed session state");
            set["params"]["epoch"] = "-1";
            set["params"]["target"] = "1";
            client.processResponse(set);
            require(client.m_session->epoch == 1 &&
                        client.m_session->nextWorkBoundary == dev::h256(target),
                "invalid mining.set partly committed session state");
        }
        {
            EthStratumClient client(60, 1);
            auto uri = std::make_shared<dev::URI>("stratum://127.0.0.1:1");
            client.setConnection(uri);
            client.init_socket();
            client.m_connectionWanted = true;
            uri->SetStratumMode(2, false);
            uri->Responds(true);
            unsigned disconnected = 0;
            client.onDisconnected([&] { ++disconnected; client.unsetConnection(); });
            client.disconnectInternal();
            require(uri->StratumMode() == 1 && !uri->Responds() && disconnected == 0,
                "Stratum autodetection did not advance mode");
            client.disconnect();
            require(disconnected == 1 && !client.isPendingState(),
                "explicit disconnect did not cancel queued autodetection");
            client.start_connect();
            client.workloop_timer_elapsed({});
            require(!client.isPendingState(), "queued reconnect restarted a stopped client");
        }
        {
            EthStratumClient client(60, 1);
            client.setConnection(std::make_shared<dev::URI>("stratum://127.0.0.1:1"));
            client.init_socket();
            client.m_connecting.store(true);
            unsigned disconnected = 0;
            client.onDisconnected([&] { ++disconnected; });
            client.disconnect();
            require(!client.isPendingState() && disconnected == 1,
                "cancelled connect retained pending state");
        }
    }
};

int main()
{
    try
    {
        ProtocolTest::run();
    }
    catch (std::exception const& ex)
    {
        std::cerr << ex.what() << '\n';
        return 1;
    }

    uint32_t value = 0;
    if (!ParseUInt32("4294967295", &value) || value != std::numeric_limits<uint32_t>::max() ||
        !ParseUInt32("001300", &value) || value != 1300 ||
        !ParseUInt32("ffffffff", &value, 16) || value != std::numeric_limits<uint32_t>::max() ||
        ParseUInt32("4294967296", &value) || ParseUInt32("100000000", &value, 16) ||
        ParseUInt32("-1", &value) || ParseUInt32("1junk", &value) || ParseUInt32(" 1", &value))
    {
        std::cerr << "strict uint32 parsing failed\n";
        return 1;
    }

    std::string hash;
    if (!NormalizeHex256("1", &hash) ||
        hash != "0x0000000000000000000000000000000000000000000000000000000000000001" ||
        !NormalizeHex256(std::string(64, 'a'), &hash, true) || NormalizeHex256("0x", &hash) ||
        NormalizeHex256("xyz", &hash) || NormalizeHex256("0x0x1", &hash) ||
        NormalizeHex256(std::string(65, '0'), &hash) ||
        NormalizeHex256("1", &hash, true))
    {
        std::cerr << "256-bit hex normalization failed\n";
        return 1;
    }

    bool negative = false;
    bool overflow = false;
    auto const compact = ethash::from_compact(0x13010000, &negative, &overflow);
    if (negative || overflow || ethash::to_hex(compact) != std::string(27, '0') + "1" + std::string(36, '0'))
    {
        std::cerr << "compact target shift failed\n";
        return 1;
    }

    bool rejectedInvalidUri = false;
    try
    {
        dev::URI invalid{"not-a-uri"};
    }
    catch (std::runtime_error const&)
    {
        rejectedInvalidUri = true;
    }

    dev::URI uri{"stratum+tcp://wallet@example.com:1234"};
    if (!rejectedInvalidUri || uri.getDuration() != 0)
    {
        std::cerr << "URI validation or initialization failed\n";
        return 1;
    }

    try
    {
        dev::URI tooLong{"stratum+tcp://" + std::string(1025, 'a')};
        std::cerr << "Pool URI length limit was not enforced\n";
        return 1;
    }
    catch (std::runtime_error const&)
    {}
    uri.addDuration(7);
    if (uri.getDuration() != 7)
    {
        std::cerr << "URI duration accounting failed\n";
        return 1;
    }

    using boost::asio::ip::tcp;
    {
        tcp::acceptor server(
            g_io_service, tcp::endpoint(boost::asio::ip::address_v4::loopback(), 0));
        tcp::socket peer(g_io_service);
        boost::asio::streambuf request;
        bool subscribed = false;
        EthStratumClient client(60, 1);
        client.setConnection(std::make_shared<dev::URI>("stratum2+tcp://127.0.0.1:" +
            std::to_string(server.local_endpoint().port())));
        server.async_accept(peer, [&](boost::system::error_code const& ec) {
            if (!ec)
                boost::asio::async_read_until(peer, request, '\n',
                    [&](boost::system::error_code const& readError, size_t) {
                        if (!readError)
                        {
                            std::istream input(&request);
                            std::string line;
                            std::getline(input, line);
                            auto hello = ProtocolTest::json(line);
                            subscribed = hello["method"] == "mining.subscribe" &&
                                hello["params"][1] == "EthereumStratum/1.0.0";
                        }
                        client.disconnect();
                    });
        });
        client.connect();
        g_io_service.run();
        if (!subscribed)
        {
            std::cerr << "stratum2+tcp did not send EthereumStratum/1.0.0 subscription\n";
            return 1;
        }
    }

    g_io_service.reset();
    {
        tcp::acceptor stalledServer(
            g_io_service, tcp::endpoint(boost::asio::ip::address_v4::loopback(), 0));
        tcp::socket stalledPeer(g_io_service);
        stalledServer.async_accept(stalledPeer, [](boost::system::error_code const&) {});

        auto stalledUri = std::make_shared<dev::URI>("stratum+tls://127.0.0.1:" +
                                                     std::to_string(stalledServer.local_endpoint().port()));
        bool disconnected = false;
        EthStratumClient client(60, 1);
        client.setConnection(stalledUri);
        client.onDisconnected([&]() { disconnected = true; });
        client.connect();
        g_io_service.run();

        if (!disconnected || stalledUri->IsUnrecoverable())
        {
            std::cerr << "TLS handshake timeout was treated as unrecoverable\n";
            return 1;
        }
    }

    g_io_service.reset();
    std::map<std::string, dev::eth::DeviceDescriptor> devices;
    dev::eth::Farm farm(devices, {}, {}, {}, {});
    try
    {
        ProtocolTest::networkMismatch();
    }
    catch (std::exception const& ex)
    {
        std::cerr << ex.what() << '\n';
        return 1;
    }

    {
        tcp::acceptor stoppedServer(
            g_io_service, tcp::endpoint(boost::asio::ip::address_v4::loopback(), 0));
        tcp::socket stoppedPeer(g_io_service);
        std::atomic<bool> acceptedAfterStop{false};
        stoppedServer.async_accept(stoppedPeer, [&](boost::system::error_code const& ec) {
            if (!ec)
                acceptedAfterStop.store(true, std::memory_order_relaxed);
        });

        dev::eth::PoolSettings stoppedSettings;
        stoppedSettings.connections.push_back(std::make_shared<dev::URI>(
            "stratum+tcp://127.0.0.1:" + std::to_string(stoppedServer.local_endpoint().port())));
        dev::eth::PoolManager stoppedManager(stoppedSettings);
        stoppedManager.start();
        stoppedManager.stop();

        boost::asio::deadline_timer stopTestTimer(g_io_service);
        stopTestTimer.expires_from_now(boost::posix_time::milliseconds(100));
        stopTestTimer.async_wait(
            [](boost::system::error_code const&) { g_io_service.stop(); });
        g_io_service.run();
        if (stoppedManager.isRunning() || acceptedAfterStop.load(std::memory_order_relaxed))
        {
            std::cerr << "Stopped pool manager executed a queued connection attempt\n";
            return 1;
        }
    }

    g_io_service.reset();
    tcp::acceptor managerServer(
        g_io_service, tcp::endpoint(boost::asio::ip::address_v4::loopback(), 0));
    tcp::socket managerPeer(g_io_service);
    std::promise<void> managerAccepted;
    managerServer.async_accept(managerPeer, [&](boost::system::error_code const& ec) {
        if (!ec)
            managerAccepted.set_value();
    });

    dev::eth::PoolSettings settings;
    settings.noResponseTimeout = 1;
    settings.connections.push_back(std::make_shared<dev::URI>(
        "stratum+tls://127.0.0.1:" + std::to_string(managerServer.local_endpoint().port())));
    dev::eth::PoolManager manager(settings);
    manager.start();
    std::thread ioThread([]() { g_io_service.run(); });

    auto accepted = managerAccepted.get_future();
    if (accepted.wait_for(std::chrono::seconds(2)) != std::future_status::ready)
    {
        g_io_service.stop();
        ioThread.join();
        std::cerr << "Pool manager TLS test did not connect\n";
        return 1;
    }

    boost::system::error_code availableError;
    const auto helloDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!managerPeer.available(availableError) && !availableError &&
           std::chrono::steady_clock::now() < helloDeadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

    if (availableError || !managerPeer.available(availableError) || manager.isConnected())
    {
        g_io_service.stop();
        ioThread.join();
        std::cerr << "Pool manager TLS handshake did not remain pending\n";
        return 1;
    }

    manager.stop();
    const bool managerStopped = !manager.isRunning();
    g_io_service.stop();
    ioThread.join();
    if (!managerStopped)
    {
        std::cerr << "Pool manager remained running after stopping a pending TLS connection\n";
        return 1;
    }
}
