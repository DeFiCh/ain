// Copyright (c) 2009-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_RPC_REGISTER_H
#define DEFI_RPC_REGISTER_H

/** These are in one header file to avoid creating tons of single-function
 * headers for everything under src/rpc/ */
class CRPCTable;

/** Register block chain RPC commands */
void RegisterBlockchainRPCCommands(CRPCTable &tableRPC);
/** Register P2P networking RPC commands */
void RegisterNetRPCCommands(CRPCTable &tableRPC);
/** Register miscellaneous RPC commands */
void RegisterMiscRPCCommands(CRPCTable &tableRPC);
/** Register mining RPC commands */
void RegisterMiningRPCCommands(CRPCTable &tableRPC);
/** Register raw transaction RPC commands */
void RegisterRawTransactionRPCCommands(CRPCTable &tableRPC);
/** Register masternodes RPC commands */
void RegisterMasternodesRPCCommands(CRPCTable &tableRPC);
/** Register accounts RPC commands */
void RegisterAccountsRPCCommands(CRPCTable &tableRPC);
/** Register orderbook RPC commands */
void RegisterOrderbookRPCCommands(CRPCTable &tableRPC);
/** Register poolpair RPC commands */
void RegisterPoolpairRPCCommands(CRPCTable &tableRPC);
/** Register tokens RPC commands */
void RegisterTokensRPCCommands(CRPCTable &tableRPC);
/** Register blockchain masternode RPC commands */
void RegisterMNBlockchainRPCCommands(CRPCTable &tableRPC);
/** Register SPV (anchoring) RPC commands */
void RegisterSpvRPCCommands(CRPCTable &tableRPC);

static inline void RegisterAllCoreRPCCommands(CRPCTable &t)
{
    RegisterBlockchainRPCCommands(t);
    RegisterNetRPCCommands(t);
    RegisterMiscRPCCommands(t);
    RegisterMiningRPCCommands(t);
    RegisterRawTransactionRPCCommands(t);
    RegisterMasternodesRPCCommands(t);
    RegisterAccountsRPCCommands(t);
    RegisterOrderbookRPCCommands(t);
    RegisterPoolpairRPCCommands(t);
    RegisterTokensRPCCommands(t);
    RegisterMNBlockchainRPCCommands(t);
    RegisterSpvRPCCommands(t);
}

#endif // DEFI_RPC_REGISTER_H
