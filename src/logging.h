// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_LOGGING_H
#define DEFI_LOGGING_H

#include <fs.h>
#include <tinyformat.h>
#include <util/time.h>

#include <atomic>
#include <cstdint>
#include <list>
#include <mutex>
#include <string>
#include <vector>
#include <unordered_map>

static const bool DEFAULT_LOGTIMEMICROS = false;
static const bool DEFAULT_LOGIPS        = false;
static const bool DEFAULT_LOGTIMESTAMPS = true;
static const bool DEFAULT_LOGTHREADNAMES = false;
extern const char * const DEFAULT_DEBUGLOGFILE;

extern bool fLogIPs;

struct CLogCategoryActive
{
    std::string category;
    bool active;
};

namespace BCLog {
    enum LogFlags : uint64_t {
        NONE          = 0UL,
        NET           = (1UL <<  0),
        TOR           = (1UL <<  1),
        MEMPOOL       = (1UL <<  2),
        HTTP          = (1UL <<  3),
        BENCH         = (1UL <<  4),
        ZMQ           = (1UL <<  5),
        DB            = (1UL <<  6),
        RPC           = (1UL <<  7),
        ESTIMATEFEE   = (1UL <<  8),
        ADDRMAN       = (1UL <<  9),
        SELECTCOINS   = (1UL << 10),
        REINDEX       = (1UL << 11),
        CMPCTBLOCK    = (1UL << 12),
        RAND          = (1UL << 13),
        PRUNE         = (1UL << 14),
        PROXY         = (1UL << 15),
        MEMPOOLREJ    = (1UL << 16),
        LIBEVENT      = (1UL << 17),
        COINDB        = (1UL << 18),
        LEVELDB       = (1UL << 20),
        STAKING       = (1UL << 21),
        ANCHORING     = (1UL << 22),
        SPV           = (1UL << 23),
        ORACLE        = (1UL << 24),
        LOAN          = (1UL << 25),
        ACCOUNTCHANGE = (1UL << 26),
        FUTURESWAP    = (1UL << 27),
        TOKENSPLIT    = (1UL << 28),
        RPCCACHE      = (1UL << 29),
        CUSTOMTXBENCH = (1UL << 30),
        ALL           = ~(0UL),
    };

    class Logger
    {
    private:
        mutable std::mutex m_cs;                   // Can not use Mutex from sync.h because in debug mode it would cause a deadlock when a potential deadlock was detected
        FILE* m_fileout = nullptr;                 // GUARDED_BY(m_cs)
        std::list<std::string> m_msgs_before_open; // GUARDED_BY(m_cs)
        bool m_buffering{true};                    //!< Buffer messages before logging can be started. GUARDED_BY(m_cs)

        /**
         * m_started_new_line is a state variable that will suppress printing of
         * the timestamp when multiple calls are made that don't end in a
         * newline.
         */
        std::atomic_bool m_started_new_line{true};

        /** Log categories bitfield. */
        std::atomic<uint32_t> m_categories{0};

        std::string LogTimestampStr(const std::string& str);

    public:
        bool m_print_to_console = false;
        bool m_print_to_file = false;

        bool m_log_timestamps = DEFAULT_LOGTIMESTAMPS;
        bool m_log_time_micros = DEFAULT_LOGTIMEMICROS;
        bool m_log_threadnames = DEFAULT_LOGTHREADNAMES;

        fs::path m_file_path;
        std::atomic<bool> m_reopen_file{false};

        /** Send a string to the log output */
        void LogPrintStr(const std::string& str);

        /** Returns whether logs will be written to any output */
        bool Enabled() const
        {
            std::lock_guard<std::mutex> scoped_lock(m_cs);
            return m_buffering || m_print_to_console || m_print_to_file;
        }

        /** Start logging (and flush all buffered messages) */
        bool StartLogging();
        /** Only for testing */
        void DisconnectTestLogger();

        void ShrinkDebugFile();

        uint32_t GetCategoryMask() const { return m_categories.load(); }

        void EnableCategory(LogFlags flag);
        bool EnableCategory(const std::string& str);
        void DisableCategory(LogFlags flag);
        bool DisableCategory(const std::string& str);

        bool WillLogCategory(LogFlags category) const;

        bool DefaultShrinkDebugFile() const;
    };

} // namespace BCLog

BCLog::Logger& LogInstance();

/** Return true if log accepts specified category */
static inline bool LogAcceptCategory(BCLog::LogFlags category)
{
    return LogInstance().WillLogCategory(category);
}

/** Returns a string with the log categories. */
std::string ListLogCategories();

/** Returns a vector of the active log categories. */
std::vector<CLogCategoryActive> ListActiveLogCategories();

/** Return true if str parses as a log category and set the flag */
bool GetLogCategory(BCLog::LogFlags& flag, const std::string& str);

// Be conservative when using LogPrintf/error or other things which
// unconditionally log to debug.log! It should not be the case that an inbound
// peer can fill up a user's disk with debug.log entries.

template <typename... Args>
static inline void LogPrintf(const char* fmt, const Args&... args)
{
    if (LogInstance().Enabled()) {
        std::string log_msg;
        try {
            log_msg = tfm::format(fmt, args...);
        } catch (tinyformat::format_error& fmterr) {
            /* Original format string will have newline so don't add one here */
            log_msg = "Error \"" + std::string(fmterr.what()) + "\" while formatting log message: " + fmt;
        }
        LogInstance().LogPrintStr(log_msg);
    }
}

template <typename... Args>
static inline void LogPrint(const BCLog::LogFlags& category, const Args&... args)
{
    if (LogAcceptCategory((category))) {
        LogPrintf(args...);
    }
}

/** Implementation that logs at most every x milliseconds. If the category is enabled, it does not time throttle */
template <typename... Args>
static inline void LogPrintCategoryOrThreadThrottled(const BCLog::LogFlags& category, std::string message_key, uint64_t milliseconds, const Args&... args)
{
    // Map containing pairs of message key and timestamp of last log
    // In case different threads use the same message key, use thread_local
    thread_local std::unordered_map<std::string, uint64_t> last_log_timestamps;

    // Log directly if category is enabled..
    if (LogAcceptCategory((category))) {
        LogPrintf(args...);
    } else { // .. and otherwise time throttle logging

        int64_t current_time = GetTimeMillis();
        auto it = last_log_timestamps.find(message_key);

        if (it != last_log_timestamps.end()) {
            if ((current_time - it->second) > milliseconds)
            {
                LogPrintf(args...);
                it->second = current_time;
            }
        }        
        else {
            // No entry yet -> log directly and save timestamp
            last_log_timestamps.insert(std::make_pair(message_key, current_time));
            LogPrintf(args...);
        }
    }
}

#endif // DEFI_LOGGING_H
