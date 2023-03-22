// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_MASTERNODES_COINSELECT_H
#define DEFI_MASTERNODES_COINSELECT_H

#include <util/system.h>
#include <optional>

/** Default for skipping IsSolvable and return on first valid auth */
static const bool DEFAULT_COIN_SELECT_FAST_SELECT = false;
/** Default for skipping IsSolvable */
static const bool DEFAULT_COIN_SELECT_SKIP_SOLVABLE = false;
/** Default for returning on first valid auth */
static const bool DEFAULT_COIN_SELECT_EAGER_SELECT = false;

static const std::string& ARG_STR_WALLET_FAST_SELECT = "-walletfastselect";
static const std::string& ARG_STR_WALLET_COIN_OPT_SKIP_SOLVABLE = "-walletcoinoptskipsolvable";
static const std::string& ARG_STR_WALLET_COIN_OPT_EAGER_SELECT = "-walletcoinopteagerselect";

struct CoinSelectionOptions {
private:
    std::optional<bool> fastSelect{};
    std::optional<bool> skipSolvable{};
    std::optional<bool> eagerSelect{};

    inline static std::unique_ptr<CoinSelectionOptions> DEFAULT;

public:
    bool IsFastSelectEnabled() const { return fastSelect.value_or(false); }
    bool IsSkipSolvableEnabled() const { return skipSolvable.value_or(false); }
    bool IsEagerSelectEnabled() const { return eagerSelect.value_or(false); }

    static void InitFromArgs(ArgsManager& args) {
        auto m = std::make_unique<CoinSelectionOptions>();
        FromArgs(*m, args);
        LogValues(*m);
        CoinSelectionOptions::DEFAULT = std::move(m);
    }

    static void LogValues(const CoinSelectionOptions& m) {
        struct V { const std::optional<bool> v; const std::string& arg; };
        for (auto &[v, arg]: std::vector<V> { 
            { m.fastSelect, ARG_STR_WALLET_FAST_SELECT },
            { m.skipSolvable, ARG_STR_WALLET_COIN_OPT_SKIP_SOLVABLE },
            { m.eagerSelect, ARG_STR_WALLET_COIN_OPT_EAGER_SELECT },
        }) {
            if (v) LogPrintf("conf: %s: %s\n", arg.substr(1), *v ? "true" : "false");
        }
    }

    static void SetupArgs(ArgsManager& args) {
        args.AddArg(ARG_STR_WALLET_FAST_SELECT, strprintf("Faster coin select - Enables walletcoinoptskipsolvable and walletcoinopteagerselect. This ends up in faster selection but has the disadvantage of not being able to pick complex input scripts (default: %u)", DEFAULT_COIN_SELECT_FAST_SELECT), ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
        args.AddArg(ARG_STR_WALLET_COIN_OPT_SKIP_SOLVABLE, strprintf("Coin select option: Skips IsSolvable signable UTXO check (default: %u)", DEFAULT_COIN_SELECT_SKIP_SOLVABLE), ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
        args.AddArg(ARG_STR_WALLET_COIN_OPT_EAGER_SELECT, strprintf("Coin select option: Take fast path and eagerly exit on match even without having through the entire set (default: %u)", DEFAULT_COIN_SELECT_EAGER_SELECT), ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    }

    static CoinSelectionOptions CreateDefault() {
        if (DEFAULT == nullptr) {
            // We still return a default so tests, benches that don't use it
            // still work as expected.
            return CoinSelectionOptions{};
        }
        // Create a copy
        return *DEFAULT;
    }

    static void FromArgs(CoinSelectionOptions& m, ArgsManager& args) {
        struct V {
            std::optional<bool>& target;
            const std::string& arg;
            const bool& def;
        };
        for (auto &[v, str, def]: {
            V { m.fastSelect, ARG_STR_WALLET_FAST_SELECT, DEFAULT_COIN_SELECT_FAST_SELECT},
            V { m.skipSolvable, ARG_STR_WALLET_COIN_OPT_SKIP_SOLVABLE, DEFAULT_COIN_SELECT_SKIP_SOLVABLE},
            V { m.eagerSelect, ARG_STR_WALLET_COIN_OPT_EAGER_SELECT, DEFAULT_COIN_SELECT_EAGER_SELECT},
        }) {
            // If it's defid, respond with defaults.
            // If it's defi-cli, just skip init unless it's provided,
            // so we just directly call GetOptionalBoolArg
            #ifdef DEFI_CLI
                v = args.GetOptionalBoolArg(str);
            #else
                v = args.GetBoolArg(str, def);
            #endif
        }
    }

    static void FromHTTPHeader(CoinSelectionOptions &m, const HTTPHeaderQueryFunc headerFunc) {
        struct V {
            std::optional<bool>& target;
            const std::string& arg;
        };
        for (auto &[v, str]: {
            V { m.fastSelect, ARG_STR_WALLET_FAST_SELECT},
            V { m.skipSolvable, ARG_STR_WALLET_COIN_OPT_SKIP_SOLVABLE},
            V { m.eagerSelect, ARG_STR_WALLET_COIN_OPT_EAGER_SELECT},
        }) {
            const auto &[present, val] = headerFunc("x" + str);
            if (present) v = val == "1" ? true : false;
        }
    }

    static void ToHTTPHeader(const CoinSelectionOptions& m, const HTTPHeaderWriterFunc writer) {
        struct V {
            const std::optional<bool>& val;
            const std::string& arg;
        };
        for (auto &[v, str]: {
            V { m.fastSelect, ARG_STR_WALLET_FAST_SELECT},
            V { m.skipSolvable, ARG_STR_WALLET_COIN_OPT_SKIP_SOLVABLE},
            V { m.eagerSelect, ARG_STR_WALLET_COIN_OPT_EAGER_SELECT},
        }) {
            if (!v) continue;
            writer("x" + str, *v ? "1" : "0");
        }
    }
};

#endif // DEFI_MASTERNODES_COINSELECT_H
