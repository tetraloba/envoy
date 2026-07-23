#include <cmath>
#include <limits>
#include <stdexcept>

#include "source/extensions/load_balancing_policies/least_response_time/least_response_time_lb.h"

#include "least_response_time_lb.h"
#include "source/common/common/logger.h"

namespace Envoy {
namespace Upstream {

// TODO:tetraloba
// implement
double factorial(u_int32_t n) {
  if (n < 0) {
    throw std::invalid_argument("Factorial is not defined for negative numbers.");
  }
  double result = 1.0;
  for (u_int32_t i = 2; i <= n; ++i) {
    result *= i;
  }
  return result;
}
double LeastResponseTimeLoadBalancer::calculateAveRtt(double lambda, double mu, u_int32_t c) {
  if (c <= 0) {
    throw std::invalid_argument("Number of cores (c) must be greater than 0.");
  }
  if (mu <= lambda) {
    throw std::runtime_error("The system is unstable (arrival rate exceeds total service rate).");
  }

  double mu_per_core = mu / c;
  double sum = 0.0; // p0の分母の総和部分
  for (u_int32_t k = 0; k < c; ++k) {
    sum += std::pow(lambda / mu_per_core, k) / factorial(k);
  }
  double p0 = 1.0 / (sum + (1.0 / factorial(c)) * std::pow(lambda / mu_per_core, c) * (c * mu_per_core / (c * mu_per_core - lambda)));
  double aveRTT = 1000.0 * (std::pow(lambda / mu_per_core, c) * mu_per_core / (factorial(c - 1) * std::pow(c * mu_per_core - lambda, 2)) * p0 + (1.0 / mu_per_core));
  return aveRTT;
}
u_int64_t LeastResponseTimeLoadBalancer::calculatePredictedMu(u_int32_t c, u_int64_t lambda, u_int64_t average_rtt) {
  auto func = [](u_int32_t c, double mu, double lambda, double average_rtt) {return calculateAveRtt(c, mu, lambda) - average_rtt;};
  /* func(mu) = 0 となる mu を二分探索 */
  // #todo
  (void)func; (void)c; (void)lambda; (void)average_rtt; // avoid -Werror -Wunused-parameter
  return 0;
}
u_int64_t calculateCSsthresh(u_int64_t previous_c_ssthresh, double rho, double target_rho) {
  if (previous_c_ssthresh == 0) {
    return std::numeric_limits<u_int64_t>::max();
  }
  if (rho > target_rho) {
    return previous_c_ssthresh / 2; // ceiler #todo
  } else {
    return previous_c_ssthresh;
  }
}
u_int32_t LeastResponseTimeLoadBalancer::calculatePredictedC(u_int32_t previous_c, double rho, double target_rho, u_int64_t c_ssthresh) {
  if (previous_c == 0) {
    return 1;
  }
  if (previous_c < c_ssthresh) {
    return previous_c == 0 ? 1 : 2 * previous_c;
  } else {
    return rho > target_rho ? previous_c / 2 : previous_c + 1; // ceiler? #todo
  }
}

// std::pair<double, double> LeastResponseTimeLoadBalancer::calculateWeight(u_int32_t c1, u_int64_t mu1, u_int32_t c2, u_int64_t mu2) const {
  
// }
void LeastResponseTimeLoadBalancer::updateWeights(const std::vector<HostSharedPtr>& hosts) const {
  ENVOY_LOG(error, "tetraloba: least_request_lb.cc:59: updateWeights() called!");
  if (hosts.empty()) {
    ENVOY_LOG(warn, "tetraloba: least_request_lb.cc:59: No hosts available to update weights.");
    return;
  }
  std::vector<double> host_temporarily_weights(hosts.size());
  host_temporarily_weights[0] = 1.0; // host 0 の重み1を基準とする
  u_int32_t max_weight_index = 0; // 重みが最大であるホストのindex
  for (u_int32_t i = 1; i < host_cs_.size(); i++) {
    // 重み設定
    u_int32_t weight = 0; // 1-128
    // #todo
    (void)max_weight_index; // to avoid -Werror -Wunused-variable
    ENVOY_LOG(debug, "tetraloba: least_request_lb.cc:11: Setting weight {} for host {}", weight, hosts[i]->address()->asString());
    host_weights_[i] = weight;
  }
  // // 最適重み決定 ニブタンとかで適当に良い感じに頑張る。
  // for (const auto& host_set : prioritySet().hostSetsPerPriority()) {
  //   for (const auto& host : host_set->hosts()) {
  //     const double weight = hostWeight(*host);
  //     // 重み設定
  //     ENVOY_LOG(debug, "tetraloba: least_request_lb.cc:11: Setting weight {} for host {}", weight, host->address()->asString());
  //     host->setDynamicWeight(weight); // DynamicWeightを使うのか別途LeastResponseTimeLoadBalancer.weights_を用意してしまうか？ #todo
  //   }
  // }
}

double LeastResponseTimeLoadBalancer::hostWeight(const Host& target_host) const {
  ENVOY_LOG(info, "tetraloba: least_response_time_lb.cc:8: hostWeight() called!");
  // TODO:tetraloba
  // EdfLoadBalancerはhost毎にhostWeight()を呼び出すので、hostWeight()内で全hostを探索するとhost数をnとしてO(n^2)になってしまう。
  // TODO:tetraloba
  // hard codingの解消(config)
  const u_int64_t timeslice_range = 1000000000; // 1 second in nanoseconds // hard coding
  const double target_rho = 0.8; // hard coding
  u_int64_t max_current_time = 0;
  std::vector<HostSharedPtr> hosts; // hostの一次元配列(shared_pointer)
  for (const auto& host_set : priority_set_.hostSetsPerPriority()) {
    for (const auto& host_ptr : host_set->hosts()) {
      hosts.push_back(host_ptr);
    }
  }
  for (const auto& host : hosts) {
    max_current_time = max_current_time < host->stats().current_time_.value() ? host->stats().current_time_.value() : max_current_time;
  }

  u_int32_t target_host_index;
  u_int32_t i = 0; // host index
  bool recalc_weight_required = false;
  for (const auto& host : hosts) {
    u_int64_t lambda = host_lambdas_[i]; // 到着率
    u_int64_t rtt = host_rtts_[i]; // 平均応答時間
    u_int64_t c_ssthresh = host_c_ssthresh_[i]; // cのスロースタート閾値
    u_int32_t c = host_cs_[i]; // 窓口数
    u_int64_t mu = host_mus_[i]; // サービス率
    // current_timeがmax_current_time - time_slice_size以前のものはcurrent_から計算して、以降のものはprevious_から計算する。
    if (host->stats().current_time_.value() <= max_current_time - timeslice_range) {
      lambda = host->stats().current_rq_total_.value();
      rtt = host->stats().current_rq_duration_total_.value() / host->stats().current_rq_total_.value();
    } else {
      lambda = host->stats().previous_rq_total_.value();
      rtt = host->stats().previous_rq_duration_total_.value() / host->stats().previous_rq_total_.value();
    }
    // 平均応答時間が変化していればcとμを再計算。
    if (rtt != host_rtts_[i]) {
      double rho = static_cast<double>(lambda) / calculatePredictedMu(c, lambda, rtt);
      c_ssthresh = calculateCSsthresh(c_ssthresh, rho, target_rho);
      c = calculatePredictedC(c, rho, target_rho, c_ssthresh); // #todo
      mu = calculatePredictedMu(lambda, rtt, c); // #todo
      host_rtts_[i] = rtt;
    }
    // cかμが変化していれば重みを再計算 (updateWeights())
    if (c != host_cs_[i] || mu != host_mus_[i]) {
      recalc_weight_required = true;
      host_cs_[i] = c;
      host_mus_[i] = mu;
    }

    if (host->address()->asString() == target_host.address()->asString()) {
      target_host_index = i;
    }
    i++;
  }
  if (recalc_weight_required) {
    updateWeights(hosts);
  }

  // if (!noHostsAreInSlowStart()) {
  //   return applySlowStartFactor(host_weight, host);
  // }

  ENVOY_LOG(info, "tetraloba: least_response_time_lb.cc:8: hostWeight(): host_weight: " + std::to_string(host_weights_[i]));
  return host_weights_[target_host_index];
}

// Since shouldCreateEdf() forces the use of EDF,
// unweightedHostPeek() and unweightedHostPick are not called.
HostConstSharedPtr LeastResponseTimeLoadBalancer::unweightedHostPeek(const HostVector&,
                                                                const HostsSource&) {
  ENVOY_LOG(error, "tetraloba: least_response_time_lb.cc:55: unweightedHostPeek() called!");
  return nullptr;
}
HostConstSharedPtr LeastResponseTimeLoadBalancer::unweightedHostPick(const HostVector&,
                                                                const HostsSource&) {
  ENVOY_LOG(error, "tetraloba: least_response_time_lb.cc:61: unweightedHostPick() called!");
  return nullptr;
}

} // namespace Upstream
} // namespace Envoy
