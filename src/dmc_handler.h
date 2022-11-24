#ifndef DMC_HANDLER_H
#define DMC_HANDLER_H

#include <miner.h>
#include <primitives/block.h>

/**
 * Initializes metachain handler
 *
 * @throws Exception on failure to instantiate RPC client
 * @returns true if RPC handler has been setup and false otherwise
 *
 * NOTE: This depends on ArgsManager, so it must be called only after parsing arguments
 */
bool SetupMetachainHandler();

/**
 * Instructs the metachain node to mint a block using the newly minted miner block
 *
 * @param[in] pblock Recently mined block
 * @returns Ok on successfully adding serialized metachain block data to the minted block
*/
Res MintDmcBlock(CBlock& pblock);

/**
 * Instructs the metachain node to connect the given block
 *
 * @param[in] block Recently mined block
 * @returns Ok on successfully connecting the block
 */
Res ConnectDmcBlock(const CBlock& block);

/**
 * Instructs the metachain node to get the transactions for moving funds back
 * to native chain (if any)
 *
 */
Res AddNativeTx(CBlock& pblock, std::unique_ptr<CBlockTemplate>& pblocktemplate, int32_t txVersion, int nHeight);

#endif // DMC_HANDLER_H
