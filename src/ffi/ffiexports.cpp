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
            addresses.push_back(EncodeDestination(it.first));
    }
    return addresses;
}
