cmake_minimum_required(VERSION 3.16)

project(MiniDB VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(MiniDB
    src/main.cpp
    src/Database.cpp
    src/Logger.cpp
    src/ThreadPool.cpp
    src/Transaction.cpp
)

target_include_directories(MiniDB PRIVATE include)