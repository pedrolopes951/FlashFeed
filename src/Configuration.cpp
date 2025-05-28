// Configuration.cpp
#include "Configuration.hpp"
#include "Logger.hpp"
#include <fstream>
#include <nlohmann/json.hpp>
#include <filesystem> 

using json = nlohmann::json;

AppConfig ConfigLoader::loadConfig(const std::string& filePath) {
    Logger::getInstance().log("Attempting to load configuration from: " + filePath, Logger::LogLevel::INFO);

    if (!std::filesystem::exists(filePath)) {
         throw std::runtime_error("Configuration file not found: " + filePath);
    }
    std::filesystem::path config_file_path_obj(filePath); // Store path object

    std::ifstream configFileStream(config_file_path_obj); // Use path object
    if (!configFileStream.is_open()) {
        throw std::runtime_error("Failed to open configuration file: " + filePath);
    }

    json configJson;
    try {
        configJson = json::parse(configFileStream);
    } catch (const json::parse_error& e) { /* ... */ }

    AppConfig config;

    try {
        config.logFilePath = configJson.value("/logging/log_file_path"_json_pointer, config.logFilePath);

        if (configJson.contains("server")) {
            const auto& serverJson = configJson["server"];
            config.serverConfig.port = serverJson.value("port", config.serverConfig.port);
            config.serverConfig.apiKey = serverJson.at("api_key").get<std::string>();
            config.serverConfig.symbols = serverJson.at("symbols").get<std::vector<std::string>>();

            config.serverConfig.apiRefreshSeconds = serverJson.value("api_refresh_seconds", config.serverConfig.apiRefreshSeconds);
            config.serverConfig.apiHost = serverJson.value("api_host", config.serverConfig.apiHost);
            config.serverConfig.apiBasePath = serverJson.value("api_base_path", config.serverConfig.apiBasePath);
            config.serverConfig.apiFunction = serverJson.value("api_function", config.serverConfig.apiFunction);
            config.serverConfig.apiInterval = serverJson.value("api_interval", config.serverConfig.apiInterval);

            if (serverJson.contains("csv_fallback_paths")) {
                config.serverConfig.symbolCSVPaths.clear();
                const auto& pathsJson = serverJson["csv_fallback_paths"];
                // Assume CSV paths in config are relative to the *project root*
                // Project root is likely the parent dir of the 'input' dir where config lives
                std::filesystem::path config_dir = config_file_path_obj.parent_path();
                std::filesystem::path project_root_dir = config_dir.parent_path(); // Go up one more level
                Logger::getInstance().log("Assuming project root for CSV paths: " + project_root_dir.string(), Logger::LogLevel::INFO);


                for (auto& [symbol, path_json] : pathsJson.items()) {
                     if (path_json.is_string()){
                         std::filesystem::path relative_csv_path = path_json.get<std::string>();
                         std::filesystem::path absolute_csv_path = project_root_dir / relative_csv_path;
                         absolute_csv_path = std::filesystem::absolute(absolute_csv_path).lexically_normal();

                         config.serverConfig.symbolCSVPaths[symbol] = absolute_csv_path.string();

                         Logger::getInstance().log("Resolved CSV path for " + symbol + ": " + absolute_csv_path.string(), Logger::LogLevel::INFO);
                     } else { 
                        Logger::getInstance().log("No 'csv_fallback_paths' found in server config.", Logger::LogLevel::WARNING);
                        config.serverConfig.symbolCSVPaths.clear();
                      }
                }
            } else {  }

            if (config.serverConfig.apiKey.empty()) {  throw std::runtime_error("Server 'api_key' cannot be empty."); }
            if (config.serverConfig.apiRefreshSeconds <= 0) {
                Logger::getInstance().log("Invalid 'api_refresh_seconds' <= 0. Using default 60.", Logger::LogLevel::WARNING);
                config.serverConfig.apiRefreshSeconds = 60; // Reset to default
           }
        } else { 
            Logger::getInstance().log("Configuration file missing 'server' section. Using defaults.", Logger::LogLevel::WARNING);
         }

        config.clientServerAddress = configJson.value("/client/server_address"_json_pointer, config.clientServerAddress);

    } catch (const json::exception& e) {  }

    Logger::getInstance().log("Configuration loaded successfully.", Logger::LogLevel::INFO);
    return config;
}