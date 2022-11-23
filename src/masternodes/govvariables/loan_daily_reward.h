// Copyright (c) 2020 The DeFi Foundation
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_MASTERNODES_GOVVARIABLES_LOAN_DAILY_REWARD_H
#define DEFI_MASTERNODES_GOVVARIABLES_LOAN_DAILY_REWARD_H

#include <amount.h>
#include <masternodes/gv.h>

class LP_DAILY_LOAN_TOKEN_REWARD : public GovVariable, public AutoRegistrator<GovVariable, LP_DAILY_LOAN_TOKEN_REWARD> {
   public:
    virtual ~LP_DAILY_LOAN_TOKEN_REWARD() override {}

    std::string GetName() const override { return TypeName(); }

    bool IsEmpty() const override;
    Res Import(const UniValue &val) override;
    UniValue Export() const override;
    Res Validate(const CCustomCSView &mnview) const override;
    Res Apply(CCustomCSView &mnview, uint32_t height) override;
    Res Erase(CCustomCSView &mnview, uint32_t height, const std::vector<std::string> &) override;

    static constexpr const char *TypeName() { return "LP_DAILY_LOAN_TOKEN_REWARD"; }
    static GovVariable *Create() { return new LP_DAILY_LOAN_TOKEN_REWARD(); }

    ADD_OVERRIDE_VECTOR_SERIALIZE_METHODS
    ADD_OVERRIDE_SERIALIZE_METHODS(CDataStream)

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action) {
        READWRITE(dailyReward);
    }

    GvOptional<CAmount> dailyReward;
};

#endif  // DEFI_MASTERNODES_GOVVARIABLES_LOAN_DAILY_REWARD_H
