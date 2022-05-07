// Copyright (c) 2016-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <bench/bench.h>

#include <chainparams.h>
#include <validation.h>
#include <streams.h>
#include <sync.h>
#include <consensus/validation.h>

// These are the two major time-sinks which happen after we have fully received
// a block off the wire, but before we can relay the block on to peers using
// compact block relay.

static void DeserializeBlockTest(benchmark::State& state)
{
    const auto chainParams = CreateChainParams(CBaseChainParams::MAIN);

    CDataStream stream(SER_NETWORK, PROTOCOL_VERSION);
    stream << chainParams->GenesisBlock();
    auto size = stream.size();

    char a = '\0';
    stream.write(&a, 1); // Prevent compaction

    while (state.KeepRunning()) {
        CBlock block;
        stream >> block;
        bool rewound = stream.Rewind(size);
        assert(rewound);
    }
}

static void DeserializeAndCheckBlockTest(benchmark::State& state)
{
    const auto chainParams = CreateChainParams(CBaseChainParams::MAIN);

    CDataStream stream(SER_NETWORK, PROTOCOL_VERSION);

    stream << chainParams->GenesisBlock();
    auto size = stream.size();

    char a = '\0';
    stream.write(&a, 1); // Prevent compaction

    while (state.KeepRunning()) {
        CBlock block; // Note that CBlock caches its checked state, so we need to recreate it here
        stream >> block;
        bool rewound = stream.Rewind(size);
        assert(rewound);

        CValidationState validationState;
        CheckContextState ctxState;

        LockAssertion lock(cs_main);
        bool checked = CheckBlock(block, validationState, chainParams->GetConsensus(), ctxState, false, 413567);
        assert(checked);
    }
}

BENCHMARK(DeserializeBlockTest, 130);
BENCHMARK(DeserializeAndCheckBlockTest, 160);
