// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_UTIL_TIME_H
#define DEFI_UTIL_TIME_H

#include <stdint.h>
#include <string>
#include <chrono>

void UninterruptibleSleep(const std::chrono::microseconds& n);

/**
 * DEPRECATED
 * Use either GetSystemTimeInSeconds (not mockable) or GetTime<T> (mockable)
 */
int64_t GetTime();

/** Returns the system time (not mockable) */
int64_t GetTimeMillis();
/** Returns the system time (not mockable) */
int64_t GetTimeMicros();
/** Returns the system time (not mockable) */
int64_t GetSystemTimeInSeconds(); // Like GetTime(), but not mockable

/** For testing. Set e.g. with the setmocktime rpc, or -mocktime argument */
void SetMockTime(int64_t nMockTimeIn);
/** For testing */
int64_t GetMockTime();

/** Return system time (or mocked time, if set) */
template <typename T>
T GetTime();

/**
 * ISO 8601 formatting is preferred. Use the FormatISO8601{DateTime,Date}
 * helper functions if possible.
 */
std::string FormatISO8601DateTime(int64_t nTime);
std::string FormatISO8601Date(int64_t nTime);

#endif // DEFI_UTIL_TIME_H
