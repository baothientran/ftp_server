project(lib LANGUAGES CXX)

set(src
    "Utility.cpp"
    "Socket.cpp"
    "FtpSession.cpp")

set(header
    "Utility.h"
    "Socket.h"
    "FtpSession.h")

find_package (Threads)

add_library(lib
    ${src}
    ${header}
)

target_link_libraries(lib ${CMAKE_THREAD_LIBS_INIT})


target_compile_features(lib PUBLIC cxx_std_14)
target_include_directories(lib PUBLIC ${PROJECT_SOURCE_DIR})

add_executable(bin
    "main.cpp"
)
target_link_libraries(bin PRIVATE lib)
