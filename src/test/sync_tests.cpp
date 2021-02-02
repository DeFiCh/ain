// Copyright (c) 2012-2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <sync.h>
#include <test/setup_common.h>

#include <boost/test/unit_test.hpp>

namespace {
template <typename MutexType>
void TestPotentialDeadLockDetected(MutexType& mutex1, MutexType& mutex2)
{
    {
        LOCK2(mutex1, mutex2);
    }
    bool error_thrown = false;
    try {
        LOCK2(mutex2, mutex1);
    } catch (const std::logic_error& e) {
        BOOST_CHECK_EQUAL(e.what(), "potential deadlock detected");
        error_thrown = true;
    }
    #ifdef DEBUG_LOCKORDER
    BOOST_CHECK(error_thrown);
    #else
    BOOST_CHECK(!error_thrown);
    #endif
}
} // namespace

BOOST_FIXTURE_TEST_SUITE(sync_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(potential_deadlock_detected)
{
    #ifdef DEBUG_LOCKORDER
    bool prev = g_debug_lockorder_abort;
    g_debug_lockorder_abort = false;
    #endif

    CCriticalSection rmutex1, rmutex2;
    TestPotentialDeadLockDetected(rmutex1, rmutex2);

    Mutex mutex1, mutex2;
    TestPotentialDeadLockDetected(mutex1, mutex2);

    #ifdef DEBUG_LOCKORDER
    g_debug_lockorder_abort = prev;
    #endif
}

BOOST_AUTO_TEST_CASE(lock_free)
{
    constexpr int num_threads = 10;

    auto testFunc = []() {
        static std::atomic_bool cs_lock;
        static std::atomic_int context(0);
        static std::atomic_int threads(num_threads);

        threads--; // every thread decrements count

        CLockFreeGuard lock(cs_lock);
        context++;
        while (threads > 0); // wait all therads to be here
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
