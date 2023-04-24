#include <ffi/ffiexports.h>
#include <util/system.h>
#include <masternodes/mn_rpc.h>
#include <key_io.h>


uint64_t getChainId() {
    return Params().GetConsensus().evmChainId;
}

bool isMining() {
    return gArgs.GetBoolArg("-gen", false);
}

bool publishEthTransaction(rust::Vec<uint8_t> rawTransaction) {
    std::vector<uint8_t> evmTx(rawTransaction.size());
    std::copy(rawTransaction.begin(), rawTransaction.end(), evmTx.begin());
    CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(CustomTxType::EvmTx)
             << CEvmTxMessage{evmTx};

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(metadata);

    int targetHeight;
    {
        LOCK(cs_main);
        targetHeight = ::ChainActive().Height() + 1;
    }

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);

    rawTx.vin.resize(2);
    rawTx.vin[0].scriptSig = CScript() << OP_0;
    rawTx.vin[1].scriptSig = CScript() << OP_0;

    rawTx.vout.emplace_back(0, scriptMeta);

    // check execution
    CTransactionRef optAuthTx;

    // TODO Replace execTestTx with non-throwing function
    try {
        execTestTx(CTransaction(rawTx), targetHeight, optAuthTx);
        send(MakeTransactionRef(std::move(rawTx)), optAuthTx)->GetHash().ToString();
    } catch (...) {
        return false;
    }

    return true;
}

rust::vec<rust::string> getAccounts() {
    rust::vec<rust::string> addresses;
    std::vector<std::shared_ptr<CWallet>> const wallets = GetWallets();
    for (const std::shared_ptr<CWallet>& wallet : wallets) {
        for (auto & it : wallet->mapAddressBook)
            if (std::holds_alternative<WitnessV16EthHash>(it.first)) {
                addresses.push_back(EncodeDestination(it.first));
            }
    }
    return addresses;
}

rust::string getDatadir() {
    return GetDataDir().c_str();
}

uint32_t getDifficulty(std::array<uint8_t, 32> blockHash) {
    uint256 hash{};
    std::copy(blockHash.begin(), blockHash.end(), hash.begin());

    const CBlockIndex* pblockindex;
    uint32_t difficulty{};
    {
        LOCK(cs_main);
        pblockindex = LookupBlockIndex(hash);

        if (!pblockindex) {
            return difficulty;
        }

        difficulty = pblockindex->nBits;
    }

    return difficulty;
}

std::array<uint8_t, 32> getChainWork(std::array<uint8_t, 32> blockHash) {
    uint256 hash{};
    std::copy(blockHash.begin(), blockHash.end(), hash.begin());

    const CBlockIndex* pblockindex;
    std::array<uint8_t, 32> chainWork{};
    {
        LOCK(cs_main);
        pblockindex = LookupBlockIndex(hash);

        if (!pblockindex) {
            return chainWork;
        }

        const auto sourceWork = ArithToUint256(pblockindex->nChainWork);
        std::copy(sourceWork.begin(), sourceWork.end(), chainWork.begin());
    }

    return chainWork;
}

rust::vec<rust::vec<uint8_t>> getPoolTransactions() {
    rust::vec<rust::vec<uint8_t>> poolTransactions;

    for (auto mi = mempool.mapTx.get<entry_time>().begin(); mi != mempool.mapTx.get<entry_time>().end(); ++mi) {
        const auto &tx = mi->GetTx();
        if (!IsEVMTx(tx)) {
            continue;
        }

        std::vector<unsigned char> metadata;
        const auto txType = GuessCustomTxType(tx, metadata, true);
        if (txType != CustomTxType::EvmTx) {
            continue;
        }

        CCustomTxMessage txMessage{CEvmTxMessage{}};
        const auto res = CustomMetadataParse(std::numeric_limits<uint32_t>::max(), Params().GetConsensus(), metadata, txMessage);
        if (!res) {
            continue;
        }

        const auto obj = std::get<CEvmTxMessage>(txMessage);

        rust::vec<uint8_t> transaction;
        transaction.reserve(obj.evmTx.size());
        std::copy(obj.evmTx.begin(), obj.evmTx.end(), transaction.begin());

        poolTransactions.push_back(transaction);
    }

    return poolTransactions;
}
