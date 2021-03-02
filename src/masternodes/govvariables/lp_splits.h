// Copyright (c) 2020 The DeFi Foundation
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_MASTERNODES_GOVVARIABLES_LP_SPLITS_H
#define DEFI_MASTERNODES_GOVVARIABLES_LP_SPLITS_H

#include <masternodes/gv.h>
#include <amount.h>

class LP_SPLITS : public GovVariable, public AutoRegistrator<GovVariable, LP_SPLITS>
{
public:
    virtual ~LP_SPLITS() override {}

    std::string GetName() const override {
        return TypeName();
    }

    Res Import(UniValue const &val) override;
    UniValue Export() const override;
    Res Validate(CCustomCSView const &mnview) const override;
    Res Apply(CCustomCSView &mnview) override;

    static constexpr char const * TypeName() { return "LP_SPLITS"; }
    static GovVariable * Create() { return new LP_SPLITS(); }

    ADD_OVERRIDE_SERIALIZE_METHODS(CDataStream)

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(splits);
    }

    std::map<DCT_ID, CAmount> splits;
};

#endif // DEFI_MASTERNODES_GOVVARIABLES_LP_SPLITS_H
