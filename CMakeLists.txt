cmake_minimum_required(VERSION 3.27)
project(webserver)

set(CMAKE_CXX_STANDARD 17)
set(libs_DIR ${CMAKE_CURRENT_SOURCE_DIR}/libs)

add_library(cpp_redis STATIC IMPORTED)
add_library(tacopie STATIC IMPORTED)

set_target_properties(cpp_redis PROPERTIES IMPORTED_LOCATION
        ${libs_DIR}/libcpp_redis.a)

set_target_properties(tacopie PROPERTIES IMPORTED_LOCATION
        ${libs_DIR}/libtacopie.a)

target_include_directories(cpp_redis INTERFACE
        ${libs_DIR}/includes)

add_executable(webserver
        main.cpp
        server.cpp
        socket/socket.cpp
        server.cpp
        Logger.h
        Exceptions.h
)

target_link_libraries(
        webserver
        cpp_redis
        tacopie)
