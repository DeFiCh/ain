#include <masternodes/mn_rpc.h>

UniValue evmtx(const JSONRPCRequest& request) {

    RPCHelpMan{"evmtx",
                "Creates (and submits to local node and network) a tx to send DFI token to EVM address.\n",
                {
                    {"rawEvmTx", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "Raw tx in hex"},
                },
                RPCResult{
                        "\"hash\"                  (string) The hex-encoded hash of broadcasted transaction\n"
                },
                RPCExamples{
                        HelpExampleCli("evmtx", R"('"<hex>"')")
                        },
    }.Check(request);

    auto evmTx = ParseHex(request.params[0].get_str());

    int targetHeight;
    {
        LOCK(cs_main);
        targetHeight = ::ChainActive().Height() + 1;
    }

    CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(CustomTxType::EvmTx)
                << CEvmTxMessage{evmTx};

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(metadata);

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);

    rawTx.vin.resize(2);
    rawTx.vin[0].scriptSig = CScript() << OP_0;
    rawTx.vin[1].scriptSig = CScript() << OP_0;

    rawTx.vout.emplace_back(0, scriptMeta);

    // check execution
    CTransactionRef optAuthTx;
    execTestTx(CTransaction(rawTx), targetHeight, optAuthTx);

    return send(MakeTransactionRef(std::move(rawTx)), optAuthTx)->GetHash().ToString();
}

static const CRPCCommand commands[] =
{
//  category        name                         actor (function)        params
//  --------------- ----------------------       ---------------------   ----------
    {"evm",         "evmtx",                     &evmtx,                 {"rawEvmTx", "inputs"}},
};

void RegisterEVMRPCCommands(CRPCTable& tableRPC) {
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
