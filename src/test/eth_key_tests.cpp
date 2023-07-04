// Copyright (c) 2017 Pieter Wuille
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <bech32.h>
#include <key_io.h>
#include <test/setup_common.h>
#include <boost/test/unit_test.hpp>

#include <wallet/wallet.h>
#include <wallet/coincontrol.h>
#include <wallet/test/wallet_test_fixture.h>

#define FMT_HEADER_ONLY
#include <fmt/format.h>

BOOST_FIXTURE_TEST_SUITE(eth_key_tests, WalletTestingSetup)

static void AddKey(CWallet& wallet, const CKey& key)
{
    LOCK(wallet.cs_wallet);
    wallet.AddKeyPubKey(key, key.GetPubKey(), {});
}

CKeyID GetKeyIDForDestination(const CTxDestination& dest)
{
    if (auto id = std::get_if<PKHash>(&dest)) {
        return CKeyID(*id);
    }
    if (auto witness_id = std::get_if<WitnessV0KeyHash>(&dest)) {
        return CKeyID(*witness_id);
    }
    if (auto witness_id = std::get_if<WitnessV16EthHash>(&dest)) {
        return CKeyID(*witness_id);
    }
    return {};
}

std::string CPubKeyToLogString(const CPubKey& key) {
    return fmt::format("CPubKey Hash: {}\nID: {}\nETH-ID: {}\nIsComp: {}\n", 
                key.GetHash().ToString(), 
                key.GetID().ToString(), 
                key.GetEthID().ToString(), 
                key.IsCompressed() ? "true" : "false");
}

std::string CKeyIDToLogString(const CKeyID& key) {
    return fmt::format("KeyID Hex: {}\n", key.GetHex());
}

void CheckKeyString(const std::string& keyStr) {
    auto keyId = GetKeyIDForDestination(DecodeDestination(keyStr));
    auto encoded = EncodeDestination(WitnessV16EthHash(keyId));

    BOOST_TEST_MESSAGE(CKeyIDToLogString(keyId));
    BOOST_TEST_MESSAGE(encoded);
}

BOOST_AUTO_TEST_CASE(eth_key_test_1)
{
    auto ethAddr1 = "0x9b8a4af42140d8a4c153a822f02571a1dd037e89";
    CheckKeyString(ethAddr1);

    auto ethAddr2 = "0x2E04dbc946c6473DFd318d3bE2BE36E5dfbdACDC";
    CheckKeyString(ethAddr2);
}

BOOST_AUTO_TEST_SUITE_END()
