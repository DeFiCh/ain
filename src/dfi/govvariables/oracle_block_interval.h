// Copyright (c) 2020 The DeFi Foundation
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_DFI_GOVVARIABLES_ORACLE_BLOCK_INTERVAL_H
#define DEFI_DFI_GOVVARIABLES_ORACLE_BLOCK_INTERVAL_H

#include <amount.h>
#include <dfi/gv.h>

class ORACLE_BLOCK_INTERVAL : public GovVariable, public AutoRegistrator<GovVariable, ORACLE_BLOCK_INTERVAL> {
public:
    virtual ~ORACLE_BLOCK_INTERVAL() override {}

    std::string GetName() const override { return TypeName(); }

    bool IsEmpty() const override;
    Res Import(const UniValue &val) override;
    UniValue Export() const override;
    Res Validate(const CCustomCSView &mnview) const override;
    Res Apply(CCustomCSView &mnview, uint32_t height) override;
    Res Erase(CCustomCSView &mnview, uint32_t height, const std::vector<std::string> &) override;

    static constexpr const char *TypeName() { return "ORACLE_BLOCK_INTERVAL"; }
    static GovVariable *Create() { return new ORACLE_BLOCK_INTERVAL(); }

    ADD_OVERRIDE_VECTOR_SERIALIZE_METHODS
    ADD_OVERRIDE_SERIALIZE_METHODS(CDataStream)

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action) {
        READWRITE(blockInterval);
    }

    GvOptional<uint32_t> blockInterval;
};

#endif  // DEFI_DFI_GOVVARIABLES_ORACLE_BLOCK_INTERVAL_H
