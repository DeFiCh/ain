// Copyright (c) 2020 The DeFi Foundation
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_DFI_GOVVARIABLES_ATTRIBUTES_H
#define DEFI_DFI_GOVVARIABLES_ATTRIBUTES_H

#include <dfi/govvariables/attributetypes.h>
#include <dfi/gv.h>

class CBlockIndex;
namespace Consensus {
    struct Params;
}

void TrackNegativeInterest(CCustomCSView &mnview, const CTokenAmount &amount);
void TrackLiveBalances(CCustomCSView &mnview, const CBalances &balances, const uint8_t key);
void TrackDUSDAdd(CCustomCSView &mnview, const CTokenAmount &amount);
void TrackDUSDSub(CCustomCSView &mnview, const CTokenAmount &amount);

bool IsEVMEnabled(CCustomCSView &view);
Res StoreGovVars(const CGovernanceHeightMessage &obj, CCustomCSView &view);
ResVal<CScript> GetFutureSwapContractAddress(const std::string &contract);

enum GovVarsFilter {
    All,
    NoAttributes,
    AttributesOnly,
    PrefixedAttributes,
    LiveAttributes,
    Version2Dot7,
};

class ATTRIBUTES : public GovVariable, public AutoRegistrator<GovVariable, ATTRIBUTES> {
public:
    virtual ~ATTRIBUTES() override {}

    std::string GetName() const override { return TypeName(); }

    bool IsEmpty() const override;
    Res Import(const UniValue &val) override;
    UniValue Export() const override;
    UniValue ExportFiltered(GovVarsFilter filter, const std::string &prefix) const;
    Res CheckKeys() const;

    Res Validate(const CCustomCSView &mnview) const override;
    Res Apply(CCustomCSView &mnview, const uint32_t height) override;
    Res Erase(CCustomCSView &mnview, uint32_t height, const std::vector<std::string> &) override;

    static constexpr const char *TypeName() { return "ATTRIBUTES"; }
    static GovVariable *Create() { return new ATTRIBUTES(); }

private:
    template <typename T>
    static void GetIf(std::optional<T> &opt, const CAttributeValue &var) {
        if (auto value = std::get_if<T>(&var)) {
            opt = *value;
        }
    }

    template <typename T>
    static void GetIf(T &val, const CAttributeValue &var) {
        if (auto value = std::get_if<T>(&var)) {
            val = *value;
        }
    }

    uint32_t time{};
    std::shared_ptr<CScopedTemplate> evmTemplate{};

public:
    template <typename K, typename T>
    [[nodiscard]] T GetValue(const K &key, T value) const {
        static_assert(std::is_convertible_v<K, CAttributeType>);
        auto it = attributes.find(key);
        if (it != attributes.end()) {
            GetIf(value, it->second);
        }
        return value;
    }

    template <typename K, typename T>
    void SetValue(const K &key, T &&value) {
        static_assert(std::is_convertible_v<K, CAttributeType>);
        static_assert(std::is_convertible_v<T, CAttributeValue>);
        changed.insert(key);
        attributes[key] = std::forward<T>(value);
    }

    template <typename K>
    bool EraseKey(const K &key) {
        static_assert(std::is_convertible_v<K, CAttributeType>);
        if (attributes.erase(key)) {
            changed.insert(key);
            return true;
        }
        return false;
    }

    template <typename K>
    [[nodiscard]] bool CheckKey(const K &key) const {
        static_assert(std::is_convertible_v<K, CAttributeType>);
        return attributes.count(key) > 0;
    }

    template <typename C, typename K>
    void ForEach(const C &callback, const K &key) const {
        static_assert(std::is_convertible_v<K, CAttributeType>);
        static_assert(std::is_invocable_r_v<bool, C, K, CAttributeValue>);
        for (auto it = attributes.lower_bound(key); it != attributes.end(); ++it) {
            if (auto attrV0 = std::get_if<K>(&it->first)) {
                if (!std::invoke(callback, *attrV0, it->second)) {
                    break;
                }
            }
        }
    }

    ADD_OVERRIDE_VECTOR_SERIALIZE_METHODS
    ADD_OVERRIDE_SERIALIZE_METHODS(CDataStream)

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action) {
        READWRITE(attributes);
    }

    // For formatting in export
    static const std::map<uint8_t, std::string> &displayVersions();
    static const std::map<uint8_t, std::string> &displayTypes();
    static const std::map<uint8_t, std::string> &displayParamsIDs();
    static const std::map<uint8_t, std::string> &allowedExportParamsIDs();
    static const std::map<uint8_t, std::string> &displayLocksIDs();
    static const std::map<uint8_t, std::string> &displayOracleIDs();
    static const std::map<uint8_t, std::string> &displayGovernanceIDs();
    static const std::map<uint8_t, std::string> &displayTransferIDs();
    static const std::map<uint8_t, std::string> &displayEVMIDs();
    static const std::map<uint8_t, std::string> &displayVaultIDs();
    static const std::map<uint8_t, std::string> &displayRulesIDs();
    static const std::map<uint8_t, std::map<uint8_t, std::string>> &displayKeys();

private:
    friend class CGovView;
    friend class CCustomCSView;
    bool futureUpdated{};
    bool futureDUSDUpdated{};
    std::set<uint32_t> tokenSplits{};
    std::set<uint32_t> interestTokens{};
    std::set<CAttributeType> changed;
    std::map<CAttributeType, CAttributeValue> attributes;

    // Defined allowed arguments
    static const std::map<std::string, uint8_t> &allowedVersions();
    static const std::map<std::string, uint8_t> &allowedTypes();
    static const std::map<std::string, uint8_t> &allowedParamIDs();
    static const std::map<std::string, uint8_t> &allowedLocksIDs();
    static const std::map<std::string, uint8_t> &allowedOracleIDs();
    static const std::map<std::string, uint8_t> &allowedGovernanceIDs();
    static const std::map<std::string, uint8_t> &allowedTransferIDs();
    static const std::map<std::string, uint8_t> &allowedEVMIDs();
    static const std::map<std::string, uint8_t> &allowedVaultIDs();
    static const std::map<std::string, uint8_t> &allowedRulesIDs();
    static const std::map<uint8_t, std::map<std::string, uint8_t>> &allowedKeys();
    static const std::map<uint8_t, std::map<uint8_t, std::function<ResVal<CAttributeValue>(const std::string &)>>>
        &parseValue();

    Res ProcessVariable(const std::string &key,
                        const std::optional<UniValue> &value,
                        std::function<Res(const CAttributeType &, const CAttributeValue &)> applyVariable);
    Res RefundFuturesDUSD(CCustomCSView &mnview, const uint32_t height);
    Res RefundFuturesContracts(CCustomCSView &mnview,
                               const uint32_t height,
                               const uint32_t tokenID = std::numeric_limits<uint32_t>::max());
    void SetAttributesMembers(const int64_t setTime, const std::shared_ptr<CScopedTemplate> &setEvmTemplate);
    [[nodiscard]] std::optional<CLoanView::CLoanSetLoanTokenImpl> GetLoanTokenByID(const CCustomCSView &view,
                                                                                   const DCT_ID &id) const;
    [[nodiscard]] bool IsChanged() const { return !changed.empty(); }
};

#endif  // DEFI_DFI_GOVVARIABLES_ATTRIBUTES_H
