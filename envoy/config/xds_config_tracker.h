#pragma once

#include <string>
#include <vector>

#include "envoy/common/optref.h"
#include "envoy/config/subscription.h"
#include "envoy/config/typed_config.h"
#include "envoy/protobuf/message_validator.h"
#include "envoy/stats/scope.h"

#include "source/common/protobuf/protobuf.h"

#include "google/rpc/status.pb.h"

namespace Envoy {
namespace Config {

/**
 * An interface for hooking into xDS update events to provide the ablility to use some external
 * processor in xDS update. This traker provides the process point when the discovery response
 * is received, when the resoures are successfully processed and applied, and when there is any
 * failure.
 *
 * Instance of this interface get invoked on the main Envoy thread. Thus, it is important
 * for implementations of this interface to not execute any blocking operations on the same
 * thread.
 */
class XdsConfigTracker {
public:
  virtual ~XdsConfigTracker() = default;

  /**
   * Invoked when SotW xDS configuration updates have been successfully parsed, applied on
   * the Envoy instance, and are about to be ACK'ed.
   *
   * @param type_url The type url of xDS message.
   * @param resources A list of decoded resources to add to the current state.
   */
  virtual void onConfigAccepted(const absl::string_view type_url,
                                const std::vector<DecodedResourcePtr>& resources) PURE;

  /**
   * Invoked when Delta xDS configuration updates have been successfully parsed, applied on
   * the Envoy instance, and are about to be ACK'ed.
   *
   * @param type_url The type url of xDS message.
   * @param added_resources A list of decoded resources to add to the current state.
   * @param removed_resources A list of resources to remove from the current state.
   */
  virtual void onConfigAccepted(
      const absl::string_view type_url,
      const Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource>& added_resources,
      const Protobuf::RepeatedPtrField<std::string>& removed_resources) PURE;

  /**
   * Invoked when xds configs are rejected during xDS ingestion.
   *
   * @param message The SotW discovery response message body.
   * @param details The process state and error details.
   */
  virtual void onConfigRejected(const envoy::service::discovery::v3::DiscoveryResponse& message,
                                const absl::string_view error_detail) PURE;

  /**
   * Invoked when xds configs are rejected during xDS ingestion.
   *
   * @param message The Delta discovery response message body.
   * @param details The process state and error details.
   */
  virtual void
  onConfigRejected(const envoy::service::discovery::v3::DeltaDiscoveryResponse& message,
                   const absl::string_view error_detail) PURE;
};

using XdsConfigTrackerPtr = std::unique_ptr<XdsConfigTracker>;
using XdsConfigTrackerOptRef = OptRef<XdsConfigTracker>;

/**
 * A factory abstract class for creating instances of XdsConfigTracker.
 */
class XdsConfigTrackerFactory : public Config::TypedFactory {
public:
  ~XdsConfigTrackerFactory() override = default;

  /**
   * Creates an XdsConfigTracker using the given config.
   */
  virtual XdsConfigTrackerPtr
  createXdsConfigTracker(const ProtobufWkt::Any& config,
                         ProtobufMessage::ValidationVisitor& validation_visitor,
                         Event::Dispatcher& dispatcher, Stats::Scope& stats) PURE;

  std::string category() const override { return "envoy.config.xds_tracker"; }
};

} // namespace Config
} // namespace Envoy
