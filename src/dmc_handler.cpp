#include <dmc_handler.h>
#include <functional>
#include <libain_rpc.h>
#include <libmc.h>
#include <miner.h>
#include <consensus/consensus.h>
#include <consensus/tx_verify.h>
#include <masternodes/mn_checks.h>
#include <primitives/transaction.h>
#include <rpc/rawtransaction_util.h>
#include <util/strencodings.h>
#include <util/system.h>

std::string rpc_url;
bool is_ffi = true;  // FIXME: Set this to `false` if both -meta and -meta_rpc is missing


bool SetupMetachainHandler() {
    auto url = gArgs.GetArg("-meta_rpc", "");
    if (!url.empty()) {
        // Test the instantiation of client for this URL. Calling this multiple
        // times wouldn't hurt because it's a singleton for each URL in libain
        NewClient(url);
        rpc_url = url;
        is_ffi = false;
    }

    return !url.empty();
}

Res MintDmcBlock(CBlock& pblock, int nHeight) {
    auto dmcTxIter = [&](std::function<void(std::string, std::string, int64_t)> call) {
        for (size_t i = 0; i < pblock.vtx.size(); i++) {
            auto tx = pblock.vtx[i];
            std::vector<unsigned char> metadata;
            const auto txType = GuessCustomTxType(*tx.get(), metadata);
            if (txType == CustomTxType::MetachainBridge) {
                auto txMessage = customTypeToMessage(txType);
                auto res = CustomMetadataParse(static_cast<uint32_t>(nHeight), Params().GetConsensus(), metadata, txMessage);
                if (!res) {
                    return res;
                }
                auto mcMsg = std::get<CMetachainMessage>(txMessage);
                if (mcMsg.direction == CMetachainMessage::Direction::ToMetachain) {
                    call(mcMsg.from.GetHex(), mcMsg.to.GetHex(), int64_t(mcMsg.amount));
                }
            }
        }
        return Res::Ok();
    };

    if (is_ffi) {
        std::vector<DmcTx> txs;
        auto res = dmcTxIter([&](std::string from, std::string to, int64_t amount) {
            txs.push_back(DmcTx{from, to, amount});
        });
        if (!res) {
            return res;
        }

        try {
            auto block = mint_block(txs);
            std::copy(block.payload.begin(), block.payload.end(), std::back_inserter(pblock.dmc_payload));
        } catch (const std::exception& e) {
            return Res::Err(e.what());
        }
    } else {
        auto inp = MakeMetaBlockInput();
        auto res = dmcTxIter([&](std::string from, std::string to, int64_t amount) {
            inp.txs.push_back(MetaTransaction{from, to, amount});
        });
        if (!res) {
            return res;
        }

        try {
            auto client = NewClient(rpc_url);
            auto block = CallMetaMintBlock(client, inp);
            std::copy(block.payload.begin(), block.payload.end(), std::back_inserter(pblock.dmc_payload));
        } catch (const std::exception& e) {
            return Res::Err(e.what());
        }
    }

    return Res::Ok();
}

Res ConnectDmcBlock(const CBlock& block) {
    return Res::Ok();
}

Res AddNativeTx(CBlock& pblock, std::unique_ptr<CBlockTemplate> &pblocktemplate, int32_t txVersion, int nHeight) {
    std::vector<CMutableTransaction> txs;

    // NOTE: List of all relevant transactions i.e., only those tx's involving funds
    // that have been burned at DMC and supposed to be moved over to native
    std::vector<DmcTx> incoming; // TODO: Call metachain for getting associated tx

    for (size_t i = 0; i < incoming.size(); i++) {
        auto tx = incoming[i];
        CMetachainMessage msg{};
        msg.direction = CMetachainMessage::Direction::FromMetachain;
        try {
            msg.from = DecodeScript(std::string(tx.from)); // metachain address
            msg.to = DecodeMetachainAddress(std::string(tx.to)); // native chain address
        } catch (const std::exception& e) {
            return Res::Err(e.what());
        }
        msg.amount = CAmount(tx.amount);

        CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
        metadata << static_cast<unsigned char>(CustomTxType::MetachainBridge)
                 << msg;

        CMutableTransaction mTx(txVersion);
        mTx.vin.resize(1);
        mTx.vin[0].prevout.SetNull();
        mTx.vin[0].scriptSig = CScript() << nHeight << OP_0;
        mTx.vout.resize(1);
        mTx.vout[0].scriptPubKey = CScript() << OP_RETURN << ToByteVector(metadata);
        mTx.vout[0].nValue = 0;
        pblock.vtx.push_back(MakeTransactionRef(std::move(mTx)));
        pblocktemplate->vTxFees.push_back(0);
        pblocktemplate->vTxSigOpsCost.push_back(WITNESS_SCALE_FACTOR * GetLegacySigOpCount(*pblock.vtx.back()));
    }

    return Res::Ok();
}
