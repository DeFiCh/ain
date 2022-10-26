#include <featuremanager.h>

Res CFeatureManager::Register(const CFeatureComponent& feature){
    // TODO
    return Res::Ok();
};

ResVal<CFeatureComponent> CFeatureManager::GetInstance(const ::ComponentId& componentId) const{
    CFeatureComponent component{};
    // TODO
    return ResVal<CFeatureComponent>(component, Res::Ok());
};

ResVal<CFeatureComponent[]> CFeatureManager::ListAll(const ::CategoryId& categoryId, const uint32_t height) const {
    CFeatureComponent components[]{};
    // TODO
    return ResVal<CFeatureComponent[]>(components, Res::Ok());
};

ResVal<CFeatureComponent[]> CFeatureManager::ListActive(const ::CategoryId& categoryId, const uint32_t height) const{
    CFeatureComponent components[]{};
    // TODO
    return ResVal<CFeatureComponent[]>(components, Res::Ok());
}
Res Enable(const ::ComponentId& componentId);
Res Disable(const ::ComponentId& componentId);
ResVal<FeatureActivationInfo> GetActivationInfo(const ::ComponentId& componentId);
bool isAvailable(const ::ComponentId& componentId);



