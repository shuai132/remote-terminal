project(terminal_client)

add_executable(${PROJECT_NAME} main.cpp)
target_link_libraries(${PROJECT_NAME} asio_net)
