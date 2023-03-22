#include <consensus/validation.h>
#include <core_io.h>
#include <libain_grpc.h>
#include <masternodes/masternodes.h>
#include <rpc/blockchain.h>
#include <rpc/libain_wrapper.h>
#include <rpc/util.h>
#include <sync.h>
#include <validation.h>

void Eth_Accounts(EthAccountsResult& result)
{
    LOCK(cs_main);

    std::vector<std::string> accounts;
    // Get eth accounts from wallet

    // result.accounts = accounts;
}

void Eth_Call(EthCallInput& request, EthCallResult& result)
{
    LOCK(cs_main);

    // TODO
    // Call sputnikVM FFI call method with request fields
    // return output of sputnikVM call

    // result.data = "done";
}

void Eth_Sign(EthSignInput& request, EthSignResult& result)
{
    LOCK(cs_main);

    // TODO
    // Call sputnikVM FFI call method with request fields
    // return output of sputnikVM call

    // result.data = "done";
}

void Eth_GetBalance(EthGetBalanceInput& request, EthGetBalanceResult& result)
{
    LOCK(cs_main);

    // TODO
    // Call sputnikVM FFI call method with request fields
    // return output of sputnikVM call

    // result.data = "done";
}

void Eth_SendTransaction(EthSendTransactionInput& request, EthSendTransactionResult& result)
{
    LOCK(cs_main);

    // TODO
    // Call sputnikVM FFI call method with request fields
    // return output of sputnikVM call

    // result.data = "done";
}

void Eth_CoinBase(EthCoinBaseResult& result)
{
    LOCK(cs_main);

    // TODO
    // Call sputnikVM FFI call method with request fields
    // return output of sputnikVM call

    // result.data = "done";
}

void Eth_MiningResult(EthMiningResult& result)
{
    LOCK(cs_main);

    // TODO
    // Call sputnikVM FFI call method with request fields
    // return output of sputnikVM call

    // result.data = "done";
}

void Eth_HashRate(EthHashRateResult& result)
{
    LOCK(cs_main);

    // TODO
    // Call sputnikVM FFI call method with request fields
    // return output of sputnikVM call

    // result.data = "done";
}

void Eth_GasPrice(EthGasPriceResult& result)
{
    LOCK(cs_main);

    // TODO
    // Call sputnikVM FFI call method with request fields
    // return output of sputnikVM call

    // result.data = "done";
}

void Eth_BlockNumber(EthBlockNumberResult& result)
{
    LOCK(cs_main);

    // TODO
    // Call sputnikVM FFI call method with request fields
    // return output of sputnikVM call

    // result.data = "done";
}

void Eth_GetTransactionCount(EthGetTransactionCountInput& request, EthGetTransactionCountResult& result)
{
    LOCK(cs_main);

    // TODO
    // Call sputnikVM FFI call method with request fields
    // return output of sputnikVM call

    // result.data = "done";
}

void Eth_GetBlockCountByHash(EthGetBlockTransactionCountByHashInput& request, EthGetBlockTransactionCountByHashResult& result)
{
    LOCK(cs_main);

    // TODO
    // Call sputnikVM FFI call method with request fields
    // return output of sputnikVM call

    // result.data = "done";
}

void Eth_GetBlockTransactionCountByNumber(EthGetBlockTransactionCountByNumberInput& request, EthGetBlockTransactionCountByNumberResult& result)
{
    LOCK(cs_main);

    // TODO
    // Call sputnikVM FFI call method with request fields
    // return output of sputnikVM call

    // result.data = "done";
}

void Eth_GetUncleCountByBlockHash(EthGetUncleCountByBlockHashInput& request, EthGetUncleCountByBlockHashResult& result)
{
    LOCK(cs_main);

    // TODO
    // Call sputnikVM FFI call method with request fields
    // return output of sputnikVM call

    // result.data = "done";
}

void Eth_GetUncleCountByBlockNumber(EthGetUncleCountByBlockNumberInput& request, EthGetUncleCountByBlockNumberResult& result)
{
    LOCK(cs_main);

    // TODO
    // Call sputnikVM FFI call method with request fields
    // return output of sputnikVM call

    // result.data = "done";
}

void Eth_GetCode(EthGetCodeInput& request, EthGetCodeResult& result)
{
    LOCK(cs_main);

    // TODO
    // Call sputnikVM FFI call method with request fields
    // return output of sputnikVM call

    // result.data = "done";
}

void Eth_SignTransaction(EthSignTransactionInput& request, EthSignTransactionResult& result)
{
    LOCK(cs_main);

    // TODO
    // Call sputnikVM FFI call method with request fields
    // return output of sputnikVM call

    // result.data = "done";
}

void Eth_SendRawTransaction(EthSendRawTransactionInput& request, EthSendRawTransactionResult& result)
{
    LOCK(cs_main);

    // TODO
    // Call sputnikVM FFI call method with request fields
    // return output of sputnikVM call

    // result.data = "done";
}

void Eth_EstimateGas(EthEstimateGasInput& request, EthEstimateGasResult& result)
{
    LOCK(cs_main);

    // TODO
    // Call sputnikVM FFI call method with request fields
    // return output of sputnikVM call

    // result.data = "done";
}

void Eth_GetBlockByHash(EthGetBlockByHashInput& request, EthGetBlockByHashResult& result)
{
    LOCK(cs_main);

    // TODO
    // Call sputnikVM FFI call method with request fields
    // return output of sputnikVM call

    // result.data = "done";
}

void Eth_GetBlockByNumber(EthGetBlockByNumberInput& request, EthGetBlockByNumberResult& result)
{
    LOCK(cs_main);

    // TODO
    // Call sputnikVM FFI call method with request fields
    // return output of sputnikVM call

    // result.data = "done";
}

void Eth_GetTransactionByHash(EthGetTransactionByHashInput& request, EthGetTransactionByHashResult& result)
{
    LOCK(cs_main);

    // TODO
    // Call sputnikVM FFI call method with request fields
    // return output of sputnikVM call

    // result.data = "done";
}

void Eth_GetTransactionByBlockHashAndIndex(EthGetTransactionByBlockHashAndIndexInput& request, EthGetTransactionByBlockHashAndIndexResult& result)
{
    LOCK(cs_main);

    // TODO
    // Call sputnikVM FFI call method with request fields
    // return output of sputnikVM call

    // result.data = "done";
}

void Eth_GetTransactionByBlockNumberAndIndex(EthGetTransactionByBlockNumberAndIndexInput& request, EthGetTransactionByBlockNumberAndIndexResult& result)
{
    LOCK(cs_main);

    // TODO
    // Call sputnikVM FFI call method with request fields
    // return output of sputnikVM call

    // result.data = "done";
}

void Eth_GetUncleByBlockHashAndIndex(EthGetUncleByBlockHashAndIndexInput& request, EthGetUncleByBlockHashAndIndexResult& result)
{
    LOCK(cs_main);

    // TODO
    // Call sputnikVM FFI call method with request fields
    // return output of sputnikVM call

    // result.data = "done";
}

void Eth_GetUncleByBlockNumberAndIndex(EthGetUncleByBlockNumberAndIndexInput& request, EthGetUncleByBlockNumberAndIndexResult& result)
{
    LOCK(cs_main);

    // TODO
    // Call sputnikVM FFI call method with request fields
    // return output of sputnikVM call

    // result.data = "done";
}

void Eth_GetCompilers(EthGetCompilersResult& result)
{
    LOCK(cs_main);

    // TODO
    // Call sputnikVM FFI call method with request fields
    // return output of sputnikVM call

    // result.data = "done";
}

void Eth_CompileSolidity(EthCompileSolidityInput& request, EthCompileSolidityResult& result)
{
    LOCK(cs_main);

    // TODO
    // Call sputnikVM FFI call method with request fields
    // return output of sputnikVM call

    // result.data = "done";
}

void Eth_CompileLll(EthCompileLllInput& request, EthCompileLllResult& result)
{
    LOCK(cs_main);

    // TODO
    // Call sputnikVM FFI call method with request fields
    // return output of sputnikVM call

    // result.data = "done";
}

void Eth_CompileSerpent(EthCompileSerpentInput& request, EthCompileSerpentResult& result)
{
    LOCK(cs_main);

    // TODO
    // Call sputnikVM FFI call method with request fields
    // return output of sputnikVM call

    // result.data = "done";
}

void Eth_ProtocolVersion(EthProtocolVersionResult& result)
{
    LOCK(cs_main);

    // TODO
    // Call sputnikVM FFI call method with request fields
    // return output of sputnikVM call

    // result.data = "done";
}

void Eth_Web3Sha3(Web3Sha3Input& request, Web3Sha3Result& result)
{
    LOCK(cs_main);

    // TODO
    // Call sputnikVM FFI call method with request fields
    // return output of sputnikVM call

    // result.data = "done";
}

void Eth_NetPeerCount(NetPeerCountResult& result)
{
    LOCK(cs_main);

    // TODO
    // Call sputnikVM FFI call method with request fields
    // return output of sputnikVM call

    // result.data = "done";
}

void Eth_NetVersion(NetVersionResult& result)
{
    LOCK(cs_main);

    // TODO
    // Call sputnikVM FFI call method with request fields
    // return output of sputnikVM call

    // result.data = "done";
}

void Eth_Web3ClientVersion(Web3ClientVersionResult& result)
{
    LOCK(cs_main);

    // TODO
    // Call sputnikVM FFI call method with request fields
    // return output of sputnikVM call

    // result.data = "done";
}

void Eth_GetWork(EthGetWorkResult& result)
{
    LOCK(cs_main);

    // TODO
    // Call sputnikVM FFI call method with request fields
    // return output of sputnikVM call

    // result.data = "done";
}

void Eth_SubmitWork(EthSubmitWorkInput& request, EthSubmitWorkResult& result)
{
    LOCK(cs_main);

    // TODO
    // Call sputnikVM FFI call method with request fields
    // return output of sputnikVM call

    // result.data = "done";
}

void Eth_SubmitHashRate(EthSubmitHashrateInput& request, EthSubmitHashrateResult& result)
{
    LOCK(cs_main);

    // TODO
    // Call sputnikVM FFI call method with request fields
    // return output of sputnikVM call

    // result.data = "done";
}

void Eth_GetStorageAt(EthGetStorageAtInput& request, EthGetStorageAtResult& result)
{
    LOCK(cs_main);

    // TODO
    // Call sputnikVM FFI call method with request fields
    // return output of sputnikVM call

    // result.data = "done";
}

void Eth_GetTransactionReceipt(EthGetTransactionReceiptInput& request, EthGetTransactionReceiptResult& result)
{
    LOCK(cs_main);

    // TODO
    // Call sputnikVM FFI call method with request fields
    // return output of sputnikVM call

    // result.data = "done";
}

void Eth_Syncing(EthSyncingResult& result)
{
    LOCK(cs_main);

    // TODO
    // Call sputnikVM FFI call method with request fields
    // return output of sputnikVM call

    // result.data = "done";
}


void GetBestBlockHash(BlockResult& result)
{
    LOCK(cs_main);
    result.hash = ::ChainActive().Tip()->GetBlockHash().GetHex();
}
