cmake_minimum_required(VERSION 3.5)

project(ftp_server LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(ftp_server)
add_subdirectory(test)
add_test(NAME test_ftp_server COMMAND test_ftp_server)
