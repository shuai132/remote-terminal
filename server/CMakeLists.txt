project(terminal_server)

add_executable(${PROJECT_NAME} main.cpp)
target_link_libraries(${PROJECT_NAME} asio_net)

add_executable(${PROJECT_NAME}_nc main_nc.cpp)
