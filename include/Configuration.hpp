#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <stdexcept> 
#include "MarketDataServer.hpp" 

// Define a structure to hold all configuration parameters
struct AppConfig {
    std::string logFilePath = "market_data_log.txt"; // Default log path
    MarketDataServer::ServerConfig serverConfig;     // Nested server config
    std::string clientServerAddress = "127.0.0.1";   // Default client target address
    // Client port will default to serverConfig.port unless overridden (we can add later if needed)
};

// Class responsible for loading configuration
class ConfigLoader {
public:
    // Loads configuration from the specified JSON file.
    // Throws std::runtime_error or nlohmann::json::exception on failure.
    static AppConfig loadConfig(const std::string& filePath);
};