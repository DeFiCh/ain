// Copyright (c) 2016-2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_WALLET_TEST_WALLET_TEST_FIXTURE_H
#define DEFI_WALLET_TEST_WALLET_TEST_FIXTURE_H

#include <test/setup_common.h>

#include <interfaces/chain.h>
#include <wallet/wallet.h>

#include <memory>

/** Testing setup and teardown for wallet.
 */
struct WalletTestingSetup: public TestingSetup {
    explicit WalletTestingSetup(const std::string& chainName = CBaseChainParams::MAIN);
    ~WalletTestingSetup();

    std::unique_ptr<interfaces::Chain> m_chain = interfaces::MakeChain();
    std::unique_ptr<interfaces::ChainClient> m_chain_client = interfaces::MakeWalletClient(*m_chain, {});
    CWallet m_wallet;
};

#endif // DEFI_WALLET_TEST_WALLET_TEST_FIXTURE_H
