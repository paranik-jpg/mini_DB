#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <functional>

class ThreadPool {
private:
    std::vector<std::thread> workers;        // Stores all worker threads
    std::queue<std::function<void()>> tasks; // Queue of pending jobs (each of which is void)
    std::mutex queueMtx;
    std::condition_variable cv;
    bool stop = false;                       // Tells whether the pool is shutting down

public:
    ThreadPool(size_t threads);

    void enqueue(std::function<void()> task);

    ~ThreadPool();
};

#endif