#include "ThreadPool.h"

ThreadPool::ThreadPool(size_t threads) {          // Threadpool constructor
    for(size_t i = 0; i < threads; ++i) {         // Tranversing the all pool
        workers.emplace_back([this] {             // Capturing the pointer to the current ThreadPool object
            while(true) {
                std::function<void()> task;       // Temp var where worker stores the task it takes from the queue, 'tasks'
                {
                    std::unique_lock<std::mutex> lock(this->queueMtx);
                    this->cv.wait(lock, [this] {  // Wakes up only if stop is true OR tasks is non-empty
                        return this->stop ||
                        !this->tasks.empty();
                    });

                    if(this->stop && this->tasks.empty()) {
                        return;                              // Terminates the current worker thread
                    }

                    task = std::move(this->tasks.front());   // Handed the first task from queue(tasks)
                    this->tasks.pop();                       // Removed that task from tasks
                }

                spaceAvailable.notify_one();

                try {
                  task();  // Execute the operation
                } catch(const std::exception& e) {
                  std::cerr << "Task failed:" << e.what() << '\n';
                } catch(...) {
                  std::cerr << "Task failed: unknown exception\n";
                }
            }
        });
    }
}

bool ThreadPool::enqueue(                           // Adds new work
    std::function<void()> task,
    std::chrono::milliseconds timeout
) {
    std::unique_lock<std::mutex> lock(queueMtx);

    bool available = spaceAvailable.wait_for(       // like select statement in Go
        lock, timeout, [this] { return stop || tasks.size() < maxQueueSize; });

    if (!available || stop) {
      return false;
    }

    tasks.push(std::move(task));

    lock.unlock();
    cv.notify_one();

    return true;
}

ThreadPool::~ThreadPool() {        // Threadpool destructor
    {
        std::lock_guard<std::mutex> lock(queueMtx);
        stop = true;
    }

    cv.notify_all();               // Wake up all threads so they can check the 'stop' flag
    spaceAvailable.notify_all();

    for(std::thread &worker : workers) {
        worker.join();             // Wait for everyone to finish
    }
}
