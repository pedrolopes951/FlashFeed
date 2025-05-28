#include "DataParser.hpp"
#include "Logger.hpp"
#include "BenchMark.hpp"
#include <algorithm> // for std::min
#include <nlohmann/json.hpp>
#include <vector>   
#include <string>    
#include <sstream>   
#include <fstream>   

// Used for Json parsing
using json = nlohmann::json;
// constexpr const int NUMELEMENTS = 10000;

bool compareMarketDataEntryTimestamps(const MarketDataEntry& a, const MarketDataEntry& b) {
    return a.m_timestamp < b.m_timestamp;
}



DataParserCSVAlphaAPI::DataParserCSVAlphaAPI(const std::string& CSVPath)
    : m_CSVPath(CSVPath)
{
}

bool DataParserCSVAlphaAPI::parseData()
{
    // Clear any previously parsed data
    m_data.clear();
        
    try {
        // Open the file
        std::ifstream file(m_CSVPath);
        if (!file.is_open()) {
            Logger::getInstance().log("File not Open: " + m_CSVPath, Logger::LogLevel::ERROR);
            return false;
        }
        
        // Read the entire file into memory for faster processing
        std::vector<char> buffer(std::istreambuf_iterator<char>(file), {});
        std::istringstream fileContent(std::string(buffer.data(), buffer.size()));
        
        std::string line;
        
        // Skip header line
        if (!std::getline(fileContent, line)) { // Read header
            Logger::getInstance().log("CSV file is empty or cannot read header.", Logger::LogLevel::ERROR);
            return false;
        }       
        Logger::getInstance().log("Header Line skipped successfully", Logger::LogLevel::INFO);
        
        // Process each line
        while (std::getline(fileContent, line)) {
            std::stringstream ss(line);
            std::string timestamp;
            double open, high, low, close, volume;
            
            // Parse the CSV line - expecting format: timestamp,open,high,low,close,volume
            if (std::getline(ss, timestamp, ',') && 
                ss >> open && ss.ignore(1) &&
                ss >> high && ss.ignore(1) &&
                ss >> low && ss.ignore(1) &&
                ss >> close && ss.ignore(1) &&
                ss >> volume) {
                
                m_data.emplace_back(timestamp, open, high, low, close, volume);
            }
            else {
                Logger::getInstance().log("Bad Line: " + line, Logger::LogLevel::WARNING);
            }
        }
        
        
        // Log successful parsing
        std::ostringstream logStream;
        logStream << "Successfully parsed " << m_data.size() << " rows from CSV.";
        Logger::getInstance().log(logStream.str(), Logger::LogLevel::INFO);

        if (!m_data.empty()) {
            Logger::getInstance().log("Sorting " + std::to_string(m_data.size()) + " CSV entries by timestamp...", Logger::LogLevel::INFO);
            std::sort(m_data.begin(), m_data.end(), compareMarketDataEntryTimestamps);
            Logger::getInstance().log("CSV data sorted.", Logger::LogLevel::INFO);
        }
        
        return !m_data.empty();
        
    } catch (const std::exception& e) {
        Logger::getInstance().log("Error parsing CSV: " + std::string(e.what()), Logger::LogLevel::ERROR);
        return false;
    }
}

const std::vector<MarketDataEntry>& DataParserCSVAlphaAPI::getData() const
{
    return m_data;
}

//----------------------------------------------
// DataParserJsonAlphaAPI Implementation
//----------------------------------------------

DataParserJsonAlphaAPI::DataParserJsonAlphaAPI(const std::string& jsonContent)
    : m_jsonContent(jsonContent)
{
}

bool DataParserJsonAlphaAPI::parseData() {
    // Timer timer; timer.start();
    m_data.clear();
    // m_data.reserve(NUMELEMENTS);

    try {
        json jsonData = json::parse(m_jsonContent);
        if (jsonData.contains("Information") || jsonData.contains("Error") || jsonData.contains("Note")) {
            // ... handle API message, return false ...
            std::string message;
            // ... extract message ...
            Logger::getInstance().log("API message: " + message, Logger::LogLevel::WARNING);
            return false;
        }

        // Determine time series key (Daily, 1min, etc.)
        std::string timeSeriesKey;
        const std::vector<std::string> possibleKeys = {
            "Time Series (1min)", "Time Series (5min)", "Time Series (15min)",
            "Time Series (30min)", "Time Series (60min)", "Time Series (Daily)"
        };
        for (const auto& key : possibleKeys) {
            if (jsonData.contains(key)) {
                timeSeriesKey = key;
                break;
            }
        }

        if (!timeSeriesKey.empty() && jsonData.contains(timeSeriesKey)) {
            const auto& timeSeries = jsonData[timeSeriesKey];
            m_data.reserve(timeSeries.size()); // Reserve based on actual size

            for (auto it = timeSeries.begin(); it != timeSeries.end(); ++it) {
                const std::string& timestamp = it.key();
                const auto& dataPoint = it.value();
                try { // Add try-catch for stod and .at
                    double open = std::stod(dataPoint.at("1. open").get<std::string>());
                    double high = std::stod(dataPoint.at("2. high").get<std::string>());
                    double low = std::stod(dataPoint.at("3. low").get<std::string>());
                    double close = std::stod(dataPoint.at("4. close").get<std::string>());
                    double volume = std::stod(dataPoint.at("5. volume").get<std::string>());
                    m_data.emplace_back(timestamp, open, high, low, close, volume);
                } catch (const std::exception& ex) { // Catch stod errors or missing keys from .at()
                     Logger::getInstance().log("Error parsing data point for timestamp " + timestamp + ": " + ex.what(), Logger::LogLevel::WARNING);
                     // Continue to next data point
                }
            }
        } else if (jsonData.is_array()) { // Fallback for simple array
            Logger::getInstance().log("Attempting to parse as simple JSON array.", Logger::LogLevel::INFO);
            m_data.reserve(jsonData.size());
            for (const auto& entry : jsonData) {
                if (entry.contains("timestamp") && /* ... other checks ... */ entry.contains("volume")) {
                    try { // Add try-catch for .at and .get
                        m_data.emplace_back(
                            entry.at("timestamp").get<std::string>(),
                            entry.at("open").get<double>(),
                            entry.at("high").get<double>(),
                            entry.at("low").get<double>(),
                            entry.at("close").get<double>(),
                            entry.at("volume").get<double>()
                        );
                    } catch (const std::exception& ex) {
                        Logger::getInstance().log("Error parsing array entry: " + std::string(ex.what()), Logger::LogLevel::WARNING);
                    }
                }
            }
        } else {
            Logger::getInstance().log("JSON format not recognized as Alpha Vantage API response or simple array.", Logger::LogLevel::WARNING);
            return false;
        }

      
        if (!m_data.empty()) {
            Logger::getInstance().log("Sorting " + std::to_string(m_data.size()) + " JSON entries by timestamp...", Logger::LogLevel::INFO);
            std::sort(m_data.begin(), m_data.end(), compareMarketDataEntryTimestamps);
            Logger::getInstance().log("JSON data sorted.", Logger::LogLevel::INFO);
        }


        return !m_data.empty();

    } catch (const json::parse_error& e) {  return false; }
    catch (const std::exception& e) {  return false; }
}

const std::vector<MarketDataEntry>& DataParserJsonAlphaAPI::getData() const
{
    return m_data;
}


std::unique_ptr<IDataParser> ParserFactory::createParser(const std::string& source)
{
    // Check file extension using C++17 compatible methods
    // For CSV files
    if (source.size() >= 4 && 
        (source.compare(source.size() - 4, 4, ".csv") == 0 || 
         source.compare(source.size() - 4, 4, ".CSV") == 0)) {
        return createCSVParser(source);
    }
    // For JSON files or JSON content
    else if ((source.size() > 0 && source[0] == '{') || // Starts with '{'
             (source.size() >= 5 && source.compare(source.size() - 5, 5, ".json") == 0) ||
             (source.size() >= 5 && source.compare(source.size() - 5, 5, ".JSON") == 0)) {
        return createJSONParser(source);
    }
    else {
        Logger::getInstance().log("Unknown data format: " + source, Logger::LogLevel::WARNING);
        // Default to CSV parser
        return createCSVParser(source);
    }
}

std::unique_ptr<IDataParser> ParserFactory::createCSVParser(const std::string& filePath)
{
    return std::make_unique<DataParserCSVAlphaAPI>(filePath);
}

std::unique_ptr<IDataParser> ParserFactory::createJSONParser(const std::string& jsonContent)
{
    return std::make_unique<DataParserJsonAlphaAPI>(jsonContent);
}



// Legacy

namespace ParsingFunctions {
    std::vector<MarketDataEntry> readCSV(const char* pathFileCSV)
    {
        auto parser = ParserFactory::createCSVParser(pathFileCSV);
        if (parser->parseData()) {
            return parser->getData();
        }
        return {};
    }
}