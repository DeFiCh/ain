#include <masternodes/threadpool.h>

#include <logging.h>
#include <util/system.h>


TaskPool::TaskPool(size_t size): pool{size}, size{size} {}

void TaskPool::Shutdown() {
    pool.wait();
}

void InitDfTxGlobalTaskPool() {
    auto threadPoolThreads = gArgs.GetArg("-dftxworkers", DEFAULT_DFTX_WORKERS);
    LogPrintf("DfTxTaskPool: Init (%d)\n", threadPoolThreads);
    if (threadPoolThreads <= 0) {
        auto n = GetNumCores() - 1;
        threadPoolThreads = std::max(1, n);
    }
    LogPrintf("DfTxTaskPool: Size: %d\n", threadPoolThreads);
    DfTxTaskPool = std::make_unique<TaskPool>(static_cast<size_t>(threadPoolThreads));
}

void ShutdownDfTxGlobalTaskPool() {
    if (!DfTxTaskPool)
        return;
    LogPrintf("DfTxTaskPool: Waiting for tasks\n");
    DfTxTaskPool->Shutdown();
    LogPrintf("DfTxTaskPool: Shutdown\n");
}


void TaskGroup::AddTask() { 
    tasks.fetch_add(1, std::memory_order_release);
}

void TaskGroup::RemoveTask() {
    if (tasks.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        cv.notify_all();
    }
}

void TaskGroup::WaitForCompletion(bool checkForPrematureCompletion) {
    if (checkForPrematureCompletion && tasks.load() == 0) return;
    std::unique_lock<std::mutex> l(cv_m);
    cv.wait(l, [&] { return tasks.load() == 0; });
}

std::unique_ptr<TaskPool> DfTxTaskPool;
