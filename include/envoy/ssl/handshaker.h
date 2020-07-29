#pragma once

#include "envoy/api/api.h"
#include "envoy/common/pure.h"
#include "envoy/config/typed_config.h"
#include "envoy/network/transport_socket.h"
#include "envoy/protobuf/message_validator.h"
#include "envoy/ssl/socket_state.h"

#include "openssl/ssl.h"

namespace Envoy {
namespace Ssl {

class HandshakerCallbacks {
public:
  virtual ~HandshakerCallbacks() = default;

  /**
   * Called when a handshake is successfully performed.
   */
  virtual void onSuccessCb(SSL* ssl) PURE;
  /**
   * Called when a handshake fails.
   */
  virtual void onFailureCb() PURE;
};

/*
 * Interface for a Handshaker which is responsible for owning the
 * `bssl::UniquePtr<SSL>` and performing handshakes.
 */
class Handshaker {
public:
  virtual ~Handshaker() = default;

  /**
   * Do the handshake.
   *
   * NB: |state| is a mutable reference.
   */
  virtual Network::PostIoAction doHandshake(SocketState& state) PURE;

  /**
   * Set internal pointers to  Network::TransportSocketCallbacks and
   * Ssl::HandshakerCallbacks.
   * Depending on impl, these callbacks can be invoked to access connection
   * state, raise connection events, etc.
   */
  virtual void setCallbacks(Network::TransportSocketCallbacks& callbacks,
                            Ssl::HandshakerCallbacks& handshaker_callbacks) PURE;

  /*
   * Access the held SSL object as a ptr. Callsites should handle nullptr
   * gracefully.
   */
  virtual SSL* ssl() PURE;
};

using HandshakerPtr = std::shared_ptr<Handshaker>;

class HandshakerFactoryContext {
public:
  virtual ~HandshakerFactoryContext() = default;

  /**
   * @return reference to the Api object
   */
  virtual Api::Api& api() PURE;

  /**
   * The list of supported protocols exposed via ALPN, from ContextConfig.
   */
  virtual absl::string_view alpnProtocols() const PURE;
};

using HandshakerFactoryCb = std::function<HandshakerPtr(bssl::UniquePtr<SSL>)>;

class HandshakerFactory : public Config::TypedFactory {
public:
  /**
   * @returns a callback (of type HandshakerFactoryCb). Accepts the |config| and
   * |validation_visitor| for early config validation. This virtual base doesn't
   * perform MessageUtil::downcastAndValidate, but an implementation should.
   */
  virtual HandshakerFactoryCb
  createHandshakerCb(const Protobuf::Message& message,
                     HandshakerFactoryContext& handshaker_factory_context,
                     ProtobufMessage::ValidationVisitor& validation_visitor) PURE;

  std::string category() const override { return "envoy.tls_handshakers"; }

  /**
   * Implementations should return true if the tls context accompanying this
   * handshaker expects certificates.
   */
  virtual bool requireCertificates() const PURE;
};

} // namespace Ssl
} // namespace Envoy
