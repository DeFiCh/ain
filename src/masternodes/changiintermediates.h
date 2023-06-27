#ifndef DEFI_MASTERNODES_CHANGIINTERMEDIATES_H
#define DEFI_MASTERNODES_CHANGIINTERMEDIATES_H

#include <coins.h>
#include <primitives/transaction.h>
#include <consensus/params.h>
#include <masternodes/balances.h>

/// The file and class that has all the old things that will be removed.
/// The goal of this entire class and file is to be removed. It's a collection of the 
/// accumulated bugs in early releases. 
class ChangiBuggyIntermediates {
public:
    static Res ValidateTransferDomainEdge2(const CTransaction &tx,
                                    uint32_t height,
                                    const CCoinsViewCache &coins,
                                    const Consensus::Params &consensus,
                                    CTransferDomainItem src, CTransferDomainItem dst);
};

 #endif // DEFI_MASTERNODES_CHANGIINTERMEDIATES_H
