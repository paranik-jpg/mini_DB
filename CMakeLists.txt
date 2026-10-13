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


include(CTest)

if(BUILD_TESTING)
    add_executable(MiniDB_WAL_Test
        tests/wal_recovery_test.cpp
        src/Database.cpp
        src/Logger.cpp
        src/Transaction.cpp
    )

    target_include_directories(MiniDB_WAL_Test PRIVATE include)
    target_link_libraries(MiniDB_WAL_Test PRIVATE Threads::Threads)

    target_compile_options(MiniDB_WAL_Test PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )

    add_executable(MiniDB_TransactionIsolation_Test
        tests/transaction_isolation_test.cpp
        src/Database.cpp
        src/Logger.cpp
        src/Transaction.cpp
    )

    target_include_directories(MiniDB_TransactionIsolation_Test PRIVATE include)
    target_link_libraries(MiniDB_TransactionIsolation_Test PRIVATE Threads::Threads)

    target_compile_options(MiniDB_TransactionIsolation_Test PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )

    add_executable(MiniDB_WALFailure_Test
        tests/wal_failure_test.cpp
        src/Database.cpp
        src/Logger.cpp
        src/Transaction.cpp
    )

    target_include_directories(MiniDB_WALFailure_Test PRIVATE include)
    target_link_libraries(MiniDB_WALFailure_Test PRIVATE Threads::Threads)

    target_compile_options(MiniDB_WALFailure_Test PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )

    add_test(NAME MiniDB_WAL_Failure COMMAND MiniDB_WALFailure_Test)

    add_test(NAME MiniDB_WAL_Recovery COMMAND MiniDB_WAL_Test)

    add_test(NAME MiniDB_TransactionIsolation COMMAND MiniDB_TransactionIsolation_Test)

endif()
