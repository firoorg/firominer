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

int main()
{
    uint32_t value = 0;
    if (!ParseUInt32("4294967295", &value) || value != std::numeric_limits<uint32_t>::max() ||
        !ParseUInt32("0x10", &value, 0) || value != 16 ||
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
    uri.addDuration(7);
    if (uri.getDuration() != 7)
    {
        std::cerr << "URI duration accounting failed\n";
        return 1;
    }

    {
        auto autodetectUri =
            std::make_shared<dev::URI>("stratum://127.0.0.1:1");
        EthStratumClient autodetectClient(60, 1);
        autodetectClient.setConnection(autodetectUri);
        autodetectClient.init_socket();
        autodetectUri->SetStratumMode(2, false);
        autodetectUri->Responds(true);
        autodetectClient.disconnect();
        if (autodetectUri->StratumMode() != 1 || autodetectUri->Responds() ||
            autodetectUri->IsUnrecoverable())
        {
            std::cerr << "Stratum autodetection carried a stale response into the next mode\n";
            return 1;
        }
    }

    using boost::asio::ip::tcp;
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
