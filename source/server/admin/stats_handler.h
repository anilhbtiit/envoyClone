#pragma once

#include <regex>
#include <string>

#include "envoy/buffer/buffer.h"
#include "envoy/http/codes.h"
#include "envoy/http/header_map.h"
#include "envoy/server/admin.h"
#include "envoy/server/instance.h"

#include "source/common/stats/histogram_impl.h"
#include "source/server/admin/handler_ctx.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Server {

class StatsHandler : public HandlerContextBase {

public:
  StatsHandler(Server::Instance& server);

  Http::Code handlerResetCounters(absl::string_view path_and_query,
                                  Http::ResponseHeaderMap& response_headers,
                                  Buffer::Instance& response, AdminStream&);
  Http::Code handlerStatsRecentLookups(absl::string_view path_and_query,
                                       Http::ResponseHeaderMap& response_headers,
                                       Buffer::Instance& response, AdminStream&);
  Http::Code handlerStatsRecentLookupsClear(absl::string_view path_and_query,
                                            Http::ResponseHeaderMap& response_headers,
                                            Buffer::Instance& response, AdminStream&);
  Http::Code handlerStatsRecentLookupsDisable(absl::string_view path_and_query,
                                              Http::ResponseHeaderMap& response_headers,
                                              Buffer::Instance& response, AdminStream&);
  Http::Code handlerStatsRecentLookupsEnable(absl::string_view path_and_query,
                                             Http::ResponseHeaderMap& response_headers,
                                             Buffer::Instance& response, AdminStream&);
  Http::Code handlerStats(absl::string_view path_and_query,
                          Http::ResponseHeaderMap& response_headers, Buffer::Instance& response,
                          AdminStream&);
  Http::Code handlerStatsPrometheus(absl::string_view path_and_query,
                                    Http::ResponseHeaderMap& response_headers,
                                    Buffer::Instance& response, AdminStream&);
  Http::Code handlerContention(absl::string_view path_and_query,
                               Http::ResponseHeaderMap& response_headers,
                               Buffer::Instance& response, AdminStream&);

  Admin::UrlHandler statsHandler();

private:
  class Context;
  class HtmlRender;
  class JsonRender;
  class Render;
  class TextRender;

  enum class Format {
    Html,
    Json,
    Prometheus,
    Text,
  };

  // The order is used to linearize the ordering of stats of all types.
  enum class Type {
    TextReadouts,
    Counters,
    Gauges,
    Histograms,
    All,
  };

  struct Params {
    Http::Code parse(absl::string_view url, Buffer::Instance& response);
    bool shouldShowMetric(const Stats::Metric& metric) const;

    bool used_only_{false};
    bool prometheus_text_readouts_{false};
    bool pretty_{false};
    Format format_{
        Format::Text}; // If no `format=` param we use Text, but the `UI` defaults to HTML.
    Type type_{Type::All};
    Type start_type_{Type::TextReadouts};
    std::string filter_string_;
    absl::optional<std::regex> filter_;
    std::string scope_;
    Http::Utility::QueryParams query_;
  };

  friend class StatsHandlerTest;

  Http::Code stats(const Params& parmams, Stats::Store& store,
                   Http::ResponseHeaderMap& response_headers, Buffer::Instance& response);

  static Http::Code prometheusStats(absl::string_view path_and_query, Buffer::Instance& response,
                                    Stats::Store& stats,
                                    Stats::CustomStatNamespaces& custom_namespaces);

  static std::string statsAsJson(const std::map<std::string, uint64_t>& all_stats,
                                 const std::map<std::string, std::string>& text_readouts,
                                 const std::vector<Stats::HistogramSharedPtr>& all_histograms,
                                 bool pretty_print);

  static absl::string_view typeToString(Type type);
};

} // namespace Server
} // namespace Envoy
