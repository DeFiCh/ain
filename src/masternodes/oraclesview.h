#ifndef AIN_ORACLESVIEW_H
#define AIN_ORACLESVIEW_H

#include <string>

#include <flushablestorage.h>
#include <masternodes/factory.h>
#include <masternodes/res.h>
#include <univalue/include/univalue.h>

class CPriceFeedValidator {
public:
    virtual ~CPriceFeedValidator() = default;

    virtual bool IsValidPriceFeedName(const std::string& priceFeed) const = 0;
};

struct CPriceFeed {
    uint64_t timestamp{};
    double value{};

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(timestamp);
        READWRITE(value);
    }
};

class COraclesView: public virtual CStorageView {
public:
    explicit COraclesView(std::shared_ptr<CPriceFeedValidator> priceFeedValidator);

    virtual ~COraclesView() override = default;

    bool SetPriceFeedValue(const std::string& feedName, uint64_t timestamp, double rawPrice);

    ResVal<CPriceFeed> GetPriceFeedValue(const std::string& feedName);

    /// check if price feed name exists
    bool ExistPriceFeed(const std::string& feedName) const;

    struct ByName { static const unsigned char prefix; };

private:
    std::shared_ptr<CPriceFeedValidator> _validator;
};

#endif //AIN_ORACLESVIEW_H
