#pragma once

#include "source/common/common/assert.h"
#include "source/common/common/logger.h"

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "absl/types/variant.h"
#include "contrib/envoy/extensions/filters/network/sip_proxy/v3alpha/sip_proxy.pb.h"
#include "contrib/sip_proxy/filters/network/source/operation.h"
#include "contrib/sip_proxy/filters/network/source/sip.h"
#include <vector>

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SipProxy {

#define ALL_PROTOCOL_STATES(FUNCTION)                                                              \
  FUNCTION(StopIteration)                                                                          \
  FUNCTION(WaitForData)                                                                            \
  FUNCTION(TransportBegin)                                                                         \
  FUNCTION(MessageBegin)                                                                           \
  FUNCTION(MessageEnd)                                                                             \
  FUNCTION(TransportEnd)                                                                           \
  FUNCTION(HandleAffinity)                                                                         \
  FUNCTION(Done)

/**
 * ProtocolState represents a set of states used in a state machine to decode
 * Sip requests and responses.
 */
enum class State { ALL_PROTOCOL_STATES(GENERATE_ENUM) };

class SipHeader {
public:
  SipHeader(HeaderType type, absl::string_view value) : type_(type), raw_text_(value) {}
  void parseHeader();

  bool empty() const { return raw_text_.empty(); }

  // "text" as the special param for raw_text_
  bool hasParam(absl::string_view param) const {
    if (param == "text") {
      return true;
    }

    for (auto& p : params_) {
      if (p.first == param) {
        return true;
      }
    }
    return false;
  }

  // "text" as the special param for raw_text_
  absl::string_view param(absl::string_view param) const {
    for (const auto& p : params_) {
      if (p.first == param) {
        return p.second;
      }
    }
    return "";
  }

  absl::string_view text() const { return raw_text_; }

  HeaderType type_;
  absl::string_view raw_text_;
  std::vector<std::pair<absl::string_view, absl::string_view>> params_;
};

struct AffinityEntry {
  AffinityEntry(const std::string& header, const std::string& type, const std::string& key, bool query, bool subscribe)
      : header_(header), type_(type), key_(key), query_(query), subscribe_(subscribe) {}
  AffinityEntry(const std::string& header, const std::string& type, const std::string& key, const std::string& value, bool query, bool subscribe)
      : header_(header), type_(type), key_(key), value_(value), query_(query), subscribe_(subscribe) {}
  std::string header_;
  std::string type_;
  std::string key_;
  std::string value_;
  bool query_;
  bool subscribe_;
};

/**
 * MessageMetadata encapsulates metadata about Sip messages. The various fields are considered
 * optional. Unless otherwise noted, accessor methods throw absl::bad_optional_access if the
 * corresponding value has not been set.
 */
class MessageMetadata : public Logger::Loggable<Logger::Id::filter> {
public:
  MessageMetadata() = default;
  MessageMetadata(std::string&& raw_msg) : raw_msg_(std::move(raw_msg)) {}

  /**
   * The whole SIP message is stored in metadata raw_msg, it is initialized when construct metadata.
   */
  std::string& rawMsg() { return raw_msg_; }

  MsgType msgType() { return msg_type_; }
  void setMsgType(MsgType data) { msg_type_ = data; }

  MethodType methodType() { return method_type_; }
  void setMethodType(MethodType data) { method_type_ = data; }

  MethodType respMethodType() { return resp_method_type_; }
  void setRespMethodType(MethodType data) { resp_method_type_ = data; }

  absl::optional<absl::string_view> ep() { return ep_; }
  void setEP(absl::string_view data) { ep_ = data; }

  std::vector<Operation>& operationList() { return operation_list_; }
  void setOperation(Operation op) { operation_list_.emplace_back(op); }

#if 1
  // TODO Only used for NOKIA customized affinity. should be deleted later.
  absl::optional<std::pair<std::string, std::string>> pCookieIpMap() { return p_cookie_ip_map_; }
  void setPCookieIpMap(std::pair<std::string, std::string>&& data) { p_cookie_ip_map_ = data; }
#endif

  absl::optional<absl::string_view> transactionId() { return transaction_id_; }
  /**
   * @param data full SIP header
   */
  void setTransactionId(absl::string_view data);

  std::string destination() { return destination_; }
  void setDestination(std::string destination) { destination_ = destination; }
  void resetDestination() { destination_.clear(); }

  bool stopLoadBalance() { return stop_load_balance_; };
  void setStopLoadBalance(bool stop_load_balance) { stop_load_balance_ = stop_load_balance; };

  State state() { return state_; };
  void setState(State state) { state_ = state; };

  std::vector<AffinityEntry>& affinity() { return affinity_; }
  void resetAffinityIteration() { affinity_iteration_ = affinity_.begin(); }
  std::vector<AffinityEntry>::iterator& affinityIteration() { return affinity_iteration_; };
  std::vector<AffinityEntry>::iterator& nextAffinityIteration() { return ++affinity_iteration_; };

  void addEPOperation(
      size_t raw_offset, absl::string_view& header,
      const std::vector<envoy::extensions::filters::network::sip_proxy::v3alpha::LocalService>&
          local_services);
  void addOpaqueOperation(size_t raw_offset, absl::string_view& header);
  void deleteInstipOperation(size_t raw_offset, absl::string_view& header);

  void addMsgHeader(HeaderType type, absl::string_view value);

  std::string getDomainFromHeaderParameter(absl::string_view& header, const std::string& parameter);

  void parseHeader(HeaderType type, unsigned short index = 0) {
    return headers_[type][index].parseHeader();
  }

  // TODO how to combine the interface of header and listHeader?
  SipHeader header(HeaderType type, unsigned int index = 0) {
    if (index >= headers_[type].size()) {
      return SipHeader(type, "");
    }
    return headers_[type].at(index);
  }

  std::vector<SipHeader> & listHeader(HeaderType type) {
    return headers_[type];
  }

private:
  MsgType msg_type_;
  MethodType method_type_;
  MethodType resp_method_type_;
  std::vector<std::vector<SipHeader>> headers_{HeaderType::HeaderMaxNum};

  std::vector<Operation> operation_list_;
  absl::optional<absl::string_view> ep_{};

  absl::optional<std::pair<std::string, std::string>> p_cookie_ip_map_{};

  absl::optional<absl::string_view> transaction_id_{};

  std::string destination_ = "";

  std::vector<AffinityEntry> affinity_;
  std::vector<AffinityEntry>::iterator affinity_iteration_;

  std::string raw_msg_{};
  State state_{State::TransportBegin};
  bool stop_load_balance_{};

  bool isDomainMatched(absl::string_view& header,
      const std::vector<envoy::extensions::filters::network::sip_proxy::v3alpha::LocalService>&
          local_services) {
    for (auto& service : local_services) {
      if (service.parameter().empty() || service.domain().empty()) {
        // no default value
        continue;
      }
      if (service.domain() == getDomainFromHeaderParameter(header, service.parameter())) {
        return true;
      }
    }
    return false;
  }
};

using MessageMetadataSharedPtr = std::shared_ptr<MessageMetadata>;

} // namespace SipProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
