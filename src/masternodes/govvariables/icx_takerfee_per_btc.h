#ifndef DEFI_MASTERNODES_GOVVARIABLES_ICX_TAKERFEE_PER_BTC_H
#define DEFI_MASTERNODES_GOVVARIABLES_ICX_TAKERFEE_PER_BTC_H

#include <masternodes/gv.h>
#include <amount.h>

class ICX_TAKERFEE_PER_BTC : public GovVariable, public AutoRegistrator<GovVariable, ICX_TAKERFEE_PER_BTC>
{
public:
    bool IsEmpty() const override;
    Res Import(UniValue const &val) override;
    UniValue Export() const override;
    Res Validate(CCustomCSView const &mnview) const override;
    Res Apply(CCustomCSView &mnview, uint32_t height) override;
    Res Erase(CCustomCSView &mnview, uint32_t, std::vector<std::string> const &) override;

    std::string GetName() const override { return TypeName(); }
    static constexpr char const * TypeName() { return "ICX_TAKERFEE_PER_BTC"; }
    static GovVariable * Create() { return new ICX_TAKERFEE_PER_BTC(); }

    ADD_OVERRIDE_VECTOR_SERIALIZE_METHODS
    ADD_OVERRIDE_SERIALIZE_METHODS(CDataStream)

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(takerFeePerBTC);
    }

    GvOptional<CAmount> takerFeePerBTC;
};

#endif // DEFI_MASTERNODES_GOVVARIABLES_ICX_TAKERFEE_PER_BTC_H
