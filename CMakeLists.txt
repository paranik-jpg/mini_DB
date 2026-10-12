cmake_minimum_required(VERSION 3.16)

project(MiniDB VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)

option(ENABLE_SANITIZERS "Enable AddressSanitizer and UBSan" OFF)

add_executable(MiniDB
    src/main.cpp
    src/Database.cpp
    src/Logger.cpp
    src/ThreadPool.cpp
    src/Transaction.cpp
)

target_include_directories(MiniDB PRIVATE include)
target_link_libraries(MiniDB PRIVATE Threads::Threads)

target_compile_options(MiniDB PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

if(ENABLE_SANITIZERS)
    target_compile_options(MiniDB PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
        -g
    )
    target_link_options(MiniDB PRIVATE
        -fsanitize=address,undefined
    )
endif()
