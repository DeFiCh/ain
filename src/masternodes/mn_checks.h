// Copyright (c) 2019 DeFi Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_MASTERNODES_MN_CHECKS_H
#define BITCOIN_MASTERNODES_MN_CHECKS_H

#include <consensus/params.h>
#include <masternodes/masternodes.h>
#include <vector>

class CBlock;
class CTransaction;
class CTxMemPool;

class CMasternodesView;
class CMasternodesViewCache;

bool CheckMasternodeTx(CMasternodesViewCache & mnview, CTransaction const & tx, const Consensus::Params& consensusParams, int height, int txn, bool isCheck = true);

bool CheckInputsForCollateralSpent(CMasternodesViewCache & mnview, CTransaction const & tx, int nHeight, bool isCheck);
//! Deep check (and write)
bool CheckCreateMasternodeTx(CMasternodesViewCache & mnview, CTransaction const & tx, int height, int txn, std::vector<unsigned char> const & metadata, bool isCheck);
bool CheckResignMasternodeTx(CMasternodesViewCache & mnview, CTransaction const & tx, int height, int txn, std::vector<unsigned char> const & metadata, bool isCheck);

bool IsMempooledMnCreate(const CTxMemPool& pool, const uint256 & txid);

#endif // BITCOIN_MASTERNODES_MN_CHECKS_H
