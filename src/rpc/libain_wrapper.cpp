#include <libain.hpp>
#include <rpc/libain_wrapper.hpp>
#include <sync.h>
#include <validation.h>

void GetBestBlockHash(BlockResult &result)
{
    LOCK(cs_main);
    result.hash = ::ChainActive().Tip()->GetBlockHash().GetHex();
}
