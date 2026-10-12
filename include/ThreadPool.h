#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <functional>
#include <stdexcept>
#include <iostream>
#include <exception>
#include <chrono>

class ThreadPool {
private:
    std::vector<std::thread> workers;        // Stores all worker threads
    std::queue<std::function<void()>> tasks; // Queue of pending jobs (each of which is void)

    std::mutex queueMtx;
    std::condition_variable cv;             // - Work available
    std::condition_variable spaceAvailable; // - Queue space available

    const std::size_t maxQueueSize = 1000;
    bool stop = false;  // Tells whether the pool is shutting down

   public:
    ThreadPool(size_t threads);

    bool enqueue(
        std::function<void()> task,
        std::chrono::milliseconds timeout = std::chrono::milliseconds(100)
    );

    ~ThreadPool();
};

#endif
