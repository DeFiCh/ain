// Copyright (c) 2017-2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

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

BOOST_FIXTURE_TEST_CASE(tx_transaction_compatibility_2, TestChain100Setup)
{
    CTxOut::SERIALIZE_FORCED_TO_OLD_IN_TESTS = false;
    CScript scriptPubKey = CScript() << ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG;
    CMutableTransaction tx;
    tx.vin.resize(1);
    tx.vout.resize(1);
    tx.vin[0].scriptSig = CScript() << OP_11 << OP_EQUAL;
    tx.vout[0].nValue = 1 * CENT;
    tx.vout[0].scriptPubKey = scriptPubKey;
    tx.vout[0].nTokenId = DCT_ID{100};
    const std::vector<int> streamTypes{SER_NETWORK, SER_DISK, SER_GETHASH};
    for (const auto streamType : streamTypes)
    {
        // Serialize the transaction and put to a buffer with nTokenId
        CDataStream stream(streamType, PROTOCOL_VERSION);
        stream << tx;
        const auto txStr = stream.str();
        CSerializeData txBinary(txStr.begin(), txStr.end());
        // Use the DataStream to laod it again
        CDataStream loadStreamNew(txBinary.begin(), txBinary.end(), streamType, PROTOCOL_VERSION);
        CMutableTransaction txLoadNew;
        loadStreamNew >> txLoadNew;
        // Check the load result is same as original result
        BOOST_CHECK(CTransaction(tx) == CTransaction(txLoadNew));
    }
    CTxOut::SERIALIZE_FORCED_TO_OLD_IN_TESTS = true;
}

BOOST_AUTO_TEST_SUITE_END()
