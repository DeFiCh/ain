// Copyright (c) 2012-2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <sync.h>
#include <test/setup_common.h>

#include <future>

#include <boost/test/unit_test.hpp>

template<typename MutexType>
void TryPotentialDeadlock(MutexType& mutex1, MutexType& mutex2)
{
    std::promise<void> threadFinished;
    auto futureFinished = threadFinished.get_future();
    {
        LOCK2(mutex1, mutex2);
        std::promise<void> threadStarted;
        // start new thread
        std::thread([&]() {
            // secondary thread started
            threadStarted.set_value();
            // simulate deadlock
            LOCK2(mutex2, mutex1);
            // secondary thread is about to finish
            threadFinished.set_value();
        }).detach();
        // wait secondary thread to start
        threadStarted.get_future().wait();
        // keep mutex for an extra while
        futureFinished.wait_for(std::chrono::milliseconds{50});
    }
    // wait secondary thread to finish
    futureFinished.wait();
}

BOOST_FIXTURE_TEST_SUITE(sync_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(simulate_potential_deadlock)
{
    {
        CCriticalSection rmutex1, rmutex2;
        TryPotentialDeadlock(rmutex1, rmutex2);
    }

    {
        Mutex mutex1, mutex2;
        TryPotentialDeadlock(mutex1, mutex2);
    }
}

BOOST_AUTO_TEST_CASE(lock_free)
{
    constexpr int num_threads = 10;

    auto testFunc = []() {
        static AtomicMutex m;
        static std::atomic_int context(0);
        static std::atomic_int threads(num_threads);

        threads--; // every thread decrements count

        std::unique_lock lock{m};
        context++;
        while (threads > 0); // wait all threads to be here
        BOOST_CHECK_EQUAL(threads.load(), 0); // now they wait for lock
        BOOST_CHECK_EQUAL(context.load(), 1); // but only one operates
        context--;
    };

    std::vector<std::thread> threads;

    for (int i = 0; i < num_threads; i++)
        threads.emplace_back(testFunc);

    for (auto& thread : threads)
        thread.join();
}

BOOST_AUTO_TEST_SUITE_END()
