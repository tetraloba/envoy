#include <cmath>
#include <limits>
#include <queue>
#include <stdexcept>
#include <string>

#include "source/extensions/load_balancing_policies/least_response_time/least_response_time_lb.h"

#include "least_response_time_lb.h"
#include "source/common/common/logger.h"

namespace Envoy {
namespace Upstream {

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
    ENVOY_LOG(error, "tetraloba: LeastResponseTimeLoadBalancer::calculateAveRtt(): `c` ({}) must be greater than 0.", c);
    throw std::invalid_argument("Number of cores (c) must be greater than 0.");
  }
  if (mu <= lambda) {
    ENVOY_LOG(warn, "tetraloba: LeastResponseTimeLoadBalancer::calculateAveRtt(): `mu` ({}) should be greater than `lambda` ({}). Returning infinity.", mu, lambda);
    return std::numeric_limits<double>::infinity();
  }

  double mu_per_core = mu / c;
  double sum = 0.0; // p0の分母の総和部分
  for (u_int32_t k = 0; k < c; ++k) {
    sum += std::pow(lambda / mu_per_core, k) / factorial(k);
  }
  double p0 = 1.0 / (sum + (1.0 / factorial(c)) * std::pow(lambda / mu_per_core, c) * (c * mu_per_core / (c * mu_per_core - lambda)));
  double aveRTT = std::pow(lambda / mu_per_core, c) * mu_per_core / (factorial(c - 1) * std::pow(c * mu_per_core - lambda, 2)) * p0 + (1.0 / mu_per_core);
  ENVOY_LOG(debug, "tetraloba: LeastResponseTimeLoadBalancer::calculateAveRtt(): (lambda={}, mu={}, c={}) -> aveRTT={}", lambda, mu, c, aveRTT);
  return aveRTT;
}
double LeastResponseTimeLoadBalancer::calculatePredictedMu(u_int32_t c, double lambda, double average_rtt) {
  if (c <= 0) {
    ENVOY_LOG(error, "tetraloba: LeastResponseTimeLoadBalancer::calculatePredictedMu(): `c` ({}) must be greater than 0.", c);
    throw std::invalid_argument("Number of cores (c) must be greater than 0.");
  }
  if (average_rtt <= 0) {
    ENVOY_LOG(error, "tetraloba: LeastResponseTimeLoadBalancer::calculatePredictedMu(): `average_rtt` ({}) must be positive.", average_rtt);
    throw std::invalid_argument("Average RTT must be positive.");
  }
  auto func = [](u_int32_t c, double mu, double lambda, double average_rtt) {return calculateAveRtt(lambda, mu, c) - average_rtt;};
  /* func(mu) = 0 となる mu を二分探索 */
  double mu_left = lambda + 1 / average_rtt;
  double mu_right = lambda + c / average_rtt;
  while (true) {
    double mu_mid = (mu_left + mu_right) / 2;
    if (mu_right - mu_left < 1) { // 探索範囲が十分に狭まった
      ENVOY_LOG(debug, "tetraloba: LeastResponseTimeLoadBalancer::calculatePredictedMu(): (c={}, lambda={}, average_rtt={}) -> mu={}", c, lambda, average_rtt, mu_mid);
      return mu_mid;
    }
    double res = func(c, mu_mid, lambda, average_rtt);
    if (res < 0) { // mu_mid(予測mu)が過大
      mu_right = mu_mid;
    } else if (res > 0) { // mu_mid(予測mu)が過小
      mu_left = mu_mid;
    } else {
      ENVOY_LOG(debug, "tetraloba: LeastResponseTimeLoadBalancer::calculatePredictedMu(): (c={}, lambda={}, average_rtt={}) -> mu={}", c, lambda, average_rtt, mu_mid);
      return mu_mid;
    }
  }
}
u_int32_t LeastResponseTimeLoadBalancer::calculateCSsthresh(u_int32_t previous_c_ssthresh, double rtt, double predicted_rtt) {
  if (previous_c_ssthresh == 0) {
    ENVOY_LOG(debug, "tetraloba: LeastResponseTimeLoadBalancer::calculateCSsthresh(): (previous_c_ssthresh={}, rtt={}, predicted_rtt={}) -> CSsthresh={}",
      previous_c_ssthresh,
      rtt,
      predicted_rtt,
      std::numeric_limits<u_int32_t>::max()
    );
    return std::numeric_limits<u_int32_t>::max();
  }
  if (rtt > predicted_rtt) {
    ENVOY_LOG(debug, "tetraloba: LeastResponseTimeLoadBalancer::calculateCSsthresh(): (previous_c_ssthresh={}, rtt={}, predicted_rtt={}) -> CSsthresh={}",
      previous_c_ssthresh,
      rtt,
      predicted_rtt,
      (previous_c_ssthresh > 2 ? previous_c_ssthresh / 2 : 1)
    );
    return previous_c_ssthresh > 2 ? previous_c_ssthresh / 2 : 1;
  } else {
    ENVOY_LOG(debug, "tetraloba: LeastResponseTimeLoadBalancer::calculateCSsthresh(): (previous_c_ssthresh={}, rtt={}, predicted_rtt={}) -> CSsthresh={}",
      previous_c_ssthresh,
      rtt,
      predicted_rtt,
      previous_c_ssthresh
    );
    return previous_c_ssthresh;
  }
}
u_int32_t LeastResponseTimeLoadBalancer::calculatePredictedC(u_int32_t previous_c, double rtt, double predicted_rtt, u_int32_t c_ssthresh) {
  ENVOY_LOG(debug, "tetraloba: LeastResponseTimeLoadBalancer::calculatePredictedC(): (previous_c={}, rtt={}, predicted_rtt={}, c_ssthresh={}) -> c={}",
    previous_c,
    rtt,
    predicted_rtt,
    c_ssthresh,
    1
  );
  return 1; // TODO:tetraloba
  if (previous_c == 0) {
    ENVOY_LOG(debug, "tetraloba: LeastResponseTimeLoadBalancer::calculatePredictedC(): (previous_c={}, rtt={}, predicted_rtt={}, c_ssthresh={}) -> c={}",
      previous_c,
      rtt,
      predicted_rtt,
      c_ssthresh,
      1
    );
    return 1;
  }
  if (previous_c < c_ssthresh) {
    ENVOY_LOG(debug, "tetraloba: LeastResponseTimeLoadBalancer::calculatePredictedC(): (previous_c={}, rtt={}, predicted_rtt={}, c_ssthresh={}) -> c={}",
      previous_c,
      rtt,
      predicted_rtt,
      c_ssthresh,
      (previous_c == 0 ? 1 : 2 * previous_c)
    );
    return previous_c == 0 ? 1 : 2 * previous_c;
  } else {
    ENVOY_LOG(debug, "tetraloba: LeastResponseTimeLoadBalancer::calculatePredictedC(): (previous_c={}, rtt={}, predicted_rtt={}, c_ssthresh={}) -> c={}",
      previous_c,
      rtt,
      predicted_rtt,
      c_ssthresh,
      (rtt > predicted_rtt ? (previous_c > 2 ? previous_c / 2 : 1) : previous_c + 1)
    );
    return rtt > predicted_rtt ? (previous_c > 2 ? previous_c / 2 : 1) : previous_c + 1;
  }
}

// std::pair<double, double> LeastResponseTimeLoadBalancer::calculateWeight(u_int32_t c1, u_int64_t mu1, u_int32_t c2, u_int64_t mu2) const {
  
// }
void LeastResponseTimeLoadBalancer::updateWeights(const u_int64_t lambda, const std::vector<HostSharedPtr>& hosts) const {
  ENVOY_LOG(debug, "tetraloba: LeastResponseTimeLoadBalancer::updateWeights() called!");
  if (hosts.empty()) {
    ENVOY_LOG(warn, "tetraloba: LeastResponseTimeLoadBalancer::updateWeights(): No hosts available to update weights.");
    return;
  }
  const u_int32_t lambda_per_weight = lambda > 128 ? lambda / 128 : 1;
  const u_int32_t weight_sum = lambda > 128 ? 128 : lambda; // lambda / lambda_per_weight
  ENVOY_LOG(debug, "tetraloba: LeastResponseTimeLoadBalancer::updateWeights(): lambda_per_weight={}, weight_sum={}", lambda_per_weight, weight_sum);
  // greedy algorithm
  using node = std::pair<double, u_int32_t>;
  std::priority_queue<node, std::vector<node>, std::greater<node>> host_expected_rtts; // (rtt, host_index)
  for (u_int32_t i = 0; i < host_specs_.size(); i++) {
    host_specs_[i].weight = 0;
    const double expected_rtt = calculateAveRtt(lambda_per_weight, host_specs_[i].mu, host_specs_[i].c); // weightが1の場合の平均応答時間
    host_expected_rtts.push(std::make_pair(expected_rtt, i));
  }
  for (u_int32_t w = 0; w < weight_sum; w++) {
    auto [rtt, host_index] = host_expected_rtts.top(); host_expected_rtts.pop();
    ENVOY_LOG(debug, "tetraloba: LeastResponseTimeLoadBalancer::updateWeights(): rtt={}, host_index={}", rtt, host_index);
    host_specs_[host_index].weight++;
    const double expected_rtt = calculateAveRtt((host_specs_[host_index].weight + 1) * lambda_per_weight, host_specs_[host_index].mu, host_specs_[host_index].c); // weightを1増やした場合の応答時間
    host_expected_rtts.push(std::make_pair(expected_rtt, host_index));
  }

  using std::literals::string_literals::operator""s;
  std::string weights_str = "["s;
  for (u_int32_t i = 0; i < host_specs_.size(); i++) {
    weights_str += "{\""s + hosts[i]->address()->asString() + "\","s + std::to_string(host_specs_[i].weight) + "},"s;
  }
  weights_str.back() = ']';
  ENVOY_LOG(info, "tetraloba: LeastResponseTimeLoadBalancer::updateWeights(): weights: {} ", weights_str);
}

double LeastResponseTimeLoadBalancer::hostWeight(const Host& target_host) const {
  ENVOY_LOG(debug, "tetraloba: LeastResponseTimeLoadBalancer::hostWeight() (for {}) called!", target_host.address()->asString());
  // TODO:tetraloba
  // EdfLoadBalancerはhost毎にhostWeight()を呼び出すので、hostWeight()内で全hostを探索するとhost数をnとしてO(n^2)になってしまう。
  // TODO:tetraloba
  // hard codingの解消(config)
  const u_int64_t timeslice_range_nano = 1'000'000'000; // 1 second in nanoseconds // hard coding

  std::vector<HostSharedPtr> hosts; // hostの一次元配列(shared_pointer)
  for (const auto& host_set : priority_set_.hostSetsPerPriority()) {
    for (const auto& host_ptr : host_set->hosts()) {
      hosts.push_back(host_ptr);
    }
  }
  if (hosts.size() != host_specs_.size()) {
    ENVOY_LOG(warn, "tetraloba: LeastResponseTimeLoadBalancer::hostWeight(): clear and resize host_specs_ ({}) to hosts.size() ({}).", host_specs_.size(), hosts.size());
    host_specs_.clear();
    host_specs_.resize(hosts.size());
  }

  u_int64_t latest_timeslice_start_nano = 0; // 厳密には現在時刻またはリクエストの開始時刻であるべき #todo
  for (const auto& host : hosts) {
    u_int64_t timeslice_start_nano = host->stats().timeslice_start_nano_.value();
    if (timeslice_start_nano == 0) {
      return 1; // まだ1つもリクエストを処理し終えていない(=平均応答時間が分からない)hostが有るので、全てのhostのweightは1(=ラウンドロビン)
    }
    latest_timeslice_start_nano = latest_timeslice_start_nano < timeslice_start_nano ? timeslice_start_nano : latest_timeslice_start_nano;
  }

  u_int32_t target_host_index = std::numeric_limits<u_int32_t>::max();
  u_int32_t i = 0; // host index
  bool recalc_weight_required = false;
  u_int64_t lambda_sum = 0;
  for (const auto& host : hosts) {
    u_int64_t lambda     = host_specs_[i].lambda;     // 到着率[requests / timeslice]
    u_int64_t rtt        = host_specs_[i].rtt;        // 平均応答時間[nanoseconds]
    u_int32_t c_ssthresh = host_specs_[i].c_ssthresh; // cのスロースタート閾値
    u_int32_t c          = host_specs_[i].c;          // 窓口数
    u_int64_t mu         = host_specs_[i].mu;         // サービス率[requests / timeslice]
    {
      absl::MutexLock lock(&host->stats().mutex_);
      // timeslice_start_nano_がlatest_timeslice_start_nano - time_slice_size以前のものはcurrent_*から計算して、以降のものはprevious_*から計算する。
      ENVOY_LOG(debug, "tetraloba: LeastResponseTimeLoadBalancer::hostWeight(): host:{}, timeslice_start_nano_:{}, latest_timeslice_start_nano_:{}",
        host->address()->asString(),
        host->stats().timeslice_start_nano_.value(),
        latest_timeslice_start_nano
      );
      if (host->stats().timeslice_start_nano_.value() <= latest_timeslice_start_nano - timeslice_range_nano) {
        ENVOY_LOG(debug, "tetraloba: LeastResponseTimeLoadBalancer::hostWeight(): host:{}, current_rq_total_:{}, current_rq_duration_total_:{}",
          host->address()->asString(),
          host->stats().current_rq_total_.value(),
          host->stats().current_rq_duration_total_.value()
        );
        lambda = host->stats().current_rq_total_.value();
        rtt = host->stats().current_rq_duration_total_.value() / lambda; // timeslice_start_nano_.value() != 0 => lambda > 0
      } else {
        ENVOY_LOG(debug, "tetraloba: LeastResponseTimeLoadBalancer::hostWeight(): host:{}, previous_rq_total_:{}, previous_rq_duration_total_:{}",
          host->address()->asString(),
          host->stats().previous_rq_total_.value(),
          host->stats().previous_rq_duration_total_.value()
        );
        lambda = host->stats().previous_rq_total_.value();
        if (lambda == 0) {
          return 1; // まだtimeslice秒間分の応答時間が収集できていないhostが有るので、全てのhostのweightは1(=ラウンドロビン)
        }
        rtt = host->stats().previous_rq_duration_total_.value() / lambda;
      }
    }
    lambda_sum += lambda;
    // 到着率か平均応答時間が変化していればcとμを再計算。
    ENVOY_LOG(debug, "tetraloba: LeastResponseTimeLoadBalancer::hostWeight(): host:{}, lambda:{}, rtt:{}", host->address()->asString(), lambda, rtt);
    if (lambda != host_specs_[i].lambda || rtt != host_specs_[i].rtt) {
      double timeslice_rtt = static_cast<double>(rtt) / timeslice_range_nano;
      double predicted_rtt = timeslice_rtt;
      if (0 < c && 0 < mu) {
        predicted_rtt = calculateAveRtt(lambda, mu, c);
      }
      c_ssthresh = calculateCSsthresh(c_ssthresh, timeslice_rtt, predicted_rtt);
      c = calculatePredictedC(c, timeslice_rtt, predicted_rtt, c_ssthresh);
      mu = calculatePredictedMu(c, lambda, timeslice_rtt);
      host_specs_[i].lambda = lambda;
      host_specs_[i].rtt = rtt;
    }
    // cかμが変化していれば重みを再計算 (updateWeights())
    if (c != host_specs_[i].c || mu != host_specs_[i].mu) {
      recalc_weight_required = true;
      host_specs_[i].c = c;
      host_specs_[i].mu = mu;
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
  if (host_specs_.size() != hosts.size() || hosts.size() <= target_host_index) {
    ENVOY_LOG(error, "tetraloba: LeastResponseTimeLoadBalancer::hostWeight(): target host index {} not found!", target_host_index);
  }
  ENVOY_LOG(debug, "tetraloba: LeastResponseTimeLoadBalancer::hostWeight(): host_weight of {} is {}", target_host.address()->asString(), host_specs_[target_host_index].weight);
  return 1 < host_specs_[target_host_index].weight ? host_specs_[target_host_index].weight : 1;
}

// Since shouldCreateEdf() forces the use of EDF,
// unweightedHostPeek() and unweightedHostPick are not called.
HostConstSharedPtr LeastResponseTimeLoadBalancer::unweightedHostPeek(const HostVector&,
                                                                const HostsSource&) {
  ENVOY_LOG(error, "tetraloba: LeastResponseTimeLoadBalancer::unweightedHostPeek called!");
  return nullptr;
}
HostConstSharedPtr LeastResponseTimeLoadBalancer::unweightedHostPick(const HostVector&,
                                                                const HostsSource&) {
  ENVOY_LOG(error, "tetraloba: LeastResponseTimeLoadBalancer::unweightedHostPick called!");
  return nullptr;
}

} // namespace Upstream
} // namespace Envoy
