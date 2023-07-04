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

static void AddKey(CWallet& wallet, const CKey& key, bool includeCompressed)
{
    LOCK(wallet.cs_wallet);
    auto compressedPubKey = key.GetPubKey();
    compressedPubKey.Compress();
    wallet.AddKeyPubKey(key, key.GetPubKey(), includeCompressed ? compressedPubKey : CPubKey{});
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

void VerifyKeyInWallet(const std::string& keyStr, const CWallet& wallet) {
    BOOST_TEST_MESSAGE("=======\nKey: " + keyStr);

    auto keyId = GetKeyIDForDestination(DecodeDestination(keyStr));
    BOOST_TEST_MESSAGE(CKeyIDToLogString(keyId));

    CPubKey pubKey;
    if (wallet.GetPubKey(keyId, pubKey)) {
        BOOST_TEST_MESSAGE("Found key in wallet");
        BOOST_TEST_MESSAGE(CPubKeyToLogString(pubKey));
    }

    auto encoded = EncodeDestination(WitnessV16EthHash(keyId));
    BOOST_TEST_MESSAGE(encoded);
}

CKey StrToKey(const std::string& strSecret) {
    CKey key;
    const auto ethKey{IsHex(strSecret)};
    if (ethKey) {
        const auto vch = ParseHex(strSecret);
        key.Set(vch.begin(), vch.end(), false);
    } else {
        key = DecodeSecret(strSecret);
    }
    return key;
}

BOOST_AUTO_TEST_CASE(eth_key_test_1)
{
    auto wallet = &m_wallet;
    m_chain->lock();

    // Priv key for: 0x9b8a4af42140d8a4c153a822f02571a1dd037e89
    auto key = StrToKey("af990cc3ba17e776f7f57fcc59942a82846d75833fa17d2ba59ce6858d886e23");

    AddKey(*wallet, key, true);

    auto ethAddr1 = "0x9b8a4af42140d8a4c153a822f02571a1dd037e89";
    VerifyKeyInWallet(ethAddr1, *wallet);

    auto ethAddr2 = "0x2E04dbc946c6473DFd318d3bE2BE36E5dfbdACDC";
    VerifyKeyInWallet(ethAddr2, *wallet);
}

BOOST_AUTO_TEST_SUITE_END()
