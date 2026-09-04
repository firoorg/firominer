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
        else
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
        else
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

std::string authorize(unsigned id, std::string const& password)
{
    return "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) +
           ",\"method\":\"api_authorize\",\"params\":{\"psw\":\"" + password + "\"}}\n";
}

struct IoRunner
{
    std::thread thread{[]() { g_io_service.run(); }};
    ~IoRunner()
    {
        g_io_service.stop();
        thread.join();
    }
};
}  // namespace

int runTests()
{
    {
        ApiServer stoppedWithoutRunner("127.0.0.1", 0, "secret");
        stoppedWithoutRunner.start();
        if (!stoppedWithoutRunner.isRunning())
            throw std::runtime_error("API server did not start without runner");
        auto stoppedClient = connect(stoppedWithoutRunner.getPort());
        if (g_io_service.run_one() != 1)
        {
            std::cerr << "API session was not accepted\n";
            return 1;
        }
        stoppedWithoutRunner.stop();
        readToClose(stoppedClient, std::chrono::steady_clock::now() + 1s);
    }

    {
        ApiServer stoppedBeforeRun("127.0.0.1", 0, "secret");
        stoppedBeforeRun.start();
        if (!stoppedBeforeRun.isRunning())
            throw std::runtime_error("API server did not start before run");
        stoppedBeforeRun.stop();
    }

    std::map<std::string, DeviceDescriptor> devices;
    Farm farm(devices, {}, {}, {}, {});
    PoolManager manager({});
    std::string password(500, 'a');
    password += 'X';
    ApiServer server("127.0.0.1", 0, password);
    server.start();
    if (!server.isRunning())
    {
        std::cerr << "API server did not start\n";
        return 1;
    }
    auto port = server.getPort();
    IoRunner ioRunner;

    auto deadline = std::chrono::steady_clock::now() + 5s;
    auto shortJson = connect(port);
    boost::asio::write(shortJson, boost::asio::buffer("{}\n", 3));
    if (readLine(shortJson, deadline).find("\"error\"") == std::string::npos)
    {
        std::cerr << "complete three-byte request stalled\n";
        return 1;
    }
    // Exercise the API's small nesting budget, well below jsoncpp's default stack limit.
    std::string nested = std::string(64, '[') + "0" + std::string(64, ']') + '\n';
    const std::string ping = "{\"jsonrpc\":\"2.0\",\"id\":0,\"method\":\"miner_ping\"}\n";
    for (auto const& request : {std::string("{]\n"), nested})
    {
        boost::asio::write(shortJson, boost::asio::buffer(request));
        if (readLine(shortJson, std::chrono::steady_clock::now() + 5s)
                .find("\"code\":-32700") == std::string::npos)
            throw std::runtime_error("invalid JSON did not return a parse error");
        boost::asio::write(shortJson, boost::asio::buffer(ping));
        if (readLine(shortJson, std::chrono::steady_clock::now() + 5s)
                .find("\"code\":-403") == std::string::npos)
            throw std::runtime_error("parse error changed session liveness or authorization");
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

    auto call = [&](std::string const& method, std::string const& params = "{}") {
        auto request = "{\"jsonrpc\":\"2.0\",\"id\":34,\"method\":\"" + method +
                       "\",\"params\":" + params + "}\n";
        boost::asio::write(json, boost::asio::buffer(request));
        return readLine(json, std::chrono::steady_clock::now() + 5s);
    };
    if (call("miner_setscramblerinfo", "{\"noncescrambler\":\"0xfedcba9876543210\"}")
                .find("\"result\":true") == std::string::npos ||
        call("miner_getscramblerinfo").find("0xfedcba9876543210") == std::string::npos ||
        call("miner_setscramblerinfo", "{\"noncescrambler\":18446744073709551615}")
                .find("\"result\":true") == std::string::npos ||
        call("miner_getscramblerinfo").find("0xffffffffffffffff") == std::string::npos)
        throw std::runtime_error("64-bit nonce scrambler roundtrip failed");

    for (auto value : {"-1", "false", "{}", "\"invalid\""})
    {
        if (call("miner_setscramblerinfo", "{\"noncescrambler\":" + std::string(value) + "}")
                .find("\"code\":-32602") == std::string::npos)
            throw std::runtime_error("invalid uint64 parameter did not return an error code");
    }
    if (call("miner_setscramblerinfo", "{\"noncescrambler\":\"0x1junk\"}")
            .find("\"code\":-422") == std::string::npos)
        throw std::runtime_error("nonce scrambler accepted trailing characters");
    if (call("miner_addconnection", "{\"uri\":\"getwork://127.0.0.1:1234\"}")
            .find("reward address is required") == std::string::npos)
        throw std::runtime_error("missing reward address was reported as a malformed URI");

    auto http = connect(port);
    boost::asio::write(http, boost::asio::buffer("GET / HT", 8));
    std::this_thread::sleep_for(20ms);
    boost::asio::write(http, boost::asio::buffer("TP/1.1\r\n\r\n", 10));
    deadline = std::chrono::steady_clock::now() + 5s;
    auto httpResponse = readToClose(http, deadline);
    if (httpResponse.find("401 Unauthorized") == std::string::npos ||
        httpResponse.find("200 Ok") != std::string::npos)
    {
        std::cerr << "unauthenticated HTTP request was not rejected\n";
        return 1;
    }

    {
        ApiServer publicServer("127.0.0.1", 0, "");
        publicServer.start();
        if (!publicServer.isRunning())
            throw std::runtime_error("public API server did not start");
        for (auto const& example :
            {std::make_pair("GET /", "200 Ok"), std::make_pair("GET /missing", "404 Not Found"),
                std::make_pair("POST /", "405 Method not allowed")})
        {
            auto client = connect(publicServer.getPort());
            auto request = std::string(example.first) + " HTTP/1.1\r\n\r\n";
            boost::asio::write(client, boost::asio::buffer(request));
            if (readToClose(client, std::chrono::steady_clock::now() + 5s)
                    .find(example.second) == std::string::npos)
                throw std::runtime_error("HTTP request-line parsing failed");
        }
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

    tcp::socket destructionClient(g_io_service);
    {
        ApiServer destroyedWithSession("127.0.0.1", 0, password);
        destroyedWithSession.start();
        if (!destroyedWithSession.isRunning())
            throw std::runtime_error("API server did not start for destruction test");
        destructionClient = connect(destroyedWithSession.getPort());
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
    return 0;
}

int main()
{
    try
    {
        return runTests();
    }
    catch (const std::exception& ex)
    {
        std::cerr << ex.what() << '\n';
        return 1;
    }
}
