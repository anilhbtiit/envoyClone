#include "extensions/filters/http/aws_request_signing/aws_request_signing_filter.h"

#include "common/http/utility.h"

#include "envoy/extensions/filters/http/aws_request_signing/v3/aws_request_signing.pb.h"
#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AwsRequestSigningFilter {

FilterConfigImpl::FilterConfigImpl(Extensions::Common::Aws::SignerPtr&& signer,
                                   const std::string& stats_prefix, Stats::Scope& scope,
                                   const std::string& host_rewrite)
    : signer_(std::move(signer)), stats_(Filter::generateStats(stats_prefix, scope)),
      host_rewrite_(host_rewrite) {}

Filter::Filter(const std::shared_ptr<FilterConfig>& config) : config_(config) {}

Extensions::Common::Aws::Signer& FilterConfigImpl::signer() const { return *signer_; }

FilterStats& FilterConfigImpl::stats() const { return stats_; }

const std::string& FilterConfigImpl::hostRewrite() const { return host_rewrite_; }

FilterStats Filter::generateStats(const std::string& prefix, Stats::Scope& scope) {
  const std::string final_prefix = prefix + "aws_request_signing.";
  return {ALL_AWS_REQUEST_SIGNING_FILTER_STATS(POOL_COUNTER_PREFIX(scope, final_prefix))};
}

Http::FilterHeadersStatus Filter::decodeHeaders(Http::RequestHeaderMap& headers, bool) {
  const auto* config = getConfig();
  const auto& host_rewrite = config->hostRewrite();
  if (!host_rewrite.empty()) {
    headers.setHost(host_rewrite);
  }

  try {
    config->signer().sign(headers);
    config->stats().signing_added_.inc();
  } catch (const EnvoyException& e) {
    ENVOY_LOG(debug, "signing failed: {}", e.what());
    config->stats().signing_failed_.inc();
  }

  return Http::FilterHeadersStatus::Continue;
}

const FilterConfig* Filter::getConfig() const {
  // Cached config pointer.
  if (effective_config_) {
    return effective_config_;
  }

  effective_config_ = Http::Utility::resolveMostSpecificPerFilterConfig<FilterConfig>(
      HttpFilterNames::get().AwsRequestSigning, decoder_callbacks_->route());
  if (effective_config_) {
    return effective_config_;
  }

  effective_config_ = config_.get();
  return effective_config_;
}

} // namespace AwsRequestSigningFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
