#include "source/server/admin/stats_params.h"

namespace Envoy {
namespace Server {

Http::Code StatsParams::parse(absl::string_view url, Buffer::Instance& response) {
  query_ = Http::Utility::parseAndDecodeQueryString(url);
  used_only_ = query_.find("usedonly") != query_.end();
  pretty_ = query_.find("pretty") != query_.end();
  prometheus_text_readouts_ = query_.find("text_readouts") != query_.end();
  safe_regex_ = query_.find("safe") != query_.end();

  auto filter_iter = query_.find("filter");
  if (filter_iter != query_.end()) {
    filter_string_ =  filter_iter->second;
    if (safe_regex_) {
      safe_filter_ = std::make_shared<re2::RE2>(filter_string_);
      if (!safe_filter_->ok()) {
        response.add("invalid safe regex");
        return Http::Code::BadRequest;
      }
    } else if (!Utility::filterParam(query_, response, filter_)) {
      return Http::Code::BadRequest;
    }
  }

  absl::Status status = Utility::histogramBucketsParam(query_, histogram_buckets_mode_);
  if (!status.ok()) {
    response.add(status.message());
    return Http::Code::BadRequest;
  }

  const absl::optional<std::string> format_value = Utility::formatParam(query_);
  if (format_value.has_value()) {
    if (format_value.value() == "prometheus") {
      format_ = StatsFormat::Prometheus;
    } else if (format_value.value() == "json") {
      format_ = StatsFormat::Json;
    } else if (format_value.value() == "text") {
      format_ = StatsFormat::Text;
    } else {
      response.add("usage: /stats?format=json  or /stats?format=prometheus \n\n");
      return Http::Code::BadRequest;
    }
  }

  return Http::Code::OK;
}

} // namespace Server
} // namespace Envoy
