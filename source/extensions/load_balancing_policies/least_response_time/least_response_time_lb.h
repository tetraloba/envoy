#pragma once

#include "source/extensions/load_balancing_policies/common/load_balancer_impl.h"

namespace Envoy {
namespace Upstream {

struct LBHostSpec {
  u_int32_t c = 0;
  u_int32_t c_ssthresh = 0;
  u_int64_t mu = 0;
  u_int64_t lambda = 0;
  u_int64_t rtt = 0;
  u_int64_t weight = 0;
};

/**
 * Weighted Least Response Time load balancer.
 *
 * An RR EDF schedule is used. Host weight is scaled by the predicted response time. Predicted
 * response time is calculated based on past response times and the M/M/c model.
 */
class LeastResponseTimeLoadBalancer : public EdfLoadBalancerBase {
public:
  LeastResponseTimeLoadBalancer(
      const PrioritySet& priority_set, const PrioritySet* local_priority_set, ClusterLbStats& stats,
      Runtime::Loader& runtime, Random::RandomGenerator& random,
      const envoy::config::cluster::v3::Cluster::CommonLbConfig& common_config,
      OptRef<const envoy::config::cluster::v3::Cluster::LeastResponseTimeLbConfig> least_response_time_config,
      TimeSource& time_source)
      : EdfLoadBalancerBase(
            priority_set, local_priority_set, stats, runtime, random,
            PROTOBUF_PERCENT_TO_ROUNDED_INTEGER_OR_DEFAULT(common_config, healthy_panic_threshold,
                                                           100, 50),
            LoadBalancerConfigHelper::localityLbConfigFromCommonLbConfig(common_config),
            least_response_time_config.has_value()
                ? LoadBalancerConfigHelper::slowStartConfigFromLegacyProto(
                      least_response_time_config.ref())
                : absl::nullopt,
            time_source),
        choice_count_(
            least_response_time_config.has_value()
                ? PROTOBUF_GET_WRAPPED_OR_DEFAULT(least_response_time_config.ref(), choice_count, 2)
                : 2),
        active_request_bias_runtime_(
            least_response_time_config.has_value() && least_response_time_config->has_active_request_bias()
                ? absl::optional<Runtime::Double>(
                      {least_response_time_config->active_request_bias(), runtime})
                : absl::nullopt) {
    ENVOY_LOG(debug, "tetraloba: LeastResponseTimeLoadBalancer::LeastResponseTimeLoadBalancer() called!");
    initialize();
  }

  LeastResponseTimeLoadBalancer(
      const PrioritySet& priority_set, const PrioritySet* local_priority_set, ClusterLbStats& stats,
      Runtime::Loader& runtime, Random::RandomGenerator& random, uint32_t healthy_panic_threshold,
      const envoy::extensions::load_balancing_policies::least_response_time::v3::LeastResponseTime&
          least_response_time_config,
      TimeSource& time_source)
      : EdfLoadBalancerBase(
            priority_set, local_priority_set, stats, runtime, random, healthy_panic_threshold,
            LoadBalancerConfigHelper::localityLbConfigFromProto(least_response_time_config),
            LoadBalancerConfigHelper::slowStartConfigFromProto(least_response_time_config), time_source),
        choice_count_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(least_response_time_config, choice_count, 2)),
        active_request_bias_runtime_(
            least_response_time_config.has_active_request_bias()
                ? absl::optional<Runtime::Double>(
                      {least_response_time_config.active_request_bias(), runtime})
                : absl::nullopt),
        selection_method_(least_response_time_config.selection_method()) {
    ENVOY_LOG(debug, "tetraloba: LeastResponseTimeLoadBalancer::LeastResponseTimeLoadBalancer() called!");
    initialize();
  }

protected:
  void refresh(uint32_t priority) override {
    active_request_bias_ = active_request_bias_runtime_ != absl::nullopt
                               ? active_request_bias_runtime_.value().value()
                               : 1.0;

    if (active_request_bias_ < 0.0 || std::isnan(active_request_bias_)) {
      ENVOY_LOG_MISC(warn,
                     "upstream: invalid active request bias supplied (runtime key {}), using 1.0",
                     active_request_bias_runtime_->runtimeKey());
      active_request_bias_ = 1.0;
    }

    EdfLoadBalancerBase::refresh(priority);
  }
  // force the use of EDF (with hostWeight()). see common/load_balancer_impl.cc
  bool shouldCreateEdf(const HostVector&) const override {
    return true;
  }

private:
  void refreshHostSource(const HostsSource&) override {}
  double hostWeight(const Host& host) const override;
  HostConstSharedPtr unweightedHostPeek(const HostVector& hosts_to_use,
                                        const HostsSource& source) override;
  HostConstSharedPtr unweightedHostPick(const HostVector& hosts_to_use,
                                        const HostsSource& source) override;

  const uint32_t choice_count_;
  /** 
  * @brief 待ち行列理論 M/M/cモデルに基づいて応答時間を算出
  * @param (lambda) リクエスト到着率[requests / timeslice]
  * @param (mu) サービス率[requests / timeslice]
  * @param (c) 窓口数
  * @return 応答時間[timeslice]
  */
  static double calculateAveRtt(double lambda, double mu, u_int32_t c);
  static u_int32_t calculateCSsthresh(u_int32_t previous_c_ssthresh, double rtt, double predicted_rtt);
  static u_int32_t calculatePredictedC(u_int32_t previous_c, double rtt, double predicted_rtt, u_int32_t c_ssthresh);
  static double calculatePredictedMu(u_int32_t c, double lambda, double average_rtt);
  void updateWeights(const u_int64_t lambda_sum, const std::vector<HostSharedPtr>& hosts) const;

  mutable std::vector<LBHostSpec> host_specs_;

  // The exponent used to calculate host weights can be configured via runtime. We cache it for
  // performance reasons and refresh it in `LeastResponseTimeLoadBalancer::refresh(uint32_t priority)`
  // whenever a `HostSet` is updated.
  double active_request_bias_{};

  const absl::optional<Runtime::Double> active_request_bias_runtime_;
  const envoy::extensions::load_balancing_policies::least_response_time::v3::LeastResponseTime::SelectionMethod
      selection_method_{};
};

} // namespace Upstream
} // namespace Envoy
