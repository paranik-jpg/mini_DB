#include "Logger.h"
#include "ThreadPool.h"
#include "Database.h"

#include <iostream>

std::mutex coutMtx;
void safePrint(const std::string& msg) { // For clear output in terminal
        std::lock_guard<std::mutex> lock(coutMtx);
        std::cout << msg << std::endl;
}

int main() {
    Logger logger("db.log");
    Database mydb(logger);
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