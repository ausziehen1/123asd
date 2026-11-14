#pragma once
#include <condition_variable>
#include <cstdint>
#include <mutex>
// Every database operation receives a ticket. Waiting operations cannot pass
// one another, including when several HTTP worker threads arrive together.
class FifoMutex {
    std::mutex mutex_;
    std::condition_variable ready_;
    std::uint64_t issued_ = 0, serving_ = 0;
public:
    void lock() { std::unique_lock<std::mutex> lock(mutex_); auto ticket = issued_++; ready_.wait(lock, [&] {return ticket == serving_;}); }
    void unlock() { {std::lock_guard<std::mutex> lock(mutex_); ++serving_;} ready_.notify_all(); }
};
