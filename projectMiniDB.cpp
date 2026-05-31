#include <iostream>
#include <unordered_map>
#include <vector>
#include <stack>
#include <mutex>
#include <thread>
#include <functional> // For function wrapper
#include <queue>
#include <condition_variable> 
#include <fstream>    // For file handling
#include <sstream>    // Include this for stringstream

class Logger {
    std::ofstream logFile;                     // logFile is a file stream object (write only)
public:
    Logger(const std::string& filename) {
        logFile.open(filename, std::ios::app); // Open in append mode
    }

    void log(const std::string& key, const std::string& val) {
        logFile << key << "," << val << "\n";
        logFile.flush();                       // Ensure it actually writes to disk!
    }
};

class ThreadPool {
private:
    std::vector<std::thread> workers;        // Stores all worker threads
    std::queue<std::function<void()>> tasks; // Queue of pending jobs (each of which is void)
    std::mutex queueMtx;
    std::condition_variable cv;
    bool stop = false;                       // Tells whether the pool is shutting down

public:
    ThreadPool(size_t threads) {          // Threadpool constructor
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

    void enqueue(std::function<void()> task) { // Adds new work
        {
            std::lock_guard<std::mutex> lock(queueMtx);
            tasks.push(std::move(task));
        }
        cv.notify_one();
    }

    ~ThreadPool() {        // Threadpool destructor
        {
            std::lock_guard<std::mutex> lock(queueMtx);
            stop = true;
        }
        cv.notify_all();   // Wake up all threads so they can check the 'stop' flag
        for(std::thread &worker : workers) {
            worker.join(); // Wait for everyone to finish
        }
    }
};

class MiniDB {
private:
    std::unordered_map<std::string, std::string> data;
    struct Transaction {
        std::unordered_map<std::string, std::string> undo_log;
    };
    std::stack<Transaction> transaction_stack;
    std::mutex mtx;
    Logger logger{"db.log"}; // Important! otherwise, recover() will not work,, Opens the file

public:
    MiniDB() {
        recover();
    }

    void recover() {
        std::ifstream logFile("db.log");
        std::string line;

        // Lock the mutex because we are modifying 'data' during recovery
        std::lock_guard<std::mutex> lock(mtx);
        while(std::getline(logFile, line)) {
            std::stringstream ss(line);
            std::string key, value;

            // Read until the comma
            if(std::getline(ss, key, ',') && std::getline(ss, value)) {
                data[key] = value; // Reconstruct the state
            }
        }
    }

    bool isTransactionActive () {
        return (!transaction_stack.empty());
    }

    void set(std::string key, std::string value) {
        logger.log(key, value); // Storing in file 
        {
            std::lock_guard<std::mutex> lock(mtx);
            if (isTransactionActive()) {
                auto& current_txn = transaction_stack.top();                       // If transaction stack is not empty, we are extracting the top one
                if(current_txn.undo_log.find(key) == current_txn.undo_log.end()) { // Not found in logs
                    if(data.find(key) != data.end()) {                             // Found in data
                        current_txn.undo_log[key] = data[key];                     // Backup
                    } else {
                        current_txn.undo_log[key] = "__DELETE__";                  // This will be deleted on rollback, becoz we created this key, new 
                    }
                }
            }
            data[key] = value;
        }
    }

    std::string get(std::string key) {
        std::lock_guard<std::mutex> lock(mtx);
        return data.count(key) ? data[key] : "NULL";
    }

    void commit() {
        transaction_stack.pop();
    }

    // Begin a new transaction
    void begin() {
        std::lock_guard<std::mutex> lock(mtx);
        transaction_stack.push(Transaction()); // Initialize a new transaction object
    }

    // Rollback to the last transaction
    void rollback() {
        std::lock_guard<std::mutex> lock(mtx);
        if(isTransactionActive()) {
            auto& current_txn = transaction_stack.top(); // If transaction stack is not empty, we are extracting the top one
            
            // Revert cahnges from the log
            for(auto const& [key, old_val] : current_txn.undo_log) {
                if (old_val == "__DELETE__") {
                    data.erase(key);     // If not present earlier, remove now
                } else {
                    data[key] = old_val; // Restoring the old value
                }
            }
            transaction_stack.pop();     // Removal after rollback from stack
        }
    }
};

std::mutex coutMtx;
void safePrint(const std::string& msg) { // For clear output in terminal
        std::lock_guard<std::mutex> lock(coutMtx);
        std::cout << msg << std::endl;
}

int main() {
    MiniDB mydb;
    ThreadPool pool(4); // Create the pool

    for (int i=1; i<=100; i++) {
        pool.enqueue([&mydb, i]() {
            if(i%3==0) {
                mydb.begin();
            }
            else if(i%3==1) {
                mydb.set(std::to_string(i), std::to_string(i*i));
            }
            else {
                mydb.rollback();
            }
            safePrint("Therad finished task for: " + std::to_string(i));
        });
    }

    // After this, the ThreadPool will go out of scope, 
    // trigger the destructor, and shut down cleanly.
    return 0;
}