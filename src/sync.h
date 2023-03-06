// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_SYNC_H
#define DEFI_SYNC_H

#include <threadsafety.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <thread>
#include <mutex>


////////////////////////////////////////////////
//                                            //
// THE SIMPLE DEFINITION, EXCLUDING DEBUG CODE //
//                                            //
////////////////////////////////////////////////

/*
RecursiveMutex mutex;
    std::recursive_mutex mutex;

LOCK(mutex);
    std::unique_lock<std::recursive_mutex> criticalblock(mutex);

LOCK2(mutex1, mutex2);
    std::unique_lock<std::recursive_mutex> criticalblock1(mutex1);
    std::unique_lock<std::recursive_mutex> criticalblock2(mutex2);

TRY_LOCK(mutex, name);
    std::unique_lock<std::recursive_mutex> name(mutex, std::try_to_lock_t);

ENTER_CRITICAL_SECTION(mutex); // no RAII
    mutex.lock();

LEAVE_CRITICAL_SECTION(mutex); // no RAII
    mutex.unlock();
 */

///////////////////////////////
//                           //
// THE ACTUAL IMPLEMENTATION //
//                           //
///////////////////////////////

#ifdef DEBUG_LOCKORDER
void EnterCritical(const char* pszName, const char* pszFile, int nLine, void* cs, bool fTry = false);
void LeaveCritical();
std::string LocksHeld();
void AssertLockHeldInternal(const char* pszName, const char* pszFile, int nLine, void* cs) ASSERT_EXCLUSIVE_LOCK(cs);
void AssertLockNotHeldInternal(const char* pszName, const char* pszFile, int nLine, void* cs);
void DeleteLock(void* cs);

/**
 * Call abort() if a potential lock order deadlock bug is detected, instead of
 * just logging information and throwing a logic_error. Defaults to true, and
 * set to false in DEBUG_LOCKORDER unit tests.
 */
extern bool g_debug_lockorder_abort;
#else
#define DeleteLock(cs)
#endif

/**
 * Template mixin that adds -Wthread-safety locking annotations and lock order
 * checking to a subset of the mutex API.
 */
template <typename PARENT>
class LOCKABLE AnnotatedMixin : public PARENT
{
public:
    ~AnnotatedMixin() {
        DeleteLock((void*)this);
    }

    void lock() EXCLUSIVE_LOCK_FUNCTION()
    {
        PARENT::lock();
    }

    void unlock() UNLOCK_FUNCTION()
    {
        PARENT::unlock();
    }

    bool try_lock() EXCLUSIVE_TRYLOCK_FUNCTION(true)
    {
        return PARENT::try_lock();
    }

    using UniqueLock = std::unique_lock<PARENT>;
};

/**
 * Wrapped mutex: supports recursive locking, but no waiting
 * TODO: We should move away from using the recursive lock by default.
 */
using RecursiveMutex = AnnotatedMixin<std::recursive_mutex>;
using CCriticalSection = RecursiveMutex;

/** Wrapped mutex: supports waiting but not recursive locking */
using Mutex = AnnotatedMixin<std::mutex>;

#ifdef DEBUG_LOCKCONTENTION

#define AssertLockHeld(cs) AssertLockHeldInternal(#cs, __FILE__, __LINE__, &cs)
#define AssertLockNotHeld(cs) AssertLockNotHeldInternal(#cs, __FILE__, __LINE__, &cs)

void PrintLockContention(const char* pszName, const char* pszFile, int nLine);

/** Wrapper around std::unique_lock style lock for Mutex. */
template <typename Mutex, typename Base = typename Mutex::UniqueLock>
class SCOPED_LOCKABLE UniqueLock : public Base
{
private:
    void Enter(const char* pszName, const char* pszFile, int nLine)
    {
        EnterCritical(pszName, pszFile, nLine, (void*)(Base::mutex()));
        if (!Base::try_lock()) {
            PrintLockContention(pszName, pszFile, nLine);
            Base::lock();
        }
    }

    bool TryEnter(const char* pszName, const char* pszFile, int nLine)
    {
        EnterCritical(pszName, pszFile, nLine, (void*)(Base::mutex()), true);
        Base::try_lock();
        if (!Base::owns_lock())
            LeaveCritical();
        return Base::owns_lock();
    }

public:
    UniqueLock(Mutex& mutexIn, const char* pszName, const char* pszFile, int nLine, bool fTry = false) EXCLUSIVE_LOCK_FUNCTION(mutexIn) : Base(mutexIn, std::defer_lock)
    {
        if (fTry)
            TryEnter(pszName, pszFile, nLine);
        else
            Enter(pszName, pszFile, nLine);
    }

    UniqueLock(Mutex* pmutexIn, const char* pszName, const char* pszFile, int nLine, bool fTry = false) EXCLUSIVE_LOCK_FUNCTION(pmutexIn)
    {
        if (!pmutexIn) return;

        *static_cast<Base*>(this) = Base(*pmutexIn, std::defer_lock);
        if (fTry)
            TryEnter(pszName, pszFile, nLine);
        else
            Enter(pszName, pszFile, nLine);
    }

    ~UniqueLock() UNLOCK_FUNCTION()
    {
        if (Base::owns_lock())
            LeaveCritical();
    }

    operator bool()
    {
        return Base::owns_lock();
    }
};

template<typename MutexArg>
using DebugLock = UniqueLock<typename std::remove_reference<typename std::remove_pointer<MutexArg>::type>::type>;

#define LOCK(cs) DebugLock<decltype(cs)> criticalblock1(cs, #cs, __FILE__, __LINE__)
#define LOCK2(cs1, cs2)                                               \
    DebugLock<decltype(cs1)> criticalblock1(cs1, #cs1, __FILE__, __LINE__); \
    DebugLock<decltype(cs2)> criticalblock2(cs2, #cs2, __FILE__, __LINE__);
#define TRY_LOCK(cs, name) DebugLock<decltype(cs)> name(cs, #cs, __FILE__, __LINE__, true)
#define WAIT_LOCK(cs, name) DebugLock<decltype(cs)> name(cs, #cs, __FILE__, __LINE__)

#define ENTER_CRITICAL_SECTION(cs)                            \
    {                                                         \
        EnterCritical(#cs, __FILE__, __LINE__, (void*)(&cs)); \
        (cs).lock();                                          \
    }

#define LEAVE_CRITICAL_SECTION(cs) \
    {                              \
        (cs).unlock();             \
        LeaveCritical();           \
    }

#else

#define AssertLockHeld(cs)
#define AssertLockNotHeld(cs)

template<typename T>
using unique_lock_type = typename std::decay<T>::type::UniqueLock;

#define LOCK(cs) unique_lock_type<decltype(cs)> criticalblock1(cs)
#define LOCK2(cs1, cs2)                                                   \
    unique_lock_type<decltype(cs1)> criticalblock1(cs1, std::defer_lock); \
    unique_lock_type<decltype(cs2)> criticalblock2(cs2, std::defer_lock); \
    std::lock(criticalblock1, criticalblock2)

#define TRY_LOCK(cs, name) unique_lock_type<decltype(cs)> name(cs, std::try_to_lock)
#define WAIT_LOCK(cs, name) unique_lock_type<decltype(cs)> name(cs)

#define ENTER_CRITICAL_SECTION(cs) (cs).lock()
#define LEAVE_CRITICAL_SECTION(cs) (cs).unlock()

#endif
//! Run code while locking a mutex.
//!
//! Examples:
//!
//!   WITH_LOCK(cs, shared_val = shared_val + 1);
//!
//!   int val = WITH_LOCK(cs, return shared_val);
//!
#define WITH_LOCK(cs, code) [&] { LOCK(cs); code; }()

class CSemaphore
{
private:
    std::condition_variable condition;
    std::mutex mutex;
    int value;

public:
    explicit CSemaphore(int init) : value(init) {}

    void wait()
    {
        std::unique_lock<std::mutex> lock(mutex);
        condition.wait(lock, [&]() { return value >= 1; });
        value--;
    }

    bool try_wait()
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (value < 1)
            return false;
        value--;
        return true;
    }

    void post()
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            value++;
        }
        condition.notify_one();
    }
};

/** RAII-style semaphore lock */
class CSemaphoreGrant
{
private:
    CSemaphore* sem;
    bool fHaveGrant;

public:
    void Acquire()
    {
        if (fHaveGrant)
            return;
        sem->wait();
        fHaveGrant = true;
    }

    void Release()
    {
        if (!fHaveGrant)
            return;
        sem->post();
        fHaveGrant = false;
    }

    bool TryAcquire()
    {
        if (!fHaveGrant && sem->try_wait())
            fHaveGrant = true;
        return fHaveGrant;
    }

    void MoveTo(CSemaphoreGrant& grant)
    {
        grant.Release();
        grant.sem = sem;
        grant.fHaveGrant = fHaveGrant;
        fHaveGrant = false;
    }

    CSemaphoreGrant() : sem(nullptr), fHaveGrant(false) {}

    explicit CSemaphoreGrant(CSemaphore& sema, bool fTry = false) : sem(&sema), fHaveGrant(false)
    {
        if (fTry)
            TryAcquire();
        else
            Acquire();
    }

    ~CSemaphoreGrant()
    {
        Release();
    }

    operator bool() const
    {
        return fHaveGrant;
    }
};

// Utility class for indicating to compiler thread analysis that a mutex is
// locked (when it couldn't be determined otherwise).
struct SCOPED_LOCKABLE LockAssertion
{
    template <typename Mutex>
    explicit LockAssertion(Mutex& mutex) EXCLUSIVE_LOCK_FUNCTION(mutex)
    {
#ifdef DEBUG_LOCKORDER
        AssertLockHeld(mutex);
#endif
    }
    ~LockAssertion() UNLOCK_FUNCTION() {}
};

class AtomicMutex {
private:
    std::atomic<bool> flag{false};
    int64_t spins;
    int64_t yields;

public:
    AtomicMutex(int64_t spins = 10, int64_t yields = 16): 
        spins(spins), yields(yields) {}

    void lock() {
        // Note: The loop here addresses both, spurious failures as well
        // as to suspend or spin wait until it's set
        // Additional:
        // - We use this a lock for external critical section, so we use
        // seq ordering, to ensure it provides the right ordering guarantees
        // for the others
        // On failure of CAS, we don't care about the existing value, we just
        // discard it, so relaxed ordering is sufficient.
        bool expected = false;
        auto i = 0;
        while (std::atomic_compare_exchange_weak_explicit(
                   &flag,
                   &expected, true,
                   std::memory_order_seq_cst,
                   std::memory_order_relaxed) == false) {
            // Could have been a spurious failure or another thread could have taken the
            // lock in-between since we're now out of the atomic ops. 
            // Reset expected to start from scratch again, since we only want
            // a singular atomic false -> true transition.
            expected = false;
            if (i > spins) {
                if (i > spins + yields) {
                    // Use larger sleep, in line with the largest quantum, which is 
                    // Windows with 16ms
                    std::this_thread::sleep_for(std::chrono::milliseconds(16));
                } else {
                    std::this_thread::yield();
                }
            }
            i++;
        }
    }

    void unlock() {
        flag.store(false, std::memory_order_seq_cst);
    }

    bool try_lock() noexcept {
        // We locked it if and only if it was a false -> true transition.
        // Otherwise, we just re-wrote an already existing value as true which is harmless 
        // We could theoritically use CAS here to prevent the additional write, but
        // but requires loop on weak, or using strong. Simpler to just use an exchange for
        // for now, since all ops are seq_cst anyway.
        return !flag.exchange(true, std::memory_order_seq_cst);
    }
};


#endif // DEFI_SYNC_H
