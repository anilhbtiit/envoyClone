// Note: this should be run with --compilation_mode=opt, and would benefit from a
// quiescent system with disabled cstate power management.

#include <chrono>
#include <string>
#include <vector>

#include "common/common/fmt.h"
#include "common/stats/isolated_store_impl.h"

#include "extensions/filters/network/common/redis/client_impl.h"
#include "extensions/filters/network/common/redis/supported_commands.h"
#include "extensions/filters/network/redis_proxy/command_splitter_impl.h"
#include "extensions/filters/network/redis_proxy/router_impl.h"

#include "test/test_common/simulated_time_system.h"

#include "benchmark/benchmark.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {

class CommandSplitSpeedTest {
public:
  Common::Redis::RespValuePtr makeBulkStringArray(uint64_t batch_size, uint64_t key_size,
                                                  uint64_t value_size) {
    Common::Redis::RespValuePtr request{new Common::Redis::RespValue()};
    std::vector<Common::Redis::RespValue> values(batch_size * 2 + 1);
    values[0].type(Common::Redis::RespType::BulkString);
    values[0].asString() = "mset";
    for (uint64_t i = 1; i < batch_size * 2 + 1; i += 2) {
      values[i].type(Common::Redis::RespType::BulkString);
      values[i].asString() = std::string(key_size, 'k');
      values[i + 1].type(Common::Redis::RespType::BulkString);
      values[i + 1].asString() = std::string(value_size, 'v');
    }

    request->type(Common::Redis::RespType::Array);
    request->asArray().swap(values);

    return request;
  }

  Common::Redis::RespValueSharedPtr
  makeSharedBulkStringArray(uint64_t batch_size, uint64_t key_size, uint64_t value_size) {
    Common::Redis::RespValueSharedPtr request{new Common::Redis::RespValue()};
    std::vector<Common::Redis::RespValue> values(batch_size * 2 + 1);
    values[0].type(Common::Redis::RespType::BulkString);
    values[0].asString() = "mset";
    for (uint64_t i = 1; i < batch_size * 2 + 1; i += 2) {
      values[i].type(Common::Redis::RespType::BulkString);
      values[i].asString() = std::string(key_size, 'k');
      values[i + 1].type(Common::Redis::RespType::BulkString);
      values[i + 1].asString() = std::string(value_size, 'v');
    }

    request->type(Common::Redis::RespType::Array);
    request->asArray().swap(values);

    return request;
  }

  void move(Common::Redis::RespValueSharedPtr request) {
    for (uint64_t i = 1; i < request->asArray().size(); i += 2) {
      //      auto single_set = std::make_shared<Common::Redis::RespValue>();
      //      single_set->type(Common::Redis::RespType::CompositeArray);
      //      single_set->asCompositeArray().initialize(request,
      //      Common::Redis::Utility::SetRequest::instance(), i, i + 2);

      auto single_set = std::make_shared<Common::Redis::RespValue>(
          request, Common::Redis::Utility::SetRequest::instance(), i, i + 2);
    }
  }

  void copy(Common::Redis::RespValueSharedPtr request) {
    std::vector<Common::Redis::RespValue> values(3);
    values[0].type(Common::Redis::RespType::BulkString);
    values[0].asString() = "set";
    values[1].type(Common::Redis::RespType::BulkString);
    values[2].type(Common::Redis::RespType::BulkString);
    Common::Redis::RespValue single_mset;
    single_mset.type(Common::Redis::RespType::Array);
    single_mset.asArray().swap(values);

    for (uint64_t i = 1; i < request->asArray().size(); i += 2) {
      single_mset.asArray()[1].asString() = request->asArray()[i].asString();
      single_mset.asArray()[2].asString() = request->asArray()[i + 1].asString();
    }
  }
};
} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy

static void BM_Split_Move(benchmark::State& state) {
  Envoy::Extensions::NetworkFilters::RedisProxy::CommandSplitSpeedTest context;
  Envoy::Extensions::NetworkFilters::Common::Redis::RespValueSharedPtr request =
      context.makeSharedBulkStringArray(state.range(0), 36, state.range(1));
  for (auto _ : state) {
    context.move(request);
  }
}
BENCHMARK(BM_Split_Move)->Ranges({{1, 100}, {512, 8 << 14}});

static void BM_Split_Copy(benchmark::State& state) {
  Envoy::Extensions::NetworkFilters::RedisProxy::CommandSplitSpeedTest context;
  Envoy::Extensions::NetworkFilters::Common::Redis::RespValueSharedPtr request =
      context.makeSharedBulkStringArray(state.range(0), 36, state.range(1));
  for (auto _ : state) {
    context.copy(request);
  }
}
BENCHMARK(BM_Split_Copy)->Ranges({{1, 100}, {512, 8 << 14}});

// Boilerplate main(), which discovers benchmarks in the same file and runs them.
int main(int argc, char** argv) {
  benchmark::Initialize(&argc, argv);

  if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
    return 1;
  }
  benchmark::RunSpecifiedBenchmarks();
}
