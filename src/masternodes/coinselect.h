// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_MASTERNODES_COINSELECT_H
#define DEFI_MASTERNODES_COINSELECT_H

#include <util/system.h>

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
    public:
        bool fastSelect{};
        bool skipSolvable{};
        bool eagerSelect{};

    static void SetupArgs(ArgsManager& args) {
        args.AddArg(ARG_STR_WALLET_FAST_SELECT, strprintf("Faster coin select - Enables walletcoinoptskipsolvable and walletcoinopteagerselect. This ends up in faster selection but has the disadvantage of not being able to pick complex input scripts (default: %u)", DEFAULT_COIN_SELECT_FAST_SELECT), ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
        args.AddArg(ARG_STR_WALLET_COIN_OPT_SKIP_SOLVABLE, strprintf("Coin select option: Skips IsSolvable signable UTXO check (default: %u)", DEFAULT_COIN_SELECT_SKIP_SOLVABLE), ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
        args.AddArg(ARG_STR_WALLET_COIN_OPT_EAGER_SELECT, strprintf("Coin select option: Take fast path and eagerly exit on match even without having through the entire set (default: %u)", DEFAULT_COIN_SELECT_EAGER_SELECT), ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    }

    static CoinSelectionOptions CreateDefault() {
        CoinSelectionOptions opts;
        FromArgs(opts, gArgs);
        return opts;
    }

    static void FromArgs(CoinSelectionOptions& m, ArgsManager& args) {
        struct V {
            bool& target;
            const std::string& arg;
            const bool& def;
        };
        for (auto &[v, str, def]: {
            V { m.fastSelect, ARG_STR_WALLET_FAST_SELECT, DEFAULT_COIN_SELECT_FAST_SELECT},
            V { m.skipSolvable, ARG_STR_WALLET_COIN_OPT_SKIP_SOLVABLE, DEFAULT_COIN_SELECT_SKIP_SOLVABLE},
            V { m.eagerSelect, ARG_STR_WALLET_COIN_OPT_EAGER_SELECT, DEFAULT_COIN_SELECT_EAGER_SELECT},
        }) {
            v = args.GetBoolArg(str, def);
        }
    }

    static void FromHTTPHeader(CoinSelectionOptions &m, const HTTPHeaderQueryFunc headerFunc) {
        struct V {
            bool& target;
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
            const bool& val;
            const std::string& arg;
        };
        for (auto &[v, str]: {
            V { m.fastSelect, ARG_STR_WALLET_FAST_SELECT},
            V { m.skipSolvable, ARG_STR_WALLET_COIN_OPT_SKIP_SOLVABLE},
            V { m.eagerSelect, ARG_STR_WALLET_COIN_OPT_EAGER_SELECT},
        }) {
            writer("x" + str, v ? "1" : "0");
        }
    }
};

#endif // DEFI_MASTERNODES_COINSELECT_H
