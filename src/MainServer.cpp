#include "MarketDataServer.hpp" 
#include "Logger.hpp"
#include "Configuration.hpp"
#include <fstream>
#include <string>
#include <vector>
#include <thread>
#include <iostream>
#include <filesystem>

#ifdef _WIN32
#include <windows.h> 
#elif defined(__linux__) || defined(__APPLE__)
#include <unistd.h> 
#include <limits.h> 
#if defined(__APPLE__) 
#include <mach-o/dyld.h>
#endif
#endif

std::filesystem::path get_executable_dir()
{
#ifdef _WIN32
    wchar_t path[MAX_PATH] = {0};
    GetModuleFileNameW(NULL, path, MAX_PATH);
    return std::filesystem::path(path).parent_path();
#elif defined(__linux__)
    char result[PATH_MAX];
    ssize_t count = readlink("/proc/self/exe", result, PATH_MAX);
    if (count > 0)
    {
        return std::filesystem::path(std::string(result, count)).parent_path(); 
    }
#elif defined(__APPLE__)
    char path[PATH_MAX];
    uint32_t size = sizeof(path);
    if (_NSGetExecutablePath(path, &size) == 0)
    {
        return std::filesystem::path(path).parent_path();
    }
    char actualpath[PATH_MAX];
    if (realpath(path, actualpath) != NULL) {
        return std::filesystem::path(actualpath).parent_path();
    }
#endif
    Logger::getInstance().log("Warning: Could not reliably determine executable directory. Falling back to current working directory.", Logger::LogLevel::WARNING);
    return std::filesystem::current_path();
}

int main(int argc, char *argv[])
{
    std::filesystem::path base_path = get_executable_dir();
    std::cout << "Server executable base path: " << base_path << std::endl;

    std::filesystem::path default_config_path_rel = std::filesystem::path("..") / "input" / "config.json";
    std::filesystem::path configFilePath = base_path / default_config_path_rel;
    configFilePath = std::filesystem::absolute(configFilePath).lexically_normal();

    std::string configArgPath;
    for (int i = 1; i < argc; ++i) // Check all args for --config
    {
        if (std::string(argv[i]) == "--config" && (i + 1 < argc) && argv[i+1][0] != '-')
        {
            configArgPath = argv[i + 1];
            break;
        }
    }

    if (!configArgPath.empty())
    {
        configFilePath = std::filesystem::absolute(configArgPath).lexically_normal();
        std::cout << "Server using config path from argument: " << configFilePath << std::endl;
    }
    else
    {
        std::cout << "Server using default config path: " << configFilePath << std::endl;
    }

    // --- Load Configuration FIRST ---
    AppConfig appConfig;
    try
    {
        appConfig = ConfigLoader::loadConfig(configFilePath.string());
        Logger::getInstance().setLogFile(appConfig.logFilePath);
        Logger::getInstance().log("Server application starting with config: " + configFilePath.string(), Logger::LogLevel::INFO);
    }
    catch (const std::exception &e)
    {
        std::cerr << "SERVER FATAL ERROR loading configuration '" << configFilePath.string() << "': " << e.what() << std::endl;
        Logger::getInstance().log("SERVER FATAL ERROR loading configuration '" + configFilePath.string() + "': " + e.what(), Logger::LogLevel::ERROR);
        return 1;
    }

    // --- Run as Server ---
    Logger::getInstance().log("Running in Server mode.", Logger::LogLevel::INFO);
    std::cout << "Starting Market Data Server..." << std::endl;

    MarketDataServer::ServerConfig &config = appConfig.serverConfig;
    MarketDataServer::SubscriptionManager subscriptionManager;
    std::thread fetchThread = MarketDataServer::StartPeriodicFetching(config, subscriptionManager);

    MarketDataServer::StartServer(config, subscriptionManager); // This will block until server stops

    // --- Cleanup ---
    Logger::getInstance().log("Server has stopped listening. Cleaning up...", Logger::LogLevel::INFO);
    MarketDataServer::StopPeriodicFetching(); // Signal the fetching thread to stop
    if (fetchThread.joinable())
    {
        Logger::getInstance().log("Waiting for data fetching thread to join...", Logger::LogLevel::INFO);
        fetchThread.join(); // Wait for the thread to complete its current cycle and exit
        Logger::getInstance().log("Data fetching thread joined.", Logger::LogLevel::INFO);
    }
    Logger::getInstance().log("Server shutdown complete.", Logger::LogLevel::INFO);
    Logger::getInstance().log("Server application exiting normally.", Logger::LogLevel::INFO);
    return 0;
}