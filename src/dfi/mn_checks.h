// Copyright (c) DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_DFI_MN_CHECKS_H
#define DEFI_DFI_MN_CHECKS_H

#include <consensus/params.h>
#include <consensus/tx_check.h>
#include <dfi/customtx.h>
#include <dfi/evm.h>
#include <dfi/masternodes.h>
#include <cstring>
#include <vector>

#include <variant>

class BlockContext;
class CTransaction;
class CTxMemPool;
class CCoinsViewCache;

class CCustomCSView;

struct EVM {
    uint32_t version;
    std::string blockHash;
    uint64_t burntFee;
    uint64_t priorityFee;
    std::string beneficiary;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action) {
        READWRITE(version);
        READWRITE(blockHash);
        READWRITE(burntFee);
        READWRITE(priorityFee);
        READWRITE(beneficiary);
    }

    UniValue ToUniValue() const;
};

struct XVM {
    uint32_t version;
    EVM evm;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action) {
        READWRITE(version);
        READWRITE(evm);
    }

    static ResVal<XVM> TryFrom(const CScript &scriptPubKey);
    UniValue ToUniValue() const;
    CScript ToScript() const;
};

constexpr uint8_t MAX_POOL_SWAPS = 3;

struct OpReturnLimits {
    bool shouldEnforce{};
    uint64_t coreSizeBytes{};
    uint64_t dvmSizeBytes{};
    uint64_t evmSizeBytes{};

    static OpReturnLimits Default();
    static OpReturnLimits From(const uint64_t height, const Consensus::Params &consensus, const ATTRIBUTES &attributes);

    void SetToAttributesIfNotExists(ATTRIBUTES &attrs) const;
    Res Validate(const CTransaction &tx, const CustomTxType txType) const;
    uint64_t MaxSize() { return std::max({coreSizeBytes, dvmSizeBytes, evmSizeBytes}); }
};

struct TransferDomainConfig {
    bool dvmToEvmEnabled;
    bool evmToDvmEnabled;
    XVmAddressFormatItems dvmToEvmSrcAddresses;
    XVmAddressFormatItems dvmToEvmDestAddresses;
    XVmAddressFormatItems evmToDvmDestAddresses;
    XVmAddressFormatItems evmToDvmSrcAddresses;
    XVmAddressFormatItems evmToDvmAuthFormats;
    bool dvmToEvmNativeTokenEnabled;
    bool evmToDvmNativeTokenEnabled;
    bool dvmToEvmDatEnabled;
    bool evmToDvmDatEnabled;
    std::set<uint32_t> dvmToEvmDisallowedTokens;
    std::set<uint32_t> evmToDvmDisallowedTokens;

    static TransferDomainConfig Default();
    static TransferDomainConfig From(const CCustomCSView &mnview);

    void SetToAttributesIfNotExists(ATTRIBUTES &attrs) const;
};

struct CCustomTxMessageNone {};

using CCustomTxMessage = std::variant<CCustomTxMessageNone,
                                      CCreateMasterNodeMessage,
                                      CResignMasterNodeMessage,
                                      CUpdateMasterNodeMessage,
                                      CCreateTokenMessage,
                                      CUpdateTokenPreAMKMessage,
                                      CUpdateTokenMessage,
                                      CMintTokensMessage,
                                      CBurnTokensMessage,
                                      CCreatePoolPairMessage,
                                      CUpdatePoolPairMessage,
                                      CPoolSwapMessage,
                                      CPoolSwapMessageV2,
                                      CLiquidityMessage,
                                      CRemoveLiquidityMessage,
                                      CUtxosToAccountMessage,
                                      CAccountToUtxosMessage,
                                      CAccountToAccountMessage,
                                      CAnyAccountsToAccountsMessage,
                                      CSmartContractMessage,
                                      CFutureSwapMessage,
                                      CGovernanceMessage,
                                      CGovernanceUnsetMessage,
                                      CGovernanceHeightMessage,
                                      CAppointOracleMessage,
                                      CRemoveOracleAppointMessage,
                                      CUpdateOracleAppointMessage,
                                      CSetOracleDataMessage,
                                      CICXCreateOrderMessage,
                                      CICXMakeOfferMessage,
                                      CICXSubmitDFCHTLCMessage,
                                      CICXSubmitEXTHTLCMessage,
                                      CICXClaimDFCHTLCMessage,
                                      CICXCloseOrderMessage,
                                      CICXCloseOfferMessage,
                                      CLoanSetCollateralTokenMessage,
                                      CLoanSetLoanTokenMessage,
                                      CLoanUpdateLoanTokenMessage,
                                      CLoanSchemeMessage,
                                      CDefaultLoanSchemeMessage,
                                      CDestroyLoanSchemeMessage,
                                      CVaultMessage,
                                      CCloseVaultMessage,
                                      CUpdateVaultMessage,
                                      CDepositToVaultMessage,
                                      CWithdrawFromVaultMessage,
                                      CPaybackWithCollateralMessage,
                                      CLoanTakeLoanMessage,
                                      CLoanPaybackLoanMessage,
                                      CLoanPaybackLoanV2Message,
                                      CAuctionBidMessage,
                                      CCreateProposalMessage,
                                      CProposalVoteMessage,
                                      CTransferDomainMessage,
                                      CEvmTxMessage>;

class BlockContext {
    std::shared_ptr<CCustomCSView> cache;
    CCustomCSView *view;
    std::optional<bool> isEvmEnabledForBlock;
    std::shared_ptr<CScopedTemplate> evmTemplate{};
    bool evmPreValidate{};
    const uint32_t height{};
    const uint64_t time{};
    const Consensus::Params &consensus;

public:
    explicit BlockContext(const uint32_t height,
                          const uint64_t time,
                          const Consensus::Params &consensus,
                          CCustomCSView *view = {},
                          const std::optional<bool> enabled = std::nullopt,
                          const std::shared_ptr<CScopedTemplate> &evmTemplate = {},
                          const bool prevalidate = {});
    explicit BlockContext(BlockContext &other, CCustomCSView &otherView);

    [[nodiscard]] CCustomCSView &GetView();
    [[nodiscard]] bool GetEVMEnabledForBlock();
    [[nodiscard]] bool GetEVMPreValidate() const;
    [[nodiscard]] const std::shared_ptr<CScopedTemplate> &GetEVMTemplate() const;
    [[nodiscard]] const uint32_t &GetHeight() const;
    [[nodiscard]] const uint64_t &GetTime() const;
    [[nodiscard]] const Consensus::Params &GetConsensus() const;

    void SetView(CCustomCSView &other);
    void SetEVMPreValidate(const bool other);
    void SetEVMTemplate(const std::shared_ptr<CScopedTemplate> &evmTemplate);
};

class TransactionContext {
    const CCoinsViewCache &coins;
    const CTransaction &tx;
    const Consensus::Params &consensus;
    const uint32_t &height;
    const uint64_t &time;
    const uint32_t txn{};

    std::vector<unsigned char> metadata;
    std::optional<CustomTxType> txType;
    std::optional<std::pair<Res, CCustomTxMessage>> txMessageResult;
    bool metadataValidation{};

public:
    TransactionContext(const CCoinsViewCache &coins,
                       const CTransaction &tx,
                       const BlockContext &blockCtx,
                       const uint32_t txn = {});

    [[nodiscard]] const CCoinsViewCache &GetCoins() const;
    [[nodiscard]] const CTransaction &GetTransaction() const;
    [[nodiscard]] const Consensus::Params &GetConsensus() const;
    [[nodiscard]] uint32_t GetHeight() const;
    [[nodiscard]] uint64_t GetTime() const;
    [[nodiscard]] uint32_t GetTxn() const;
    [[nodiscard]] CustomTxType GetTxType();
    [[nodiscard]] std::pair<Res, CCustomTxMessage> &GetTxMessage();
    [[nodiscard]] bool GetMetadataValidation() const;
};

CCustomTxMessage customTypeToMessage(CustomTxType txType);
bool IsMempooledCustomTxCreate(const CTxMemPool &pool, const uint256 &txid);
Res RpcInfo(CCustomCSView &view, const CTransaction &tx, uint32_t height, CustomTxType &type, UniValue &results);
Res CustomMetadataParse(uint32_t height,
                        const Consensus::Params &consensus,
                        const std::vector<unsigned char> &metadata,
                        CCustomTxMessage &txMessage);

Res ApplyCustomTx(BlockContext &blockCtx, TransactionContext &txCtx);

Res CustomTxVisit(const CCustomTxMessage &txMessage, BlockContext &blockCtx, const TransactionContext &txCtx);

ResVal<uint256> ApplyAnchorRewardTx(CCustomCSView &mnview,
                                    const CTransaction &tx,
                                    int height,
                                    const uint256 &prevStakeModifier,
                                    const std::vector<unsigned char> &metadata,
                                    const Consensus::Params &consensusParams);
ResVal<uint256> ApplyAnchorRewardTxPlus(CCustomCSView &mnview,
                                        const CTransaction &tx,
                                        int height,
                                        const std::vector<unsigned char> &metadata,
                                        const Consensus::Params &consensusParams);
ResVal<CAmount> GetAggregatePrice(CCustomCSView &view,
                                  const std::string &token,
                                  const std::string &currency,
                                  uint64_t lastBlockTime);
bool IsVaultPriceValid(CCustomCSView &mnview, const CVaultId &vaultId, uint32_t height);
Res SwapToDFIorDUSD(CCustomCSView &mnview,
                    DCT_ID tokenId,
                    CAmount amount,
                    const CScript &from,
                    const CScript &to,
                    uint32_t height,
                    const Consensus::Params &consensus,
                    bool forceLoanSwap = false);

bool IsRegtestNetwork();
bool IsTestNetwork();
bool IsMainNetwork();

bool OraclePriceFeed(CCustomCSView &view, const CTokenCurrencyPair &priceFeed);
bool CheckOPReturnSize(const CScript &scriptPubKey, const uint32_t opreturnSize);

class CPoolSwap {
    const CPoolSwapMessage &obj;
    uint32_t height;
    CAmount result{0};
    DCT_ID currentID;

public:
    std::vector<std::pair<std::string, std::string>> errors;

    CPoolSwap(const CPoolSwapMessage &obj, uint32_t height)
        : obj(obj),
          height(height) {}

    std::vector<DCT_ID> CalculateSwaps(CCustomCSView &view, const Consensus::Params &consensus, bool testOnly = false);
    Res ExecuteSwap(CCustomCSView &view,
                    std::vector<DCT_ID> poolIDs,
                    const Consensus::Params &consensus,
                    bool testOnly = false);
    std::vector<std::vector<DCT_ID>> CalculatePoolPaths(CCustomCSView &view);
    CTokenAmount GetResult() { return CTokenAmount{obj.idTokenTo, result}; };
};

#endif  // DEFI_DFI_MN_CHECKS_H
