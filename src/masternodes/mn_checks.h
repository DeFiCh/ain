#ifndef MN_CHECKS_H
#define MN_CHECKS_H

#include "consensus/params.h"
#include <vector>

class CBlock;
class CTransaction;

class CMasternodesView;
class CMasternodesViewCache;

bool CheckMasternodeTx(CMasternodesViewCache & mnview, CTransaction const & tx, const Consensus::Params& consensusParams, int height, bool isCheck = true);
//bool ProcessMasternodeTxsOnConnect(CMasternodesViewCache & mnview, CBlock const & block, int nHeight);
//bool ProcessMasternodeTxsOnDisconnect(CMasternodesViewCache & mnview, CBlock const & block, int height);

bool CheckInputsForCollateralSpent(CMasternodesViewCache & mnview, CTransaction const & tx, int nHeight, bool isCheck);
//! Deep check (and write)
bool CheckCreateMasternodeTx(CMasternodesViewCache & mnview, CTransaction const & tx, int height, std::vector<unsigned char> const & metadata, bool isCheck);
bool CheckResignMasternodeTx(CMasternodesViewCache & mnview, CTransaction const & tx, int height, std::vector<unsigned char> const & metadata, bool isCheck);

#endif // MN_CHECKS_H
