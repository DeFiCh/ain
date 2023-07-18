#include <masternodes/changiintermediates.h>
#include <masternodes/mn_checks.h>
#include <masternodes/errors.h>

/// The file and class that has all the old things that will be removed.
/// The goal of this entire class and file is to be removed. It's a collection of the 
/// accumulated bugs in early releases. 

Res ChangiBuggyIntermediates::ValidateTransferDomainEdge2(const CTransaction &tx,
                                uint32_t height,
                                const CCoinsViewCache &coins,
                                const Consensus::Params &consensus,
                                CTransferDomainItem src, CTransferDomainItem dst) {

        auto res = Res::Ok();

        if (src.domain == dst.domain)
            return DeFiErrors::TransferDomainSameDomain();

        if (src.amount.nValue != dst.amount.nValue)
            return DeFiErrors::TransferDomainUnequalAmount();

        // Restrict only for use with DFI token for now. Will be enabled later.
        if (src.amount.nTokenId != DCT_ID{0} || dst.amount.nTokenId != DCT_ID{0})
            return DeFiErrors::TransferDomainIncorrectToken();

        if (src.domain == static_cast<uint8_t>(VMDomain::DVM) && dst.domain == static_cast<uint8_t>(VMDomain::EVM)) {
            CTxDestination dest;
            // Reject if source address is ETH address
            if (ExtractDestination(src.address, dest)) {
                if (dest.index() == WitV16KeyEthHashType) {
                    return DeFiErrors::TransferDomainDVMSourceAddress();
                }
            }
            if (ExtractDestination(dst.address, dest)) {
                if (dest.index() != WitV16KeyEthHashType) {
                    return DeFiErrors::TransferDomainETHDestAddress();
                }
            }
            // Check for authorization on source address
            return HasAuth(tx, coins, src.address);

        } else if (src.domain == static_cast<uint8_t>(VMDomain::EVM) && dst.domain == static_cast<uint8_t>(VMDomain::DVM)) {
            CTxDestination dest;
            // Reject if source address is DFI address
            if (ExtractDestination(src.address, dest)) {
                if (dest.index() != WitV16KeyEthHashType) {
                    return DeFiErrors::TransferDomainETHSourceAddress();
                }
            }
            if (ExtractDestination(dst.address, dest)) {
                if (dest.index() == WitV16KeyEthHashType) {
                    return DeFiErrors::TransferDomainDVMDestAddress();
                }
            }
            return HasAuth(tx, coins, src.address, AuthStrategy::Mapped,
                           static_cast<AuthFlags>(AuthFlags::SourceBech32 | AuthFlags::SourcePKHash));
        }
        return res;
    }
