# Set minimum CMake version
cmake_minimum_required(VERSION 3.10)

# Add executable for TestMarketDataServer
add_executable(TestMarketDataServer TestMarketDataServer.cpp)

# Add executable for TestMarketDataClient
add_executable(TestMarketDataClient TestMarketDataClient.cpp)

# Link necessary libraries (Boost and pthread)
target_link_libraries(TestMarketDataServer pthread boost_system)
target_link_libraries(TestMarketDataClient pthread boost_system)
