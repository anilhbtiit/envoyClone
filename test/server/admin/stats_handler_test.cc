#include <regex>
#include <string>

#include "source/common/stats/custom_stat_namespaces_impl.h"
#include "source/common/stats/thread_local_store.h"
#include "source/server/admin/stats_handler.h"

#include "test/mocks/server/admin_stream.h"
#include "test/mocks/server/instance.h"
#include "test/server/admin/admin_instance.h"
#include "test/test_common/logging.h"
#include "test/test_common/utility.h"

using testing::EndsWith;
using testing::HasSubstr;
using testing::InSequence;
using testing::Not;
using testing::Ref;
using testing::StartsWith;

namespace Envoy {
namespace Server {

class StatsHandlerTest {
public:
  StatsHandlerTest() : alloc_(symbol_table_), pool_(symbol_table_) {
    store_ = std::make_unique<Stats::ThreadLocalStoreImpl>(alloc_);
    store_->addSink(sink_);
  }

  std::shared_ptr<MockInstance> setupMockedInstance() {
    auto instance = std::make_shared<MockInstance>();
    EXPECT_CALL(stats_config_, flushOnAdmin()).WillRepeatedly(testing::Return(false));
    store_->initializeThreading(main_thread_dispatcher_, tls_);
    EXPECT_CALL(*instance, stats()).WillRepeatedly(testing::ReturnRef(*store_));
    EXPECT_CALL(*instance, statsConfig()).WillRepeatedly(testing::ReturnRef(stats_config_));
    EXPECT_CALL(api_, customStatNamespaces())
        .WillRepeatedly(testing::ReturnRef(customNamespaces()));
    EXPECT_CALL(*instance, api()).WillRepeatedly(testing::ReturnRef(api_));
    return instance;
  }

  Http::Code handlerStats(absl::string_view url, Buffer::Instance& response) {
    Http::TestResponseHeaderMapImpl response_headers;
    StatsHandler::Params params;
    Http::Code code = params.parse(url, response);
    if (code != Http::Code::OK) {
      return code;
    }
    std::shared_ptr<MockInstance> instance = setupMockedInstance();
    StatsHandler handler(*instance);
    return handler.stats(params, *store_, response_headers, response);
  }

  Http::Code statsAsJsonHandler(std::string& data, absl::string_view params = "") {
    std::string url = absl::StrCat("/stats?format=json&pretty", params);
    Buffer::OwnedImpl response;
    Http::Code code = handlerStats(url, response);
    data = response.toString();
    return code;
  }

  Stats::StatName makeStat(absl::string_view name) { return pool_.add(name); }
  Stats::CustomStatNamespaces& customNamespaces() { return custom_namespaces_; }

  void shutdownThreading() {
    tls_.shutdownGlobalThreading();
    store_->shutdownThreading();
    tls_.shutdownThread();
  }

  Api::MockApi api_;
  Configuration::MockStatsConfig stats_config_;
  Stats::SymbolTableImpl symbol_table_;
  NiceMock<Event::MockDispatcher> main_thread_dispatcher_;
  NiceMock<ThreadLocal::MockInstance> tls_;
  Stats::AllocatorImpl alloc_;
  Stats::MockSink sink_;
  Stats::ThreadLocalStoreImplPtr store_;
  Stats::StatNamePool pool_;
  Stats::CustomStatNamespacesImpl custom_namespaces_;
};

class AdminStatsTest : public StatsHandlerTest,
                       public testing::TestWithParam<Network::Address::IpVersion> {};

INSTANTIATE_TEST_SUITE_P(IpVersions, AdminStatsTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(AdminStatsTest, HandlerStatsInvalidFormat) {
  Buffer::OwnedImpl data;
  Http::Code code = handlerStats("/stats?format=blergh", data);
  EXPECT_EQ(Http::Code::BadRequest, code);
  EXPECT_EQ("usage: /stats?format=json  or /stats?format=prometheus \n\n", data.toString());
}

TEST_P(AdminStatsTest, HandlerStatsPlainText) {
  InSequence s;
  store_->initializeThreading(main_thread_dispatcher_, tls_);

  const std::string url = "/stats";
  Buffer::OwnedImpl data;

  Stats::Counter& c1 = store_->counterFromString("c1");
  Stats::Counter& c2 = store_->counterFromString("c2");

  c1.add(10);
  c2.add(20);

  Stats::TextReadout& t = store_->textReadoutFromString("t");
  t.set("hello world");

  Stats::Histogram& h1 = store_->histogramFromString("h1", Stats::Histogram::Unit::Unspecified);
  Stats::Histogram& h2 = store_->histogramFromString("h2", Stats::Histogram::Unit::Unspecified);

  EXPECT_CALL(sink_, onHistogramComplete(Ref(h1), 200));
  h1.recordValue(200);

  EXPECT_CALL(sink_, onHistogramComplete(Ref(h2), 100));
  h2.recordValue(100);

  store_->mergeHistograms([]() -> void {});

  Http::Code code = handlerStats(url, data);
  EXPECT_EQ(Http::Code::OK, code);
  EXPECT_EQ("t: \"hello world\"\n"
            "c1: 10\n"
            "c2: 20\n"
            "h1: P0(200.0,200.0) P25(202.5,202.5) P50(205.0,205.0) P75(207.5,207.5) "
            "P90(209.0,209.0) P95(209.5,209.5) P99(209.9,209.9) P99.5(209.95,209.95) "
            "P99.9(209.99,209.99) P100(210.0,210.0)\n"
            "h2: P0(100.0,100.0) P25(102.5,102.5) P50(105.0,105.0) P75(107.5,107.5) "
            "P90(109.0,109.0) P95(109.5,109.5) P99(109.9,109.9) P99.5(109.95,109.95) "
            "P99.9(109.99,109.99) P100(110.0,110.0)\n",
            data.toString());

  shutdownThreading();
}

TEST_P(AdminStatsTest, HandlerStatsPage) {
  InSequence s;
  store_->initializeThreading(main_thread_dispatcher_, tls_);

  std::vector<Stats::Counter*> counters;
  for (uint32_t i = 0; i < 10; ++i) {
    Stats::Counter& c = store_->counterFromString(absl::StrCat("c", i));
    counters.push_back(&c);
    c.add(10 * i);
  }

  auto test_page = [this](absl::string_view direction, absl::string_view start, uint32_t first,
                          uint32_t last, absl::string_view prev, absl::string_view next) {
    Buffer::OwnedImpl data;
    std::string url = "/stats?format=html&pagesize=4&type=All&";
    if (direction == "prev") {
      absl::StrAppend(&url, "before=Counters:", start);
    } else {
      absl::StrAppend(&url, "after=Counters:", start);
    }

    Http::Code code = handlerStats(url, data);
    EXPECT_EQ(Http::Code::OK, code);
    std::string expected = "<pre>\n";
    for (uint32_t i = first; i <= last; ++i) {
      absl::StrAppend(&expected, "c", i, ": ", 10 * i, "\n");
    }
    absl::StrAppend(&expected, "</pre>");
    std::string out = data.toString();
    EXPECT_THAT(out, HasSubstr(expected)) << "url=" << url;

    auto nav = [](absl::string_view direction, absl::string_view start = "") -> std::string {
      std::string out = absl::StrCat("javascript:", direction);
      if (!start.empty()) {
        absl::StrAppend(&out, "(\"Counters:", start, "\")");
      }
      return out;
    };

    if (prev.empty()) {
      EXPECT_THAT(out, Not(HasSubstr(nav("prev")))) << "url=" << url;
    } else {
      EXPECT_THAT(out, HasSubstr(nav("prev", prev))) << "url=" << url;
    }
    if (next.empty()) {
      EXPECT_THAT(out, Not(HasSubstr(nav("next")))) << "url=" << url;
    } else {
      EXPECT_THAT(out, HasSubstr(nav("next", next))) << "url=" << url;
    }
  };

  // Forward walk to end.
  test_page("next", "", 0, 3, "", "c3");
  test_page("next", "c3", 4, 7, "c4", "c7");
  test_page("next", "c7", 8, 9, "c8", "");

  // Reverse walk to beginning.
  test_page("prev", "c8", 4, 7, "c4", "c7");
  test_page("prev", "c4", 0, 3, "", "c3");

  shutdownThreading();
}

TEST_P(AdminStatsTest, HandlerStatsJson) {
  InSequence s;
  store_->initializeThreading(main_thread_dispatcher_, tls_);
  const std::string url = "/stats?format=json";

  Stats::Counter& c1 = store_->counterFromString("c1");
  Stats::Counter& c2 = store_->counterFromString("c2");

  c1.add(10);
  c2.add(20);

  Stats::TextReadout& t = store_->textReadoutFromString("t");
  t.set("hello world");

  Stats::Histogram& h = store_->histogramFromString("h", Stats::Histogram::Unit::Unspecified);

  EXPECT_CALL(sink_, onHistogramComplete(Ref(h), 200));
  h.recordValue(200);

  store_->mergeHistograms([]() -> void {});

  std::string json_out;
  EXPECT_EQ(Http::Code::OK, statsAsJsonHandler(json_out));

  const std::string expected_json_old = R"EOF({
    "stats": [
        {
            "name":"t",
            "value":"hello world"
        },
        {
            "name":"c1",
            "value":10,
        },
        {
            "name":"c2",
            "value":20
        },
        {
            "histograms": {
                "supported_quantiles": [
                    0.0,
                    25.0,
                    50.0,
                    75.0,
                    90.0,
                    95.0,
                    99.0,
                    99.5,
                    99.9,
                    100.0
                ],
                "computed_quantiles": [
                    {
                        "name":"h",
                        "values": [
                            {
                                "cumulative":200,
                                "interval":200
                            },
                            {
                                "cumulative":202.5,
                                "interval":202.5
                            },
                            {
                                "cumulative":205,
                                "interval":205
                            },
                            {
                                "cumulative":207.5,
                                "interval":207.5
                            },
                            {
                                "cumulative":209,
                                "interval":209
                            },
                            {
                                "cumulative":209.5,
                                "interval":209.5
                            },
                            {
                                "cumulative":209.9,
                                "interval":209.9
                            },
                            {
                                "cumulative":209.95,
                                "interval":209.95
                            },
                            {
                                "cumulative":209.99,
                                "interval":209.99
                            },
                            {
                                "cumulative":210,
                                "interval":210
                            }
                        ]
                    },
                ]
            }
        }
    ]
})EOF";

  EXPECT_THAT(expected_json_old, JsonStringEq(json_out));

  shutdownThreading();
}

TEST_P(AdminStatsTest, StatsAsJson) {
  InSequence s;
  store_->initializeThreading(main_thread_dispatcher_, tls_);

  Stats::Histogram& h1 = store_->histogramFromString("h1", Stats::Histogram::Unit::Unspecified);
  Stats::Histogram& h2 = store_->histogramFromString("h2", Stats::Histogram::Unit::Unspecified);

  EXPECT_CALL(sink_, onHistogramComplete(Ref(h1), 200));
  h1.recordValue(200);

  EXPECT_CALL(sink_, onHistogramComplete(Ref(h2), 100));
  h2.recordValue(100);

  store_->mergeHistograms([]() -> void {});

  // Again record a new value in h1 so that it has both interval and cumulative values.
  // h2 should only have cumulative values.
  EXPECT_CALL(sink_, onHistogramComplete(Ref(h1), 100));
  h1.recordValue(100);

  store_->mergeHistograms([]() -> void {});
  std::string actual_json;
  ASSERT_EQ(Http::Code::OK, statsAsJsonHandler(actual_json));

  const std::string expected_json = R"EOF({
    "stats": [
        {
            "histograms": {
                "supported_quantiles": [
                    0.0,
                    25.0,
                    50.0,
                    75.0,
                    90.0,
                    95.0,
                    99.0,
                    99.5,
                    99.9,
                    100.0
                ],
                "computed_quantiles": [
                    {
                        "name": "h1",
                        "values": [
                            {
                                "interval": 100.0,
                                "cumulative": 100.0
                            },
                            {
                                "interval": 102.5,
                                "cumulative": 105.0
                            },
                            {
                                "interval": 105.0,
                                "cumulative": 110.0
                            },
                            {
                                "interval": 107.5,
                                "cumulative": 205.0
                            },
                            {
                                "interval": 109.0,
                                "cumulative": 208.0
                            },
                            {
                                "interval": 109.5,
                                "cumulative": 209.0
                            },
                            {
                                "interval": 109.9,
                                "cumulative": 209.8
                            },
                            {
                                "interval": 109.95,
                                "cumulative": 209.9
                            },
                            {
                                "interval": 109.99,
                                "cumulative": 209.98
                            },
                            {
                                "interval": 110.0,
                                "cumulative": 210.0
                            }
                        ]
                    },
                    {
                        "name": "h2",
                        "values": [
                            {
                                "interval": null,
                                "cumulative": 100.0
                            },
                            {
                                "interval": null,
                                "cumulative": 102.5
                            },
                            {
                                "interval": null,
                                "cumulative": 105.0
                            },
                            {
                                "interval": null,
                                "cumulative": 107.5
                            },
                            {
                                "interval": null,
                                "cumulative": 109.0
                            },
                            {
                                "interval": null,
                                "cumulative": 109.5
                            },
                            {
                                "interval": null,
                                "cumulative": 109.9
                            },
                            {
                                "interval": null,
                                "cumulative": 109.95
                            },
                            {
                                "interval": null,
                                "cumulative": 109.99
                            },
                            {
                                "interval": null,
                                "cumulative": 110.0
                            }
                        ]
                    }
                ]
            }
        }
    ]
})EOF";

  EXPECT_THAT(expected_json, JsonStringEq(actual_json));
  shutdownThreading();
}

TEST_P(AdminStatsTest, UsedOnlyStatsAsJson) {
  InSequence s;
  store_->initializeThreading(main_thread_dispatcher_, tls_);

  Stats::Histogram& h1 = store_->histogramFromString("h1", Stats::Histogram::Unit::Unspecified);
  Stats::Histogram& h2 = store_->histogramFromString("h2", Stats::Histogram::Unit::Unspecified);

  EXPECT_EQ("h1", h1.name());
  EXPECT_EQ("h2", h2.name());

  EXPECT_CALL(sink_, onHistogramComplete(Ref(h1), 200));
  h1.recordValue(200);

  store_->mergeHistograms([]() -> void {});

  // Again record a new value in h1 so that it has both interval and cumulative values.
  // h2 should only have cumulative values.
  EXPECT_CALL(sink_, onHistogramComplete(Ref(h1), 100));
  h1.recordValue(100);

  store_->mergeHistograms([]() -> void {});
  std::string actual_json;
  ASSERT_EQ(Http::Code::OK, statsAsJsonHandler(actual_json, "&usedonly"));

  // Expected JSON should not have h2 values as it is not used.
  const std::string expected_json = R"EOF({
    "stats": [
        {
            "histograms": {
                "supported_quantiles": [
                    0.0,
                    25.0,
                    50.0,
                    75.0,
                    90.0,
                    95.0,
                    99.0,
                    99.5,
                    99.9,
                    100.0
                ],
                "computed_quantiles": [
                    {
                        "name": "h1",
                        "values": [
                            {
                                "interval": 100.0,
                                "cumulative": 100.0
                            },
                            {
                                "interval": 102.5,
                                "cumulative": 105.0
                            },
                            {
                                "interval": 105.0,
                                "cumulative": 110.0
                            },
                            {
                                "interval": 107.5,
                                "cumulative": 205.0
                            },
                            {
                                "interval": 109.0,
                                "cumulative": 208.0
                            },
                            {
                                "interval": 109.5,
                                "cumulative": 209.0
                            },
                            {
                                "interval": 109.9,
                                "cumulative": 209.8
                            },
                            {
                                "interval": 109.95,
                                "cumulative": 209.9
                            },
                            {
                                "interval": 109.99,
                                "cumulative": 209.98
                            },
                            {
                                "interval": 110.0,
                                "cumulative": 210.0
                            }
                        ]
                    }
                ]
            }
        }
    ]
})EOF";

  EXPECT_THAT(expected_json, JsonStringEq(actual_json));
  shutdownThreading();
}

TEST_P(AdminStatsTest, StatsAsJsonFilterString) {
  InSequence s;
  store_->initializeThreading(main_thread_dispatcher_, tls_);

  Stats::Histogram& h1 = store_->histogramFromString("h1", Stats::Histogram::Unit::Unspecified);
  Stats::Histogram& h2 = store_->histogramFromString("h2", Stats::Histogram::Unit::Unspecified);

  EXPECT_CALL(sink_, onHistogramComplete(Ref(h1), 200));
  h1.recordValue(200);

  EXPECT_CALL(sink_, onHistogramComplete(Ref(h2), 100));
  h2.recordValue(100);

  store_->mergeHistograms([]() -> void {});

  // Again record a new value in h1 so that it has both interval and cumulative values.
  // h2 should only have cumulative values.
  EXPECT_CALL(sink_, onHistogramComplete(Ref(h1), 100));
  h1.recordValue(100);

  store_->mergeHistograms([]() -> void {});
  std::string actual_json;
  ASSERT_EQ(Http::Code::OK, statsAsJsonHandler(actual_json, "&filter=[a-z]1"));

  // Because this is a filter case, we don't expect to see any stats except for those containing
  // "h1" in their name.
  const std::string expected_json = R"EOF({
    "stats": [
        {
            "histograms": {
                "supported_quantiles": [
                    0.0,
                    25.0,
                    50.0,
                    75.0,
                    90.0,
                    95.0,
                    99.0,
                    99.5,
                    99.9,
                    100.0
                ],
                "computed_quantiles": [
                    {
                        "name": "h1",
                        "values": [
                            {
                                "interval": 100.0,
                                "cumulative": 100.0
                            },
                            {
                                "interval": 102.5,
                                "cumulative": 105.0
                            },
                            {
                                "interval": 105.0,
                                "cumulative": 110.0
                            },
                            {
                                "interval": 107.5,
                                "cumulative": 205.0
                            },
                            {
                                "interval": 109.0,
                                "cumulative": 208.0
                            },
                            {
                                "interval": 109.5,
                                "cumulative": 209.0
                            },
                            {
                                "interval": 109.9,
                                "cumulative": 209.8
                            },
                            {
                                "interval": 109.95,
                                "cumulative": 209.9
                            },
                            {
                                "interval": 109.99,
                                "cumulative": 209.98
                            },
                            {
                                "interval": 110.0,
                                "cumulative": 210.0
                            }
                        ]
                    }
                ]
            }
        }
    ]
})EOF";

  EXPECT_THAT(expected_json, JsonStringEq(actual_json));
  shutdownThreading();
}

TEST_P(AdminStatsTest, UsedOnlyStatsAsJsonFilterString) {
  InSequence s;
  store_->initializeThreading(main_thread_dispatcher_, tls_);

  Stats::Histogram& h1 = store_->histogramFromString(
      "h1_matches", Stats::Histogram::Unit::Unspecified); // Will match, be used, and print
  Stats::Histogram& h2 = store_->histogramFromString(
      "h2_matches", Stats::Histogram::Unit::Unspecified); // Will match but not be used
  Stats::Histogram& h3 = store_->histogramFromString(
      "h3_not", Stats::Histogram::Unit::Unspecified); // Will be used but not match

  EXPECT_EQ("h1_matches", h1.name());
  EXPECT_EQ("h2_matches", h2.name());
  EXPECT_EQ("h3_not", h3.name());

  EXPECT_CALL(sink_, onHistogramComplete(Ref(h1), 200));
  h1.recordValue(200);
  EXPECT_CALL(sink_, onHistogramComplete(Ref(h3), 200));
  h3.recordValue(200);

  store_->mergeHistograms([]() -> void {});

  // Again record a new value in h1 and h3 so that they have both interval and cumulative values.
  // h2 should only have cumulative values.
  EXPECT_CALL(sink_, onHistogramComplete(Ref(h1), 100));
  h1.recordValue(100);
  EXPECT_CALL(sink_, onHistogramComplete(Ref(h3), 100));
  h3.recordValue(100);

  store_->mergeHistograms([]() -> void {});
  std::string actual_json;
  ASSERT_EQ(Http::Code::OK, statsAsJsonHandler(actual_json, "&usedonly&filter=h[12]"));

  // Expected JSON should not have h2 values as it is not used, and should not have h3 values as
  // they are used but do not match.
  const std::string expected_json = R"EOF({
    "stats": [
        {
            "histograms": {
                "supported_quantiles": [
                    0.0,
                    25.0,
                    50.0,
                    75.0,
                    90.0,
                    95.0,
                    99.0,
                    99.5,
                    99.9,
                    100.0
                ],
                "computed_quantiles": [
                    {
                        "name": "h1_matches",
                        "values": [
                            {
                                "interval": 100.0,
                                "cumulative": 100.0
                            },
                            {
                                "interval": 102.5,
                                "cumulative": 105.0
                            },
                            {
                                "interval": 105.0,
                                "cumulative": 110.0
                            },
                            {
                                "interval": 107.5,
                                "cumulative": 205.0
                            },
                            {
                                "interval": 109.0,
                                "cumulative": 208.0
                            },
                            {
                                "interval": 109.5,
                                "cumulative": 209.0
                            },
                            {
                                "interval": 109.9,
                                "cumulative": 209.8
                            },
                            {
                                "interval": 109.95,
                                "cumulative": 209.9
                            },
                            {
                                "interval": 109.99,
                                "cumulative": 209.98
                            },
                            {
                                "interval": 110.0,
                                "cumulative": 210.0
                            }
                        ]
                    }
                ]
            }
        }
    ]
})EOF";

  EXPECT_THAT(expected_json, JsonStringEq(actual_json));
  shutdownThreading();
}

INSTANTIATE_TEST_SUITE_P(IpVersions, AdminInstanceTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(AdminInstanceTest, StatsInvalidRegex) {
  Http::TestResponseHeaderMapImpl header_map;
  Buffer::OwnedImpl data;
  EXPECT_LOG_CONTAINS(
      "error", "Invalid regex: ",
      EXPECT_EQ(Http::Code::BadRequest, getCallback("/stats?filter=*.test", header_map, data)));

  // Note: depending on the library, the detailed error message might be one of:
  //   "One of *?+{ was not preceded by a valid regular expression."
  //   "regex_error"
  // but we always precede by 'Invalid regex: "'.
  EXPECT_THAT(data.toString(), StartsWith("Invalid regex: \""));
  EXPECT_THAT(data.toString(), EndsWith("\"\n"));
}

TEST_P(AdminInstanceTest, PrometheusStatsInvalidRegex) {
  Http::TestResponseHeaderMapImpl header_map;
  Buffer::OwnedImpl data;
  EXPECT_LOG_CONTAINS(
      "error", ": *.ptest",
      EXPECT_EQ(Http::Code::BadRequest,
                getCallback("/stats?format=prometheus&filter=*.ptest", header_map, data)));

  // Note: depending on the library, the detailed error message might be one of:
  //   "One of *?+{ was not preceded by a valid regular expression."
  //   "regex_error"
  // but we always precede by 'Invalid regex: "'.
  EXPECT_THAT(data.toString(), StartsWith("Invalid regex: \""));
  EXPECT_THAT(data.toString(), EndsWith("\"\n"));
}

TEST_P(AdminInstanceTest, TracingStatsDisabled) {
  const std::string& name = admin_.tracingStats().service_forced_.name();
  for (const Stats::CounterSharedPtr& counter : server_.stats().counters()) {
    EXPECT_NE(counter->name(), name) << "Unexpected tracing stat found in server stats: " << name;
  }
}

TEST_P(AdminInstanceTest, GetRequestJson) {
  Http::TestResponseHeaderMapImpl response_headers;
  std::string body;
  EXPECT_EQ(Http::Code::OK, admin_.request("/stats?format=json", "GET", response_headers, body));
  EXPECT_THAT(body, HasSubstr("{\"stats\":["));
  EXPECT_THAT(std::string(response_headers.getContentTypeValue()), HasSubstr("application/json"));
}

TEST_P(AdminInstanceTest, RecentLookups) {
  Http::TestResponseHeaderMapImpl response_headers;
  std::string body;

  // Recent lookup tracking is disabled by default.
  EXPECT_EQ(Http::Code::OK, admin_.request("/stats/recentlookups", "GET", response_headers, body));
  EXPECT_THAT(body, HasSubstr("Lookup tracking is not enabled"));
  EXPECT_THAT(std::string(response_headers.getContentTypeValue()), HasSubstr("text/plain"));

  // We can't test RecentLookups in admin unit tests as it doesn't work with a
  // fake symbol table. However we cover this solidly in integration tests.
}

class StatsHandlerPrometheusTest : public StatsHandlerTest {
public:
  Http::TestResponseHeaderMapImpl response_headers;
  Buffer::OwnedImpl data;
  MockAdminStream admin_stream;

  void createTestStats() {
    Stats::StatNameTagVector c1Tags{{makeStat("cluster"), makeStat("c1")}};
    Stats::StatNameTagVector c2Tags{{makeStat("cluster"), makeStat("c2")}};

    Stats::Counter& c1 =
        store_->counterFromStatNameWithTags(makeStat("cluster.upstream.cx.total"), c1Tags);
    c1.add(10);
    Stats::Counter& c2 =
        store_->counterFromStatNameWithTags(makeStat("cluster.upstream.cx.total"), c2Tags);
    c2.add(20);

    Stats::Gauge& g1 = store_->gaugeFromStatNameWithTags(
        makeStat("cluster.upstream.cx.active"), c1Tags, Stats::Gauge::ImportMode::Accumulate);
    g1.set(11);
    Stats::Gauge& g2 = store_->gaugeFromStatNameWithTags(
        makeStat("cluster.upstream.cx.active"), c2Tags, Stats::Gauge::ImportMode::Accumulate);
    g2.set(12);

    Stats::TextReadout& t1 =
        store_->textReadoutFromStatNameWithTags(makeStat("control_plane.identifier"), c1Tags);
    t1.set("cp-1");
  }
};

class StatsHandlerPrometheusDefaultTest
    : public StatsHandlerPrometheusTest,
      public testing::TestWithParam<Network::Address::IpVersion> {};

INSTANTIATE_TEST_SUITE_P(IpVersions, StatsHandlerPrometheusDefaultTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(StatsHandlerPrometheusDefaultTest, StatsHandlerPrometheusDefaultTest) {
  std::string url = "/stats?format=prometheus";

  createTestStats();
  std::shared_ptr<MockInstance> instance = setupMockedInstance();
  StatsHandler handler(*instance);

  const std::string expected_response = R"EOF(# TYPE envoy_cluster_upstream_cx_total counter
envoy_cluster_upstream_cx_total{cluster="c1"} 10
envoy_cluster_upstream_cx_total{cluster="c2"} 20

# TYPE envoy_cluster_upstream_cx_active gauge
envoy_cluster_upstream_cx_active{cluster="c1"} 11
envoy_cluster_upstream_cx_active{cluster="c2"} 12

)EOF";

  Http::Code code = handler.handlerStats(url, response_headers, data, admin_stream);
  EXPECT_EQ(Http::Code::OK, code);
  EXPECT_THAT(expected_response, data.toString());

  shutdownThreading();
}

class StatsHandlerPrometheusWithTextReadoutsTest
    : public StatsHandlerPrometheusTest,
      public testing::TestWithParam<std::tuple<Network::Address::IpVersion, std::string>> {};

INSTANTIATE_TEST_SUITE_P(
    IpVersionsAndUrls, StatsHandlerPrometheusWithTextReadoutsTest,
    testing::Combine(testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                     testing::Values("/stats?format=prometheus&text_readouts",
                                     "/stats?format=prometheus&text_readouts=true",
                                     "/stats?format=prometheus&text_readouts=false",
                                     "/stats?format=prometheus&text_readouts=abc")));

TEST_P(StatsHandlerPrometheusWithTextReadoutsTest, StatsHandlerPrometheusWithTextReadoutsTest) {
  std::string url = std::get<1>(GetParam());

  createTestStats();
  std::shared_ptr<MockInstance> instance = setupMockedInstance();
  StatsHandler handler(*instance);

  const std::string expected_response = R"EOF(# TYPE envoy_cluster_upstream_cx_total counter
envoy_cluster_upstream_cx_total{cluster="c1"} 10
envoy_cluster_upstream_cx_total{cluster="c2"} 20

# TYPE envoy_cluster_upstream_cx_active gauge
envoy_cluster_upstream_cx_active{cluster="c1"} 11
envoy_cluster_upstream_cx_active{cluster="c2"} 12

# TYPE envoy_control_plane_identifier gauge
envoy_control_plane_identifier{cluster="c1",text_value="cp-1"} 0

)EOF";

  Http::Code code = handler.handlerStats(url, response_headers, data, admin_stream);
  EXPECT_EQ(Http::Code::OK, code);
  EXPECT_THAT(expected_response, data.toString());

  shutdownThreading();
}

} // namespace Server
} // namespace Envoy
