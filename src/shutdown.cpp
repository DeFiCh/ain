// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <shutdown.h>

#include <atomic>

static std::atomic<bool> fRequestShutdown(false);
std::condition_variable shutdown_cv;
std::mutex shutdown_mutex;

void StartShutdown()
{
    fRequestShutdown = true;
    shutdown_cv.notify_all();
}
void AbortShutdown()
{
    fRequestShutdown = false;
}
bool ShutdownRequested()
{
    return fRequestShutdown;
}
