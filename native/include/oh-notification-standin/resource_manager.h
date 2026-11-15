/*
 * Stand-in for global_resource_management's interfaces/inner_api/include/resource_manager.h. The notification
 * headers hold button icons only as std::shared_ptr<ResourceManager::Resource>, which needs only the declaration
 * of the nested type. Westlake sets no icon button.
 */
#ifndef WESTLAKE_STANDIN_RESOURCE_MANAGER_H
#define WESTLAKE_STANDIN_RESOURCE_MANAGER_H

namespace OHOS {
namespace Global {
namespace Resource {
class ResourceManager {
public:
    struct Resource;
};
}  // namespace Resource
}  // namespace Global
}  // namespace OHOS

#endif
