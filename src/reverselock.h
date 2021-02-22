// Copyright (c) 2015-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_REVERSELOCK_H
#define DEFI_REVERSELOCK_H

/**
 * An RAII-style reverse lock. Unlocks on construction and locks on destruction.
 */
template<typename Lock>
class reverse_lock
{
public:

    explicit reverse_lock(Lock& _lock) : lock(_lock) {
        _lock.unlock();
        _lock.swap(templock);
    }

    ~reverse_lock() {
        templock.lock();
        templock.swap(lock);
    }

private:
    reverse_lock(reverse_lock const&);
    reverse_lock& operator=(reverse_lock const&);

    Lock& lock;
    Lock templock;
};

#endif // DEFI_REVERSELOCK_H
