// Copyright (c) 2012-2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <key.h>

#include <bech32.h>
#include <key_io.h>
#include <uint256.h>
#include <util/system.h>
#include <util/strencodings.h>
#include <test/setup_common.h>

#include <string>
#include <vector>

#include <boost/test/unit_test.hpp>

// private keys
static const std::string strSecret1 = "5HxWvvfubhXpYYpS3tJkw6fq9jE9j18THftkZjHHfmFiWtmAbrj";
static const std::string strSecret2 = "5KC4ejrDjv152FGwP386VD1i2NYc5KkfSMyv1nGy1VGDxGHqVY3";
static const std::string strSecret1C = "Kwr371tjA9u2rFSMZjTNun2PXXP3WPZu2afRHTcta6KxEUdm1vEw";
static const std::string strSecret2C = "L3Hq7a8FEQwJkW1M2GNKDW28546Vp5miewcCzSqUD9kCAXrJdS3g";

// public key hash addresses
static const std::string pkh_addr1 = "8eLhZJqPrKuFmBonk7tK3Tma6oyRvJM4Tz";
static const std::string pkh_addr2 = "8VApoBSS8rRKiRpSchh5JjYDNLrvyXEYgJ";
static const std::string pkh_addr1C = "8ctAamF4jdX6NzoTk5So1qXUBc4CxovyK9";
static const std::string pkh_addr2C = "8SWakFLXnSsHi5g5mxtSzbsr1T68JmXMdR";

// witness public key hash addresses
static const std::string wpkh_addr1 = "df1qluvhk989q245ruau3n95339t4j02kddu2vqwve";
static const std::string wpkh_addr2 = "df1qn2prk6v0w78vay9sjnwr7y4gra0rcv69f5qxqz";
static const std::string wpkh_addr1C = "df1qauw2aajwu832l7rhkl5wjufacfdj9z0jquwv3z";
static const std::string wpkh_addr2C = "df1q04t8rax7tc7s2jzeuphjpyvuc0vgygsz3drcsg";

// erc55 addresses
static const std::string erc55_addr1 = "0x482e975Ee029d6d268CC1dCce529748a06A46AAc";
static const std::string erc55_addr2 = "0x43162a466BD5891dfBf7c438b0c35F0144690D26";
static const std::string erc55_addr1C = "0x2D586e4Dec0798F728b626a4f134a3728772a8E5";
static const std::string erc55_addr2C = "0x83bB997178Cd7F6876620096EFADB18a712eDdca";

static const std::string strAddressBad = "1HV9Lc3sNHZxwj4Zk6fB38tEmBryq2cBiF";


BOOST_FIXTURE_TEST_SUITE(key_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(key_test_1)
{
    CKey key1  = DecodeSecret(strSecret1);
    BOOST_CHECK(key1.IsValid() && !key1.IsCompressed());
    CKey key2  = DecodeSecret(strSecret2);
    BOOST_CHECK(key2.IsValid() && !key2.IsCompressed());
    CKey key1C = DecodeSecret(strSecret1C);
    BOOST_CHECK(key1C.IsValid() && key1C.IsCompressed());
    CKey key2C = DecodeSecret(strSecret2C);
    BOOST_CHECK(key2C.IsValid() && key2C.IsCompressed());
    CKey bad_key = DecodeSecret(strAddressBad);
    BOOST_CHECK(!bad_key.IsValid());

    CPubKey pubkey1  = key1. GetPubKey();
    CPubKey pubkey2  = key2. GetPubKey();
    CPubKey pubkey1C = key1C.GetPubKey();
    CPubKey pubkey2C = key2C.GetPubKey();

    BOOST_CHECK(key1.VerifyPubKey(pubkey1));
    BOOST_CHECK(!key1.VerifyPubKey(pubkey1C));
    BOOST_CHECK(!key1.VerifyPubKey(pubkey2));
    BOOST_CHECK(!key1.VerifyPubKey(pubkey2C));

    BOOST_CHECK(!key1C.VerifyPubKey(pubkey1));
    BOOST_CHECK(key1C.VerifyPubKey(pubkey1C));
    BOOST_CHECK(!key1C.VerifyPubKey(pubkey2));
    BOOST_CHECK(!key1C.VerifyPubKey(pubkey2C));

    BOOST_CHECK(!key2.VerifyPubKey(pubkey1));
    BOOST_CHECK(!key2.VerifyPubKey(pubkey1C));
    BOOST_CHECK(key2.VerifyPubKey(pubkey2));
    BOOST_CHECK(!key2.VerifyPubKey(pubkey2C));

    BOOST_CHECK(!key2C.VerifyPubKey(pubkey1));
    BOOST_CHECK(!key2C.VerifyPubKey(pubkey1C));
    BOOST_CHECK(!key2C.VerifyPubKey(pubkey2));
    BOOST_CHECK(key2C.VerifyPubKey(pubkey2C));

    BOOST_CHECK(DecodeDestination(pkh_addr1)  == CTxDestination(PKHash(pubkey1)));
    BOOST_CHECK(DecodeDestination(pkh_addr2)  == CTxDestination(PKHash(pubkey2)));
    BOOST_CHECK(DecodeDestination(pkh_addr1C) == CTxDestination(PKHash(pubkey1C)));
    BOOST_CHECK(DecodeDestination(pkh_addr2C) == CTxDestination(PKHash(pubkey2C)));

    for (int n=0; n<16; n++)
    {
        std::string strMsg = strprintf("Very secret message %i: 11", n);
        uint256 hashMsg = Hash(strMsg.begin(), strMsg.end());

        // normal signatures

        std::vector<unsigned char> sign1, sign2, sign1C, sign2C;

        BOOST_CHECK(key1.Sign (hashMsg, sign1));
        BOOST_CHECK(key2.Sign (hashMsg, sign2));
        BOOST_CHECK(key1C.Sign(hashMsg, sign1C));
        BOOST_CHECK(key2C.Sign(hashMsg, sign2C));

        BOOST_CHECK( pubkey1.Verify(hashMsg, sign1));
        BOOST_CHECK(!pubkey1.Verify(hashMsg, sign2));
        BOOST_CHECK( pubkey1.Verify(hashMsg, sign1C));
        BOOST_CHECK(!pubkey1.Verify(hashMsg, sign2C));

        BOOST_CHECK(!pubkey2.Verify(hashMsg, sign1));
        BOOST_CHECK( pubkey2.Verify(hashMsg, sign2));
        BOOST_CHECK(!pubkey2.Verify(hashMsg, sign1C));
        BOOST_CHECK( pubkey2.Verify(hashMsg, sign2C));

        BOOST_CHECK( pubkey1C.Verify(hashMsg, sign1));
        BOOST_CHECK(!pubkey1C.Verify(hashMsg, sign2));
        BOOST_CHECK( pubkey1C.Verify(hashMsg, sign1C));
        BOOST_CHECK(!pubkey1C.Verify(hashMsg, sign2C));

        BOOST_CHECK(!pubkey2C.Verify(hashMsg, sign1));
        BOOST_CHECK( pubkey2C.Verify(hashMsg, sign2));
        BOOST_CHECK(!pubkey2C.Verify(hashMsg, sign1C));
        BOOST_CHECK( pubkey2C.Verify(hashMsg, sign2C));

        // compact signatures (with key recovery)

        std::vector<unsigned char> csign1, csign2, csign1C, csign2C;

        BOOST_CHECK(key1.SignCompact (hashMsg, csign1));
        BOOST_CHECK(key2.SignCompact (hashMsg, csign2));
        BOOST_CHECK(key1C.SignCompact(hashMsg, csign1C));
        BOOST_CHECK(key2C.SignCompact(hashMsg, csign2C));

        CPubKey rkey1, rkey2, rkey1C, rkey2C;

        BOOST_CHECK(rkey1.RecoverCompact (hashMsg, csign1));
        BOOST_CHECK(rkey2.RecoverCompact (hashMsg, csign2));
        BOOST_CHECK(rkey1C.RecoverCompact(hashMsg, csign1C));
        BOOST_CHECK(rkey2C.RecoverCompact(hashMsg, csign2C));

        BOOST_CHECK(rkey1  == pubkey1);
        BOOST_CHECK(rkey2  == pubkey2);
        BOOST_CHECK(rkey1C == pubkey1C);
        BOOST_CHECK(rkey2C == pubkey2C);
    }

    // test deterministic signing

    std::vector<unsigned char> detsig, detsigc;
    std::string strMsg = "Very deterministic message";
    uint256 hashMsg = Hash(strMsg.begin(), strMsg.end());
    BOOST_CHECK(key1.Sign(hashMsg, detsig));
    BOOST_CHECK(key1C.Sign(hashMsg, detsigc));
    BOOST_CHECK(detsig == detsigc);
    BOOST_CHECK(detsig == ParseHex("304402205dbbddda71772d95ce91cd2d14b592cfbc1dd0aabd6a394b6c2d377bbe59d31d022014ddda21494a4e221f0824f0b8b924c43fa43c0ad57dccdaa11f81a6bd4582f6"));
    BOOST_CHECK(key2.Sign(hashMsg, detsig));
    BOOST_CHECK(key2C.Sign(hashMsg, detsigc));
    BOOST_CHECK(detsig == detsigc);
    BOOST_CHECK(detsig == ParseHex("3044022052d8a32079c11e79db95af63bb9600c5b04f21a9ca33dc129c2bfa8ac9dc1cd5022061d8ae5e0f6c1a16bde3719c64c2fd70e404b6428ab9a69566962e8771b5944d"));
    BOOST_CHECK(key1.SignCompact(hashMsg, detsig));
    BOOST_CHECK(key1C.SignCompact(hashMsg, detsigc));
    BOOST_CHECK(detsig == ParseHex("1c5dbbddda71772d95ce91cd2d14b592cfbc1dd0aabd6a394b6c2d377bbe59d31d14ddda21494a4e221f0824f0b8b924c43fa43c0ad57dccdaa11f81a6bd4582f6"));
    BOOST_CHECK(detsigc == ParseHex("205dbbddda71772d95ce91cd2d14b592cfbc1dd0aabd6a394b6c2d377bbe59d31d14ddda21494a4e221f0824f0b8b924c43fa43c0ad57dccdaa11f81a6bd4582f6"));
    BOOST_CHECK(key2.SignCompact(hashMsg, detsig));
    BOOST_CHECK(key2C.SignCompact(hashMsg, detsigc));
    BOOST_CHECK(detsig == ParseHex("1c52d8a32079c11e79db95af63bb9600c5b04f21a9ca33dc129c2bfa8ac9dc1cd561d8ae5e0f6c1a16bde3719c64c2fd70e404b6428ab9a69566962e8771b5944d"));
    BOOST_CHECK(detsigc == ParseHex("2052d8a32079c11e79db95af63bb9600c5b04f21a9ca33dc129c2bfa8ac9dc1cd561d8ae5e0f6c1a16bde3719c64c2fd70e404b6428ab9a69566962e8771b5944d"));
}

BOOST_AUTO_TEST_CASE(key_signature_tests)
{
    // When entropy is specified, we should see at least one high R signature within 20 signatures
    CKey key = DecodeSecret(strSecret1);
    std::string msg = "A message to be signed";
    uint256 msg_hash = Hash(msg.begin(), msg.end());
    std::vector<unsigned char> sig;
    bool found = false;

    for (int i = 1; i <=20; ++i) {
        sig.clear();
        BOOST_CHECK(key.Sign(msg_hash, sig, false, i));
        found = sig[3] == 0x21 && sig[4] == 0x00;
        if (found) {
            break;
        }
    }
    BOOST_CHECK(found);

    // When entropy is not specified, we should always see low R signatures that are less than 70 bytes in 256 tries
    // We should see at least one signature that is less than 70 bytes.
    found = true;
    bool found_small = false;
    for (int i = 0; i < 256; ++i) {
        sig.clear();
        std::string msg = "A message to be signed" + std::to_string(i);
        msg_hash = Hash(msg.begin(), msg.end());
        BOOST_CHECK(key.Sign(msg_hash, sig));
        found = sig[3] == 0x20;
        BOOST_CHECK(sig.size() <= 70);
        found_small |= sig.size() < 70;
    }
    BOOST_CHECK(found);
    BOOST_CHECK(found_small);
}

BOOST_AUTO_TEST_CASE(key_key_negation)
{
    // create a dummy hash for signature comparison
    unsigned char rnd[8];
    std::string str = "Defi key verification\n";
    GetRandBytes(rnd, sizeof(rnd));
    uint256 hash;
    CHash256().Write((unsigned char*)str.data(), str.size()).Write(rnd, sizeof(rnd)).Finalize(hash.begin());

    // import the static test key
    CKey key = DecodeSecret(strSecret1C);

    // create a signature
    std::vector<unsigned char> vch_sig;
    std::vector<unsigned char> vch_sig_cmp;
    key.Sign(hash, vch_sig);

    // negate the key twice
    BOOST_CHECK(key.GetPubKey().data()[0] == 0x03);
    key.Negate();
    // after the first negation, the signature must be different
    key.Sign(hash, vch_sig_cmp);
    BOOST_CHECK(vch_sig_cmp != vch_sig);
    BOOST_CHECK(key.GetPubKey().data()[0] == 0x02);
    key.Negate();
    // after the second negation, we should have the original key and thus the
    // same signature
    key.Sign(hash, vch_sig_cmp);
    BOOST_CHECK(vch_sig_cmp == vch_sig);
    BOOST_CHECK(key.GetPubKey().data()[0] == 0x03);
}

BOOST_AUTO_TEST_CASE(pkh_key_test)
{
    CKey key1  = DecodeSecret(strSecret1);
    BOOST_CHECK(key1.IsValid() && !key1.IsCompressed());
    CKey key2  = DecodeSecret(strSecret2);
    BOOST_CHECK(key2.IsValid() && !key2.IsCompressed());
    CKey key1C = DecodeSecret(strSecret1C);
    BOOST_CHECK(key1C.IsValid() && key1C.IsCompressed());
    CKey key2C = DecodeSecret(strSecret2C);
    BOOST_CHECK(key2C.IsValid() && key2C.IsCompressed());
    CKey bad_key = DecodeSecret(strAddressBad);
    BOOST_CHECK(!bad_key.IsValid());

    CPubKey pubkey1  = key1. GetPubKey();
    CPubKey pubkey2  = key2. GetPubKey();
    CPubKey pubkey1C = key1C.GetPubKey();
    CPubKey pubkey2C = key2C.GetPubKey();

    BOOST_CHECK(key1.VerifyPubKey(pubkey1));
    BOOST_CHECK(!key1.VerifyPubKey(pubkey1C));
    BOOST_CHECK(!key1.VerifyPubKey(pubkey2));
    BOOST_CHECK(!key1.VerifyPubKey(pubkey2C));

    BOOST_CHECK(!key1C.VerifyPubKey(pubkey1));
    BOOST_CHECK(key1C.VerifyPubKey(pubkey1C));
    BOOST_CHECK(!key1C.VerifyPubKey(pubkey2));
    BOOST_CHECK(!key1C.VerifyPubKey(pubkey2C));

    BOOST_CHECK(!key2.VerifyPubKey(pubkey1));
    BOOST_CHECK(!key2.VerifyPubKey(pubkey1C));
    BOOST_CHECK(key2.VerifyPubKey(pubkey2));
    BOOST_CHECK(!key2.VerifyPubKey(pubkey2C));

    BOOST_CHECK(!key2C.VerifyPubKey(pubkey1));
    BOOST_CHECK(!key2C.VerifyPubKey(pubkey1C));
    BOOST_CHECK(!key2C.VerifyPubKey(pubkey2));
    BOOST_CHECK(key2C.VerifyPubKey(pubkey2C));

    BOOST_CHECK(DecodeDestination(pkh_addr1)  == CTxDestination(PKHash(pubkey1)));
    BOOST_CHECK(DecodeDestination(pkh_addr2)  == CTxDestination(PKHash(pubkey2)));
    BOOST_CHECK(DecodeDestination(pkh_addr1C) == CTxDestination(PKHash(pubkey1C)));
    BOOST_CHECK(DecodeDestination(pkh_addr2C) == CTxDestination(PKHash(pubkey2C)));

    BOOST_CHECK(pkh_addr1 == EncodeDestination(CTxDestination(PKHash(pubkey1))));
    BOOST_CHECK(pkh_addr2 == EncodeDestination(CTxDestination(PKHash(pubkey2))));
    BOOST_CHECK(pkh_addr1C == EncodeDestination(CTxDestination(PKHash(pubkey1C))));
    BOOST_CHECK(pkh_addr2C == EncodeDestination(CTxDestination(PKHash(pubkey2C))));

    // Test script to destination conversions
    CScript pkh_addr1_script = GetScriptForDestination(DecodeDestination(pkh_addr1));
    CScript pkh_addr2_script = GetScriptForDestination(DecodeDestination(pkh_addr2));
    CScript pkh_addr1C_script = GetScriptForDestination(DecodeDestination(pkh_addr1C));
    CScript pkh_addr2C_script = GetScriptForDestination(DecodeDestination(pkh_addr2C));

    CTxDestination pkh_addr1_script_dest;
    CTxDestination pkh_addr2_script_dest;
    CTxDestination pkh_addr1C_script_dest;
    CTxDestination pkh_addr2C_script_dest;
    ExtractDestination(pkh_addr1_script, pkh_addr1_script_dest);
    ExtractDestination(pkh_addr2_script, pkh_addr2_script_dest);
    ExtractDestination(pkh_addr1C_script, pkh_addr1C_script_dest);
    ExtractDestination(pkh_addr2C_script, pkh_addr2C_script_dest);

    BOOST_CHECK(pkh_addr1 == EncodeDestination(pkh_addr1_script_dest));
    BOOST_CHECK(pkh_addr2 == EncodeDestination(pkh_addr2_script_dest));
    BOOST_CHECK(pkh_addr1C == EncodeDestination(pkh_addr1C_script_dest));
    BOOST_CHECK(pkh_addr2C == EncodeDestination(pkh_addr2C_script_dest));
}

BOOST_AUTO_TEST_CASE(serialised_address_from_block_test)
{
    // Addresses
    auto bech32 = "bcrt1qta8meuczw0mhqupzjl5wplz47xajz0dn0wxxr8";
    auto eth = "0x9b8a4af42140d8a4c153a822f02571a1dd037e89";

    // CKeyIDs taken from serialised block
    auto bech32Hex = "5f4fbcf30273f770702297e8e0fc55f1bb213db3";
    auto ethHex = "897e03dda17125f022a853c1a4d84021f44a8a9b";

    // Encode Bech32
    auto bech32Vec = ParseHex(bech32Hex);
    std::vector<unsigned char> data = {0};
    data.reserve(33);
    ConvertBits<8, 5, true>([&](unsigned char c) { data.push_back(c); }, bech32Vec.begin(), bech32Vec.end());
    auto bech32Encoded = bech32::Encode("bcrt", data);

    // Encode Eth
    auto ethVec = ParseHex(ethHex);
    auto ethID = HexStr(ethVec.rbegin(), ethVec.rend());
    std::vector<uint8_t> ethOutput, ethInput(ethID.begin(), ethID.end());
    sha3_256_safe(ethInput, ethOutput);
    auto hashedAddress = HexStr(ethOutput);
    std::string ethEncoded = "0x";
    for (size_t i{}; i < ethID.size(); ++i) {
        if (std::isdigit(ethID[i]) || hashedAddress[i] < '8') {
            ethEncoded += ethID[i];
        } else {
            ethEncoded += std::toupper(ethID[i]);
        }
    }

    // Check results match
    BOOST_CHECK_EQUAL(bech32, bech32Encoded);
    BOOST_CHECK_EQUAL(eth, ethEncoded);
}

BOOST_AUTO_TEST_CASE(wpkh_key_test)
{
    CKey key1  = DecodeSecret(strSecret1);
    BOOST_CHECK(key1.IsValid() && !key1.IsCompressed());
    CKey key2  = DecodeSecret(strSecret2);
    BOOST_CHECK(key2.IsValid() && !key2.IsCompressed());
    CKey key1C = DecodeSecret(strSecret1C);
    BOOST_CHECK(key1C.IsValid() && key1C.IsCompressed());
    CKey key2C = DecodeSecret(strSecret2C);
    BOOST_CHECK(key2C.IsValid() && key2C.IsCompressed());
    CKey bad_key = DecodeSecret(strAddressBad);
    BOOST_CHECK(!bad_key.IsValid());

    CPubKey pubkey1  = key1. GetPubKey();
    CPubKey pubkey2  = key2. GetPubKey();
    CPubKey pubkey1C = key1C.GetPubKey();
    CPubKey pubkey2C = key2C.GetPubKey();

    BOOST_CHECK(key1.VerifyPubKey(pubkey1));
    BOOST_CHECK(!key1.VerifyPubKey(pubkey1C));
    BOOST_CHECK(!key1.VerifyPubKey(pubkey2));
    BOOST_CHECK(!key1.VerifyPubKey(pubkey2C));

    BOOST_CHECK(!key1C.VerifyPubKey(pubkey1));
    BOOST_CHECK(key1C.VerifyPubKey(pubkey1C));
    BOOST_CHECK(!key1C.VerifyPubKey(pubkey2));
    BOOST_CHECK(!key1C.VerifyPubKey(pubkey2C));

    BOOST_CHECK(!key2.VerifyPubKey(pubkey1));
    BOOST_CHECK(!key2.VerifyPubKey(pubkey1C));
    BOOST_CHECK(key2.VerifyPubKey(pubkey2));
    BOOST_CHECK(!key2.VerifyPubKey(pubkey2C));

    BOOST_CHECK(!key2C.VerifyPubKey(pubkey1));
    BOOST_CHECK(!key2C.VerifyPubKey(pubkey1C));
    BOOST_CHECK(!key2C.VerifyPubKey(pubkey2));
    BOOST_CHECK(key2C.VerifyPubKey(pubkey2C));

    BOOST_CHECK(DecodeDestination(wpkh_addr1)  == CTxDestination(WitnessV0KeyHash(pubkey1)));
    BOOST_CHECK(DecodeDestination(wpkh_addr2)  == CTxDestination(WitnessV0KeyHash(pubkey2)));
    BOOST_CHECK(DecodeDestination(wpkh_addr1C) == CTxDestination(WitnessV0KeyHash(pubkey1C)));
    BOOST_CHECK(DecodeDestination(wpkh_addr2C) == CTxDestination(WitnessV0KeyHash(pubkey2C)));

    BOOST_CHECK(wpkh_addr1 == EncodeDestination(CTxDestination(WitnessV0KeyHash(pubkey1))));
    BOOST_CHECK(wpkh_addr2 == EncodeDestination(CTxDestination(WitnessV0KeyHash(pubkey2))));
    BOOST_CHECK(wpkh_addr1C == EncodeDestination(CTxDestination(WitnessV0KeyHash(pubkey1C))));
    BOOST_CHECK(wpkh_addr2C == EncodeDestination(CTxDestination(WitnessV0KeyHash(pubkey2C))));

    // Test script to destination conversions
    CScript wpkh_addr1_script = GetScriptForDestination(DecodeDestination(wpkh_addr1));
    CScript wpkh_addr2_script = GetScriptForDestination(DecodeDestination(wpkh_addr2));
    CScript wpkh_addr1C_script = GetScriptForDestination(DecodeDestination(wpkh_addr1C));
    CScript wpkh_addr2C_script = GetScriptForDestination(DecodeDestination(wpkh_addr2C));

    CTxDestination wpkh_addr1_script_dest;
    CTxDestination wpkh_addr2_script_dest;
    CTxDestination wpkh_addr1C_script_dest;
    CTxDestination wpkh_addr2C_script_dest;
    ExtractDestination(wpkh_addr1_script, wpkh_addr1_script_dest);
    ExtractDestination(wpkh_addr2_script, wpkh_addr2_script_dest);
    ExtractDestination(wpkh_addr1C_script, wpkh_addr1C_script_dest);
    ExtractDestination(wpkh_addr2C_script, wpkh_addr2C_script_dest);

    BOOST_CHECK(wpkh_addr1 == EncodeDestination(wpkh_addr1_script_dest));
    BOOST_CHECK(wpkh_addr2 == EncodeDestination(wpkh_addr2_script_dest));
    BOOST_CHECK(wpkh_addr1C == EncodeDestination(wpkh_addr1C_script_dest));
    BOOST_CHECK(wpkh_addr2C == EncodeDestination(wpkh_addr2C_script_dest));
}

BOOST_AUTO_TEST_CASE(erc55_key_test)
{
    CKey key1  = DecodeSecret(strSecret1);
    BOOST_CHECK(key1.IsValid() && !key1.IsCompressed());
    CKey key2  = DecodeSecret(strSecret2);
    BOOST_CHECK(key2.IsValid() && !key2.IsCompressed());
    CKey key1C = DecodeSecret(strSecret1C);
    BOOST_CHECK(key1C.IsValid() && key1C.IsCompressed());
    CKey key2C = DecodeSecret(strSecret2C);
    BOOST_CHECK(key2C.IsValid() && key2C.IsCompressed());
    CKey bad_key = DecodeSecret(strAddressBad);
    BOOST_CHECK(!bad_key.IsValid());

    CPubKey pubkey1  = key1. GetPubKey();
    CPubKey pubkey2  = key2. GetPubKey();
    CPubKey pubkey1C = key1C.GetPubKey();
    CPubKey pubkey2C = key2C.GetPubKey();

    BOOST_CHECK(key1.VerifyPubKey(pubkey1));
    BOOST_CHECK(!key1.VerifyPubKey(pubkey1C));
    BOOST_CHECK(!key1.VerifyPubKey(pubkey2));
    BOOST_CHECK(!key1.VerifyPubKey(pubkey2C));

    BOOST_CHECK(!key1C.VerifyPubKey(pubkey1));
    BOOST_CHECK(key1C.VerifyPubKey(pubkey1C));
    BOOST_CHECK(!key1C.VerifyPubKey(pubkey2));
    BOOST_CHECK(!key1C.VerifyPubKey(pubkey2C));

    BOOST_CHECK(!key2.VerifyPubKey(pubkey1));
    BOOST_CHECK(!key2.VerifyPubKey(pubkey1C));
    BOOST_CHECK(key2.VerifyPubKey(pubkey2));
    BOOST_CHECK(!key2.VerifyPubKey(pubkey2C));

    BOOST_CHECK(!key2C.VerifyPubKey(pubkey1));
    BOOST_CHECK(!key2C.VerifyPubKey(pubkey1C));
    BOOST_CHECK(!key2C.VerifyPubKey(pubkey2));
    BOOST_CHECK(key2C.VerifyPubKey(pubkey2C));

    BOOST_CHECK(DecodeDestination(erc55_addr1)  == CTxDestination(WitnessV16EthHash(pubkey1)));
    BOOST_CHECK(DecodeDestination(erc55_addr2)  == CTxDestination(WitnessV16EthHash(pubkey2)));
    BOOST_CHECK(DecodeDestination(erc55_addr1C) == CTxDestination(WitnessV16EthHash(pubkey1C)));
    BOOST_CHECK(DecodeDestination(erc55_addr2C) == CTxDestination(WitnessV16EthHash(pubkey2C)));

    BOOST_CHECK(erc55_addr1 == EncodeDestination(CTxDestination(WitnessV16EthHash(pubkey1))));
    BOOST_CHECK(erc55_addr2 == EncodeDestination(CTxDestination(WitnessV16EthHash(pubkey2))));
    BOOST_CHECK(erc55_addr1C == EncodeDestination(CTxDestination(WitnessV16EthHash(pubkey1C))));
    BOOST_CHECK(erc55_addr2C == EncodeDestination(CTxDestination(WitnessV16EthHash(pubkey2C))));

    // Test script to destination conversions
    CScript erc55_addr1_script = GetScriptForDestination(DecodeDestination(erc55_addr1));
    CScript erc55_addr2_script = GetScriptForDestination(DecodeDestination(erc55_addr2));
    CScript erc55_addr1C_script = GetScriptForDestination(DecodeDestination(erc55_addr1C));
    CScript erc55_addr2C_script = GetScriptForDestination(DecodeDestination(erc55_addr2C));

    CTxDestination erc55_addr1_script_dest;
    CTxDestination erc55_addr2_script_dest;
    CTxDestination erc55_addr1C_script_dest;
    CTxDestination erc55_addr2C_script_dest;
    ExtractDestination(erc55_addr1_script, erc55_addr1_script_dest);
    ExtractDestination(erc55_addr2_script, erc55_addr2_script_dest);
    ExtractDestination(erc55_addr1C_script, erc55_addr1C_script_dest);
    ExtractDestination(erc55_addr2C_script, erc55_addr2C_script_dest);

    BOOST_CHECK(erc55_addr1 == EncodeDestination(erc55_addr1_script_dest));
    BOOST_CHECK(erc55_addr2 == EncodeDestination(erc55_addr2_script_dest));
    BOOST_CHECK(erc55_addr1C == EncodeDestination(erc55_addr1C_script_dest));
    BOOST_CHECK(erc55_addr2C == EncodeDestination(erc55_addr2C_script_dest));
}

BOOST_AUTO_TEST_SUITE_END()
