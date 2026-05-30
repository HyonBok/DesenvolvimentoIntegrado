cmake_minimum_required(VERSION 3.16)
project(IntegratedSystemsDevelopmentCpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(recon_client
    client/client.cpp
)

add_executable(recon_server
    server/server.cpp
    server/recon.cpp
    server/image_utils.cpp
)

target_include_directories(recon_server PRIVATE server)

if(WIN32)
    target_link_libraries(recon_client PRIVATE ws2_32)
    target_link_libraries(recon_server PRIVATE ws2_32 psapi)
endif()
