// Copyright (c) 2012-2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <bench/bench.h>
#include <interfaces/chain.h>
#include <test/util.h>
#include <validationinterface.h>
#include <wallet/wallet.h>

#include <optional>

static void WalletBalance(benchmark::State& state, const bool set_dirty, const bool add_watchonly, const bool add_mine)
{
    const auto& ADDRESS_WATCHONLY = ADDRESS_BCRT1_UNSPENDABLE;

    std::unique_ptr<interfaces::Chain> chain = interfaces::MakeChain();
    CWallet wallet{chain.get(), WalletLocation(), WalletDatabase::CreateMock()};
    {
        bool first_run;
        if (wallet.LoadWallet(first_run) != DBErrors::LOAD_OK) assert(false);
        wallet.handleNotifications();
    }


    const std::optional<std::string> address_mine{add_mine ? std::optional<std::string>{getnewaddress(wallet)} : std::nullopt};
    if (add_watchonly) importaddress(wallet, ADDRESS_WATCHONLY);

    for (int i = 0; i < 100; ++i) {
        generatetoaddress(address_mine.value_or(ADDRESS_WATCHONLY));
        generatetoaddress(ADDRESS_WATCHONLY);
    }
    SyncWithValidationInterfaceQueue();

    auto bal = wallet.GetBalance(); // Cache

    while (state.KeepRunning()) {
        if (set_dirty) wallet.MarkDirty();
        bal = wallet.GetBalance();
        if (add_mine) assert(bal.m_mine_trusted[DCT_ID{0}] > 0);            // tokens `0`
        if (add_watchonly) assert(bal.m_watchonly_trusted[DCT_ID{0}] > 0);  // tokens `0`
    }
}

static void WalletBalanceDirty(benchmark::State& state) { WalletBalance(state, /* set_dirty */ true, /* add_watchonly */ true, /* add_mine */ true); }
static void WalletBalanceClean(benchmark::State& state) { WalletBalance(state, /* set_dirty */ false, /* add_watchonly */ true, /* add_mine */ true); }
static void WalletBalanceMine(benchmark::State& state) { WalletBalance(state, /* set_dirty */ false, /* add_watchonly */ false, /* add_mine */ true); }
static void WalletBalanceWatch(benchmark::State& state) { WalletBalance(state, /* set_dirty */ false, /* add_watchonly */ true, /* add_mine */ false); }

BENCHMARK(WalletBalanceDirty, 2500);
BENCHMARK(WalletBalanceClean, 8000);
BENCHMARK(WalletBalanceMine, 16000);
BENCHMARK(WalletBalanceWatch, 8000);
