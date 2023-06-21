// Copyright (c) 2023 The DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <ain_rs_exports.h>
#include <coins.h>
#include <consensus/params.h>
#include <ffi/cxx.h>
#include <masternodes/consensus/txvisitor.h>
#include <masternodes/consensus/xvm.h>
#include <masternodes/errors.h>
#include <masternodes/govvariables/attributes.h>
#include <masternodes/masternodes.h>

constexpr uint32_t MAX_TRANSFERDOMAIN_EVM_DATA_LEN = 0;

Res CXVMConsensus::ValidateTransferDomain(const CTransferDomainMessage &obj) const
{
    auto res = Res::Ok();

    // Check if EVM feature is active
    if (!IsEVMEnabled(height, mnview, consensus)) {
        return DeFiErrors::TransferDomainEVMNotEnabled();
    }

    // Iterate over array of transfers
    for (const auto &[src, dst] : obj.transfers) {
        // Reject if transfer is within same domain
        if (src.domain == dst.domain)
            return DeFiErrors::TransferDomainSameDomain();

        // Check for amounts out equals amounts in
        if (src.amount.nValue != dst.amount.nValue)
            return DeFiErrors::TransferDomainUnequalAmount();

        // Restrict only for use with DFI token for now
        if (src.amount.nTokenId != DCT_ID{0} || dst.amount.nTokenId != DCT_ID{0})
            return DeFiErrors::TransferDomainIncorrectToken();

        CTxDestination dest;

        // Source validation
        // Check domain type
        if (src.domain == static_cast<uint8_t>(VMDomain::DVM)) {
            // Reject if source address is ETH address
            if (ExtractDestination(src.address, dest)) {
                if (dest.index() == WitV16KeyEthHashType) {
                    return DeFiErrors::TransferDomainETHSourceAddress();
                }
            }
            // Check for authorization on source address
            res = HasAuth(src.address);
            if (!res)
                return res;
        } else if (src.domain == static_cast<uint8_t>(VMDomain::EVM)) {
            // Reject if source address is DFI address
            if (ExtractDestination(src.address, dest)) {
                if (dest.index() != WitV16KeyEthHashType) {
                    return DeFiErrors::TransferDomainDFISourceAddress();
                }
            }
            // Check for authorization on source address
            res = ::HasAuth(tx, coins, src.address, AuthStrategy::EthKeyMatch);
            if (!res)
                return res;
        } else
            return DeFiErrors::TransferDomainInvalidSourceDomain();

        // Destination validation
        // Check domain type
        if (dst.domain == static_cast<uint8_t>(VMDomain::DVM)) {
            // Reject if source address is ETH address
            if (ExtractDestination(dst.address, dest)) {
                if (dest.index() == WitV16KeyEthHashType) {
                    return DeFiErrors::TransferDomainETHDestinationAddress();
                }
            }
        } else if (dst.domain == static_cast<uint8_t>(VMDomain::EVM)) {
            // Reject if source address is DFI address
            if (ExtractDestination(dst.address, dest)) {
                if (dest.index() != WitV16KeyEthHashType) {
                    return DeFiErrors::TransferDomainDVMDestinationAddress();
                }
            }
        } else
            return DeFiErrors::TransferDomainInvalidDestinationDomain();
    }

    return res;
}


Res CXVMConsensus::operator()(const CTransferDomainMessage &obj) const {
    auto res = ValidateTransferDomain(obj);
    if (!res) {
        return res;
    }

    // Iterate over array of transfers
    for (const auto &[src, dst] : obj.transfers) {
        if (src.domain == static_cast<uint8_t>(VMDomain::DVM)) {
            // Subtract balance from DFI address
            CBalances balance;
            balance.Add(src.amount);
            res = mnview.SubBalances(src.address, balance);
            if (!res)
                return res;
        } else if (src.domain == static_cast<uint8_t>(VMDomain::EVM)) {
            // Subtract balance from ETH address
            CTxDestination dest;
            ExtractDestination(src.address, dest);
            const auto fromAddress = std::get<WitnessV16EthHash>(dest);
            arith_uint256 balanceIn = src.amount.nValue;
            balanceIn *= CAMOUNT_TO_GWEI * WEI_IN_GWEI;
            if (!evm_sub_balance(evmContext, HexStr(fromAddress.begin(), fromAddress.end()), ArithToUint256(balanceIn).ToArrayReversed(), tx.GetHash().ToArrayReversed())) {
                return DeFiErrors::TransferDomainNotEnoughBalance(EncodeDestination(dest));
            }
        }
        if (dst.domain == static_cast<uint8_t>(VMDomain::DVM)) {
            // Add balance to DFI address
            CBalances balance;
            balance.Add(dst.amount);
            res = mnview.AddBalances(dst.address, balance);
            if (!res)
                return res;
        } else if (dst.domain == static_cast<uint8_t>(VMDomain::EVM)) {
            // Add balance to ETH address
            CTxDestination dest;
            ExtractDestination(dst.address, dest);
            const auto toAddress = std::get<WitnessV16EthHash>(dest);
            arith_uint256 balanceIn = dst.amount.nValue;
            balanceIn *= CAMOUNT_TO_GWEI * WEI_IN_GWEI;
            evm_add_balance(evmContext, HexStr(toAddress.begin(), toAddress.end()), ArithToUint256(balanceIn).ToArrayReversed(), tx.GetHash().ToArrayReversed());

            // If you are here to change ChangiIntermediateHeight to NextNetworkUpgradeHeight
            // then just remove this fork guard and comment as CTransferDomainMessage is already
            // protected by NextNetworkUpgradeHeight.
            if (height >= static_cast<uint32_t>(consensus.ChangiIntermediateHeight)) {
                if (src.data.size() > MAX_TRANSFERDOMAIN_EVM_DATA_LEN || dst.data.size() > MAX_TRANSFERDOMAIN_EVM_DATA_LEN) {
                    return DeFiErrors::TransferDomainInvalidDataSize(MAX_TRANSFERDOMAIN_EVM_DATA_LEN);
                }
            }
        }
    }

    return res;
}

Res CXVMConsensus::operator()(const CEvmTxMessage &obj) const {
    if (!IsEVMEnabled(height, mnview, consensus)) {
        return Res::Err("Cannot create tx, EVM is not enabled");
    }

    if (obj.evmTx.size() > static_cast<size_t>(EVM_TX_SIZE))
        return Res::Err("evm tx size too large");

    CrossBoundaryResult result;
    const auto hashAndGas = evm_try_prevalidate_raw_tx(result, HexStr(obj.evmTx), true);

    // Completely remove this fork guard on mainnet upgrade to restore nonce check from EVM activation
    if (height >= static_cast<uint32_t>(consensus.ChangiIntermediateHeight)) {
        if (!result.ok) {
            LogPrintf("[evm_try_prevalidate_raw_tx] failed, reason : %s\n", result.reason);
            return Res::Err("evm tx failed to validate %s", result.reason);
        }
    }

    evm_try_queue_tx(result, evmContext, HexStr(obj.evmTx), tx.GetHash().ToArrayReversed());
    if (!result.ok) {
        LogPrintf("[evm_try_queue_tx] failed, reason : %s\n", result.reason);
        return Res::Err("evm tx failed to queue %s\n", result.reason);
    }

    gasUsed = hashAndGas.used_gas;

    std::vector<unsigned char> evmTxHashBytes;
    sha3(obj.evmTx, evmTxHashBytes);
    auto txHash = tx.GetHash();
    auto evmTxHash = uint256S(HexStr(evmTxHashBytes));
    mnview.SetVMDomainMapTxHash(VMDomainMapType::DVMToEVM, txHash, evmTxHash);
    mnview.SetVMDomainMapTxHash(VMDomainMapType::EVMToDVM, evmTxHash, txHash);
    return Res::Ok();
}

