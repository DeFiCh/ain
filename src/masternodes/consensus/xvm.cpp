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
#include <masternodes/mn_checks.h>

constexpr uint32_t MAX_TRANSFERDOMAIN_EVM_DATA_LEN = 1024;

static bool IsTransferDomainEnabled(const int height, const CCustomCSView &view, const Consensus::Params &consensus) {
    if (height < consensus.NextNetworkUpgradeHeight) {
        return false;
    }

    const CDataStructureV0 enabledKey{AttributeTypes::Param, ParamIDs::Feature, DFIPKeys::TransferDomain};
    auto attributes = view.GetAttributes();
    assert(attributes);
    return attributes->GetValue(enabledKey, false);
}

static XVmAddressFormatTypes FromTxDestType(const size_t index) {
    switch (index) {
        case PKHashType:
            return XVmAddressFormatTypes::PkHash;
        case WitV0KeyHashType:
            return XVmAddressFormatTypes::Bech32;
        case WitV16KeyEthHashType:
            return XVmAddressFormatTypes::Erc55;
        default:
            return XVmAddressFormatTypes::None;
    }
}

static Res ValidateTransferDomainScripts(const CScript &srcScript,
                                         const CScript &destScript,
                                         VMDomainEdge edge,
                                         const TransferDomainConfig &config,
                                         TransferDomainInfo &context) {
    CTxDestination src, dest;
    auto res = ExtractDestination(srcScript, src);
    if (!res) return DeFiErrors::ScriptUnexpected(srcScript);

    res = ExtractDestination(destScript, dest);
    if (!res) return DeFiErrors::ScriptUnexpected(destScript);

    const auto srcType = FromTxDestType(src.index());
    const auto destType = FromTxDestType(dest.index());

    if (edge == VMDomainEdge::DVMToEVM) {
        if (!config.dvmToEvmSrcAddresses.count(srcType)) {
            return DeFiErrors::TransferDomainDVMSourceAddress();
        }
        if (!config.dvmToEvmDestAddresses.count(destType)) {
            return DeFiErrors::TransferDomainETHDestAddress();
        }
        context.to = EncodeDestination(dest);
        context.native_address = EncodeDestination(src);
        return Res::Ok();

    } else if (edge == VMDomainEdge::EVMToDVM) {
        if (!config.evmToDvmSrcAddresses.count(srcType)) {
            return DeFiErrors::TransferDomainETHSourceAddress();
        }
        if (!config.evmToDvmDestAddresses.count(destType)) {
            return DeFiErrors::TransferDomainDVMDestAddress();
        }
        context.from = EncodeDestination(src);
        context.native_address = EncodeDestination(dest);
        return Res::Ok();
    }

    return DeFiErrors::TransferDomainUnknownEdge();
}

static Res ValidateTransferDomainEdge(const CTransaction &tx,
                                      const TransferDomainConfig &config,
                                      CCustomCSView &mnview,
                                      uint32_t height,
                                      const CCoinsViewCache &coins,
                                      const Consensus::Params &consensus,
                                      CTransferDomainItem src,
                                      CTransferDomainItem dst,
                                      TransferDomainInfo &context) {

    if (src.domain == dst.domain)
        return DeFiErrors::TransferDomainSameDomain();

    if (src.amount.nValue != dst.amount.nValue)
        return DeFiErrors::TransferDomainUnequalAmount();

    if (src.amount.nTokenId != dst.amount.nTokenId)
        return DeFiErrors::TransferDomainDifferentTokens();

    // We allow 0 here, just if we need to touch something
    // on either sides or special case later.
    if (src.amount.nValue < 0)
        return DeFiErrors::TransferDomainInvalid();

    auto tokenId = src.amount.nTokenId;
    context.token_id = tokenId.v;
    context.value = dst.amount.nValue;

    if (tokenId != DCT_ID{0}) {
        auto token = mnview.GetToken(tokenId);
        if (!token || !token->IsDAT() || token->IsPoolShare())
            return DeFiErrors::TransferDomainIncorrectToken();
    }

    if (src.domain == static_cast<uint8_t>(VMDomain::DVM) && dst.domain == static_cast<uint8_t>(VMDomain::EVM)) {
        if (!config.dvmToEvmEnabled)
            return DeFiErrors::TransferDomainDVMEVMNotEnabled();

        if (tokenId == DCT_ID{0} && !config.dvmToEvmNativeTokenEnabled)
            return DeFiErrors::TransferDomainDVMToEVMNativeTokenNotEnabled();

        if (tokenId != DCT_ID{0} && !config.dvmToEvmDatEnabled)
            return DeFiErrors::TransferDomainDVMToEVMDATNotEnabled();

        // DVM to EVM
        auto res = ValidateTransferDomainScripts(src.address, dst.address, VMDomainEdge::DVMToEVM, config, context);
        if (!res) return res;
        context.direction = true;

        CScript from;
        res = GetERC55AddressFromAuth(tx, coins, from);
        if (!res) return res;
        CTxDestination dest;
        if (!ExtractDestination(from, dest)) {
            return DeFiErrors::ScriptUnexpected(from);
        }
        context.from = EncodeDestination(dest);

        return HasAuth(tx, coins, src.address);

    } else if (src.domain == static_cast<uint8_t>(VMDomain::EVM) && dst.domain == static_cast<uint8_t>(VMDomain::DVM)) {
        if (!config.evmToDvmEnabled)
            return DeFiErrors::TransferDomainEVMDVMNotEnabled();

        if (tokenId == DCT_ID{0} && !config.evmToDvmNativeTokenEnabled)
            return DeFiErrors::TransferDomainEVMToDVMNativeTokenNotEnabled();

        if (tokenId != DCT_ID{0} && !config.evmToDvmDatEnabled)
            return DeFiErrors::TransferDomainEVMToDVMDATNotEnabled();

        // EVM to DVM
        auto res = ValidateTransferDomainScripts(src.address, dst.address, VMDomainEdge::EVMToDVM, config, context);
        if (!res) return res;
        context.direction = false;

        auto authType = AuthFlags::None;
        for (const auto &value : config.evmToDvmAuthFormats) {
            if (value == XVmAddressFormatTypes::PkHashProxyErc55) {
                authType = static_cast<AuthFlags::Type>(authType | AuthFlags::PKHashInSource);
            } else if (value == XVmAddressFormatTypes::Bech32ProxyErc55) {
                authType = static_cast<AuthFlags::Type>(authType | AuthFlags::Bech32InSource);
            }
        }
        return HasAuth(tx, coins, src.address, AuthStrategy::Mapped, authType);
    }

    return DeFiErrors::TransferDomainUnknownEdge();
}

static Res ValidateTransferDomain(const CTransaction &tx,
                           uint32_t height,
                           const CCoinsViewCache &coins,
                           CCustomCSView &mnview,
                           const Consensus::Params &consensus,
                           const CTransferDomainMessage &obj,
                           const bool isEvmEnabledForBlock,
                           std::vector<TransferDomainInfo> &contexts)
{
    if (!IsTransferDomainEnabled(height, mnview, consensus)) {
        return DeFiErrors::TransferDomainNotEnabled();
    }

    if (!isEvmEnabledForBlock) {
        return DeFiErrors::TransferDomainEVMNotEnabled();
    }

    if (obj.transfers.size() != 1) {
        return DeFiErrors::TransferDomainMultipleTransfers();
    }

    if (tx.vin.size() > 1) {
        return DeFiErrors::TransferDomainInvalid();
    }

    auto config = TransferDomainConfig::From(mnview);

    for (const auto &[src, dst] : obj.transfers) {
        TransferDomainInfo context;
        auto res = ValidateTransferDomainEdge(tx, config, mnview, height, coins, consensus, src, dst, context);
        if (!res) return res;
        contexts.push_back(context);
    }

    return Res::Ok();
}

Res CXVMConsensus::operator()(const CTransferDomainMessage &obj) const {
    std::vector<TransferDomainInfo> contexts;
    auto res = ValidateTransferDomain(tx, height, coins, mnview, consensus, obj, isEvmEnabledForBlock, contexts);
    if (!res) { return res; }

    auto attributes = mnview.GetAttributes();
    auto stats = attributes->GetValue(CTransferDomainStatsLive::Key, CTransferDomainStatsLive{});
    std::string evmTxHash;
    CrossBoundaryResult result;

    // Iterate over array of transfers
    auto idx = 0;
    for (const auto &[src, dst] : obj.transfers) {
        if (src.domain == static_cast<uint8_t>(VMDomain::DVM) && dst.domain == static_cast<uint8_t>(VMDomain::EVM)) {
            CTxDestination dest;
            if (!ExtractDestination(dst.address, dest)) {
                return DeFiErrors::TransferDomainETHDestAddress();
            }
            const auto toAddress = std::get_if<WitnessV16EthHash>(&dest);
            if (!toAddress) {
                return DeFiErrors::TransferDomainETHSourceAddress();
            }

            // Check if destination address is a contract
            auto isSmartContract = evm_try_unsafe_is_smart_contract_in_q(result, toAddress->GetHex(), evmQueueId);
            if (!result.ok) {
                return Res::Err("Error checking contract address: %s", result.reason);
            }
            if (isSmartContract) {
                return DeFiErrors::TransferDomainSmartContractDestAddress();
            }

            // Subtract balance from DFI address
            res = mnview.SubBalance(src.address, src.amount);
            if (!res) {
                return res;
            }
            stats.dvmEvmTotal.Add(src.amount);
            stats.dvmOut.Add(src.amount);
            stats.dvmCurrent.Sub(src.amount);

            if (dst.data.size() > MAX_TRANSFERDOMAIN_EVM_DATA_LEN) {
                return DeFiErrors::TransferDomainInvalidDataSize(MAX_TRANSFERDOMAIN_EVM_DATA_LEN);
            }
            const auto evmTx = HexStr(dst.data);
            evm_try_unsafe_validate_transferdomain_tx_in_q(result, evmQueueId, evmTx, contexts[idx]);
            if (!result.ok) {
                LogPrintf("[evm_try_validate_transferdomain_tx] failed, reason : %s\n", result.reason);
                return Res::Err("transferdomain evm tx failed to pre-validate : %s", result.reason);
            }
            if (evmPreValidate) {
                return Res::Ok();
            }

            auto hash = evm_try_get_tx_hash(result, evmTx);
            if (!result.ok) {
                return Res::Err("Error getting tx hash: %s", result.reason);
            }
            evmTxHash = std::string(hash.data(), hash.length()).substr(2);
            // Add balance to ERC55 address
            auto tokenId = dst.amount.nTokenId;
            if (tokenId == DCT_ID{0}) {
                evm_try_unsafe_add_balance_in_q(result, evmQueueId, evmTx, tx.GetHash().GetHex());
                if (!result.ok) {
                    return Res::Err("Error bridging DFI: %s", result.reason);
                }
            }
            else {
                evm_try_unsafe_bridge_dst20(result, evmQueueId, evmTx, tx.GetHash().GetHex(), tokenId.v, true);
                if (!result.ok) {
                    return Res::Err("Error bridging DST20: %s", result.reason);
                }
            }
            auto tokenAmount = CTokenAmount{tokenId, dst.amount.nValue};
            stats.evmIn.Add(tokenAmount);
            stats.evmCurrent.Add(tokenAmount);
        } else if (src.domain == static_cast<uint8_t>(VMDomain::EVM) && dst.domain == static_cast<uint8_t>(VMDomain::DVM)) {
            CTxDestination dest;
            if (!ExtractDestination(src.address, dest)) {
                return DeFiErrors::TransferDomainETHSourceAddress();
            }
            const auto fromAddress = std::get_if<WitnessV16EthHash>(&dest);
            if (!fromAddress) {
                return DeFiErrors::TransferDomainETHSourceAddress();
            }

            // Check if source address is a contract
            auto isSmartContract = evm_try_unsafe_is_smart_contract_in_q(result, fromAddress->GetHex(), evmQueueId);
            if (!result.ok) {
                return Res::Err("Error checking contract address: %s", result.reason);
            }
            if (isSmartContract) {
                return DeFiErrors::TransferDomainSmartContractSourceAddress();
            }

            if (src.data.size() > MAX_TRANSFERDOMAIN_EVM_DATA_LEN) {
                return DeFiErrors::TransferDomainInvalidDataSize(MAX_TRANSFERDOMAIN_EVM_DATA_LEN);
            }
            const auto evmTx = HexStr(src.data);
            evm_try_unsafe_validate_transferdomain_tx_in_q(result, evmQueueId, evmTx, contexts[idx]);
            if (!result.ok) {
                LogPrintf("[evm_try_validate_transferdomain_tx] failed, reason : %s\n", result.reason);
                return Res::Err("transferdomain evm tx failed to pre-validate %s", result.reason);
            }
            if (evmPreValidate) {
                return Res::Ok();
            }

            auto hash = evm_try_get_tx_hash(result, evmTx);
            if (!result.ok) {
                return Res::Err("Error getting tx hash: %s", result.reason);
            }
            evmTxHash = std::string(hash.data(), hash.length()).substr(2);

            // Subtract balance from ERC55 address
            auto tokenId = dst.amount.nTokenId;
            if (tokenId == DCT_ID{0}) {
                if (!evm_try_unsafe_sub_balance_in_q(result, evmQueueId, evmTx, tx.GetHash().GetHex())) {
                    return DeFiErrors::TransferDomainNotEnoughBalance(EncodeDestination(dest));
                }
                if (!result.ok) {
                    return Res::Err("Error bridging DFI: %s", result.reason);
                }
            }
            else {
                evm_try_unsafe_bridge_dst20(result, evmQueueId, evmTx, tx.GetHash().GetHex(), tokenId.v, false);
                if (!result.ok) {
                    return Res::Err("Error bridging DST20: %s", result.reason);
                }
            }
            auto tokenAmount = CTokenAmount{tokenId, src.amount.nValue};
            stats.evmOut.Add(tokenAmount);
            stats.evmCurrent.Sub(tokenAmount);

            // Add balance to DFI address
            res = mnview.AddBalance(dst.address, dst.amount);
            if (!res) {
                evm_try_unsafe_remove_txs_above_hash_in_q(result, evmQueueId, tx.GetHash().GetHex());
                return res;
            }
            stats.evmDvmTotal.Add(dst.amount);
            stats.dvmIn.Add(dst.amount);
            stats.dvmCurrent.Add(dst.amount);
        }
        else {
            return DeFiErrors::TransferDomainInvalidDomain();
        }

        ++idx;
    }

    auto txHash = tx.GetHash().GetHex();
    res = mnview.SetVMDomainTxEdge(VMDomainEdge::DVMToEVM, txHash, evmTxHash);
    if (!res) {
        LogPrintf("Failed to store DVMtoEVM TX hash for DFI TX %s\n", txHash);
    }
    res = mnview.SetVMDomainTxEdge(VMDomainEdge::EVMToDVM, evmTxHash, txHash);
    if (!res) {
        LogPrintf("Failed to store EVMToDVM TX hash for DFI TX %s\n", txHash);
    }

    attributes->SetValue(CTransferDomainStatsLive::Key, stats);
    res = mnview.SetVariable(*attributes);
    if (!res) {
        evm_try_unsafe_remove_txs_above_hash_in_q(result, evmQueueId, tx.GetHash().GetHex());
        return res;
    }
    return Res::Ok();
}

Res CXVMConsensus::operator()(const CEvmTxMessage &obj) const {
    if (!isEvmEnabledForBlock) {
        return Res::Err("Cannot create tx, EVM is not enabled");
    }

    if (obj.evmTx.size() > static_cast<size_t>(EVM_TX_SIZE))
        return Res::Err("evm tx size too large");

    CrossBoundaryResult result;
    if (evmPreValidate) {
        evm_try_unsafe_prevalidate_raw_tx_in_q(result, evmQueueId, HexStr(obj.evmTx));
        if (!result.ok) {
            LogPrintf("[evm_try_prevalidate_raw_tx] failed, reason : %s\n", result.reason);
            return Res::Err("evm tx failed to pre-validate %s", result.reason);
        }
        return Res::Ok();
    }

    const auto validateResults = evm_try_unsafe_push_tx_in_q(result, evmQueueId, HexStr(obj.evmTx), tx.GetHash().GetHex());
    if (!result.ok) {
        LogPrintf("[evm_try_push_tx_in_q] failed, reason : %s\n", result.reason);
        return Res::Err("evm tx failed to queue %s\n", result.reason);
    }

    auto txHash = tx.GetHash().GetHex();
    auto evmTxHash = std::string(validateResults.tx_hash.data(), validateResults.tx_hash.length()).substr(2);
    auto res = mnview.SetVMDomainTxEdge(VMDomainEdge::DVMToEVM, txHash, evmTxHash);
    if (!res) {
        LogPrintf("Failed to store DVMtoEVM TX hash for DFI TX %s\n", txHash);
    }
    res = mnview.SetVMDomainTxEdge(VMDomainEdge::EVMToDVM, evmTxHash, txHash);
    if (!res) {
        LogPrintf("Failed to store EVMToDVM TX hash for DFI TX %s\n", txHash);
    }

    return Res::Ok();
}
