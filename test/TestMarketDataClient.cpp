#include <iostream>
#include <boost/asio.hpp>

using namespace boost::asio;
using ip::tcp;

void readMarketData(const std::string &server_address, int port) {
    try {
        io_service io;
        tcp::socket socket(io); // creates a socket to estabislh the endpoint between two connections
        socket.connect(tcp::endpoint(ip::address::from_string(server_address), port)); // Initiates a TCP handshake with the server.

        std::cout << "Connected to Market Data Server" << std::endl;

        boost::asio::streambuf buffer; 
        std::istream input_stream(&buffer); 
        std::string line;

        while (true) {
            boost::asio::read_until(socket, buffer, "\n"); //Waits until a full line is received.
            std::getline(input_stream, line); // Extracts the message from the buffer.

            // Corrected: Actually print the received data
            std::cout << "Received Market Data: " << line << std::endl;
        }

    } catch (std::exception &e) {
        std::cout << "Market Data Client Error: " << e.what() << std::endl;
    }
}

int main() {
    readMarketData("127.0.0.1", 8080); // Connect to local market data server
    return 0;
}

