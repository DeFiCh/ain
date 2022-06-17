// Copyright (c) 2012-2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <random.h>
#include <scheduler.h>
#include <util/time.h>

#include <test/setup_common.h>

#include <boost/test/unit_test.hpp>

#include <mutex>
#include <thread>
#include <vector>

BOOST_AUTO_TEST_SUITE(scheduler_tests)

static void microTask(CScheduler& s, std::mutex& mutex, int& counter, int delta, std::chrono::system_clock::time_point rescheduleTime)
{
    {
        std::lock_guard<std::mutex> lock(mutex);
        counter += delta;
    }
    std::chrono::system_clock::time_point noTime = std::chrono::system_clock::time_point::min();
    if (rescheduleTime != noTime) {
        CScheduler::Function f = std::bind(&microTask, std::ref(s), std::ref(mutex), std::ref(counter), -delta + 1, noTime);
        s.schedule(f, rescheduleTime);
    }
}

BOOST_AUTO_TEST_CASE(manythreads)
{
    // Stress test: hundreds of microsecond-scheduled tasks,
    // serviced by 10 threads.
    //
    // So... ten shared counters, which if all the tasks execute
    // properly will sum to the number of tasks done.
    // Each task adds or subtracts a random amount from one of the
    // counters, and then schedules another task 0-1000
    // microseconds in the future to subtract or add from
    // the counter -random_amount+1, so in the end the shared
    // counters should sum to the number of initial tasks performed.
    CScheduler microTasks;

    std::mutex counterMutex[10];
    int counter[10] = { 0 };
    FastRandomContext rng{/* fDeterministic */ true};
    auto zeroToNine = [](FastRandomContext& rc) -> int { return rc.randrange(10); }; // [0, 9]
    auto randomMsec = [](FastRandomContext& rc) -> int { return -11 + (int)rc.randrange(1012); }; // [-11, 1000]
    auto randomDelta = [](FastRandomContext& rc) -> int { return -1000 + (int)rc.randrange(2001); }; // [-1000, 1000]

    std::chrono::system_clock::time_point start = std::chrono::system_clock::now();
    std::chrono::system_clock::time_point now = start;
    std::chrono::system_clock::time_point first, last;
    size_t nTasks = microTasks.getQueueInfo(first, last);
    BOOST_CHECK(nTasks == 0);

    for (int i = 0; i < 100; ++i) {
        std::chrono::system_clock::time_point t = now + std::chrono::microseconds(randomMsec(rng));
        std::chrono::system_clock::time_point tReschedule = now + std::chrono::microseconds(500 + randomMsec(rng));
        int whichCounter = zeroToNine(rng);
        CScheduler::Function f = std::bind(&microTask, std::ref(microTasks),
                                             std::ref(counterMutex[whichCounter]), std::ref(counter[whichCounter]),
                                             randomDelta(rng), tReschedule);
        microTasks.schedule(f, t);
    }
    nTasks = microTasks.getQueueInfo(first, last);
    BOOST_CHECK(nTasks == 100);
    BOOST_CHECK(first < last);
    BOOST_CHECK(last > now);

    // As soon as these are created they will start running and servicing the queue
    std::vector<std::thread> microThreads;
    for (int i = 0; i < 5; i++)
        microThreads.emplace_back(std::bind(&CScheduler::serviceQueue, &microTasks));

    UninterruptibleSleep(std::chrono::microseconds{600});
    now = std::chrono::system_clock::now();

    // More threads and more tasks:
    for (int i = 0; i < 5; i++)
        microThreads.emplace_back(std::bind(&CScheduler::serviceQueue, &microTasks));
    for (int i = 0; i < 100; i++) {
        std::chrono::system_clock::time_point t = now + std::chrono::microseconds(randomMsec(rng));
        std::chrono::system_clock::time_point tReschedule = now + std::chrono::microseconds(500 + randomMsec(rng));
        int whichCounter = zeroToNine(rng);
        CScheduler::Function f = std::bind(&microTask, std::ref(microTasks),
                                             std::ref(counterMutex[whichCounter]), std::ref(counter[whichCounter]),
                                             randomDelta(rng), tReschedule);
        microTasks.schedule(f, t);
    }

    // Drain the task queue then exit threads
    microTasks.StopWhenDrained();
    // wait until all the threads are done
    for (auto& thread: microThreads) {
        if (thread.joinable()) thread.join();
    }

    int counterSum = 0;
    for (int i = 0; i < 10; i++) {
        BOOST_CHECK(counter[i] != 0);
        counterSum += counter[i];
    }
    BOOST_CHECK_EQUAL(counterSum, 200);
}

BOOST_AUTO_TEST_CASE(singlethreadedscheduler_ordered)
{
    CScheduler scheduler;

    // each queue should be well ordered with respect to itself but not other queues
    SingleThreadedSchedulerClient queue1(&scheduler);
    SingleThreadedSchedulerClient queue2(&scheduler);

    // create more threads than queues
    // if the queues only permit execution of one task at once then
    // the extra threads should effectively be doing nothing
    // if they don't we'll get out of order behaviour
    std::vector<std::thread> threads;
    for (int i = 0; i < 5; ++i) {
        threads.emplace_back(std::bind(&CScheduler::serviceQueue, &scheduler));
    }

    // these are not atomic, if SinglethreadedSchedulerClient prevents
    // parallel execution at the queue level no synchronization should be required here
    int counter1 = 0;
    int counter2 = 0;

    // just simply count up on each queue - if execution is properly ordered then
    // the callbacks should run in exactly the order in which they were enqueued
    for (int i = 0; i < 100; ++i) {
        queue1.AddToProcessQueue([i, &counter1]() {
            bool expectation = i == counter1++;
            assert(expectation);
        });

        queue2.AddToProcessQueue([i, &counter2]() {
            bool expectation = i == counter2++;
            assert(expectation);
        });
    }

    // finish up
    scheduler.StopWhenDrained();
    for (auto& thread: threads) {
        if (thread.joinable()) thread.join();
    }

    BOOST_CHECK_EQUAL(counter1, 100);
    BOOST_CHECK_EQUAL(counter2, 100);
}

BOOST_AUTO_TEST_SUITE_END()
