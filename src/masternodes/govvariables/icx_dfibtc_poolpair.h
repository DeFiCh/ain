#ifndef DEFI_MASTERNODES_GOVVARIABLES_ICX_DFIBTC_POOLPAIR_H
#define DEFI_MASTERNODES_GOVVARIABLES_ICX_DFIBTC_POOLPAIR_H

#include <masternodes/gv.h>
#include <amount.h>

class ICX_DFIBTC_POOLPAIR : public GovVariable, public AutoRegistrator<GovVariable, ICX_DFIBTC_POOLPAIR>
{
public:
    virtual ~ICX_DFIBTC_POOLPAIR() override {}

    std::string GetName() const override {
        return TypeName();
    }

    Res Import(UniValue const &val) override;
    UniValue Export() const override;
    Res Validate(CCustomCSView const &mnview) const override;
    Res Apply(CCustomCSView &mnview, uint32_t height) override;

    static constexpr char const * TypeName() { return "ICX_DFIBTC_POOLPAIR"; }
    static GovVariable * Create() { return new ICX_DFIBTC_POOLPAIR(); }

    ADD_OVERRIDE_VECTOR_SERIALIZE_METHODS
    ADD_OVERRIDE_SERIALIZE_METHODS(CDataStream)

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(poolPairId);
    }
    
    DCT_ID poolPairId;
};

#endif // DEFI_MASTERNODES_GOVVARIABLES_ICX_DFIBTC_POOLPAIR_H