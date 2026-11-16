/*
 * Stand-in for ability_runtime's interfaces/inner_api/wantagent/include/want_agent.h. The notification headers
 * hold want agents only as std::shared_ptr<AbilityRuntime::WantAgent::WantAgent>, which needs no definition, and
 * the real header reaches application_context.h and the resource manager's headers. Westlake sets no want agent.
 */
#ifndef WESTLAKE_STANDIN_WANT_AGENT_H
#define WESTLAKE_STANDIN_WANT_AGENT_H

namespace OHOS {
namespace AbilityRuntime {
namespace WantAgent {
class WantAgent;
}  // namespace WantAgent
}  // namespace AbilityRuntime
}  // namespace OHOS

#endif
