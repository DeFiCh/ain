// Copyright (c) 2020 The DeFi Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_MASTERNODES_GOVVARIABLES_ORACLE_DEVIATION_H
#define BITCOIN_MASTERNODES_GOVVARIABLES_ORACLE_DEVIATION_H

#include <masternodes/gv.h>
#include <amount.h>

class ORACLE_DEVIATION : public GovVariable, public AutoRegistrator<GovVariable, ORACLE_DEVIATION>
{
public:
    virtual ~ORACLE_DEVIATION() override {}

    std::string GetName() const override {
        return TypeName();
    }

    Res Import(UniValue const &val) override;
    UniValue Export() const override;
    Res Validate(CCustomCSView const &mnview) const override;
    Res Apply(CCustomCSView &mnview, uint32_t height) override;

    static constexpr char const * TypeName() { return "ORACLE_DEVIATION"; }
    static GovVariable * Create() { return new ORACLE_DEVIATION(); }

    ADD_OVERRIDE_VECTOR_SERIALIZE_METHODS
    ADD_OVERRIDE_SERIALIZE_METHODS(CDataStream)

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(deviation);
    }
    CAmount deviation;
};

#endif // BITCOIN_MASTERNODES_GOVVARIABLES_ORACLE_DEVIATION_H
