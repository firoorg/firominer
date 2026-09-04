#include <libapicore/ApiServer.h>

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

boost::asio::io_service g_io_service;
bool g_exitOnError = false;

namespace
{
using namespace std::chrono_literals;

std::string readLine(tcp::socket& socket, std::chrono::steady_clock::time_point deadline)
{
    std::string result;
    socket.non_blocking(true);
    while (std::chrono::steady_clock::now() < deadline)
    {
        char data;
        boost::system::error_code ec;
        auto read = socket.read_some(boost::asio::buffer(&data, 1), ec);
        if (!ec)
        {
            result.append(&data, read);
            if (data == '\n')
                return result;
        }
        else if (ec != boost::asio::error::would_block && ec != boost::asio::error::try_again)
        {
            break;
        }
        std::this_thread::sleep_for(1ms);
    }
    return result;
}

std::string readToClose(tcp::socket& socket, std::chrono::steady_clock::time_point deadline)
{
    std::string result;
    socket.non_blocking(true);
    while (std::chrono::steady_clock::now() < deadline)
    {
        char data[4096];
        boost::system::error_code ec;
        auto read = socket.read_some(boost::asio::buffer(data), ec);
        if (!ec)
            result.append(data, read);
        else if (ec == boost::asio::error::eof || ec == boost::asio::error::connection_reset)
            return result;
        else if (ec != boost::asio::error::would_block && ec != boost::asio::error::try_again)
            break;
        std::this_thread::sleep_for(1ms);
    }
    throw std::runtime_error("API connection did not close before timeout");
}

tcp::socket connect(uint16_t port)
{
    tcp::socket socket(g_io_service);
    socket.connect({boost::asio::ip::address_v4::loopback(), port});
    return socket;
}

uint16_t reservePort()
{
    tcp::acceptor reservation(g_io_service, {boost::asio::ip::address_v4::loopback(), 0});
    auto port = reservation.local_endpoint().port();
    reservation.close();
    return port;
}

std::string authorize(unsigned id, std::string const& password)
{
    return "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) +
           ",\"method\":\"api_authorize\",\"params\":{\"psw\":\"" + password + "\"}}\n";
}
}  // namespace

int main()
{
    {
        auto stoppedPort = reservePort();
        ApiServer stoppedWithoutRunner("127.0.0.1", stoppedPort, "secret");
        stoppedWithoutRunner.start();
        auto stoppedClient = connect(stoppedPort);
        if (g_io_service.run_one() != 1)
        {
            std::cerr << "API session was not accepted\n";
            return 1;
        }
        stoppedWithoutRunner.stop();
        readToClose(stoppedClient, std::chrono::steady_clock::now() + 1s);
    }

    {
        ApiServer stoppedBeforeRun("127.0.0.1", reservePort(), "secret");
        stoppedBeforeRun.start();
        stoppedBeforeRun.stop();
    }

    auto port = reservePort();
    std::string password(500, 'a');
    password += 'X';
    ApiServer server("127.0.0.1", port, password);
    server.start();
    if (!server.isRunning())
    {
        std::cerr << "API server did not start\n";
        return 1;
    }
    std::thread ioThread([]() { g_io_service.run(); });

    auto deadline = std::chrono::steady_clock::now() + 5s;
    auto shortJson = connect(port);
    boost::asio::write(shortJson, boost::asio::buffer("{}\n", 3));
    if (readLine(shortJson, deadline).find("\"error\"") == std::string::npos)
    {
        std::cerr << "complete three-byte request stalled\n";
        return 1;
    }
    shortJson.close();

    auto json = connect(port);
    auto prefixOnly = authorize(0, password.substr(0, 500));
    boost::asio::write(json, boost::asio::buffer(prefixOnly));
    if (readLine(json, deadline).find("\"error\"") == std::string::npos)
    {
        std::cerr << "500-byte password prefix was accepted\n";
        return 1;
    }

    auto authorization = authorize(1, password);
    boost::asio::write(json, boost::asio::buffer(authorization.data(), 1));
    std::this_thread::sleep_for(20ms);
    boost::asio::write(
        json, boost::asio::buffer(authorization.data() + 1, authorization.size() - 1));
    if (readLine(json, deadline).find("\"id\":1") == std::string::npos)
    {
        std::cerr << "fragmented authorization request stalled\n";
        return 1;
    }

    std::string requests;
    for (unsigned id = 2; id <= 33; ++id)
        requests += authorize(id, password);
    boost::asio::write(json, boost::asio::buffer(requests));
    for (unsigned id = 2; id <= 33; ++id)
    {
        auto response = readLine(json, deadline);
        if (response.find("\"id\":" + std::to_string(id)) == std::string::npos ||
            response.find("\"error\"") != std::string::npos)
        {
            std::cerr << "pipelined response was lost or reordered\n";
            return 1;
        }
    }

    auto http = connect(port);
    boost::asio::write(http, boost::asio::buffer("GET / HTTP/1.1\r\n\r\n", 18));
    deadline = std::chrono::steady_clock::now() + 5s;
    auto httpResponse = readToClose(http, deadline);
    if (httpResponse.find("401 Unauthorized") == std::string::npos ||
        httpResponse.find("200 Ok") != std::string::npos)
    {
        std::cerr << "unauthenticated HTTP request was not rejected\n";
        return 1;
    }

    auto oversized = connect(port);
    std::string oversizedRequest(64 * 1024 + 1, 'x');
    boost::system::error_code ec;
    boost::asio::write(oversized, boost::asio::buffer(oversizedRequest), ec);
    deadline = std::chrono::steady_clock::now() + 15s;
    readToClose(oversized, deadline);

    auto abandoned = connect(port);
    boost::asio::write(abandoned, boost::asio::buffer(authorization));
    abandoned.close();

    auto destructionPort = reservePort();
    tcp::socket destructionClient(g_io_service);
    {
        ApiServer destroyedWithSession("127.0.0.1", destructionPort, password);
        destroyedWithSession.start();
        destructionClient = connect(destructionPort);
        boost::asio::write(destructionClient, boost::asio::buffer(authorization));
        if (readLine(destructionClient, deadline).find("\"id\":1") == std::string::npos)
        {
            std::cerr << "destruction test session was not accepted\n";
            return 1;
        }
    }
    destructionClient.close();
    std::this_thread::sleep_for(20ms);

    server.stop();
    if (server.isRunning())
    {
        std::cerr << "API server did not stop\n";
        return 1;
    }
    json.close();
    g_io_service.stop();
    ioThread.join();
}
