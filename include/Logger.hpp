#pragma once

#include <iostream>
#include <string>
#include <fstream>
#include <mutex>


class Logger
{
public:
enum class LogLevel
{
    INFO,
    WARNING,
    ERROR
};

    Logger(const Logger&)=delete;
    Logger& operator=(const Logger&) = delete;

    static Logger& getInstance()
    {
        static Logger instance;
        return instance;
    }


    void log(const std::string& message, LogLevel level);
    void setLogFile(const std::string& logFile);

private:
    std::ofstream m_logStream;
    std::mutex logMutex;

    Logger(/* args */)=default;
    ~Logger();

};

