#pragma once

#include <cstdint>
#include <list>
#include <string>
#include <vector>

#include "common/common/logger.h"
#include "common/buffer/buffer_impl.h"

#include "envoy/buffer/buffer.h"

#include "extensions/filters/network/memcached_proxy/codec.h"
#include <list>
#include <memory>
#include <string>
#include <vector>

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MemcachedProxy {

class BufferHelper {
public:
  static std::string drainString(Buffer::Instance& buffer, uint32_t size);
};

class RequestImpl : public virtual Request {
public:
  RequestImpl(uint8_t data_type, uint8_t vbucket_id_or_status, uint32_t opaque, uint64_t cas) :
    data_type_(data_type), vbucket_id_or_status_(vbucket_id_or_status), opaque_(opaque), cas_(cas) {}

  virtual void fromBuffer(uint16_t key_length, uint8_t extras_length, uint32_t body_length, Buffer::Instance& data) PURE;

  uint8_t dataType() const override { return data_type_; }
  uint8_t vbucketIdOrStatus() const override { return vbucket_id_or_status_; }
  uint32_t opaque() const override { return opaque_; }
  uint64_t cas() const override { return cas_; }
private:
  const uint8_t data_type_;
  const uint8_t vbucket_id_or_status_;
  const uint32_t opaque_;
  const uint64_t cas_;
};

class GetRequestImpl : public RequestImpl,
                       public GetRequest,
                       Logger::Loggable<Logger::Id::memcached> {
public:
  GetRequestImpl(uint8_t data_type, uint8_t vbucket_id_or_status, uint32_t opaque, uint64_t cas) : RequestImpl(data_type, vbucket_id_or_status, opaque, cas) {}

  // RequestImpl
  void fromBuffer(uint16_t key_length, uint8_t extras_length, uint32_t body_length, Buffer::Instance& data) override;

  // GetRequest
  bool operator==(const GetRequest& rhs) const override;
  bool quiet() const override { return false; }
  const std::string& key() const override { return key_; }

  void key(const std::string& key) { key_ = key; }
private:
  std::string key_;
};

class SetRequestImpl : public RequestImpl,
                       public SetRequest,
                       Logger::Loggable<Logger::Id::memcached> {
public:
  SetRequestImpl(uint8_t data_type, uint8_t vbucket_id_or_status, uint32_t opaque, uint64_t cas) : RequestImpl(data_type, vbucket_id_or_status, opaque, cas) {}

  // RequestImpl
  void fromBuffer(uint16_t key_length, uint8_t extras_length, uint32_t body_length, Buffer::Instance& data) override;

  // SetRequest
  bool operator==(const SetRequest& rhs) const override;
  bool quiet() const override { return false; }
  const std::string& key() const override { return key_; }
  const std::string& body() const override { return body_; }
  uint32_t flags() const override { return flags_; }
  uint32_t expiration() const override { return expiration_; }

  void key(const std::string& key) { key_ = key; }
  void body(const std::string& body) { body_ = body; }
  void flags(uint32_t flags) { flags_ = flags; }
  void expiration(uint32_t expiration) { expiration_ = expiration; }
private:
  std::string key_;
  std::string body_;
  uint32_t flags_;
  uint32_t expiration_;
};

class DecoderImpl : public Decoder, Logger::Loggable<Logger::Id::memcached> {
public:
  DecoderImpl(DecoderCallbacks& callbacks) : callbacks_(callbacks) {}

  // Memcached::Decoder
  void onData(Buffer::Instance& data) override;

private:
  bool decode(Buffer::Instance& data);
  bool decodeRequest(Buffer::Instance& data);
  bool decodeResponse(Buffer::Instance& data);

  DecoderCallbacks& callbacks_;
};

class EncoderImpl : public Encoder, Logger::Loggable<Logger::Id::memcached> {
public:
  EncoderImpl(Buffer::Instance& output) : output_(output) {}

  // Memcached::Encoder
  void encodeGet(const GetRequest& message) override;
  void encodeSet(const SetRequest& message) override;

private:
  void encodeRequestHeader(
    uint16_t key_length,
    uint8_t extras_length,
    uint32_t body_length,
    const Request& request,
    Message::OpCode op);

  Buffer::Instance& output_;
};

}
}
}
}
