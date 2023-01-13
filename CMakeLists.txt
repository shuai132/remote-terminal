cmake_minimum_required(VERSION 3.16)

set(CMAKE_CXX_STANDARD 14)
add_compile_options(-Wall)

project(remote-terminal)

# asio
add_definitions(-DASIO_STANDALONE)
include_directories(thirdparty/asio/include)

# asio_net
add_subdirectory(thirdparty/asio_net asio_net EXCLUDE_FROM_ALL)
target_include_directories(asio_net INTERFACE ../thirdparty/asio/include)

add_subdirectory(client)
add_subdirectory(server)
