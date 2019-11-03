project(test_ftp_server LANGUAGES CXX)

add_executable(test_ftp_server
    "Utility.cpp"
    "main.cpp"
)

target_link_libraries(test_ftp_server PRIVATE lib)
target_include_directories(test_ftp_server PRIVATE ${PROJECT_SOURCE_DIR})
add_test(NAME test_ftp_server COMMAND test_ftp_server)
