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

void GetBestBlockHash(BlockResult& result)
{
    LOCK(cs_main);
    result.hash = ::ChainActive().Tip()->GetBlockHash().GetHex();
}
