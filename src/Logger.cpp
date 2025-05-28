#include "Logger.hpp"


Logger::~Logger()
{
    if (m_logStream.is_open())
    {
        m_logStream.close();
    }
    
}

void Logger::log(const std::string& message, LogLevel level)
    {
        std::lock_guard<std::mutex> lock(logMutex); // Thread Safety

        // Checks if log file if not, then lets put on the cerr output stream
        std::ostream& stream = m_logStream.is_open() ? m_logStream : std::cerr;
        
        switch (level)
        {
        case LogLevel::INFO :
            stream << "[INFO]";
            break;
        case LogLevel::WARNING :
            stream << "[WARNING]";
            break;
        case LogLevel::ERROR:
            stream << "[ERROR]";
            break;
        }
        stream << message << std::endl;
    }

    void Logger::setLogFile(const std::string& logFile)
    {
        std::lock_guard<std::mutex> lock(logMutex); // Thread Safety
        if (m_logStream.is_open())
        {
            m_logStream.close();
        }        
        m_logStream.open(logFile,std::ios::app);
        if(!m_logStream.is_open())
        {
            throw std::runtime_error("Error: Unable to open log file" + logFile);
        }

    }