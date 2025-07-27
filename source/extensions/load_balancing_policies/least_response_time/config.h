#pragma once

#include "envoy/extensions/load_balancing_policies/least_response_time/v3/least_response_time.pb.h"
#include "envoy/extensions/load_balancing_policies/least_response_time/v3/least_response_time.pb.validate.h"
#include "envoy/upstream/load_balancer.h"

#include "source/common/common/logger.h"
#include "source/extensions/load_balancing_policies/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolices {
namespace LeastResponseTime {

using LeastResponseTimeLbProto =
    envoy::extensions::load_balancing_policies::least_response_time::v3::LeastResponseTime;
using ClusterProto = envoy::config::cluster::v3::Cluster;
using LegacyLeastResponseTimeLbProto = ClusterProto::LeastResponseTimeLbConfig;

/**
 * Load balancer config that used to wrap the legacy least response time config.
 */
class LegacyLeastResponseTimeLbConfig : public Upstream::LoadBalancerConfig {
public:
  LegacyLeastResponseTimeLbConfig(const ClusterProto& cluster);

  OptRef<const LegacyLeastResponseTimeLbProto> lbConfig() const {
    if (lb_config_.has_value()) {
      return lb_config_.value();
    }
    return {};
  };

private:
  absl::optional<LegacyLeastResponseTimeLbProto> lb_config_;
};

/**
 * Load balancer config that used to wrap the least response time config.
 */
class TypedLeastResponseTimeLbConfig : public Upstream::LoadBalancerConfig {
public:
  TypedLeastResponseTimeLbConfig(const LeastResponseTimeLbProto& lb_config);

  const LeastResponseTimeLbProto lb_config_;
};

struct LeastResponseTimeCreator : public Logger::Loggable<Logger::Id::upstream> {
  Upstream::LoadBalancerPtr operator()(Upstream::LoadBalancerParams params,
                                       OptRef<const Upstream::LoadBalancerConfig> lb_config,
                                       const Upstream::ClusterInfo& cluster_info,
                                       const Upstream::PrioritySet& priority_set,
                                       Runtime::Loader& runtime, Random::RandomGenerator& random,
                                       TimeSource& time_source);
};

class Factory : public Common::FactoryBase<LeastResponseTimeLbProto, LeastResponseTimeCreator> {
public:
  Factory() : FactoryBase("envoy.load_balancing_policies.least_response_time") {}

  absl::StatusOr<Upstream::LoadBalancerConfigPtr>
  loadConfig(Server::Configuration::ServerFactoryContext&,
             const Protobuf::Message& config) override {
    ASSERT(dynamic_cast<const LeastResponseTimeLbProto*>(&config) != nullptr);
    const LeastResponseTimeLbProto& typed_config = dynamic_cast<const LeastResponseTimeLbProto&>(config);
    // TODO(wbocode): to merge the legacy and typed config and related constructors into one.
    return Upstream::LoadBalancerConfigPtr{new TypedLeastResponseTimeLbConfig(typed_config)};
  }

  absl::StatusOr<Upstream::LoadBalancerConfigPtr>
  loadLegacy(Server::Configuration::ServerFactoryContext&, const ClusterProto& cluster) override {
    return Upstream::LoadBalancerConfigPtr{new LegacyLeastResponseTimeLbConfig(cluster)};
  }
};

DECLARE_FACTORY(Factory);

} // namespace LeastResponseTime
} // namespace LoadBalancingPolices
} // namespace Extensions
} // namespace Envoy
