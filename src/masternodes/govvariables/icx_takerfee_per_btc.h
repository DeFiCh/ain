#ifndef DEFI_MASTERNODES_GOVVARIABLES_ICX_TAKERFEE_PER_BTC_H
#define DEFI_MASTERNODES_GOVVARIABLES_ICX_TAKERFEE_PER_BTC_H

#include <amount.h>
#include <masternodes/gv.h>

class ICX_TAKERFEE_PER_BTC : public GovVariable, public AutoRegistrator<GovVariable, ICX_TAKERFEE_PER_BTC> {
   public:
    virtual ~ICX_TAKERFEE_PER_BTC() override {}

    std::string GetName() const override { return TypeName(); }

    Res Import(const UniValue &val) override;
    UniValue Export() const override;
    Res Validate(const CCustomCSView &mnview) const override;
    Res Apply(CCustomCSView &mnview, uint32_t height) override;

    static constexpr const char *TypeName() { return "ICX_TAKERFEE_PER_BTC"; }
    static GovVariable *Create() { return new ICX_TAKERFEE_PER_BTC(); }

    ADD_OVERRIDE_VECTOR_SERIALIZE_METHODS
    ADD_OVERRIDE_SERIALIZE_METHODS(CDataStream)

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action) {
        READWRITE(takerFeePerBTC);
    }

    CAmount takerFeePerBTC;
};

#endif  // DEFI_MASTERNODES_GOVVARIABLES_ICX_TAKERFEE_PER_BTC_H