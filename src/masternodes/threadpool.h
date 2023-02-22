#ifndef DEFI_MASTERNODES_THREADPOOL_H
#define DEFI_MASTERNODES_THREADPOOL_H

#include <boost/asio.hpp>
#include <condition_variable>

static const int DEFAULT_DFTX_WORKERS=0;

// Until C++20x concurrency impls make it into standard, std::future and std::async impls
// doesn't have the primitives needed for working with many at the same time efficiently
// So, for now, we use a queue based approach for the thread pool

// Simple boost thread pool with N threads that's kept around for now
// Additionally, later task pool can abstract task group entirely, by
// forwarding the post args into Post method of a child group created
// from the TaskPool

class TaskPool {
    public:
        explicit TaskPool(size_t size);
        void Shutdown();
        [[nodiscard]] size_t GetAvailableThreads() const { return size; }
        boost::asio::thread_pool pool;

    private:
        size_t size;
};

void InitDfTxGlobalTaskPool();
void ShutdownDfTxGlobalTaskPool();

class TaskGroup {
    public:
        void AddTask();
        void RemoveTask();
        void WaitForCompletion();
    private:
        std::atomic<uint64_t> tasks{0};
        std::mutex cv_m;
        std::condition_variable cv;
};

extern std::unique_ptr<TaskPool> DfTxTaskPool;

#endif  // DEFI_MASTERNODES_THREADPOOL_H
