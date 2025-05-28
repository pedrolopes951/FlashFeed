#pragma once
#include <fstream>
#include <sstream>
#include <vector>
#include <cstdint>
#include <string>
#include <memory> 
#include <nlohmann/json.hpp>
#include "Logger.hpp"



struct MarketDataEntry
{
    std::string m_timestamp;
    double m_open;
    double m_high;
    double m_low;
    double m_close;
    double m_volume;

    MarketDataEntry() = default;
    MarketDataEntry(const std::string &timestamp, double open, double high, double low, double close, double volume)
        : m_timestamp(timestamp), m_open(open), m_high(high), m_low(low), m_close(close), m_volume(volume) {}

    // Virutal destrocutor for proper clensing out
    ~MarketDataEntry() = default;
};

inline void to_json(nlohmann::json& j, const MarketDataEntry& entry) {
    j = nlohmann::json{
        {"timestamp", entry.m_timestamp},
        {"open", entry.m_open},
        {"high", entry.m_high},
        {"low", entry.m_low},
        {"close", entry.m_close},
        {"volume", entry.m_volume}
    };
}

inline void from_json(const nlohmann::json& j, MarketDataEntry& entry) {
    try {
        j.at("timestamp").get_to(entry.m_timestamp);
        j.at("open").get_to(entry.m_open);
        j.at("high").get_to(entry.m_high);
        j.at("low").get_to(entry.m_low);
        j.at("close").get_to(entry.m_close);
        j.at("volume").get_to(entry.m_volume);
    } catch (const nlohmann::json::exception& e) {
        Logger::getInstance().log("JSON parsing error for MarketDataEntry: " + std::string(e.what()), Logger::LogLevel::ERROR);
        throw; // Re-throw the json exception
    }
}


class IDataParser
{
public:
    IDataParser() = default; 
    virtual ~IDataParser() = default;

    virtual bool parseData() = 0; 

    virtual const std::vector<MarketDataEntry> &getData() const = 0;

    IDataParser(const IDataParser &) = delete;
    IDataParser &operator=(const IDataParser &) = delete;
};

class DataParserCSVAlphaAPI : public IDataParser
{
public:
    explicit DataParserCSVAlphaAPI(const std::string &CSVPath);
    virtual const std::vector<MarketDataEntry> &getData() const override;
    virtual bool parseData() override;

private:
    std::string m_CSVPath;
    std::vector<MarketDataEntry> m_data;
};

class DataParserJsonAlphaAPI : public IDataParser
{
public:
    explicit DataParserJsonAlphaAPI(const std::string &jsonContent);
    virtual const std::vector<MarketDataEntry> &getData() const override;
    virtual bool parseData() override;

private:
    std::string m_jsonContent;
    std::vector<MarketDataEntry> m_data;
};

/**
 * @brief Factory class for creating appropriate parsers
 */
class ParserFactory
{
public:
    // Creates a parser based on file extension or content type
    static std::unique_ptr<IDataParser> createParser(const std::string &source);

    // Specific factory methods
    static std::unique_ptr<IDataParser> createCSVParser(const std::string &filePath);
    static std::unique_ptr<IDataParser> createJSONParser(const std::string &jsonContent);
};

namespace ParsingFunctions
{

    // Function to parse the file locatedin the pathFileCSV and return a const object of the DataParserCSVAlphaAPI
    std::vector<MarketDataEntry> readCSV(const char *pathFileCSV);

};
