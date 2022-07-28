#pragma once

#include "envoy/config/typed_config.h"

#include "source/common/common/logger.h"

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Router {

/**
 * Used to decide if pattern template rewrite is needed based on the target route.
 * Subclassing Logger::Loggable so that implementations can log details.
 */
class PathRewritePredicate : Logger::Loggable<Logger::Id::router> {
public:
  PathRewritePredicate() = default;
  virtual ~PathRewritePredicate() = default;

  virtual absl::string_view name() const PURE;

  virtual absl::StatusOr<std::string> rewritePattern(absl::string_view current_pattern,
                                           absl::string_view matched_path) const PURE;

  virtual std::string pattern() const PURE;

};

using PathRewritePredicateSharedPtr = std::shared_ptr<PathRewritePredicate>;

/**
 * Factory for PatternRewriteTemplatePredicate.
 */
class PathRewritePredicateFactory : public Envoy::Config::TypedFactory {
public:
  virtual ~PathRewritePredicateFactory() override = default;

  virtual PathRewritePredicateSharedPtr
  createPathRewritePredicate(const Protobuf::Message& rewrite_config) PURE;

  virtual ProtobufTypes::MessagePtr createEmptyConfigProto() override PURE;

  virtual std::string name() const override PURE;
  virtual std::string category() const override PURE;
};

} // namespace Router
} // namespace Envoy
