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
    // Stores all worker threads
    std::vector<std::thread> workers;        
    // Queue of pending jobs (each of which is void)
    std::queue<std::function<void()>> tasks; 

    std::mutex queueMtx;

    // Work available ?!
    std::condition_variable cv;
    // Queue space available ?!
    std::condition_variable spaceAvailable; 

    const std::size_t maxQueueSize = 1000;
    // Tells whether the pool is shutting down
    bool stop = false; 

   public:
    ThreadPool(size_t threads);

    bool enqueue(
        std::function<void()> task,
        std::chrono::milliseconds timeout = std::chrono::milliseconds(100)
    );

    ~ThreadPool();
};

#endif
