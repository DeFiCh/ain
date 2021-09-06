// Copyright (c) 2015-2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <reverselock.h>
#include <test/setup_common.h>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(reverselock_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(reverselock_basics)
{
    Mutex mutex;
    std::unique_lock<std::mutex> lock(mutex);

    BOOST_CHECK(lock.owns_lock());
    {
        reverse_lock<std::unique_lock<std::mutex>> rlock(lock);
        BOOST_CHECK(!lock.owns_lock());
    }
    BOOST_CHECK(lock.owns_lock());
}

BOOST_AUTO_TEST_CASE(reverselock_multiple)
{
    Mutex mutex2;
    Mutex mutex;
    std::unique_lock<std::mutex> lock2(mutex2);
    std::unique_lock<std::mutex> lock(mutex);

    // Make sure undoing two locks succeeds
    {
        reverse_lock<std::unique_lock<std::mutex>> rlock(lock);
        BOOST_CHECK(!lock.owns_lock());
        reverse_lock<std::unique_lock<std::mutex>> rlock2(lock2);
        BOOST_CHECK(!lock2.owns_lock());
    }
    BOOST_CHECK(lock.owns_lock());
    BOOST_CHECK(lock2.owns_lock());
}

BOOST_AUTO_TEST_CASE(reverselock_errors)
{
    Mutex mutex2;
    Mutex mutex;
    std::unique_lock<std::mutex> lock2(mutex2);
    std::unique_lock<std::mutex> lock(mutex);

#ifdef DEBUG_LOCKORDER
        // Make sure trying to reverse lock a previous lock fails
    try {
        reverse_lock<std::unique_lock<std::mutex>> rlock2(lock2);
        BOOST_CHECK(false); // REVERSE_LOCK(lock2) succeeded
    } catch(...) { }
    BOOST_CHECK(lock2.owns_lock());
#endif

    // Make sure trying to reverse lock an unlocked lock fails
    lock.unlock();

    BOOST_CHECK(!lock.owns_lock());

    bool failed = false;
    try {
        reverse_lock<std::unique_lock<std::mutex>> rlock(lock);
    } catch(...) {
        failed = true;
    }

    BOOST_CHECK(failed);
    BOOST_CHECK(!lock.owns_lock());

    // Locking the original lock after it has been taken by a reverse lock
    // makes no sense. Ensure that the original lock no longer owns the lock
    // after giving it to a reverse one.

    lock.lock();
    BOOST_CHECK(lock.owns_lock());
    {
        reverse_lock<std::unique_lock<std::mutex>> rlock(lock);
        BOOST_CHECK(!lock.owns_lock());
    }

    BOOST_CHECK(failed);
    BOOST_CHECK(lock.owns_lock());
}

BOOST_AUTO_TEST_SUITE_END()
