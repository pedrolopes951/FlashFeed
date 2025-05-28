#include "BenchMark.hpp"
#include "Logger.hpp"

Timer::Timer()
{
}

Timer::~Timer()
{
}

void Timer::start()
{
    m_start = std::chrono::high_resolution_clock::now();
}

void Timer::end()
{
    m_end =  std::chrono::high_resolution_clock::now();
    m_total = static_cast<std::chrono::duration<double>>(m_end-m_start);
}

void Timer::printTime()
{
Logger::getInstance().log("Parsing Took: " + std::to_string(m_total.count()) + " seconds", Logger::LogLevel::INFO);

}