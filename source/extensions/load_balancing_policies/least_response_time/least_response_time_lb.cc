#include <cmath>
#include <limits>
#include <queue>
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
u_int64_t LeastResponseTimeLoadBalancer::calculatePredictedMu(u_int32_t c, double lambda, double average_rtt) {
  auto func = [](u_int32_t c, double mu, double lambda, double average_rtt) {return calculateAveRtt(c, mu, lambda) - average_rtt;};
  /* func(mu) = 0 となる mu を二分探索 */
  double mu_left = lambda + 1.0 / average_rtt;
  double mu_right = lambda + c / average_rtt;
  while (true) {
    double mu_mid = (mu_left + mu_right) / 2;
    if (mu_right - mu_left < 1) { // 探索範囲が十分に狭まった
      return static_cast<u_int64_t>(mu_mid);
    }
    double res = func(c, mu_mid, lambda, average_rtt);
    if (res < 0) { // mu_mid(予測mu)が過大
      mu_right = mu_mid;
    } else if (res > 0) { // mu_mid(予測mu)が過小
      mu_left = mu_mid;
    } else {
      return static_cast<u_int64_t>(mu_mid);
    }
  }
}
u_int64_t LeastResponseTimeLoadBalancer::calculateCSsthresh(u_int64_t previous_c_ssthresh, double rho, double target_rho) {
  if (previous_c_ssthresh == 0) {
    return std::numeric_limits<u_int64_t>::max();
  }
  if (rho > target_rho) {
    return previous_c_ssthresh > 2 ? previous_c_ssthresh / 2 : 1;
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
    return rho > target_rho ? (previous_c > 2 ? previous_c / 2 : 1) : previous_c + 1;
  }
}

// std::pair<double, double> LeastResponseTimeLoadBalancer::calculateWeight(u_int32_t c1, u_int64_t mu1, u_int32_t c2, u_int64_t mu2) const {
  
// }
void LeastResponseTimeLoadBalancer::updateWeights(const u_int32_t lambda, const std::vector<HostSharedPtr>& hosts) const {
  ENVOY_LOG(error, "tetraloba: least_request_lb.cc:59: updateWeights() called!");
  if (hosts.empty()) {
    ENVOY_LOG(warn, "tetraloba: least_request_lb.cc:59: No hosts available to update weights.");
    return;
  }
  u_int32_t lambda_per_weight = lambda > 128 ? lambda / 128 : 1;
  u_int32_t weight_sum = lambda > 128 ? 128 : lambda; // lambda / lambda_per_weight
  std::priority_queue<std::pair<u_int64_t, u_int32_t>> host_expected_rtts; // (rtt, host_index)
  for (u_int32_t i = 0; i < host_cs_.size(); i++) {
    host_weights_[i] = 0;
    u_int64_t expected_rtt = calculateAveRtt(lambda_per_weight, host_mus_[i], host_cs_[i]); // weightが1の場合の平均応答時間
    host_expected_rtts.push(std::make_pair(expected_rtt, i));
  }
  for (u_int32_t w = 0; w < weight_sum; w++) {
    auto [rtt, host_index] = host_expected_rtts.top(); host_expected_rtts.pop();
    host_weights_[host_index]++;
    u_int64_t expected_rtt = calculateAveRtt((host_weights_[host_index] + 1) * lambda_per_weight, host_mus_[host_index], host_cs_[host_index]); // weightを1増やした場合の応答時間
    host_expected_rtts.push(std::make_pair(expected_rtt, host_index));
  }
  for (u_int32_t i = 1; i < host_cs_.size(); i++) {
    ENVOY_LOG(debug, "tetraloba: least_request_lb.cc:11: Setting weight {} for host {}", host_weights_[i], hosts[i]->address()->asString());
  }
}

double LeastResponseTimeLoadBalancer::hostWeight(const Host& target_host) const {
  ENVOY_LOG(info, "tetraloba: least_response_time_lb.cc:8: hostWeight() called!");
  // TODO:tetraloba
  // EdfLoadBalancerはhost毎にhostWeight()を呼び出すので、hostWeight()内で全hostを探索するとhost数をnとしてO(n^2)になってしまう。
  // TODO:tetraloba
  // hard codingの解消(config)
  const u_int64_t timeslice_range = 1000000000; // 1 second in nanoseconds // hard coding
  const double target_rho = 0.8; // hard coding

  u_int64_t max_current_time = 0; // 厳密には現在時刻またはリクエストの開始時刻であるべき #todo
  std::vector<HostSharedPtr> hosts; // hostの一次元配列(shared_pointer)
  for (const auto& host_set : priority_set_.hostSetsPerPriority()) {
    for (const auto& host_ptr : host_set->hosts()) {
      hosts.push_back(host_ptr);
    }
  }
  for (const auto& host : hosts) {
    if (host->stats().current_time_.value() == 0) {
      return 1; // まだ1つもリクエストを処理し終えていない(=平均応答時間が分からない)hostが有るので、全てのhostのweightは1(=ラウンドロビン)
    }
    max_current_time = max_current_time < host->stats().current_time_.value() ? host->stats().current_time_.value() : max_current_time;
  }

  u_int32_t target_host_index;
  u_int32_t i = 0; // host index
  bool recalc_weight_required = false;
  u_int64_t lambda_sum = 0;
  for (const auto& host : hosts) {
    u_int64_t lambda = host_lambdas_[i]; // 到着率
    u_int64_t rtt = host_rtts_[i]; // 平均応答時間
    u_int64_t c_ssthresh = host_c_ssthresh_[i]; // cのスロースタート閾値
    u_int32_t c = host_cs_[i]; // 窓口数
    u_int64_t mu = host_mus_[i]; // サービス率
    // current_time_がmax_current_time - time_slice_size以前のものはcurrent_*から計算して、以降のものはprevious_*から計算する。
    if (host->stats().current_time_.value() <= max_current_time - timeslice_range) {
      lambda = host->stats().current_rq_total_.value();
      rtt = host->stats().current_rq_duration_total_.value() / lambda; // current_time_.value() != 0 => lambda > 0
    } else {
      lambda = host->stats().previous_rq_total_.value();
      if (lambda == 0) {
        return 1; // まだtimeslice秒間分の応答時間が収集できていないhostが有るので、全てのhostのweightは1(=ラウンドロビン)
      }
      rtt = host->stats().previous_rq_duration_total_.value() / lambda;
    }
    lambda_sum += lambda;
    // 平均応答時間が変化していればcとμを再計算。
    if (rtt != host_rtts_[i]) {
      double rho = static_cast<double>(lambda) / calculatePredictedMu(c, lambda, rtt);
      c_ssthresh = calculateCSsthresh(c_ssthresh, rho, target_rho);
      c = calculatePredictedC(c, rho, target_rho, c_ssthresh);
      mu = calculatePredictedMu(lambda, rtt, c);
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
    updateWeights(lambda_sum, hosts);
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
