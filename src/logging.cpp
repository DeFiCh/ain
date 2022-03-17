// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <logging.h>
#include <sync.h>
#include <util/threadnames.h>
#include <util/time.h>

const char * const DEFAULT_DEBUGLOGFILE = "debug.log";

BCLog::Logger& LogInstance()
{
    static BCLog::Logger g_logger;
    return g_logger;
}

bool fLogIPs = DEFAULT_LOGIPS;

static int FileWriteStr(const std::string &str, FILE *fp)
{
    return fwrite(str.data(), 1, str.size(), fp);
}

bool BCLog::Logger::StartLogging()
{
    CLockFreeGuard scoped_lock(m_cs);

    assert(m_buffering);
    assert(m_fileout == nullptr);

    if (m_print_to_file) {
        assert(!m_file_path.empty());
        m_fileout = fsbridge::fopen(m_file_path, "a");
        if (!m_fileout) {
            return false;
        }

        setbuf(m_fileout, nullptr); // unbuffered

        // Add newlines to the logfile to distinguish this execution from the
        // last one.
        FileWriteStr("\n\n\n\n\n", m_fileout);
    }

    // dump buffered messages from before we opened the log
    m_buffering = false;
    while (!m_msgs_before_open.empty()) {
        const std::string& s = m_msgs_before_open.front();

        if (m_print_to_file) FileWriteStr(s, m_fileout);
        if (m_print_to_console) fwrite(s.data(), 1, s.size(), stdout);

        m_msgs_before_open.pop_front();
    }
    if (m_print_to_console) fflush(stdout);

    return true;
}

void BCLog::Logger::DisconnectTestLogger()
{
    CLockFreeGuard scoped_lock(m_cs);
    m_buffering = true;
    if (m_fileout != nullptr) fclose(m_fileout);
    m_fileout = nullptr;
}

void BCLog::Logger::EnableCategory(BCLog::LogFlags flag)
{
    m_categories |= flag;
}

bool BCLog::Logger::EnableCategory(const std::string& str)
{
    BCLog::LogFlags flag;
    if (!GetLogCategory(flag, str)) return false;
    EnableCategory(flag);
    return true;
}

void BCLog::Logger::DisableCategory(BCLog::LogFlags flag)
{
    m_categories &= ~flag;
}

bool BCLog::Logger::DisableCategory(const std::string& str)
{
    BCLog::LogFlags flag;
    if (!GetLogCategory(flag, str)) return false;
    DisableCategory(flag);
    return true;
}

bool BCLog::Logger::WillLogCategory(BCLog::LogFlags category) const
{
    return (m_categories.load(std::memory_order_relaxed) & category) != 0;
}

bool BCLog::Logger::DefaultShrinkDebugFile() const
{
    return m_categories == BCLog::NONE;
}

struct CLogCategoryDesc
{
    BCLog::LogFlags flag;
    std::string category;
};

const CLogCategoryDesc LogCategories[] =
{
    {BCLog::NONE, "0"},
    {BCLog::NONE, "none"},
    {BCLog::NET, "net"},
    {BCLog::TOR, "tor"},
    {BCLog::MEMPOOL, "mempool"},
    {BCLog::HTTP, "http"},
    {BCLog::BENCH, "bench"},
    {BCLog::ZMQ, "zmq"},
    {BCLog::DB, "db"},
    {BCLog::RPC, "rpc"},
    {BCLog::ESTIMATEFEE, "estimatefee"},
    {BCLog::ADDRMAN, "addrman"},
    {BCLog::SELECTCOINS, "selectcoins"},
    {BCLog::REINDEX, "reindex"},
    {BCLog::CMPCTBLOCK, "cmpctblock"},
    {BCLog::RAND, "rand"},
    {BCLog::PRUNE, "prune"},
    {BCLog::PROXY, "proxy"},
    {BCLog::MEMPOOLREJ, "mempoolrej"},
    {BCLog::LIBEVENT, "libevent"},
    {BCLog::COINDB, "coindb"},
    {BCLog::LEVELDB, "leveldb"},
    {BCLog::STAKING, "staking"},
    {BCLog::ANCHORING, "anchoring"},
    {BCLog::SPV, "spv"},
    {BCLog::ORACLE, "oracle"},
    {BCLog::LOAN, "loan"},
    {BCLog::ALL, "1"},
    {BCLog::ALL, "all"},
};

bool GetLogCategory(BCLog::LogFlags& flag, const std::string& str)
{
    if (str == "") {
        flag = BCLog::ALL;
        return true;
    }
    for (const CLogCategoryDesc& category_desc : LogCategories) {
        if (category_desc.category == str) {
            flag = category_desc.flag;
            return true;
        }
    }
    return false;
}

std::string ListLogCategories()
{
    std::string ret;
    int outcount = 0;
    for (const CLogCategoryDesc& category_desc : LogCategories) {
        // Omit the special cases.
        if (category_desc.flag != BCLog::NONE && category_desc.flag != BCLog::ALL) {
            if (outcount != 0) ret += ", ";
            ret += category_desc.category;
            outcount++;
        }
    }
    return ret;
}

std::vector<CLogCategoryActive> ListActiveLogCategories()
{
    std::vector<CLogCategoryActive> ret;
    for (const CLogCategoryDesc& category_desc : LogCategories) {
        // Omit the special cases.
        if (category_desc.flag != BCLog::NONE && category_desc.flag != BCLog::ALL) {
            CLogCategoryActive catActive;
            catActive.category = category_desc.category;
            catActive.active = LogAcceptCategory(category_desc.flag);
            ret.push_back(catActive);
        }
    }
    return ret;
}

std::string BCLog::Logger::LogTimestampStr(const std::string& str)
{
    std::string strStamped;

    if (!m_log_timestamps)
        return str;

    if (m_started_new_line) {
        int64_t nTimeMicros = GetTimeMicros();
        strStamped = FormatISO8601DateTime(nTimeMicros/1000000);
        if (m_log_time_micros) {
            strStamped.pop_back();
            strStamped += strprintf(".%06dZ", nTimeMicros%1000000);
        }
        int64_t mocktime = GetMockTime();
        if (mocktime) {
            strStamped += " (mocktime: " + FormatISO8601DateTime(mocktime) + ")";
        }
        strStamped += ' ' + str;
    } else
        strStamped = str;

    return strStamped;
}

void BCLog::Logger::LogPrintStr(const std::string& str)
{
    CLockFreeGuard scoped_lock(m_cs);
    std::string str_prefixed = str;

    if (m_log_threadnames && m_started_new_line) {
        str_prefixed.insert(0, "[" + util::ThreadGetInternalName() + "] ");
    }

    str_prefixed = LogTimestampStr(str_prefixed);

    m_started_new_line = !str.empty() && str[str.size()-1] == '\n';

    if (m_buffering) {
        // buffer if we haven't started logging yet
        m_msgs_before_open.push_back(str_prefixed);
        return;
    }

    if (m_print_to_console) {
        // print to console
        fwrite(str_prefixed.data(), 1, str_prefixed.size(), stdout);
        fflush(stdout);
    }
    if (m_print_to_file) {
        assert(m_fileout != nullptr);

        // reopen the log file, if requested
        if (m_reopen_file) {
            m_reopen_file = false;
            FILE* new_fileout = fsbridge::fopen(m_file_path, "a");
            if (new_fileout) {
                setbuf(new_fileout, nullptr); // unbuffered
                fclose(m_fileout);
                m_fileout = new_fileout;
            }
        }
        FileWriteStr(str_prefixed, m_fileout);
    }
}

void BCLog::Logger::ShrinkDebugFile()
{
    // Amount of debug.log to save at end when shrinking (must fit in memory)
    constexpr size_t RECENT_DEBUG_HISTORY_SIZE = 10 * 1000000;

    assert(!m_file_path.empty());

    // Scroll debug.log if it's getting too big
    FILE* file = fsbridge::fopen(m_file_path, "r");

    // Special files (e.g. device nodes) may not have a size.
    size_t log_size = 0;
    try {
        log_size = fs::file_size(m_file_path);
    } catch (const fs::filesystem_error&) {}

    // If debug.log file is more than 10% bigger the RECENT_DEBUG_HISTORY_SIZE
    // trim it down by saving only the last RECENT_DEBUG_HISTORY_SIZE bytes
    if (file && log_size > 11 * (RECENT_DEBUG_HISTORY_SIZE / 10))
    {
        // Restart the file with some of the end
        std::vector<char> vch(RECENT_DEBUG_HISTORY_SIZE, 0);
        if (fseek(file, -((long)vch.size()), SEEK_END)) {
            LogPrintf("Failed to shrink debug log file: fseek(...) failed\n");
            fclose(file);
            return;
        }
        int nBytes = fread(vch.data(), 1, vch.size(), file);
        fclose(file);

        file = fsbridge::fopen(m_file_path, "w");
        if (file)
        {
            fwrite(vch.data(), 1, nBytes, file);
            fclose(file);
        }
    }
    else if (file != nullptr)
        fclose(file);
}
