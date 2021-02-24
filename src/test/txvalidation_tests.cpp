// Copyright (c) 2017-2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <validation.h>
#include <consensus/validation.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <test/setup_common.h>

#include <boost/test/unit_test.hpp>


BOOST_AUTO_TEST_SUITE(txvalidation_tests)

/**
 * Ensure that the mempool won't accept coinbase transactions.
 */
BOOST_FIXTURE_TEST_CASE(tx_mempool_reject_coinbase, TestChain100Setup)
{
    CScript scriptPubKey = CScript() << ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG;
    CMutableTransaction coinbaseTx;

    coinbaseTx.nVersion = 1;
    coinbaseTx.vin.resize(1);
    coinbaseTx.vout.resize(1);
    coinbaseTx.vin[0].scriptSig = CScript() << OP_11 << OP_EQUAL;
    coinbaseTx.vout[0].nValue = 1 * CENT;
    coinbaseTx.vout[0].scriptPubKey = scriptPubKey;

    BOOST_CHECK(CTransaction(coinbaseTx).IsCoinBase());

    CValidationState state;

    LOCK(cs_main);

    unsigned int initialPoolSize = mempool.size();

    BOOST_CHECK_EQUAL(
            false,
            AcceptToMemoryPool(mempool, state, MakeTransactionRef(coinbaseTx),
                nullptr /* pfMissingInputs */,
                nullptr /* plTxnReplaced */,
                true /* bypass_limits */,
                0 /* nAbsurdFee */));

    // Check that the transaction hasn't been added to mempool.
    BOOST_CHECK_EQUAL(mempool.size(), initialPoolSize);

    // Check that the validation state reflects the unsuccessful attempt.
    BOOST_CHECK(state.IsInvalid());
    BOOST_CHECK_EQUAL(state.GetRejectReason(), "coinbase");
    BOOST_CHECK(state.GetReason() == ValidationInvalidReason::CONSENSUS);
}

BOOST_FIXTURE_TEST_CASE(tx_check_transaction_size, TestChain100Setup)
{
    CScript scriptPubKey = CScript() << ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG;
    CMutableTransaction tx;

    tx.nVersion = CTransaction::TX_VERSION_2;
    tx.vin.resize(1);
    tx.vout.resize(1);
    tx.vin[0].scriptSig = CScript() << OP_11 << OP_EQUAL;
    tx.vout[0].nValue = 1 * CENT;
    tx.vout[0].scriptPubKey = scriptPubKey;

    int size = ::GetSerializeSize(tx, PROTOCOL_VERSION | (tx.nVersion < CTransaction::TOKENS_MIN_VERSION ? SERIALIZE_TRANSACTION_NO_TOKENS : 0));
    BOOST_CHECK(size == 97);

    CTxOut::SERIALIZE_FORCED_TO_OLD_IN_TESTS = false;
    tx.nVersion = CTransaction::TOKENS_MIN_VERSION;
    tx.vout[0].nTokenId.v = 1;
    size = ::GetSerializeSize(tx, PROTOCOL_VERSION | (tx.nVersion < CTransaction::TOKENS_MIN_VERSION ? SERIALIZE_TRANSACTION_NO_TOKENS : 0));
    BOOST_CHECK(size == 98);

    CTxOut::SERIALIZE_FORCED_TO_OLD_IN_TESTS = true;
}

BOOST_FIXTURE_TEST_CASE(tx_transaction_compatibility, TestChain100Setup)
{
    CTxOut::SERIALIZE_FORCED_TO_OLD_IN_TESTS = false;

    CScript scriptPubKey = CScript() << ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG;
    CMutableTransaction tx;

    tx.nVersion = 2;
    tx.vin.resize(1);
    tx.vout.resize(1);
    tx.vin[0].scriptSig = CScript() << OP_11 << OP_EQUAL;
    tx.vout[0].nValue = 1 * CENT;
    tx.vout[0].scriptPubKey = scriptPubKey;

    // Serialize the transaction and put to a buffer with SERIALIZE_TRANSACTION_NO_TOKENS flag,
    // So the TokenId will not be serialized.
    CDataStream stream(SER_NETWORK, PROTOCOL_VERSION | SERIALIZE_TRANSACTION_NO_TOKENS);
    stream << tx;
    const auto txStr = stream.str();
    CSerializeData txBinary(txStr.begin(), txStr.end());

    // Use the DataStream without SERIALIZE_TRANSACTION_NO_TOKENS to load, it should pass
    CDataStream loadStreamNew(txBinary.begin(), txBinary.end(), SER_NETWORK, PROTOCOL_VERSION);
    CMutableTransaction txLoadNew;
    loadStreamNew >> txLoadNew;

    // Load fail, so hash not match
    BOOST_CHECK(CTransaction(tx) == CTransaction(txLoadNew));

    // Use the DataStream with SERIALIZE_TRANSACTION_NO_TOKENS to load, it must pass
    CDataStream loadStreamGood(txBinary.begin(), txBinary.end(), SER_NETWORK, PROTOCOL_VERSION | SERIALIZE_TRANSACTION_NO_TOKENS);
    CMutableTransaction txLoad;
    loadStreamGood >> txLoad;

    BOOST_CHECK(CTransaction(tx) == CTransaction(txLoad));

    CTxOut::SERIALIZE_FORCED_TO_OLD_IN_TESTS = true;
}

BOOST_FIXTURE_TEST_CASE(tx_transaction_compatibility_2, TestChain100Setup)
{
    CTxOut::SERIALIZE_FORCED_TO_OLD_IN_TESTS = false;

    CScript scriptPubKey = CScript() << ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG;
    CMutableTransaction tx(CTransaction::TOKENS_MIN_VERSION);

    tx.vin.resize(1);
    tx.vout.resize(1);
    tx.vin[0].scriptSig = CScript() << OP_11 << OP_EQUAL;
    tx.vout[0].nValue = 1 * CENT;
    tx.vout[0].scriptPubKey = scriptPubKey;
    tx.vout[0].nTokenId = DCT_ID{100};

    const std::vector<int> streamTypes{SER_NETWORK, SER_DISK, SER_GETHASH};

    for (const auto streamType : streamTypes)
    {
        // Serialize the transaction with TokenId and put to a buffer
        CDataStream stream(streamType, PROTOCOL_VERSION);
        stream << tx;
        const auto txStr = stream.str();
        CSerializeData txBinary(txStr.begin(), txStr.end());

        // Use the DataStream without SERIALIZE_TRANSACTION_NO_TOKENS to load, it should pass
        CDataStream loadStreamNew(txBinary.begin(), txBinary.end(), streamType, PROTOCOL_VERSION);
        CMutableTransaction txLoadNew;
        loadStreamNew >> txLoadNew;

        // Load fail, so hash not match
        BOOST_CHECK(CTransaction(tx) == CTransaction(txLoadNew));

        // Use the DataStream with SERIALIZE_TRANSACTION_NO_TOKENS to load
        CDataStream loadStreamGood(txBinary.begin(), txBinary.end(), streamType, PROTOCOL_VERSION | SERIALIZE_TRANSACTION_NO_TOKENS);
        CMutableTransaction txLoad;
        loadStreamGood >> txLoad;

        // Because we didn't load Token Id, so the hash is not equal
        BOOST_CHECK(CTransaction(tx) != CTransaction(txLoad));
    }

    CTxOut::SERIALIZE_FORCED_TO_OLD_IN_TESTS = true;
}

BOOST_AUTO_TEST_SUITE_END()
