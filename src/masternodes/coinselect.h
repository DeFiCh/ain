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
static const bool DEFAULT_COIN_SELECT_EAGER_EXIT = false;

static const std::string& ARG_STR_WALLET_FAST_SELECT = "-walletfastselect";
static const std::string& ARG_STR_WALLET_COIN_OPT_SKIP_SOLVABLE = "-walletcoinoptskipsolvable";
static const std::string& ARG_STR_WALLET_COIN_OPT_EAGER_EXIT = "-walletcoinopteagerexit";

struct CoinSelectionOptions {
    public:
        bool fastSelect{};
        bool skipSolvable{};
        bool eagerExit{};

    static void SetupArgs(ArgsManager& args) {
        args.AddArg(ARG_STR_WALLET_FAST_SELECT, strprintf("Faster coin select - Enables walletcoinoptskipsolvable and walletcoinopteagerexit. This ends up in faster selection but has the disadvantage of not being able to pick complex input scripts (default: %u)", DEFAULT_COIN_SELECT_FAST_SELECT), ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
        args.AddArg(ARG_STR_WALLET_COIN_OPT_SKIP_SOLVABLE, strprintf("Coin select option: Skips IsSolvable signable UTXO check (default: %u)", DEFAULT_COIN_SELECT_SKIP_SOLVABLE), ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
        args.AddArg(ARG_STR_WALLET_COIN_OPT_EAGER_EXIT, strprintf("Coin select option: Take fast path and eagerly exit on match even without having through the entire set (default: %u)", DEFAULT_COIN_SELECT_EAGER_EXIT), ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    }

    static CoinSelectionOptions CreateDefault() {
        CoinSelectionOptions opts;
        FromArgs(opts, gArgs);
        return opts;
    }

    static void FromArgs(CoinSelectionOptions& m, ArgsManager& args) {
           m.fastSelect = args.GetBoolArg(ARG_STR_WALLET_FAST_SELECT, DEFAULT_COIN_SELECT_FAST_SELECT);
           m.skipSolvable = args.GetBoolArg(ARG_STR_WALLET_COIN_OPT_SKIP_SOLVABLE, DEFAULT_COIN_SELECT_SKIP_SOLVABLE);
           m.eagerExit = args.GetBoolArg(ARG_STR_WALLET_COIN_OPT_EAGER_EXIT, DEFAULT_COIN_SELECT_EAGER_EXIT);
    }

    static void FromHTTPHeaderFunc(CoinSelectionOptions &m, const HTTPHeaderQueryFunc headerFunc) {
        {
            const auto &[present, val] = headerFunc("x" + ARG_STR_WALLET_FAST_SELECT);
            if (present) m.fastSelect = val == "1";
        }
        {
            const auto &[present, val] = headerFunc("x" + ARG_STR_WALLET_COIN_OPT_SKIP_SOLVABLE);
            if (present) m.skipSolvable = val == "1";
        }
        {
            const auto &[present, val] = headerFunc("x" + ARG_STR_WALLET_COIN_OPT_EAGER_EXIT);
            if (present) m.eagerExit = val == "1";
        }
    }

    static void ToHTTPHeaderFunc(const CoinSelectionOptions& m, const HTTPHeaderWriterFunc writer) {
        writer("x" + ARG_STR_WALLET_FAST_SELECT, m.fastSelect ? "1" : "0");
        writer("x" + ARG_STR_WALLET_COIN_OPT_SKIP_SOLVABLE, m.skipSolvable ? "1" : "0");
        writer("x" + ARG_STR_WALLET_COIN_OPT_EAGER_EXIT, m.eagerExit ? "1" : "0");
    }
};

#endif // DEFI_MASTERNODES_COINSELECT_H
