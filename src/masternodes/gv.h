// Copyright (c) 2020 The DeFi Foundation
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_MASTERNODES_GV_H
#define DEFI_MASTERNODES_GV_H

#include <flushablestorage.h>
#include <masternodes/factory.h>
#include <masternodes/res.h>
#include <univalue/include/univalue.h>

#include <unordered_map>

class ATTRIBUTES;
class CCustomCSView;

class GovVariable
{
public:
    static std::shared_ptr<GovVariable> Create(std::string const & name) {
        return std::shared_ptr<GovVariable>(Factory<GovVariable>::Create(name));
    }
    virtual ~GovVariable() = default;

    virtual std::string GetName() const = 0;

    virtual Res Import(UniValue const &) = 0;
    virtual UniValue Export() const = 0;
    /// @todo it looks like Validate+Apply may be redundant. refactor for one?
    virtual Res Validate(CCustomCSView const &) const = 0;
    virtual Res Apply(CCustomCSView &, uint32_t) = 0;

    virtual void Serialize(CVectorWriter& s) const = 0;
    virtual void Unserialize(VectorReader& s) = 0;

    virtual void Serialize(CDataStream& s) const = 0;
    virtual void Unserialize(CDataStream& s) = 0;
};

struct CGovernanceMessage {
    std::unordered_map<std::string, std::shared_ptr<GovVariable>> govs;

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        std::string name;
        while(!s.empty()) {
            s >> name;
            auto& gov = govs[name];
            auto var = GovVariable::Create(name);
            if (!var) break;
            s >> *var;
            gov = std::move(var);
        }
    }
};

struct CGovernanceHeightMessage {
    std::string govName;
    std::shared_ptr<GovVariable> govVar;
    uint32_t startHeight;

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        if (!s.empty()) {
            s >> govName;
            if ((govVar = GovVariable::Create(govName))) {
                s >> *govVar;
                s >> startHeight;
            }
        }
    }
};

class CGovView : public virtual CStorageView
{
public:
    Res SetVariable(GovVariable const & var);
    std::shared_ptr<GovVariable> GetVariable(std::string const &govKey) const;

    Res SetStoredVariables(const std::set<std::shared_ptr<GovVariable>>& govVars, const uint32_t height);
    std::set<std::shared_ptr<GovVariable>> GetStoredVariables(const uint32_t height);
    std::map<std::string, std::map<uint64_t, std::shared_ptr<GovVariable>>> GetAllStoredVariables();
    void EraseStoredVariables(const uint32_t height);

    virtual std::shared_ptr<ATTRIBUTES> GetAttributes() const;

    [[nodiscard]] virtual bool AreTokensLocked(const std::set<uint32_t>& tokenIds) const = 0;

    struct ByHeightVars { static constexpr uint8_t prefix() { return 'G'; } };
    struct ByName { static constexpr uint8_t prefix() { return 'g'; } };
};

struct GovVarKey {
    uint32_t height;
    std::string name;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(WrapBigEndian(height));
        READWRITE(name);
    }
};

//
// please, use this as template for new variables:
//
/*
class GV_EXAMPLE : public GovVariable, public AutoRegistrator<GovVariable, GV_EXAMPLE>
{
public:
    // implement this methods:
    Res Import(UniValue const &val) override;
    UniValue Export() const override;
    Res Validate(CCustomCSView const &mnview) const override;
    Res Apply(CCustomCSView &mnview) override;

    std::string GetName() const override { return TypeName(); }
    static constexpr char const * TypeName() { return "GV_EXAMPLE"; }
    static GovVariable * Create() { return new GV_EXAMPLE(); }

    ADD_OVERRIDE_SERIALIZE_METHODS(CDataStream)

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
//        READWRITE(splits);
    }
    // place your data here
//    std::map<DCT_ID, CAmount> splits;
};
*/

#endif // DEFI_MASTERNODES_GV_H
