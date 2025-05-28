#pragma once
#include <memory>
#include <boost/asio.hpp>
#include "DataParser.hpp"
#include <utility>
#include <unordered_map>
#include <string>
#include <boost/beast.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/asio/ssl.hpp>
#include <nlohmann/json.hpp>
#include <set>        
#include <mutex>      
#include <thread>    
#include <chrono>

namespace net = boost::asio;
using tcp = net::ip::tcp;



namespace MarketDataServer
{

  constexpr int DEFAULT_PORT = 8080;
  // constexpr auto API_REFRESH_INTERVAL = std::chrono::seconds(60); // Fetch data every 1 second

  struct ServerConfig
  {
    int port = DEFAULT_PORT;
    std::string apiKey;
    std::vector<std::string> symbols;
    std::unordered_map<std::string, std::string> symbolCSVPaths;

    int apiRefreshSeconds = 60; // Default refresh interval in seconds
    std::string apiHost = "www.alphavantage.co"; // Default API host
    std::string apiBasePath = "/query";          // Default API base path
    std::string apiFunction = "TIME_SERIES_INTRADAY"; // Default function
    std::string apiInterval = "1min";                // Default interval

  };

  class DataCache
  {
  public:
    void updateData(const std::string &symbol, const std::vector<MarketDataEntry> &data);
    std::vector<MarketDataEntry> getData(const std::string &symbol) const;

  private:
    std::unordered_map<std::string, std::vector<MarketDataEntry>> m_cache;
    mutable std::mutex m_mutex;
  };

  class SubscriptionManager
  {
  public:
    SubscriptionManager(const SubscriptionManager &) = delete;
    SubscriptionManager &operator=(const SubscriptionManager &) = delete;

    SubscriptionManager() = default;
    ~SubscriptionManager() = default;

    void addSubscription(const std::string &symbol, std::shared_ptr<tcp::socket> socket_ptr);

    void removeSubscription(const std::string &symbol, std::shared_ptr<tcp::socket> socket_ptr);

    void removeAllSubscriptions(std::shared_ptr<tcp::socket> socket_ptr);

    std::vector<std::shared_ptr<tcp::socket>> getSubscribers(const std::string &symbol);

  private:
    std::unordered_map<std::string, std::set<std::weak_ptr<tcp::socket>, std::owner_less<std::weak_ptr<tcp::socket>>>> m_subscriptions;
    std::mutex m_mutex; // Mutex to protect access to m_subscriptions
  };

  // Start the server with the given configuration
  void StartServer(const ServerConfig &config,SubscriptionManager& subManager);

  // Fetch data from Alpha Vantage API
  std::string FetchMarketData(const std::string &symbol, const MarketDataServer::ServerConfig& config);

  // Method for Startting periodic fetching
  std::thread StartPeriodicFetching(const ServerConfig &config, SubscriptionManager& subManager);

  // Method to stop periodic fetching
  void StopPeriodicFetching();

  // Get the latest data for a symbol
  std::vector<MarketDataEntry> GetLatestData(const std::string &symbol);

}
