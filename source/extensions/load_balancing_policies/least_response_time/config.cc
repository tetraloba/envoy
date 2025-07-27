#include "source/extensions/load_balancing_policies/least_response_time/config.h"

#include "envoy/extensions/load_balancing_policies/least_response_time/v3/least_response_time.pb.h"

#include "source/extensions/load_balancing_policies/least_response_time/least_response_time_lb.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolices {
namespace LeastResponseTime {

LegacyLeastResponseTimeLbConfig::LegacyLeastResponseTimeLbConfig(const ClusterProto& cluster) {
  if (cluster.has_least_response_time_lb_config()) {
    lb_config_ = cluster.least_response_time_lb_config();
  }
}

TypedLeastResponseTimeLbConfig::TypedLeastResponseTimeLbConfig(const LeastResponseTimeLbProto& lb_config)
    : lb_config_(lb_config) {}

Upstream::LoadBalancerPtr LeastResponseTimeCreator::operator()(
    Upstream::LoadBalancerParams params, OptRef<const Upstream::LoadBalancerConfig> lb_config,
    const Upstream::ClusterInfo& cluster_info, const Upstream::PrioritySet&,
    Runtime::Loader& runtime, Random::RandomGenerator& random, TimeSource& time_source) {

  auto active_or_legacy =
      Common::ActiveOrLegacy<TypedLeastResponseTimeLbConfig, LegacyLeastResponseTimeLbConfig>::get(
          lb_config.ptr());

  if (active_or_legacy.hasActive()) {
    return std::make_unique<Upstream::LeastResponseTimeLoadBalancer>(
        params.priority_set, params.local_priority_set, cluster_info.lbStats(), runtime, random,
        PROTOBUF_PERCENT_TO_ROUNDED_INTEGER_OR_DEFAULT(cluster_info.lbConfig(),
                                                       healthy_panic_threshold, 100, 50),
        active_or_legacy.active()->lb_config_, time_source);
  } else {
    return std::make_unique<Upstream::LeastResponseTimeLoadBalancer>(
        params.priority_set, params.local_priority_set, cluster_info.lbStats(), runtime, random,
        cluster_info.lbConfig(),
        active_or_legacy.hasLegacy() ? active_or_legacy.legacy()->lbConfig() : absl::nullopt,
        time_source);
  }
}

/**
 * Static registration for the Factory. @see RegisterFactory.
 */
REGISTER_FACTORY(Factory, Upstream::TypedLoadBalancerFactory);

} // namespace LeastResponseTime
} // namespace LoadBalancingPolices
} // namespace Extensions
} // namespace Envoy
