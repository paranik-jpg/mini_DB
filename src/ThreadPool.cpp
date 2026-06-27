#include <ThreadPool.h>

ThreadPool::ThreadPool(size_t threads) {          // Threadpool constructor
    for(size_t i=0; i<threads; ++i) { // Tranversing the all pool
        workers.emplace_back([this] { // Capturing the pointer to the current ThreadPool object
            while(true) {
                std::function<void()> task; // Temp var where worker stores the task it takes from the queue
                {
                    std::unique_lock<std::mutex> lock(this->queueMtx);
                    this->cv.wait(lock, [this]{ return this->stop || !this->tasks.empty(); }); // Lock until stop is true OR tasks is non-empty
                    if(this->stop && this->tasks.empty()) return;
                    task = std::move(this->tasks.front());                                     // Handed the first task from queue(tasks)
                    this->tasks.pop();                                                         // Removed that task from tasks
                }
                task(); // Execute the DB operation
            }
        });
    }
}

void ThreadPool::enqueue(std::function<void()> task) { // Adds new work
    {
        std::lock_guard<std::mutex> lock(queueMtx);
        tasks.push(std::move(task));
    }
    cv.notify_one();
}

ThreadPool::~ThreadPool() {        // Threadpool destructor
    {
        std::lock_guard<std::mutex> lock(queueMtx);
        stop = true;
    }
    cv.notify_all();   // Wake up all threads so they can check the 'stop' flag
    for(std::thread &worker : workers) {
        worker.join(); // Wait for everyone to finish
    }
}
