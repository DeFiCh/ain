// Copyright (c) 2020 The DeFi Foundation
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_MASTERNODES_GOVVARIABLES_LOAN_LIQUIDATION_PENALTY_H
#define DEFI_MASTERNODES_GOVVARIABLES_LOAN_LIQUIDATION_PENALTY_H

#include <amount.h>
#include <masternodes/gv.h>

class LOAN_LIQUIDATION_PENALTY : public GovVariable, public AutoRegistrator<GovVariable, LOAN_LIQUIDATION_PENALTY> {
   public:
    virtual ~LOAN_LIQUIDATION_PENALTY() override {}

    std::string GetName() const override { return TypeName(); }

    Res Import(const UniValue &val) override;
    UniValue Export() const override;
    Res Validate(const CCustomCSView &mnview) const override;
    Res Apply(CCustomCSView &mnview, uint32_t height) override;

    static constexpr const char *TypeName() { return "LOAN_LIQUIDATION_PENALTY"; }
    static GovVariable *Create() { return new LOAN_LIQUIDATION_PENALTY(); }

    ADD_OVERRIDE_VECTOR_SERIALIZE_METHODS
    ADD_OVERRIDE_SERIALIZE_METHODS(CDataStream)

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action) {
        READWRITE(penalty);
    }
    CAmount penalty;
};

#endif  // DEFI_MASTERNODES_GOVVARIABLES_LOAN_LIQUIDATION_PENALTY_H
