#include "source/common/router/shadow_writer_impl.h"

#include <chrono>
#include <string>

#include "source/common/common/assert.h"
#include "source/common/http/headers.h"

#include "absl/strings/str_join.h"

namespace Envoy {
namespace Router {

namespace {
std::string NewHost(absl::string_view host) {
  ASSERT(!host.empty());
  // Switch authority to add a shadow postfix. This allows upstream logging to
  // make more sense.
  auto parts = StringUtil::splitToken(host, ":");
  ASSERT(!parts.empty() && parts.size() <= 2);
  return parts.size() == 2 ? absl::StrJoin(parts, "-shadow:") : absl::StrCat(host, "-shadow");
}

} // namespace

void ShadowWriterImpl::shadow(const std::string& cluster, Http::RequestMessagePtr&& request,
                              const Http::AsyncClient::RequestOptions& options) {
  // It's possible that the cluster specified in the route configuration no longer exists due
  // to a CDS removal. Check that it still exists before shadowing.
  // TODO(mattklein123): Optimally we would have a stat but for now just fix the crashing issue.
  const auto thread_local_cluster = cm_.getThreadLocalCluster(cluster);
  if (thread_local_cluster == nullptr) {
    ENVOY_LOG(debug, "shadow cluster '{}' does not exist", cluster);
    return;
  }

  request->headers().setHost(NewHost(request->headers().getHostValue()));

  const auto& shadow_options = options.is_shadow ? options : [options] {
    Http::AsyncClient::RequestOptions actual_options(options);
    actual_options.setIsShadow(true);
    return actual_options;
  }();
  // This is basically fire and forget. We don't handle cancelling.
  thread_local_cluster->httpAsyncClient().send(std::move(request), *this, shadow_options);
}

Http::AsyncClient::OngoingRequest*
ShadowWriterImpl::streamingShadow(const std::string& cluster, Http::RequestHeaderMapPtr&& headers,
                                  const Http::AsyncClient::RequestOptions& options) {
  const auto thread_local_cluster = cm_.getThreadLocalCluster(cluster);
  if (thread_local_cluster == nullptr) {
    ENVOY_LOG(debug, "shadow cluster '{}' does not exist", cluster);
    return nullptr;
  }
  headers->setHost(NewHost(headers->getHostValue()));

  const auto& shadow_options = options.is_shadow ? options : [options] {
    Http::AsyncClient::RequestOptions actual_options(options);
    actual_options.setIsShadow(true);
    return actual_options;
  }();
  return thread_local_cluster->httpAsyncClient().startRequest(std::move(headers), *this,
                                                              shadow_options);
}

} // namespace Router
} // namespace Envoy
