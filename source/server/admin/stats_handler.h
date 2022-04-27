#pragma once

#include <regex>
#include <string>

#include "envoy/buffer/buffer.h"
#include "envoy/http/codes.h"
#include "envoy/http/header_map.h"
#include "envoy/server/admin.h"
#include "envoy/server/instance.h"

#include "source/server/admin/handler_ctx.h"
#include "source/server/admin/stats_request.h"
#include "source/server/admin/utils.h"

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
  Http::Code handlerPrometheusStats(absl::string_view path_and_query,
                                    Http::ResponseHeaderMap& response_headers,
                                    Buffer::Instance& response, AdminStream&);

  /**
   * Parses and executes a prometheus stats request.
   *
   * @param path_and_query the URL path and query
   * @param response buffer into which to write response
   * @return http response code
   */
  Http::Code prometheusStats(absl::string_view path_and_query, Buffer::Instance& response);

  /**
   * Checks the server_ to see if a flush is needed, and then renders the
   * prometheus stats request.
   *
   * @params params the already-parsed parameters.
   * @param response buffer into which to write response
   */
  void prometheusFlushAndRender(const StatsParams& params, Buffer::Instance& response);

  /**
   * Renders the stats as prometheus. This is broken out as a separately
   * callable API to facilitate the benchmark
   * (test/server/admin/stats_handler_speed_test.cc) which does not have a
   * server object.
   *
   * @params stats the stats store to read
   * @param custom_namespaces namespace mappings used for prometheus
   * @params params the already-parsed parameters.
   * @param response buffer into which to write response
   */
  static void prometheusRender(Stats::Store& stats, const Stats::CustomStatNamespaces& custom_namespaces,
                               const StatsParams& params, Buffer::Instance& response);

  Http::Code handlerContention(absl::string_view path_and_query,
                               Http::ResponseHeaderMap& response_headers,
                               Buffer::Instance& response, AdminStream&);
  Admin::RequestPtr makeRequest(absl::string_view path, AdminStream& admin_stream);
  static Admin::RequestPtr makeRequest(Stats::Store& stats, const StatsParams& params);

private:
  friend class StatsHandlerTest;
};

} // namespace Server
} // namespace Envoy
