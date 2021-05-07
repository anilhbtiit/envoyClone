#pragma once

#include "extensions/filters/listener/original_dst/config.h"
#include "common/common/logger.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace OriginalDst {

/**
 * Implementation of an original destination listener filter.
 */
class OriginalDstFilter : public Network::ListenerFilter, Logger::Loggable<Logger::Id::filter> {
public:
  explicit OriginalDstFilter(const Config& config) : config_(config) {}

  virtual Network::Address::InstanceConstSharedPtr getOriginalDst(Network::Socket& sock);

  // Network::ListenerFilter
  Network::FilterStatus onAccept(Network::ListenerFilterCallbacks& cb) override;

private:
  Config config_;
};

} // namespace OriginalDst
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
