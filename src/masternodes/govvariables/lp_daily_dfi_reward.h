// Copyright (c) 2020 The DeFi Foundation
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_MASTERNODES_GOVVARIABLES_LP_DAILY_DFI_REWARD_H
#define DEFI_MASTERNODES_GOVVARIABLES_LP_DAILY_DFI_REWARD_H

#include <masternodes/gv.h>
#include <amount.h>

class LP_DAILY_DFI_REWARD : public GovVariable, public AutoRegistrator<GovVariable, LP_DAILY_DFI_REWARD>
{
public:
    virtual ~LP_DAILY_DFI_REWARD() override {}

    std::string GetName() const override {
        return TypeName();
    }

    Res Import(UniValue const &val) override;
    UniValue Export() const override;
    Res Validate(CCustomCSView const &mnview) const override;
    Res Apply(CCustomCSView &mnview) override;

    static constexpr char const * TypeName() { return "LP_DAILY_DFI_REWARD"; }
    static GovVariable * Create() { return new LP_DAILY_DFI_REWARD(); }

    ADD_OVERRIDE_SERIALIZE_METHODS(CDataStream)

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(dailyReward);
    }
    CAmount dailyReward;
};

#endif // DEFI_MASTERNODES_GOVVARIABLES_LP_DAILY_DFI_REWARD_H
