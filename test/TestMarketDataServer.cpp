#include <iostream>
#include <boost/asio.hpp>
#include <thread>
#include <vector>
#include <memory>  // Include for shared_ptr


using namespace boost::asio;
using ip::tcp;

// Function to send fake market data
void market_data_feed(std::shared_ptr<tcp::socket> socket) {
    std::vector<std::string> market_data = {
        "2025-01-16T09:00:00.123,100.5,200\n",
        "2025-01-16T09:00:01.456,101.0,150\n",
        "2025-01-16T09:00:02.789,99.8,300\n",
        "2025-01-16T09:00:03.456,102.3,250\n"
    };

    try {
        while (true) {
            for (const auto& data : market_data) {
                boost::system::error_code ignored_error;
                write(*socket, buffer(data), ignored_error);
                std::this_thread::sleep_for(std::chrono::milliseconds(500)); // Simulate real-time updates
            }
        }
    } catch (std::exception &e) {
        std::cerr << "Market data feed error: " << e.what() << std::endl;
    }
}

int main() {
    try {
        io_service io; // Hanldesasync I/O operations
        tcp::acceptor acceptor(io, tcp::endpoint(tcp::v4(), 8080)); // Crweates a listening sockt on port 8080
        std::cout << "Market Data Server Running on Port 8080..." << std::endl;

        while (true) {
            auto socket = std::make_shared<tcp::socket>(io); //  Creates a new socket for the client
            acceptor.accept(*socket); // Block until a client connects
            std::cout << "Client Connected!" << std::endl;
            std::thread([socket]() { market_data_feed(socket); }).detach();
        }
    } catch (std::exception &e) {
        std::cerr << "Server error: " << e.what() << std::endl;
    }

    return 0;
}

