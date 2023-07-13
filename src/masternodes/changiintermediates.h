#ifndef DEFI_MASTERNODES_CHANGIINTERMEDIATES_H
#define DEFI_MASTERNODES_CHANGIINTERMEDIATES_H

#include <cstdint>
#include <serialize.h>

class CCoinsViewCache;
class CTransaction;
struct CTransferDomainItem;
struct Res;

namespace Consensus {
    struct Params;
};

struct EVMChangiIntermediate {
    uint32_t version;
    uint256 blockHash;
    uint64_t minerFee;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(version);
        READWRITE(blockHash);
        READWRITE(minerFee);
    }
};

struct XVMChangiIntermediate {
    uint32_t version;
    EVMChangiIntermediate evm;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(version);
        READWRITE(evm);
    }
};

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
