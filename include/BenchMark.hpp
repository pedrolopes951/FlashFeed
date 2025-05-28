#pragma once
#include <chrono>

using time_unit = std::chrono::_V2::system_clock::time_point;

class Timer
{
public:
    Timer(/* args */);
    ~Timer();
    void start();
    void end();
    void printTime();
private:
    time_unit m_start;
    time_unit m_end;
    std::chrono::duration<double> m_total;

};

