#include <libain.hpp>
#include <rpc/libain_wrapper.hpp>
#include <sync.h>
#include <validation.h>

void GetBestBlockHash(BlockResult &result)
{
    LOCK(cs_main);
    result.hex_data = ::ChainActive().Tip()->GetBlockHash().GetHex();
}
