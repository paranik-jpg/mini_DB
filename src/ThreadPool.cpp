#include "ThreadPool.h"

// Constructor
ThreadPool::ThreadPool(size_t threads) {  
    // Tranverse the pool
    for(size_t i = 0; i < threads; ++i) {
        // Capture the pointer to the current ThreadPool object
        workers.emplace_back([this] {             
            while(true) {
                // task => temp var where worker stores the task it takes from the queue
                std::function<void()> task;       
                {
                    std::unique_lock<std::mutex> lock(this->queueMtx);
                    this->cv.wait(lock, [this] {  
                        return this->stop ||
                        !this->tasks.empty();
                    });

                    if(this->stop && this->tasks.empty()) {
                        // Terminate the current worker thread
                        return;                              
                    }

                    // Hand the first task from queue(tasks)
                    task = std::move(this->tasks.front());   
                    this->tasks.pop();                       
                }

                spaceAvailable.notify_one();

                try {
                    // Execute the operation
                    task();  
                } catch(const std::exception& e) {
                    std::cerr << "Task failed:" << e.what() << '\n';
                } catch(...) {
                    std::cerr << "Task failed: unknown exception\n";
                }
            }
        });
    }
}

// Adds new work
bool ThreadPool::enqueue(std::function<void()> task, std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(queueMtx);

    // This is like select statements in Go
    bool available = spaceAvailable.wait_for(lock, timeout, [this] { 
        return stop || tasks.size() < maxQueueSize; 
    });

    if (!available || stop) {
        return false;
    }

    tasks.push(std::move(task));

    lock.unlock();
    cv.notify_one();

    return true;
}

// Destructor
ThreadPool::~ThreadPool() {        
    {
        std::lock_guard<std::mutex> lock(queueMtx);
        stop = true;
    }

    // Wake up all threads so they can check the 'stop' flag
    cv.notify_all();               
    spaceAvailable.notify_all();

    // Wait for everyone to finish
    for(std::thread &worker : workers) {
        worker.join();             
    }
}
