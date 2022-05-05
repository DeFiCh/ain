// Copyright (c) 2016-2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <bench/bench.h>

#include <validation.h>
#include <streams.h>
#include <consensus/validation.h>
#include <rpc/blockchain.h>

#include <univalue.h>

static void BlockToJsonVerbose(benchmark::State& state) {

    const auto chainParams = CreateChainParams(CBaseChainParams::MAIN);

    CDataStream stream(SER_NETWORK, PROTOCOL_VERSION);
    stream << chainParams->GenesisBlock();

    char a = '\0';
    stream.write(&a, 1); // Prevent compaction

    CBlock block;
    stream >> block;

    CBlockIndex blockindex;
    const uint256 blockHash = block.GetHash();
    blockindex.phashBlock = &blockHash;
    blockindex.nBits = 403014710;

    while (state.KeepRunning()) {
        (void)blockToJSON(block, &blockindex, &blockindex, /*verbose*/ true);
    }
}

BENCHMARK(BlockToJsonVerbose, 10);
