#include "source/extensions/load_balancing_policies/least_response_time/least_response_time_lb.h"

#include "source/common/common/logger.h"

namespace Envoy {
namespace Upstream {

// TODO:tetraloba
// implement
u_int64_t LeastResponseTimeLoadBalancer::calculatePredictedC(u_int64_t previous_c, u_int64_t lambda, u_int64_t average_rtt, double target_rho, u_int64_t c_ssthresh) const {
  if (previous_c == 0) {
    return 1;
  }
  // #todo
}
u_int64_t LeastResponseTimeLoadBalancer::calculatePredictedMu(u_int64_t lambda, u_int64_t average_rtt) const {
  // #todo
}

void LeastResponseTimeLoadBalancer::updateWeights() {
  ENVOY_LOG(error, "tetraloba: least_request_lb.cc:6: updateWeights() called!");
  for (const auto& host_set : prioritySet().hostSetsPerPriority()) {
    for (const auto& host : host_set->hosts()) {
      // 平均応答時間を取得してcとμを計算。
      // solve_mu()の探索はとりあえずニブタンにするか。
      // 重み更新タイミングの処理の問題が有るのか。
      // host単位で、平均応答時間が変わっていればcとμを再計算
      // いずれかのhostでcかμが変わっていたら、重みを再計算、か。
      // 毎リクエストごとに確認するってこと？hostWeight()時に？
      // それだとタイミングが少しずつズレて「更新による再計算」が頻発するのよな。
      // 全hostのcurrent_timeの最大値が更新されたら、それと同値のもの以外はcurrentの方を使えばよいか？
    }
  }
  // 最適重み決定 ニブタンとかで適当に良い感じに頑張る。
  for (const auto& host_set : prioritySet().hostSetsPerPriority()) {
    for (const auto& host : host_set->hosts()) {
      const double weight = hostWeight(*host);
      // 重み設定
      ENVOY_LOG(debug, "tetraloba: least_request_lb.cc:11: Setting weight {} for host {}", weight, host->address()->asString());
      host->setDynamicWeight(weight); // DynamicWeightを使うのか別途LeastResponseTimeLoadBalancer.weights_を用意してしまうか？ #todo
    }
  }
}

double LeastResponseTimeLoadBalancer::hostWeight(const Host& host) const {
  ENVOY_LOG(info, "tetraloba: least_response_time_lb.cc:8: hostWeight() called!");
  // TODO:tetraloba
  // EdfLoadBalancerはhost毎にhostWeight()を呼び出すので、hostWeight()内で全hostを探索するとhost数をnとしてO(n^2)になってしまう。
  // TODO:tetraloba
  // hard codingの解消(config)
  const u_int64_t timeslice_range = 1000000000; // 1 second in nanoseconds // hard coding
  const double target_rho = 0.8; // hard coding
  u_int64_t max_current_time = 0;
  // TODO:tetraloba
  // ポインタ(shared?unique?)を使ってhostsという一次元配列にする。
  for (const auto& host_set : prioritySet().hostSetsPerPriority()) {
    for (const auto& host : host_set->hosts()) {
      max_current_time = std::max(max_current_time, host.stats().current_time_);
    }
  }

  // TODO:tetraloba
  // LeastResponseTimeLoadBalancerのメンバ変数にする。
  std::vector<u_int64_t> host_lambdas_;
  std::vector<u_int64_t> host_rtts_;
  std::vector<u_int32_t> host_cs_;
  std::vector<u_int64_t> host_mus_;
  std::vector<u_int64_t> host_weights_;
  u_int32_t host_index;
  u_int32_t i = 0; // host index
  bool recalc_weight_required = false;
  for (const auto& host_set : prioritySet().hostSetsPerPriority()) {
    for (const auto& host : host_set->hosts()) {
      u_int64_t lambda = host_lambdas_[i]; // 到着率
      u_int64_t rtt = host_rtts_[i]; // 平均応答時間
      u_int32_t c = host_cs_[i]; // 窓口数
      u_int64_t mu = host_mus_[i]; // サービス率
      // current_timeがmax_current_time - time_slice_size以前のものはcurrent_から計算して、以降のものはprevious_から計算する。
      if (host.stats().current_time_ <= max_current_time - timeslice_range) {
        lambda = host.stats().current_rq_total_;
        rtt = host.stats().current_rq_duration_total_ / host.stats().current_rq_total_;
      } else {
        lambda = host.stats().previous_rq_total_;
        rtt = host.stats().previous_rq_duration_total_ / host.stats().previous_rq_total_;
      }
      // 平均応答時間が変化していればcとμを再計算。
      if (rtt != host_rtts[i]) {
        c = calculatePredictedC(lambda, rtt, c); // #todo
        mu = calculatePredictedMu(lambda, rtt, c); // #todo
        host_rtts_[i] = rtt;
      }
      // cかμが変化していれば重みを再計算 (updateWeights())
      if (c != host_cs_[i] || mu != host_mus_[i]) {
        recalc_weight_required = true;
        host_cs_[i] = c;
        host_mus_[i] = mu;
      }

      if (host.address()->asString() == host.address()->asString()) { // #todo name conflict
        host_index = i;
      }
      i++;
    }
  }
  if (recalc_weight_required) {
    updateWeights();
  }

  // if (!noHostsAreInSlowStart()) {
  //   return applySlowStartFactor(host_weight, host);
  // }

  ENVOY_LOG(info, "tetraloba: least_response_time_lb.cc:8: hostWeight(): host_weight: " + std::to_string(host_weights_[i]));
  return host_weights_[i];
}

// TODO:tetraloba
// 以下は使わないはず？

HostConstSharedPtr LeastResponseTimeLoadBalancer::unweightedHostPeek(const HostVector&,
                                                                const HostsSource&) {
  ENVOY_LOG(info, "tetraloba: least_response_time_lb.cc:55: unweightedHostPeek() called!");
  // LeastResponseTimeLoadBalancer can not do deterministic preconnecting, because
  // any other thread might select the least-requested-host between preconnect and
  // host-pick, and change the rq_active checks.
  return nullptr;
}

HostConstSharedPtr LeastResponseTimeLoadBalancer::unweightedHostPick(const HostVector& hosts_to_use,
                                                                const HostsSource&) {
  ENVOY_LOG(info, "tetraloba: least_response_time_lb.cc:61: unweightedHostPick() called!");
  HostSharedPtr candidate_host = nullptr;

  switch (selection_method_) {
  case envoy::extensions::load_balancing_policies::least_response_time::v3::LeastResponseTime::FULL_SCAN:
    candidate_host = unweightedHostPickFullScan(hosts_to_use);
    break;
  case envoy::extensions::load_balancing_policies::least_response_time::v3::LeastResponseTime::N_CHOICES:
    candidate_host = unweightedHostPickNChoices(hosts_to_use);
    break;
  default:
    IS_ENVOY_BUG("unknown selection method specified for least response time load balancer");
  }

  return candidate_host;
}

HostSharedPtr LeastResponseTimeLoadBalancer::unweightedHostPickFullScan(const HostVector& hosts_to_use) {
  ENVOY_LOG(info, "tetraloba: least_response_time_lb.cc:80: unweightedHostPickFullScan() called!");
  HostSharedPtr candidate_host = nullptr;

  size_t num_hosts_known_tied_for_least = 0;

  const size_t num_hosts = hosts_to_use.size();

  for (size_t i = 0; i < num_hosts; ++i) {
    const HostSharedPtr& sampled_host = hosts_to_use[i];

    if (candidate_host == nullptr) {
      // Make a first choice to start the comparisons.
      num_hosts_known_tied_for_least = 1;
      candidate_host = sampled_host;
      continue;
    }

    const auto candidate_rq_duration = candidate_host->stats().rq_duration_.value();
    const auto sampled_rq_duration = sampled_host->stats().rq_duration_.value();

    if (sampled_rq_duration < candidate_rq_duration) {
      // Reset the count of known tied hosts.
      num_hosts_known_tied_for_least = 1;
      candidate_host = sampled_host;
    } else if (sampled_rq_duration == candidate_rq_duration) {
      ++num_hosts_known_tied_for_least;

      // Use reservoir sampling to select 1 unique sample from the total number of hosts N
      // that will tie for least response times after processing the full hosts array.
      //
      // Upon each new tie encountered, replace candidate_host with sampled_host
      // with probability (1 / num_hosts_known_tied_for_least percent).
      // The end result is that each tied host has an equal 1 / N chance of being the
      // candidate_host returned by this function.
      const size_t random_tied_host_index = random_.random() % num_hosts_known_tied_for_least;
      if (random_tied_host_index == 0) {
        candidate_host = sampled_host;
      }
    }
  }

  return candidate_host;
}

HostSharedPtr LeastResponseTimeLoadBalancer::unweightedHostPickNChoices(const HostVector& hosts_to_use) {
  ENVOY_LOG(info, "tetraloba: least_response_time_lb.cc:125: unweightedHostPickNChoices() called!");
  HostSharedPtr candidate_host = nullptr;

  for (uint32_t choice_idx = 0; choice_idx < choice_count_; ++choice_idx) {
    const int rand_idx = random_.random() % hosts_to_use.size();
    const HostSharedPtr& sampled_host = hosts_to_use[rand_idx];

    if (candidate_host == nullptr) {
      // Make a first choice to start the comparisons.
      candidate_host = sampled_host;
      continue;
    }

    const auto candidate_rq_duration = candidate_host->stats().rq_duration_.value();
    const auto sampled_rq_duration = sampled_host->stats().rq_duration_.value();

    if (sampled_rq_duration < candidate_rq_duration) {
      candidate_host = sampled_host;
    }
  }

  return candidate_host;
}

} // namespace Upstream
} // namespace Envoy
