cmake_minimum_required(VERSION 3.16)
project(FlashFeed LANGUAGES CXX) 

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)


set(DATA_FOLDER "${CMAKE_CURRENT_SOURCE_DIR}/data")
message(STATUS "DATA_FOLDER set to: ${DATA_FOLDER}")

# --- Find Packages ---
find_package(Boost 1.74 REQUIRED COMPONENTS system thread filesystem date_time program_options)
message(STATUS "Boost Includes: ${Boost_INCLUDE_DIRS}")
message(STATUS "Boost Libraries: ${Boost_LIBRARIES}") 

find_package(OpenSSL REQUIRED)
message(STATUS "OpenSSL Includes: ${OpenSSL_INCLUDE_DIR}")
message(STATUS "OpenSSL Libraries: ${OpenSSL_LIBRARIES}") 

# Fetch nlohmann/json
include(FetchContent)
FetchContent_Declare(
  json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(json)

# --- Qt ---
find_package(Qt5 COMPONENTS Widgets REQUIRED) 

# --- Source File Groups ---
set(COMMON_SOURCES
    src/Logger.cpp
    src/Configuration.cpp
    src/DataParser.cpp
    src/BenchMark.cpp 
)


set(SERVER_SOURCES
    src/MarketDataServer.cpp
)


set(GUI_CLIENT_SOURCES
    src/gui/MainGui.cpp     
    src/gui/MainWindow.cpp
    src/gui/MarketDataWorker.cpp
    src/gui/MarketDataTableModel.cpp   
)


# --- Executables ---

# Server Executable
add_executable(Market_Parser_Server
    src/MainServer.cpp # You will create this from your old main.cpp (server part)
    ${COMMON_SOURCES}
    ${SERVER_SOURCES}
)
target_include_directories(Market_Parser_Server PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${json_SOURCE_DIR}/include
    ${Boost_INCLUDE_DIRS}
    ${OpenSSL_INCLUDE_DIR}
)
target_link_libraries(Market_Parser_Server PRIVATE
    Boost::system Boost::thread Boost::filesystem Boost::date_time Boost::program_options
    nlohmann_json::nlohmann_json
    Threads::Threads 
    OpenSSL::SSL OpenSSL::Crypto
)
target_compile_definitions(Market_Parser_Server PRIVATE "DATA_FOLDER=\"${DATA_FOLDER}\"") 


# GUI Client Executable

set(CMAKE_AUTOMOC ON)     # Enables automatic moc file generation
set(CMAKE_AUTOUIC ON)     # Enables automatic .ui file compilation (if you use them)
set(CMAKE_AUTORCC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

add_executable(Market_Parser_GUI_Client WIN32 
    ${GUI_CLIENT_SOURCES}
    src/Logger.cpp      
    src/DataParser.cpp
)
target_include_directories(Market_Parser_GUI_Client PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include 
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gui 
    ${json_SOURCE_DIR}/include          
    ${Boost_INCLUDE_DIRS}             
)
target_link_libraries(Market_Parser_GUI_Client PRIVATE
    Qt5::Widgets 
)
target_compile_definitions(Market_Parser_GUI_Client PRIVATE "DATA_FOLDER=\"${DATA_FOLDER}\"") 

# Testing 
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/test)
    add_subdirectory(test)
endif()