
#ifndef DEFI_FEATUREMANAGER_H
#define DEFI_FEATUREMANAGER_H

#include <masternodes/res.h>
#include <flushablestorage.h>

using ComponentId = std::string; //TDB
using CategoryId = std::string;  //TBD

class CFeatureComponent{
    std::string name;
    bool hasToggle;
    bool enabled;

    public:
        std::string GetName();
        ComponentId GetComponentId();
        CategoryId GetCategoryId();
        bool HasToggle();
        Res Enable();
        Res Disable();
        bool IsActive(uint32_t height);



};
struct FeatureActivationInfo{
    // TBD
};

class CFeatureManager : public virtual CStorageView {
    ComponentId ComponentId{}; // TBD
    CategoryId CategoryId{};  // TBD

public:
    Res Register(const CFeatureComponent& feature);
    ResVal<CFeatureComponent> GetInstance(const ::ComponentId& componentId) const;
    ResVal<CFeatureComponent[]> ListAll(const ::CategoryId& categoryId, const uint32_t height) const;
    ResVal<CFeatureComponent[]> ListActive(const ::CategoryId& categoryId, const uint32_t height) const;
    Res Enable(const ::ComponentId& componentId);
    Res Disable(const ::ComponentId& componentId);
    ResVal<FeatureActivationInfo> GetActivationInfo(const ::ComponentId& componentId);
    bool isAvailable(const ::ComponentId& componentId);
};

#endif // DEFI_FEATUREMANAGER_H
