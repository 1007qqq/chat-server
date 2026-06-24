cmake_minimum_required(VERSION 3.20)

project(EnterpriseIMCpp VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

add_executable(chat_server
    cpp_server/src/main.cpp
    cpp_server/src/http.cpp
    cpp_server/src/app.cpp
)

target_include_directories(chat_server PRIVATE cpp_server/include)
target_compile_options(chat_server PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    $<$<CONFIG:Debug>:-g -Og>
    $<$<CONFIG:Release>:-O2 -DNDEBUG>
)
target_link_libraries(chat_server PRIVATE Threads::Threads)
