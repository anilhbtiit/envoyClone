#include "source/server/admin/stats_handler.h"

#include <functional>
#include <vector>

#include "envoy/admin/v3/mutex_stats.pb.h"

#include "source/common/common/empty_string.h"
#include "source/common/html/utility.h"
#include "source/common/http/headers.h"
#include "source/common/http/utility.h"
#include "source/server/admin/admin_html_generator.h"
#include "source/server/admin/prometheus_stats.h"
#include "source/server/admin/utils.h"

#include "absl/strings/numbers.h"

namespace Envoy {
namespace Server {

namespace {

constexpr absl::string_view AllLabel = "All";
constexpr absl::string_view CountersLabel = "Counters";
constexpr absl::string_view GaugesLabel = "Gauges";
constexpr absl::string_view HistogramsLabel = "Histograms";
constexpr absl::string_view TextReadoutsLabel = "TextReadouts";
constexpr absl::string_view NextParam = "next";
constexpr absl::string_view PrevParam = "prev";

} // namespace

const uint64_t RecentLookupsCapacity = 100;

StatsHandler::StatsHandler(Server::Instance& server) : HandlerContextBase(server) {}

Http::Code StatsHandler::handlerResetCounters(absl::string_view, Http::ResponseHeaderMap&,
                                              Buffer::Instance& response, AdminStream&) {
  for (const Stats::CounterSharedPtr& counter : server_.stats().counters()) {
    counter->reset();
  }
  server_.stats().symbolTable().clearRecentLookups();
  response.add("OK\n");
  return Http::Code::OK;
}

Http::Code StatsHandler::handlerStatsRecentLookups(absl::string_view, Http::ResponseHeaderMap&,
                                                   Buffer::Instance& response, AdminStream&) {
  Stats::SymbolTable& symbol_table = server_.stats().symbolTable();
  std::string table;
  const uint64_t total =
      symbol_table.getRecentLookups([&table](absl::string_view name, uint64_t count) {
        table += fmt::format("{:8d} {}\n", count, name);
      });
  if (table.empty() && symbol_table.recentLookupCapacity() == 0) {
    table = "Lookup tracking is not enabled. Use /stats/recentlookups/enable to enable.\n";
  } else {
    response.add("   Count Lookup\n");
  }
  response.add(absl::StrCat(table, "\ntotal: ", total, "\n"));
  return Http::Code::OK;
}

Http::Code StatsHandler::handlerStatsRecentLookupsClear(absl::string_view, Http::ResponseHeaderMap&,
                                                        Buffer::Instance& response, AdminStream&) {
  server_.stats().symbolTable().clearRecentLookups();
  response.add("OK\n");
  return Http::Code::OK;
}

Http::Code StatsHandler::handlerStatsRecentLookupsDisable(absl::string_view,
                                                          Http::ResponseHeaderMap&,
                                                          Buffer::Instance& response,
                                                          AdminStream&) {
  server_.stats().symbolTable().setRecentLookupCapacity(0);
  response.add("OK\n");
  return Http::Code::OK;
}

Http::Code StatsHandler::handlerStatsRecentLookupsEnable(absl::string_view,
                                                         Http::ResponseHeaderMap&,
                                                         Buffer::Instance& response, AdminStream&) {
  server_.stats().symbolTable().setRecentLookupCapacity(RecentLookupsCapacity);
  response.add("OK\n");
  return Http::Code::OK;
}

Http::Code StatsHandler::Params::parse(absl::string_view url, Buffer::Instance& response) {
  query_ = Http::Utility::parseAndDecodeQueryString(url);
  used_only_ = query_.find("usedonly") != query_.end();
  pretty_ = query_.find("pretty") != query_.end();
  text_readouts_ = query_.find("text_readouts") != query_.end();
  if (!Utility::filterParam(query_, response, filter_)) {
    return Http::Code::BadRequest;
  }
  if (filter_.has_value()) {
    filter_string_ = query_.find("filter")->second;
  }

  Http::Utility::QueryParams::const_iterator pagesize_iter = query_.find("pagesize");
  if (pagesize_iter != query_.end()) {
    // We don't accept arbitrary page sizes as they might be dangerous.
    uint32_t page_size = 0;
    if (pagesize_iter->second == "unlimited") {
      page_size_ = absl::nullopt;
    } else if (!absl::SimpleAtoi(pagesize_iter->second, &page_size) || page_size > 1000) {
      response.add("pagesize invalid -- must be <= 1000 or unlimited");
      return Http::Code::BadRequest;
    }
    page_size_ = page_size;
  }

  auto type_iter = query_.find("type");
  if (type_iter != query_.end()) {
    if (type_iter->second == GaugesLabel) {
      type_ = Type::Gauges;
    } else if (type_iter->second == CountersLabel) {
      type_ = Type::Counters;
    } else if (type_iter->second == HistogramsLabel) {
      type_ = Type::Histograms;
    } else if (type_iter->second == TextReadoutsLabel) {
      type_ = Type::TextReadouts;
    } else if (type_iter->second == AllLabel) {
      type_ = Type::All;
    }
  }

  const absl::optional<std::string> format_value = Utility::formatParam(query_);
  if (format_value.has_value()) {
    if (format_value.value() == "prometheus") {
      format_ = Format::Prometheus;
    } else if (format_value.value() == "json") {
      format_ = Format::Json;
    } else if (format_value.value() == "text") {
      format_ = Format::Text;
    } else if (format_value.value() == "html") {
      format_ = Format::Html;
    } else {
      response.add("usage: /stats?format=json  or /stats?format=prometheus \n\n");
      return Http::Code::NotFound;
    }
  }

  Http::Utility::QueryParams::const_iterator scope_iter = query_.find("scope");
  if (scope_iter != query_.end()) {
    scope_ = scope_iter->second;
  }

  Http::Utility::QueryParams::const_iterator start_iter = query_.find("start");
  if (start_iter != query_.end()) {
    start_ = start_iter->second;
  }

  Http::Utility::QueryParams::const_iterator direction_iter = query_.find("direction");
  if (direction_iter != query_.end()) {
    if (direction_iter->second == NextParam) {
      direction_ = Stats::PageDirection::Forward;
    } else if (direction_iter->second == PrevParam) {
      direction_ = Stats::PageDirection::Backward;
    } else {
      response.add("invalid direction; must be 'next' or 'prev'");
      return Http::Code::BadRequest;
    }
  }

  return Http::Code::OK;
}

Http::Code StatsHandler::handlerStats(absl::string_view url,
                                      Http::ResponseHeaderMap& response_headers,
                                      Buffer::Instance& response, AdminStream&) {
  Params params;
  Http::Code code = params.parse(url, response);
  if (code != Http::Code::OK) {
    return code;
  }
  if (server_.statsConfig().flushOnAdmin()) {
    server_.flushStats();
  }
  Stats::Store& store = server_.stats();
  if (params.format_ == Format::Prometheus) {
    const std::vector<Stats::TextReadoutSharedPtr>& text_readouts_vec =
        params.text_readouts_ ? store.textReadouts() : std::vector<Stats::TextReadoutSharedPtr>();
    PrometheusStatsFormatter::statsAsPrometheus(
        store.counters(), store.gauges(), store.histograms(), text_readouts_vec, response,
        params.used_only_, params.filter_, server_.api().customStatNamespaces());
    return Http::Code::OK;
  }

  return stats(params, server_.stats(), response_headers, response);
}

Http::Code StatsHandler::handlerStatsJson(absl::string_view url,
                                          Http::ResponseHeaderMap& response_headers,
                                          Buffer::Instance& response, AdminStream&) {
  Params params;
  Http::Code code = params.parse(url, response);
  if (code != Http::Code::OK) {
    return code;
  }
  params.format_ = Format::Json;
  return stats(params, server_.stats(), response_headers, response);
}

class StatsHandler::Render {
public:
  virtual ~Render() = default;
  virtual void counter(Stats::Counter&) PURE;
  virtual void gauge(Stats::Gauge&) PURE;
  virtual void textReadout(Stats::TextReadout&) PURE;
  virtual void histogram(Stats::Histogram&) PURE;
};

class StatsHandler::TextRender : public StatsHandler::Render {
public:
  explicit TextRender(Buffer::Instance& response) : response_(response) {}
  void counter(Stats::Counter& counter) override {
    response_.add(absl::StrCat(counter.name(), ": ", counter.value(), "\n"));
  }
  void gauge(Stats::Gauge& gauge) override {
    response_.add(absl::StrCat(gauge.name(), ": ", gauge.value(), "\n"));
  }
  void textReadout(Stats::TextReadout& text_readout) override {
    response_.add(absl::StrCat(text_readout.name(), ": \"",
                               Html::Utility::sanitize(text_readout.value()), "\"\n"));
  }
  void histogram(Stats::Histogram& histogram) override {
    Stats::ParentHistogram* phist = dynamic_cast<Stats::ParentHistogram*>(&histogram);
    if (phist != nullptr) {
      response_.add(absl::StrCat(phist->name(), ": ", phist->quantileSummary(), "\n"));
    }
  }

private:
  Buffer::Instance& response_;
};

class StatsHandler::JsonRender : public StatsHandler::Render {
public:
  JsonRender(Buffer::Instance& response, const Params& params)
      : params_(params), response_(response) {}
  ~JsonRender() override { render(); }

  void counter(Stats::Counter& counter) override {
    add(counter, ValueUtil::numberValue(counter.value()));
  }
  void gauge(Stats::Gauge& gauge) override { add(gauge, ValueUtil::numberValue(gauge.value())); }
  void textReadout(Stats::TextReadout& text_readout) override {
    add(text_readout, ValueUtil::stringValue(text_readout.value()));
  }
  void histogram(Stats::Histogram& histogram) override {
    Stats::ParentHistogram* phist = dynamic_cast<Stats::ParentHistogram*>(&histogram);
    if (phist != nullptr) {
      if (!found_used_histogram_) {
        auto* histograms_obj_fields = histograms_obj_.mutable_fields();

        // It is not possible for the supported quantiles to differ across histograms, so it is ok
        // to send them once.
        Stats::HistogramStatisticsImpl empty_statistics;
        std::vector<ProtobufWkt::Value> supported_quantile_array;
        for (double quantile : empty_statistics.supportedQuantiles()) {
          supported_quantile_array.push_back(ValueUtil::numberValue(quantile * 100));
        }
        (*histograms_obj_fields)["supported_quantiles"] =
            ValueUtil::listValue(supported_quantile_array);
        found_used_histogram_ = true;
      }

      ProtobufWkt::Struct computed_quantile;
      auto* computed_quantile_fields = computed_quantile.mutable_fields();
      (*computed_quantile_fields)["name"] = ValueUtil::stringValue(histogram.name());

      std::vector<ProtobufWkt::Value> computed_quantile_value_array;
      for (size_t i = 0; i < phist->intervalStatistics().supportedQuantiles().size(); ++i) {
        ProtobufWkt::Struct computed_quantile_value;
        auto* computed_quantile_value_fields = computed_quantile_value.mutable_fields();
        const auto& interval = phist->intervalStatistics().computedQuantiles()[i];
        const auto& cumulative = phist->cumulativeStatistics().computedQuantiles()[i];
        (*computed_quantile_value_fields)["interval"] =
            std::isnan(interval) ? ValueUtil::nullValue() : ValueUtil::numberValue(interval);
        (*computed_quantile_value_fields)["cumulative"] =
            std::isnan(cumulative) ? ValueUtil::nullValue() : ValueUtil::numberValue(cumulative);

        computed_quantile_value_array.push_back(ValueUtil::structValue(computed_quantile_value));
      }
      (*computed_quantile_fields)["values"] = ValueUtil::listValue(computed_quantile_value_array);
      computed_quantile_array_.push_back(ValueUtil::structValue(computed_quantile));
    }
  }

private:
  void render() {
    if (found_used_histogram_) {
      auto* histograms_obj_fields = histograms_obj_.mutable_fields();
      (*histograms_obj_fields)["computed_quantiles"] =
          ValueUtil::listValue(computed_quantile_array_);
      auto* histograms_obj_container_fields = histograms_obj_container_.mutable_fields();
      (*histograms_obj_container_fields)["histograms"] = ValueUtil::structValue(histograms_obj_);
      stats_array_.push_back(ValueUtil::structValue(histograms_obj_container_));
    }

    auto* document_fields = document_.mutable_fields();
    (*document_fields)["stats"] = ValueUtil::listValue(stats_array_);
    response_.add(MessageUtil::getJsonStringFromMessageOrDie(document_, params_.pretty_, true));
  }

  template <class StatType, class Value> void add(StatType& stat, const Value& value) {
    ProtobufWkt::Struct stat_obj;
    auto* stat_obj_fields = stat_obj.mutable_fields();
    (*stat_obj_fields)["name"] = ValueUtil::stringValue(stat.name());
    (*stat_obj_fields)["value"] = value;
    stats_array_.push_back(ValueUtil::structValue(stat_obj));
  }

  const StatsHandler::Params& params_;
  ProtobufWkt::Struct document_;
  std::vector<ProtobufWkt::Value> stats_array_;
  ProtobufWkt::Struct histograms_obj_;
  ProtobufWkt::Struct histograms_obj_container_;
  std::vector<ProtobufWkt::Value> computed_quantile_array_;
  bool found_used_histogram_{false};
  Buffer::Instance& response_;
};

class StatsHandler::Context {
public:
  Context(const Params& params, Render& render) : params_(params), render_(render) {}

  // Quick check to see if we're at the end of the page. If we are, we record the
  // name of the stat we are going to reject.
  bool checkEndOfPage() {
    if (params_.page_size_.has_value()) {
      ASSERT(num_ <= params_.page_size_.value());
      return num_ == params_.page_size_.value() - 1;
    }
    return false;
  }

  /*
  template <class StatType> bool checkMetricAndEndOfPage(const StatType& metric) {
    // Do a relatively fast check to see whether we have hit the end of the page,
    //

    ++num_;
    if (params_.page_size_.has_value()) {
      if (num_ == params_.page_size_.value()) {
        next_start_ = "";//metric->name();
        return true;
      } else if (num_ > params_.page_size_.value()) {
        return true;
      }
    }
    return false;
  }
  */

  template <class StatType>
  void emit(Buffer::Instance& response, Type type, absl::string_view label,
            std::function<void(StatType& stat_type)> render_fn,
            std::function<bool(Stats::PageFn<StatType> stat_fn)> page_fn) {
    if (params_.type_ == Type::All || params_.type_ == type) {
      std::vector<Stats::RefcountPtr<StatType>> stats;
      // auto stat_fn = [this, &written, &response, render_fn, label](StatType& stat) -> bool {
      auto stat_fn = [this, &stats](StatType& stat) -> bool {
        if (params_.shouldShowMetric(stat)) {
          ++num_;
          stats.push_back(&stat);
        }
        return !params_.page_size_.has_value() || num_ < params_.page_size_.value();
      };

      bool more = page_fn(stat_fn);

      if (stats.empty()) {
        if (params_.format_ == Format::Html) {
          response.add(absl::StrCat("<br/><i>No ", label, " found</i><br/>\n"));
        }
        return;
      }

      std::string& start_ref =
          (params_.direction_ == Stats::PageDirection::Forward) ? prev_start_ : next_start_;
      if (start_ref.empty() && !params_.start_.empty()) {
        // If this is not the first item in the set then we have found a start-point
        // for the "Previous" button.
        start_ref = stats[0]->name();
      }

      if (params_.direction_ == Stats::PageDirection::Forward) {
        if (!params_.start_.empty()) {
          prev_start_ = stats[0]->name();
        }
        if (more) {
          next_start_ = stats[stats.size() - 1]->name();
        }
      } else {
        std::reverse(stats.begin(), stats.end());
        if (more) {
          prev_start_ = stats[0]->name();
        }
        if (!params_.start_.empty()) {
          next_start_ = stats[stats.size() - 1]->name();
        }
      }

      bool written = false;
      for (const auto& stat : stats) {
        if (!written && params_.format_ == Format::Html) {
          response.add(absl::StrCat("<h1>", label, "</h1>\n<pre>\n"));
          written = true;
        }
        render_fn(*stat);
      }
      if (written) {
        response.add("</pre>\n");
      }
    }
  }

  Render& render() { return render_; }

  absl::string_view start() const {
    return params_.start_;
    /*
    if (params_.direction_ == Stats::PageDirection::Forward) {
      return next_start_;
    }
    return prev_start_;
    */
  }

  int64_t num_{0};
  const Params& params_;
  Render& render_;
  std::string next_start_;
  std::string prev_start_;
};

Http::Code StatsHandler::stats(const Params& params, Stats::Store& stats,
                               Http::ResponseHeaderMap& response_headers,
                               Buffer::Instance& response) {
  std::unique_ptr<Render> render;

  if (params.format_ == Format::Json) {
    render = std::make_unique<JsonRender>(response, params);
    response_headers.setReferenceContentType(Http::Headers::get().ContentTypeValues.Json);
  } else {
    render = std::make_unique<TextRender>(response);
    if (params.format_ == Format::Html) {
      response_headers.setReferenceContentType(Http::Headers::get().ContentTypeValues.Html);
      AdminHtmlGenerator html(response);
      html.setVisibleSubmit(false);
      html.setSubmitOnChange(true);
      html.renderHead();
      html.renderUrlHandler(statsHandler(), params.query_);
      html.renderInput("start", "stats", Admin::ParamDescriptor::Type::Hidden, params.query_, {});
      html.renderInput("direction", "stats", Admin::ParamDescriptor::Type::Hidden, params.query_,
                       {});
      html.renderTail();
      response.add("<body>\n");
    } else {
      ASSERT(params.format_ == Format::Text);
    }
  }

  Context context(params, *render);

  context.emit<Stats::TextReadout>(
      response, Type::TextReadouts, TextReadoutsLabel,
      [&context](Stats::TextReadout& text_readout) { context.render().textReadout(text_readout); },
      [&stats, &context](Stats::PageFn<Stats::TextReadout> render) -> bool {
        return stats.textReadoutPage(render, context.start(), context.params_.direction_);
      });
  context.emit<Stats::Counter>(
      response, Type::Counters, CountersLabel,
      [&context](Stats::Counter& counter) { context.render().counter(counter); },
      [&stats, &context](Stats::PageFn<Stats::Counter> render) -> bool {
        return stats.counterPage(render, context.start(), context.params_.direction_);
      });
  context.emit<Stats::Gauge>(
      response, Type::Gauges, GaugesLabel,
      [&context](Stats::Gauge& gauge) { context.render().gauge(gauge); },
      [&stats, &context](Stats::PageFn<Stats::Gauge> render) -> bool {
        return stats.gaugePage(render, context.start(), context.params_.direction_);
      });
  context.emit<Stats::Histogram>(
      response, Type::Histograms, HistogramsLabel,
      [&context](Stats::Histogram& histogram) { context.render().histogram(histogram); },
      [&stats, &context](Stats::PageFn<Stats::Histogram> render) -> bool {
        return stats.histogramPage(render, context.start(), context.params_.direction_);
      });

#if 0
  if (params.scope_.has_value()) {
    Stats::StatNameManagedStorage scope_name(params.scope_.value(), stats.symbolTable());
    stats.forEachScope([](size_t) {},
                       [&scope_name, &append_stats_from_scope, &context](
                           const Stats::Scope& scope) -> bool {
                         if (context.checkEndOfPage(scope)) {
                           return false;
                         }
                         if (scope.prefix() == scope_name.statName()) {
                           append_stats_from_scope(scope);
                         }
                         return true;
                       });
  }
#endif

  if (params.format_ == Format::Html) {
    if (!context.prev_start_.empty()) {
      response.add(absl::StrCat("  <a href='javascript:page(\"", context.prev_start_, "\", \"",
                                PrevParam, "\")'>Previous</a>\n"));
    }
    if (!context.next_start_.empty()) {
      response.add(absl::StrCat("  <a href='javascript:page(\"", context.next_start_, "\", \"",
                                NextParam, "\")'>Next</a>\n"));
    }
    response.add("</body>\n");
  }

  // Display plain stats if format query param is not there.
  // statsAsText(counters_and_gauges, text_readouts, histograms, response);
  return Http::Code::OK;
}

Http::Code StatsHandler::handlerStatsScopes(absl::string_view,
                                            Http::ResponseHeaderMap& response_headers,
                                            Buffer::Instance& response, AdminStream&) {
  if (server_.statsConfig().flushOnAdmin()) {
    server_.flushStats();
  }

  std::string preamble = R"(<html>
  <head>
    <script>
      function visitScope(scope) {
        var params = "";
        if (document.getElementById("used").checked) {
          params += "&usedonly";
        }
        var filter = document.getElementById("filter").value;
        if (filter && filter.length > 0) {
          params += "&filter=" + filter;
        }
        location.href = "/stats?scope=" + scope + params;
      }
    </script>
  </head>
  <body>
    <label for="used">Used Only</label><input type="checkbox" id="used"><br>
    <label for="filter">Filter (regex)</label><input type="text" id="filter"><br>
)";

  Stats::StatNameHashSet prefixes;
  Stats::StatFn<const Stats::Scope> add_scope = [&prefixes](const Stats::Scope& scope) {
    prefixes.insert(scope.prefix());
  };
  server_.stats().forEachScope([](size_t) {}, add_scope);
  std::vector<std::string> lines, names;
  names.reserve(prefixes.size());
  lines.reserve(prefixes.size() + 2);
  lines.push_back(preamble);
  for (Stats::StatName prefix : prefixes) {
    names.emplace_back(server_.stats().symbolTable().toString(prefix));
  }
  std::sort(names.begin(), names.end());
  for (const std::string& name : names) {
    lines.push_back(
        absl::StrCat("    <a href='javascript:visitScope(\"", name, "\")'>", name, "</a><br>\n"));
  }
  lines.push_back("  </body>\n</html>\n");
  response.add(absl::StrJoin(lines, ""));
  response_headers.setReferenceContentType(Http::Headers::get().ContentTypeValues.Html);
  return Http::Code::OK;
}

Http::Code StatsHandler::handlerStatsPrometheus(absl::string_view path_and_query,
                                                Http::ResponseHeaderMap&,
                                                Buffer::Instance& response, AdminStream&) {
  Params params;
  Http::Code code = params.parse(path_and_query, response);
  if (code != Http::Code::OK) {
    return code;
  }
  if (server_.statsConfig().flushOnAdmin()) {
    server_.flushStats();
  }
  Stats::Store& stats = server_.stats();
  const std::vector<Stats::TextReadoutSharedPtr>& text_readouts_vec =
      params.text_readouts_ ? stats.textReadouts() : std::vector<Stats::TextReadoutSharedPtr>();
  PrometheusStatsFormatter::statsAsPrometheus(stats.counters(), stats.gauges(), stats.histograms(),
                                              text_readouts_vec, response, params.used_only_,
                                              params.filter_, server_.api().customStatNamespaces());
  return Http::Code::OK;
}

// TODO(ambuc) Export this as a server (?) stat for monitoring.
Http::Code StatsHandler::handlerContention(absl::string_view,
                                           Http::ResponseHeaderMap& response_headers,
                                           Buffer::Instance& response, AdminStream&) {

  if (server_.options().mutexTracingEnabled() && server_.mutexTracer() != nullptr) {
    response_headers.setReferenceContentType(Http::Headers::get().ContentTypeValues.Json);

    envoy::admin::v3::MutexStats mutex_stats;
    mutex_stats.set_num_contentions(server_.mutexTracer()->numContentions());
    mutex_stats.set_current_wait_cycles(server_.mutexTracer()->currentWaitCycles());
    mutex_stats.set_lifetime_wait_cycles(server_.mutexTracer()->lifetimeWaitCycles());
    response.add(MessageUtil::getJsonStringFromMessageOrError(mutex_stats, true, true));
  } else {
    response.add("Mutex contention tracing is not enabled. To enable, run Envoy with flag "
                 "--enable-mutex-tracing.");
  }
  return Http::Code::OK;
}

void StatsHandler::statsAsText(const std::map<std::string, uint64_t>& counters_and_gauges,
                               const std::map<std::string, std::string>& text_readouts,
                               const std::vector<Stats::HistogramSharedPtr>& histograms,
                               Buffer::Instance& response) {
  // Display plain stats if format query param is not there.
  for (const auto& text_readout : text_readouts) {
    response.add(fmt::format("{}: \"{}\"\n", text_readout.first,
                             Html::Utility::sanitize(text_readout.second)));
  }
  for (const auto& stat : counters_and_gauges) {
    response.add(fmt::format("{}: {}\n", stat.first, stat.second));
  }
  for (const auto& histogram : histograms) {
    Stats::ParentHistogram* phist = dynamic_cast<Stats::ParentHistogram*>(histogram.get());
    if (phist != nullptr) {
      response.add(fmt::format("{}: {}\n", phist->name(), phist->quantileSummary()));
    }
  }
}

std::string StatsHandler::statsAsJson(const std::map<std::string, uint64_t>& counters_and_gauges,
                                      const std::map<std::string, std::string>& text_readouts,
                                      const std::vector<Stats::HistogramSharedPtr>& all_histograms,
                                      const bool pretty_print) {

  ProtobufWkt::Struct document;
  std::vector<ProtobufWkt::Value> stats_array;
  for (const auto& text_readout : text_readouts) {
    ProtobufWkt::Struct stat_obj;
    auto* stat_obj_fields = stat_obj.mutable_fields();
    (*stat_obj_fields)["name"] = ValueUtil::stringValue(text_readout.first);
    (*stat_obj_fields)["value"] = ValueUtil::stringValue(text_readout.second);
    stats_array.push_back(ValueUtil::structValue(stat_obj));
  }
  for (const auto& stat : counters_and_gauges) {
    ProtobufWkt::Struct stat_obj;
    auto* stat_obj_fields = stat_obj.mutable_fields();
    (*stat_obj_fields)["name"] = ValueUtil::stringValue(stat.first);
    (*stat_obj_fields)["value"] = ValueUtil::numberValue(stat.second);
    stats_array.push_back(ValueUtil::structValue(stat_obj));
  }

  ProtobufWkt::Struct histograms_obj;
  auto* histograms_obj_fields = histograms_obj.mutable_fields();

  ProtobufWkt::Struct histograms_obj_container;
  auto* histograms_obj_container_fields = histograms_obj_container.mutable_fields();
  std::vector<ProtobufWkt::Value> computed_quantile_array;

  bool found_used_histogram = false;
  for (const Stats::HistogramSharedPtr& histogram : all_histograms) {
    Stats::ParentHistogram* phist = dynamic_cast<Stats::ParentHistogram*>(histogram.get());
    if (phist != nullptr) {
      if (!found_used_histogram) {
        // It is not possible for the supported quantiles to differ across histograms, so it is ok
        // to send them once.
        Stats::HistogramStatisticsImpl empty_statistics;
        std::vector<ProtobufWkt::Value> supported_quantile_array;
        for (double quantile : empty_statistics.supportedQuantiles()) {
          supported_quantile_array.push_back(ValueUtil::numberValue(quantile * 100));
        }
        (*histograms_obj_fields)["supported_quantiles"] =
            ValueUtil::listValue(supported_quantile_array);
        found_used_histogram = true;
      }

      ProtobufWkt::Struct computed_quantile;
      auto* computed_quantile_fields = computed_quantile.mutable_fields();
      (*computed_quantile_fields)["name"] = ValueUtil::stringValue(histogram->name());

      std::vector<ProtobufWkt::Value> computed_quantile_value_array;
      for (size_t i = 0; i < phist->intervalStatistics().supportedQuantiles().size(); ++i) {
        ProtobufWkt::Struct computed_quantile_value;
        auto* computed_quantile_value_fields = computed_quantile_value.mutable_fields();
        const auto& interval = phist->intervalStatistics().computedQuantiles()[i];
        const auto& cumulative = phist->cumulativeStatistics().computedQuantiles()[i];
        (*computed_quantile_value_fields)["interval"] =
            std::isnan(interval) ? ValueUtil::nullValue() : ValueUtil::numberValue(interval);
        (*computed_quantile_value_fields)["cumulative"] =
            std::isnan(cumulative) ? ValueUtil::nullValue() : ValueUtil::numberValue(cumulative);

        computed_quantile_value_array.push_back(ValueUtil::structValue(computed_quantile_value));
      }
      (*computed_quantile_fields)["values"] = ValueUtil::listValue(computed_quantile_value_array);
      computed_quantile_array.push_back(ValueUtil::structValue(computed_quantile));
    }
  }

  if (found_used_histogram) {
    (*histograms_obj_fields)["computed_quantiles"] = ValueUtil::listValue(computed_quantile_array);
    (*histograms_obj_container_fields)["histograms"] = ValueUtil::structValue(histograms_obj);
    stats_array.push_back(ValueUtil::structValue(histograms_obj_container));
  }

  auto* document_fields = document.mutable_fields();
  (*document_fields)["stats"] = ValueUtil::listValue(stats_array);
  return MessageUtil::getJsonStringFromMessageOrDie(document, pretty_print, true);
}

Admin::UrlHandler StatsHandler::statsHandler() {
  return {"/stats",
          "Print server stats.",
          MAKE_ADMIN_HANDLER(handlerStats),
          false,
          false,
          {{Admin::ParamDescriptor::Type::Boolean, "usedonly",
            "Only include stats that have been written by system since restart"},
           {Admin::ParamDescriptor::Type::String, "filter",
            "Regular expression (ecmascript) for filtering stats"},
           {Admin::ParamDescriptor::Type::Enum,
            "format",
            "File format to use.",
            {"html", "text", "json", "prometheus"}},
           {Admin::ParamDescriptor::Type::Enum,
            "pagesize",
            "Number of stats to show per page..",
            {"25", "100", "1000", "unlimited"}},
           {Admin::ParamDescriptor::Type::Enum,
            "type",
            "Stat types to include.",
            {AllLabel, CountersLabel, HistogramsLabel, GaugesLabel, TextReadoutsLabel}}}};
}

} // namespace Server
} // namespace Envoy
