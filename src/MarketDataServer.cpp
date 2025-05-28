#include "MarketDataServer.hpp"
#include "Logger.hpp"
#include "BenchMark.hpp"
#include "DataParser.hpp"
#include <iostream>
#include <thread>
#include <vector>
#include <chrono>
#include <sstream>
#include <algorithm>


namespace beast = boost::beast;
namespace http = beast::http;
namespace ssl = net::ssl;
using json = nlohmann::json;

namespace
{
    // Global cache of market data
    std::shared_ptr<MarketDataServer::DataCache> g_dataCache = std::make_shared<MarketDataServer::DataCache>();
    std::atomic<bool> g_shouldContinueFetching(false);


    void HandleClient(std::shared_ptr<tcp::socket> socket, MarketDataServer::SubscriptionManager& subManager);
    void _do_accept(net::io_context &ioc, tcp::acceptor &acceptor, MarketDataServer::SubscriptionManager& subManager);
    void SendMarketData(std::shared_ptr<tcp::socket> socket, const std::string &symbol);
    void DataUpdateTask(const MarketDataServer::ServerConfig config, MarketDataServer::SubscriptionManager& subManager);

    
    void HandleClient(std::shared_ptr<tcp::socket> socket, MarketDataServer::SubscriptionManager &subManager)
    {
        try
        {
            // Loop to handle multiple requests from the same client
            while (true) // Loop to read commands
            {
                boost::asio::streambuf buffer;
                boost::system::error_code ec;
                boost::asio::read_until(*socket, buffer, "\n", ec);
                if (ec)
                {
                    if (ec == net::error::eof)
                    {
                        Logger::getInstance().log("Client closed connection.", Logger::LogLevel::INFO);
                    }
                    else
                    {
                        Logger::getInstance().log("Error reading from client: " + ec.message(), Logger::LogLevel::WARNING);
                    }
                    break; // Handle disconnect/error
                }

                std::istream request_stream(&buffer);
                std::string command_line;
                std::getline(request_stream, command_line);

                std::string command, argument;
                std::stringstream ss(command_line);
                ss >> command >> argument;
                std::transform(command.begin(), command.end(), command.begin(), ::toupper);

                // Use subManager methods
                if (command == "SUBSCRIBE" && !argument.empty())
                {
                    subManager.addSubscription(argument, socket); // Add subscription first

                    Logger::getInstance().log("Sending initial data for " + argument + " upon subscription.", Logger::LogLevel::INFO);
                    try
                    {
                        SendMarketData(socket, argument); // Send current data immediately
                    }
                    catch (const std::exception &push_ex)
                    {
                        // Log if initial push fails, but don't disconnect client
                        Logger::getInstance().log("Error sending initial data for " + argument + ": " + std::string(push_ex.what()), Logger::LogLevel::ERROR);
                    }
                }
                else if (command == "UNSUBSCRIBE" && !argument.empty())
                {
                    subManager.removeSubscription(argument, socket);
                }
                else if (command == "GET" && !argument.empty())
                {
                    // Keep GET for testing/debugging
                    Logger::getInstance().log("Processing GET request for: " + argument, Logger::LogLevel::INFO);
                    SendMarketData(socket, argument);
                }
                else
                {
                    Logger::getInstance().log("Received unknown command: " + command_line, Logger::LogLevel::WARNING);
                }
            } // End while loop
        }
        catch (const std::exception &e)
        {
            Logger::getInstance().log("Client handler error: " + std::string(e.what()),
                                      Logger::LogLevel::ERROR);
        }

        // Cleanup using the manager
        Logger::getInstance().log("Client handler cleaning up subscriptions...", Logger::LogLevel::INFO);
        subManager.removeAllSubscriptions(socket); // Remove using manager

        // Close socket (now done in HandleClient after loop exit)
        if (socket->is_open())
        {
            boost::system::error_code ignored_ec;
            socket->shutdown(tcp::socket::shutdown_both, ignored_ec);
            socket->close(ignored_ec);
        }
        Logger::getInstance().log("Client connection handler finished.", Logger::LogLevel::INFO);
    }

    void _do_accept(net::io_context &ioc, tcp::acceptor &acceptor, MarketDataServer::SubscriptionManager &subManager)
    {
        // Create a socket for the next potential incoming connection.

        auto socket = std::make_shared<tcp::socket>(ioc);

        // Asynchronously wait for a connection attempt.
        acceptor.async_accept(*socket, // The socket to accept the connection into
                                       // This lambda is the completion handler. It will be called when:
                                       // 1. A new connection is successfully accepted.
                                       // 2. An error occurs during the accept operation.
                                       // 3. The acceptor is closed (e.g., during shutdown).
                              [&ioc, &acceptor, &subManager, socket](boost::system::error_code ec)
                              {
                                  // Check if the operation was successful
                                  if (!ec)
                                  {
                                      // Connection accepted successfully.

                                      // Log the new client connection (use try-catch for remote_endpoint)
                                      try
                                      {
                                          Logger::getInstance().log("Client connected: " +
                                                                        socket->remote_endpoint().address().to_string(),
                                                                    Logger::LogLevel::INFO);
                                      }
                                      catch (const std::exception &e)
                                      {
                                          Logger::getInstance().log("Error getting remote endpoint: " + std::string(e.what()), Logger::LogLevel::WARNING);
                                      }
                                      // Create a new thread to handle this client's requests.
                                      // Pass the shared_ptr `socket` to the thread. `std::move` is efficient here.
                                      // Detach the thread so the acceptor loop doesn't wait for it.
                                      std::thread(HandleClient, std::move(socket), std::ref(subManager)).detach();
                                      // This recursive call keeps the server accepting connections.
                                      _do_accept(ioc, acceptor, subManager);
                                  }
                                  // Check if an error occurred, BUT ignore "operation_aborted" which means
                                  // we deliberately stopped the acceptor (e.g., during shutdown).
                                  else if (ec != net::error::operation_aborted)
                                  {
                                      // An unexpected error occurred during accept.
                                      Logger::getInstance().log("Accept error: " + ec.message(), Logger::LogLevel::ERROR);

                                      // Optional: Decide whether to continue accepting based on the error.
                                      // For robustness, we might try accepting again if the acceptor is still open.
                                      if (acceptor.is_open())
                                      {
                                          _do_accept(ioc, acceptor, subManager); // Try accepting again
                                      }
                                      else
                                      {
                                          // Acceptor was likely closed due to the error or externally.
                                          Logger::getInstance().log("Acceptor closed, stopping accept loop due to error.", Logger::LogLevel::INFO);
                                      }
                                  }
                                  // The operation was aborted (likely because acceptor.close() was called).
                                  else
                                  {
                                      Logger::getInstance().log("Stopped accepting connections (operation aborted).", Logger::LogLevel::INFO);
                                      // Don't call _do_accept again - we are shutting down the accept loop.
                                  }
                              }); // End of async_accept lambda
    }
    
    void SendMarketData(std::shared_ptr<tcp::socket> socket, const std::string &symbol)
    {
        // Use the global data cache instead of creating a new one
        std::vector<MarketDataEntry> data = g_dataCache->getData(symbol);

        try
        {
            if (data.empty())
            {
                // Send a proper error message instead of nothing
                std::string errorMsg = "ERROR: No data available for symbol: " + symbol + "\n";
                boost::asio::write(*socket, boost::asio::buffer(errorMsg));
                Logger::getInstance().log("No data available for " + symbol + ", sent error message",
                                          Logger::LogLevel::WARNING);
                return;
            }

            json jsonData = data; // Use the to_json function we defined! Converts vector<MarketDataEntry> to json array.

            // Convert the JSON object to a string
            // Using dump() with indentation (-1 means compact, no extra whitespace)
            std::string dataStr = jsonData.dump();

            // First send a header with the data size
            std::string header = "DATA_SIZE:" + std::to_string(dataStr.size()) + "\n";
            boost::asio::write(*socket, boost::asio::buffer(header));

            // Then send the actual data
            boost::asio::write(*socket, boost::asio::buffer(dataStr));

            // Update log message
            Logger::getInstance().log("Sent " + std::to_string(data.size()) +
                                          " market data entries as JSON to client for " + symbol, // Added symbol
                                      Logger::LogLevel::INFO);
        }
        catch (const json::exception &e)
        {
            // Catch errors during JSON serialization (less likely here)
            Logger::getInstance().log("JSON serialization error in SendMarketData for " + symbol + ": " + std::string(e.what()), Logger::LogLevel::ERROR);
            try
            {
                std::string errorMsg = "ERROR: Internal server error serializing data.\n";
                boost::asio::write(*socket, boost::asio::buffer(errorMsg));
            }
            catch (...)
            {
            } // Ignore errors sending error message
        }
        catch (const boost::system::system_error &bse)
        {
            if (bse.code() != net::error::broken_pipe && bse.code() != net::error::connection_reset)
            { // Avoid logging expected disconnects as errors
                Logger::getInstance().log("Network error sending data for " + symbol + ": " + bse.code().message(), Logger::LogLevel::ERROR);
            }
            else
            {
                Logger::getInstance().log("Network connection closed while sending data for " + symbol + ".", Logger::LogLevel::INFO);
            }
        }
        catch (const std::exception &e)
        {
            Logger::getInstance().log("Error sending market data for " + symbol + ": " + std::string(e.what()), Logger::LogLevel::ERROR);
        }
    }

    
    void DataUpdateTask(const MarketDataServer::ServerConfig config, MarketDataServer::SubscriptionManager& subManager)
    {
        Logger &logger = Logger::getInstance();
        logger.log("Starting periodic market data fetch task", Logger::LogLevel::INFO);

        auto refreshDuration = std::chrono::seconds(config.apiRefreshSeconds);
        logger.log("Using API refresh interval: " + std::to_string(config.apiRefreshSeconds) + " seconds.", Logger::LogLevel::INFO);

        while (g_shouldContinueFetching)
        {
            for (const auto &symbol : config.symbols)
            {
                bool dataUpdated = false;
                try
                {
                    logger.log("Fetching market data for " + symbol, Logger::LogLevel::INFO);

                    // Fetch data from API
                    std::string jsonResponse = MarketDataServer::FetchMarketData(symbol, config);

                    bool apiDataProcessed = false;

                    if (!jsonResponse.empty())
                    {
                        auto jsonParser = ParserFactory::createJSONParser(jsonResponse);
                        if (jsonParser->parseData())
                        {
                            // Update cache with new data
                            g_dataCache->updateData(symbol, jsonParser->getData());
                            apiDataProcessed = true;
                            dataUpdated = true;
                            logger.log("Updated market data for " + symbol +
                                           ": " + std::to_string(jsonParser->getData().size()) +
                                           " entries",
                                       Logger::LogLevel::INFO);
                        }
                    }

                    // If API request failed or returned no data, fall back to CSV
                    if (!apiDataProcessed)
                    {
                        logger.log("API request failed or returned no data for " + symbol +
                                       ". Falling back to CSV data.",
                                   Logger::LogLevel::INFO);

                        // CSV fallback
                        auto csvPathIt = config.symbolCSVPaths.find(symbol);
                        if (csvPathIt != config.symbolCSVPaths.end())
                        {
                            auto csvParser = ParserFactory::createCSVParser(csvPathIt->second);
                            if (csvParser->parseData())
                            {
                                g_dataCache->updateData(symbol, csvParser->getData());
                                dataUpdated = true;
                                logger.log("Updated market data for " + symbol +
                                               " from CSV: " + std::to_string(csvParser->getData().size()) +
                                               " entries",
                                           Logger::LogLevel::INFO);
                            }
                            else
                            {
                                logger.log("Failed to load CSV fallback data for " + symbol,
                                           Logger::LogLevel::ERROR);
                            }
                        }
                    }
                    if (dataUpdated)
                    {
                        // Get list of *valid* subscribers using the manager method
                        std::vector<std::shared_ptr<tcp::socket>> subscribers = subManager.getSubscribers(symbol);

                        if (!subscribers.empty())
                        {
                            logger.log("Pushing updated data for " + symbol + " to " + std::to_string(subscribers.size()) + " subscribers.", Logger::LogLevel::INFO);
                            for (const auto &sub_socket_ptr : subscribers)
                            {
                                try
                                {
                                    SendMarketData(sub_socket_ptr, symbol); // Use SendMarketData
                                }
                                catch (...)
                                { /* log error sending to specific client */
                                }
                            }
                        }
                    }
                }
                catch (const std::exception &e)
                {
                    logger.log("Error updating market data for " + symbol +
                                   ": " + std::string(e.what()),
                               Logger::LogLevel::ERROR);
                }
            }

            // Wait for next update interval
            std::this_thread::sleep_for(refreshDuration);
        }

        logger.log("Periodic market data fetch task stopped", Logger::LogLevel::INFO);
    }
    // Create SSL context for secure connections
    std::shared_ptr<ssl::context> createSSLContext()
    {
        auto ctx = std::make_shared<ssl::context>(ssl::context::tlsv12_client);
        // ctx->set_verify_mode(ssl::verify_none);
        ctx->set_default_verify_paths();
        return ctx;
    }
}

namespace MarketDataServer
{

    // Implement DataCache methods
    void DataCache::updateData(const std::string &symbol, const std::vector<MarketDataEntry> &data)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_cache[symbol] = data;
    }

    std::vector<MarketDataEntry> DataCache::getData(const std::string &symbol) const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_cache.find(symbol);
        return (it != m_cache.end()) ? it->second : std::vector<MarketDataEntry>();
    }

    void SubscriptionManager::addSubscription(const std::string &symbol, std::shared_ptr<tcp::socket> socket_ptr)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_subscriptions[symbol].insert(std::weak_ptr<tcp::socket>(socket_ptr));
        Logger::getInstance().log("Client subscribed to " + symbol, Logger::LogLevel::INFO);
    }

    void SubscriptionManager::removeSubscription(const std::string &symbol, std::shared_ptr<tcp::socket> socket_ptr)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto symbol_it = m_subscriptions.find(symbol);
        if (symbol_it != m_subscriptions.end())
        {
            std::weak_ptr<tcp::socket> weak_sock = socket_ptr; // Create weak_ptr for lookup
            if (symbol_it->second.erase(weak_sock))
            {
                Logger::getInstance().log("Client unsubscribed from " + symbol, Logger::LogLevel::INFO);
            }
            // Clean up map entry if set becomes empty
            if (symbol_it->second.empty())
            {
                m_subscriptions.erase(symbol_it);
            }
        }
    }

    void SubscriptionManager::removeAllSubscriptions(std::shared_ptr<tcp::socket> socket_ptr)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::weak_ptr<tcp::socket> weak_sock = socket_ptr; // Create weak_ptr for lookup
        bool removed = false;

        // Iterate safely, allowing removal during iteration
        for (auto it = m_subscriptions.begin(); it != m_subscriptions.end(); /* manual increment */)
        {
            if (it->second.erase(weak_sock))
            {
                removed = true;
                // Logger::getInstance().log("Removed client subscription from " + it->first + " on disconnect.", Logger::LogLevel::DEBUG);
            }

            // If set is now empty, remove the symbol entry
            if (it->second.empty())
            {
                it = m_subscriptions.erase(it); // Erase returns iterator to the next element
            }
            else
            {
                ++it; // Only increment if we didn't erase
            }
        }

        if (removed)
        {
            Logger::getInstance().log("Removed all subscriptions for disconnected client.", Logger::LogLevel::INFO);
        }
    }

    // Gets valid shared_ptrs for subscribers of a symbol
    std::vector<std::shared_ptr<tcp::socket>> SubscriptionManager::getSubscribers(const std::string &symbol)
    {
        std::vector<std::shared_ptr<tcp::socket>> active_subscribers;
        std::lock_guard<std::mutex> lock(m_mutex); // Lock for reading the map

        auto symbol_it = m_subscriptions.find(symbol);
        if (symbol_it != m_subscriptions.end())
        {
            // Reserve space for efficiency
            active_subscribers.reserve(symbol_it->second.size());

            // Iterate through the weak_ptrs in the set
            for (const auto &weak_socket_ptr : symbol_it->second)
            {
                // Try to lock the weak_ptr to get a shared_ptr
                if (auto shared_socket_ptr = weak_socket_ptr.lock())
                {
                    // If locking succeeds, the client is still connected
                    active_subscribers.push_back(shared_socket_ptr);
                }
                // NOTE: We don't remove expired weak_ptrs here to avoid modifying
                // the set while iterating without more complex logic. They will be
                // cleaned up eventually when a client explicitly unsubscribes or disconnects.
                // Alternatively, could add a periodic cleanup task.
            }
        }
        return active_subscribers;
    }

    void StartServer(const ServerConfig &config, SubscriptionManager &subManager)
    {
        net::io_context ioc; // IO context is now local to StartServer

        try
        {
            Logger::getInstance().log("Starting Market Data Server setup...", Logger::LogLevel::INFO);

            tcp::acceptor acceptor(ioc);
            tcp::endpoint endpoint(tcp::v4(), config.port);
            int port_to_use = config.port;

            // --- Port Binding Logic (copied and adapted) ---
            boost::system::error_code ec;
            acceptor.open(endpoint.protocol(), ec);
            if (ec)
            { /* Handle error */
                throw boost::system::system_error(ec, "Cannot open endpoint");
            }
            acceptor.set_option(tcp::acceptor::reuse_address(true));
            acceptor.bind(endpoint, ec);
            if (ec)
            {
                Logger::getInstance().log("Cannot bind to port " + std::to_string(config.port) + ": " + ec.message() + ". Trying alternative.", Logger::LogLevel::WARNING);
                acceptor.close();                                        // Close the failed acceptor
                endpoint.port(0);                                        // Ask for system-assigned port
                acceptor.open(endpoint.protocol());                      // Re-open
                acceptor.set_option(tcp::acceptor::reuse_address(true)); // Set option again
                acceptor.bind(endpoint);                                 // Re-bind (should succeed unless system has no ports)
                port_to_use = acceptor.local_endpoint().port();
                Logger::getInstance().log("Using alternative port: " + std::to_string(port_to_use), Logger::LogLevel::INFO);
            }
            acceptor.listen(net::socket_base::max_listen_connections, ec);
            if (ec)
            { /* Handle error */
                throw boost::system::system_error(ec, "Cannot listen on port");
            }
            // --- End Port Binding Logic ---

            Logger::getInstance().log("Server starting to listen on port " + std::to_string(port_to_use), Logger::LogLevel::INFO);

            // --- Signal Handling Setup ---
            net::signal_set signals(ioc, SIGINT, SIGTERM);
            signals.async_wait(
                [&](const boost::system::error_code &error, int signal_number)
                {
                    if (!error)
                    {
                        Logger::getInstance().log("Shutdown signal received (" + std::to_string(signal_number) + "). Stopping server...", Logger::LogLevel::INFO);

                        // 1. Stop accepting new connections
                        acceptor.close(); // This will cause pending async_accept to fail with operation_aborted

                        // 2. Signal the fetch thread to stop (will happen after ioc.run() returns in main)
                        // We can call it here too, but it's cleaner after ioc returns.
                        // StopPeriodicFetching();

                        // 3. Stop the io_context. This will cause ioc.run() to return.
                        // Note: Ensure all async operations tied to ioc are cancelable or complete quickly.
                        // HandleClient threads are detached, so they won't block ioc.stop().
                        ioc.stop();
                    }
                    else
                    {
                        Logger::getInstance().log("Error in signal handler: " + error.message(), Logger::LogLevel::ERROR);
                    }
                });
            // --- End Signal Handling Setup ---

            // Start the first asynchronous accept operation.
            // The chain reaction (accept -> handle -> accept -> ...) will continue from here.
            _do_accept(ioc, acceptor, subManager);

            Logger::getInstance().log("Server setup complete. Running IO context.", Logger::LogLevel::INFO);
            // Run the I/O context. This function will block until ioc.stop() is called (e.g., by the signal handler).
            ioc.run();

            Logger::getInstance().log("Server IO context stopped. Exiting StartServer.", Logger::LogLevel::INFO);
        }
        catch (const std::exception &e)
        {
            Logger::getInstance().log("Server error in StartServer: " + std::string(e.what()), Logger::LogLevel::ERROR);
            // Ensure io_context is stopped if an exception occurs before run()
            if (!ioc.stopped())
            {
                ioc.stop();
            }
        }
    }

    std::string FetchMarketData(const std::string &symbol, const MarketDataServer::ServerConfig& config)
    {
        // Timer timer;
        // timer.start();

        std::string response;

        try
        {
            // Host for Alpha Vantage API
            const std::string& host = config.apiHost;
            const std::string& port = "443"; // HTTPS port is standard
            const std::string& apiKey = config.apiKey;
            // Example structure - adapt if your config stores the full path or different parts
            std::string target = config.apiBasePath +
                                 "?function=" + config.apiFunction +
                                 "&symbol=" + symbol +
                                 "&interval=" + config.apiInterval +
                                 "&apikey=" + apiKey;

            // Set up I/O context and SSL context
            net::io_context ioc;
            // auto ctx = createSSLContext();
            std::shared_ptr<ssl::context> ctx; // Placeholder - ensure createSSLContext is defined/accessible
            try {
                ctx = createSSLContext();
            } catch(const std::exception& ssl_ex) {
                Logger::getInstance().log("Failed to create SSL Context: " + std::string(ssl_ex.what()), Logger::LogLevel::ERROR);
                return ""; // Return empty on SSL setup failure
            }

            // These objects perform our I/O
            tcp::resolver resolver(ioc);
            ssl::stream<tcp::socket> stream(ioc, *ctx);

            if (!SSL_set_tlsext_host_name(stream.native_handle(), host.c_str())) // Use .c_str()
            {
                boost::system::error_code ec{static_cast<int>(::ERR_get_error()), boost::asio::error::get_ssl_category()};
                Logger::getInstance().log("Error setting SNI hostname: " + ec.message(), Logger::LogLevel::ERROR);
                throw boost::system::system_error{ec};
            }
            // Look up the domain name
            auto const results = resolver.resolve(host, "443");

            // Make the TCP connection
            Logger::getInstance().log("Connecting to " + host + ":" + port + "...", Logger::LogLevel::INFO);
            net::connect(stream.next_layer(), results.begin(), results.end());
            Logger::getInstance().log("TCP Connection established.", Logger::LogLevel::INFO);

            // --- Perform the SSL Handshake (ONCE) ---
            try
            {
                Logger::getInstance().log("Performing SSL Handshake...", Logger::LogLevel::INFO);
                stream.handshake(ssl::stream_base::client); // Verification happens here
                Logger::getInstance().log("SSL Handshake successful (with verification).", Logger::LogLevel::INFO);
            }
            catch (const boost::system::system_error &e)
            {
                // Log detailed verification/handshake errors
                Logger::getInstance().log("SSL Handshake Error (Verification Failed?): " + std::string(e.what()), Logger::LogLevel::ERROR);
                Logger::getInstance().log("Verification Error Code: " + std::to_string(e.code().value()) + ", Category: " + e.code().category().name(), Logger::LogLevel::ERROR);
                // Consider adding: ERR_print_errors_fp(stderr); for more OpenSSL details
                throw; // Re-throw to be caught by the outer catch
            }

            // Set up an HTTP GET request
            // Request intraday data with 1min interval
            Logger::getInstance().log("Sending HTTP GET request to target: " + target, Logger::LogLevel::INFO); // Log target
            http::request<http::string_body> req{http::verb::get, target, 11};
            req.set(http::field::host, host);
            req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
            req.prepare_payload();

            // Send the HTTP request
            http::write(stream, req);
            Logger::getInstance().log("HTTP request sent.", Logger::LogLevel::INFO);

            Logger::getInstance().log("Receiving HTTP response...", Logger::LogLevel::INFO);
            // This buffer is used for reading
            beast::flat_buffer buffer;

            // Declare a container to hold the response
            http::response<http::dynamic_body> res;

            // Receive the HTTP response
            http::read(stream, buffer, res);
            Logger::getInstance().log("HTTP response received.", Logger::LogLevel::INFO);

            // Convert to string
            response = beast::buffers_to_string(res.body().data());

            // Log the first 500 characters of the response for debugging
            std::string response_preview = response.substr(0, std::min<size_t>(500, response.size()));
            Logger::getInstance().log("API Response preview: " + response_preview + "...",
                                      Logger::LogLevel::INFO);

            // Gracefully close the stream
            beast::error_code ec;
            stream.shutdown(ec);
            if (ec == net::error::eof)
            {
                // Rationale: http://stackoverflow.com/questions/25587403/
                ec = {};
            }
            else if (ec == ssl::error::stream_truncated)
            {
                Logger::getInstance().log("SSL stream truncated during shutdown. Assuming successful data transfer.", Logger::LogLevel::WARNING);
                ec = {}; // Treat as success for our purpose.                                          Logger::LogLevel::WARNING);
            }
            else if (ec)
            {
                Logger::getInstance().log("SSL shutdown error: " + ec.message(), Logger::LogLevel::WARNING);
            }

            // timer.end();
            // timer.printTime();

            Logger::getInstance().log("Successfully fetched market data for " + symbol, Logger::LogLevel::INFO);
        }
        catch (const boost::system::system_error &bse)
        {
            Logger::getInstance().log("Boost.System error fetching market data: " + std::string(bse.what()) + " Code: " + bse.code().message(), Logger::LogLevel::ERROR);
        }
        catch (const std::exception &e)
        {
            Logger::getInstance().log("Standard exception fetching market data: " + std::string(e.what()), Logger::LogLevel::ERROR);
        }
        catch (...)
        {
            Logger::getInstance().log("Unknown error fetching market data.", Logger::LogLevel::ERROR);
        }

        return response;
    }
    // Fixed version with only the config parameter

    std::thread StartPeriodicFetching(const ServerConfig &config, SubscriptionManager &subManager)
    {
        // Set the global flag
        g_shouldContinueFetching = true;

        // Start the thread with just the config parameter
        return std::thread(DataUpdateTask, config, std::ref(subManager));
    }

    // Method to stop periodic fetching
    void StopPeriodicFetching()
    {
        g_shouldContinueFetching = false;
    }


    std::vector<MarketDataEntry> GetLatestData(const std::string &symbol)
    {
        // Use the global data cache instead of creating a new one
        return g_dataCache->getData(symbol);
    }
}