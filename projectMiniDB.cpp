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
    std::ofstream logFile;                        // logFile is a file stream object (write only)
    std::mutex logMtx;
public:
    Logger(const std::string& filename) {
        logFile.open(filename, std::ios::app);    // Open in append mode
    }

    void log(const std::string& key, const std::string& val) {
        std::lock_guard<std::mutex> lock(logMtx); // Only one thread writes at a time
        logFile << key << "," << val << "\n";
        logFile.flush();                          // Ensure it actually writes to disk!
    }

    ~Logger() {
        logFile.close();
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

    // Separate stacks for different threads
    // std::thread::id is a type used to store 'id'
    std::unordered_map<std::thread::id, std::stack<Transaction>> transaction_stacks;
    std::mutex mtx;
    Logger logger{"db.log"}; // Important! otherwise, recover() will not work,, Opens the file

public:
    MiniDB() {
        recover();
    }

    // thread id finder
    static std::thread::id currentThread() {
        return std::this_thread::get_id();
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
        std::lock_guard<std::mutex> lock(mtx);

        // this_thread::get_id() returns current thread's id
        auto id = currentThread();
        auto it = transaction_stacks.find(id);

        // auto& txnStack = transaction_stacks[std::this_thread::get_id()]; omitted becpz this will create a stack if not available
        
        if(it == transaction_stacks.end()) { // if id not found
            return false;
        }

        return !it->second.empty();          // if id found, check for emptiness
    }

    void set(const std::string& key, const std::string& value) {
        logger.log(key, value); // Storing in file 
        {
            std::lock_guard<std::mutex> lock(mtx);

            // This will not create extra vac memo
            auto id = currentThread();
            auto it = transaction_stacks.find(id);

            if (it != transaction_stacks.end() && !it->second.empty()) {
                auto& current_txn = it->second.top();                       // If transaction stack is not empty, we are extracting the top one
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

    std::string get(const std::string& key) {
        std::lock_guard<std::mutex> lock(mtx);

        auto it = data.find(key);

        if(it != data.end()) {
            return it->second;
        }

        return "NULL";
    }

    void commit() {
        std::lock_guard<std::mutex> lock(mtx);

        // This will not create extra vac memo
        auto id = currentThread();
        auto it = transaction_stacks.find(id);


        if(it == transaction_stacks.end() || it->second.empty()) {
            return;
        }

            // If only one transaction
        if(it->second.size() == 1) {
            it->second.pop();                 // Remove the top element

            if(it->second.empty()) {          // If stack becomes empty, erase the whole stack
                transaction_stacks.erase(it);
            }
        } 
        else {
            // Save the child
            auto child = std::move(it->second.top());
            it->second.pop();

            auto& parent = it->second.top();

            for(const auto& [key, oldValue] : child.undo_log) {
                if(parent.undo_log.find(key) == parent.undo_log.end()) {
                    parent.undo_log[key] = oldValue;
                }
            }
        }
    }

    // Begin a new transaction
    void begin() {
        std::lock_guard<std::mutex> lock(mtx);

        auto& txnStack = transaction_stacks[currentThread()];

        // emplace() constructs the object directly inside the stack (No temp copy)
        txnStack.emplace(); 
    }

    // Rollback to the last transaction
    void rollback() {
        std::lock_guard<std::mutex> lock(mtx);
        auto id = currentThread();
        auto it = transaction_stacks.find(id);
        if(it != transaction_stacks.end() && !it->second.empty()) {
            auto& current_txn = it->second.top(); // If transaction stack is not empty, we are extracting the top one
            
            // Revert cahnges from the log
            for(auto const& [key, old_val] : current_txn.undo_log) {
                if (old_val == "__DELETE__") {
                    data.erase(key);          // If not present earlier, remove now
                } else {
                    data[key] = old_val;      // Restoring the old value
                }
            }
            it->second.pop();                 // Removal after rollback from stack

            if(it->second.empty()) {
                transaction_stacks.erase(it); // Removal of whole stack
            }
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
    ThreadPool pool(std::thread::hardware_concurrency()); // Create the pool of size = cores in the system

    for (int i=1; i<=1000; i++) {
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
            safePrint("Thread finished task for: " + std::to_string(i));
        });
    }

    // pool.enqueue([&mydb]() {
    //     mydb.set("A", "100");
    //     mydb.begin();
    //     mydb.set("A", "200");
    //     mydb.set("B", "500");
    //     mydb.rollback();
    //     safePrint(mydb.get("A"));
    //     safePrint(mydb.get("B"));
    //     });

    // pool.enqueue([&mydb]() {
    //     safePrint(mydb.get(std::to_string(70)));
    // });

    // After this, the ThreadPool will go out of scope, 
    // trigger the destructor, and shut down cleanly.
    return 0;
}