// Copyright (c) 2020 The DeFi Foundation
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_MASTERNODES_GOVVARIABLES_LOAN_DAILY_REWARD_H
#define DEFI_MASTERNODES_GOVVARIABLES_LOAN_DAILY_REWARD_H

#include <masternodes/gv.h>
#include <amount.h>

class LP_DAILY_LOAN_TOKEN_REWARD : public GovVariable, public AutoRegistrator<GovVariable, LP_DAILY_LOAN_TOKEN_REWARD>
{
public:
    virtual ~LP_DAILY_LOAN_TOKEN_REWARD() override {}

    std::string GetName() const override {
        return TypeName();
    }

    bool IsEmpty() const override;
    Res Import(UniValue const &val) override;
    UniValue Export() const override;
    Res Validate(CCustomCSView const &mnview) const override;
    Res Apply(CCustomCSView &mnview, uint32_t height) override;
    Res Erase(CCustomCSView &mnview, uint32_t height, std::vector<std::string> const &) override;

    static constexpr char const * TypeName() { return "LP_DAILY_LOAN_TOKEN_REWARD"; }
    static GovVariable * Create() { return new LP_DAILY_LOAN_TOKEN_REWARD(); }

    ADD_OVERRIDE_VECTOR_SERIALIZE_METHODS
    ADD_OVERRIDE_SERIALIZE_METHODS(CDataStream)

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(dailyReward);
    }

    GvOptional<CAmount> dailyReward;
};

#endif // DEFI_MASTERNODES_GOVVARIABLES_LOAN_DAILY_REWARD_H
