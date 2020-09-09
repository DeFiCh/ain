// Copyright (c) 2019 The DeFi Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_MASTERNODES_GV_H
#define DEFI_MASTERNODES_GV_H

#include <flushablestorage.h>
#include <masternodes/res.h>
#include <univalue/include/univalue.h>

class GovVariable;
class CGovView;
class CCustomCSView;

template <typename TBaseType>
class Factory
{
public:
    typedef TBaseType * TResult;
    typedef TResult (* Creator)(); // creator function

    Factory() = delete;
    ~Factory() = delete;

    template <typename TType>
    static bool Registrate() {
        auto res = creators().insert(std::pair<std::string, Creator>(TType::TypeName(), &TType::Create));
//        std::cerr << "\nRegistered " << name << " " << std::to_string(creators().size()) << "\n" << std::ends;
//        auto it = creators().find(name);
//        std::cerr << "\n--->>> " << name << " " <<  (it != creators().end()) << "\n" << std::ends;
        return res.second;
    }

    static TResult Create(std::string const & name) {
        typename TCreators::const_iterator creator(creators().find(name));
        if (creator == creators().end()) {
            return {};
        }
        return creator->second();
    }

public:
    typedef std::map<std::string, Creator> TCreators;
    static TCreators & creators() {
        static bool initialized = false;
        if (!initialized) {
            m_creators.reset(new TCreators);
            initialized = true;
        }
        return *m_creators;
    }
    static std::shared_ptr<TCreators> m_creators;
};

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
    virtual Res Validate(CCustomCSView const &mnview) const = 0;
    virtual Res Apply(CCustomCSView &mnview) = 0;

    virtual void Serialize(CDataStream& s) const = 0;
    virtual void Unserialize(CDataStream& s) = 0;
};

class CGovView : public virtual CStorageView
{
public:
    Res SetVariable(GovVariable const & var);
    std::shared_ptr<GovVariable> GetVariable(std::string const &govKey) const;

    struct ByName { static const unsigned char prefix; };
};


//
// please, use this as template for new variables:
//
/*
class GV_EXAMPLE : public GovVariable
{
public:
    virtual ~GV_EXAMPLE() override {}

    std::string GetName() const override {
        return TypeName();
    }

    // implement this methods:
    Res Import(UniValue const &val) override;
    UniValue Export() const override;
    Res Validate(CCustomCSView const &mnview) const override;
    Res Apply(CCustomCSView &mnview) override;

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
