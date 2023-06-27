#include <consensus/params.h>
#include <consensus/tx_check.h>
#include <masternodes/evm.h>
#include <masternodes/masternodes.h>
#include <masternodes/mn_checks.h>

// A class that has all the old things that will be removed.
// The goal of this entire class is to be removed. And it's a collection of the 
// accumulated bugs in early releases. 
class ChangiBuggyIntermediates {
public:
    static Res ValidateTransferDomainAspect2(const CTransaction &tx,
                                    uint32_t height,
                                    const CCoinsViewCache &coins,
                                    const Consensus::Params &consensus,
                                    CTransferDomainItem src, CTransferDomainItem dst);
};