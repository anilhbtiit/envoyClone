#include "extensions/filters/udp/dns_filter/dns_parser.h"

#include <iomanip>
#include <sstream>

#include "envoy/network/address.h"

#include "common/common/empty_string.h"
#include "common/network/address_impl.h"
#include "common/network/utility.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace DnsFilter {

inline void BaseDnsRecord::serializeName() {

  // Iterate over a name e.g. "www.domain.com" once and produce a buffer containing each name
  // segment prefixed by its length

  static constexpr char SEPARATOR('.');

  size_t last = 0;
  size_t count = name_.find_first_of(SEPARATOR);
  auto iter = name_.begin();

  while (count != std::string::npos) {

    count -= last;
    buffer_.writeBEInt<uint8_t>(count);
    for (size_t i = 0; i < count; i++) {
      buffer_.writeByte(*iter);
      ++iter;
    }

    // periods are not serialized. Skip to the next character
    if (*iter == SEPARATOR) {
      ++iter;
    }

    // Move our last marker to the first position after where we stopped. Search for the next name
    // separator
    last += count;
    ++last;
    count = name_.find_first_of(SEPARATOR, last);
  }

  // Write the remaining segment prepended by its length
  count = name_.size() - last;
  buffer_.writeBEInt<uint8_t>(count);
  for (size_t i = 0; i < count; i++) {
    buffer_.writeByte(*iter);
    ++iter;
  }

  // Terminate the name record with a null byte
  buffer_.writeByte(0x00);
}

// Serialize a DNS Query Record
void DnsQueryRecord::serialize(Buffer::OwnedImpl& output) {
  buffer_.drain(buffer_.length());

  serializeName();
  buffer_.writeBEInt<uint16_t>(type_);
  buffer_.writeBEInt<uint16_t>(class_);

  output.add(buffer_);
}

bool DnsMessageParser::parseDnsObject(const Buffer::InstancePtr& buffer) {

  auto available_bytes = buffer->length();

  memset(&incoming_, 0x00, sizeof(incoming_));

  static constexpr uint64_t field_size = sizeof(uint16_t);
  uint64_t offset = 0;
  uint16_t data;

  DnsQueryParseState state_{DnsQueryParseState::Init};

  while (state_ != DnsQueryParseState::Finish) {

    // Ensure that we have enough data remaining in the buffer to parse the query
    if (available_bytes < field_size) {
      ENVOY_LOG(error,
                "Exhausted available bytes in the buffer. Insufficient data to parse query field.");
      return false;
    }

    // Each aggregate DNS header field is 2 bytes wide.
    data = buffer->peekBEInt<uint16_t>(offset);
    offset += field_size;
    available_bytes -= field_size;

    if (offset > buffer->length()) {
      ENVOY_LOG(error, "Buffer read offset [{}] is beyond buffer length [{}].", offset,
                buffer->length());
      return false;
    }

    switch (state_) {
    case DnsQueryParseState::Init:
      incoming_.id = data;
      state_ = DnsQueryParseState::Flags;
      break;

    case DnsQueryParseState::Flags:
      ::memcpy(static_cast<void*>(&incoming_.flags), &data, sizeof(uint16_t));
      state_ = DnsQueryParseState::Questions;
      break;

    case DnsQueryParseState::Questions:
      incoming_.questions = data;
      state_ = DnsQueryParseState::Answers;
      break;

    case DnsQueryParseState::Answers:
      incoming_.answers = data;
      state_ = DnsQueryParseState::Authority;
      break;

    case DnsQueryParseState::Authority:
      incoming_.authority_rrs = data;
      state_ = DnsQueryParseState::Authority2;
      break;

    case DnsQueryParseState::Authority2:
      incoming_.additional_rrs = data;
      state_ = DnsQueryParseState::Finish;
      break;

    case DnsQueryParseState::Finish:
      break;

    default:
      NOT_REACHED_GCOVR_EXCL_LINE;
    }
  }

  // Verify that we still have available data in the buffer to read answer and query records
  if (offset > buffer->length()) {
    ENVOY_LOG(error, "Buffer read offset[{}] is larget than buffer length [{}].", offset,
              buffer->length());
    return false;
  }

  // Each dns request has a Identification ID. This is used to match the request and replies.
  // We should not see a duplicate ID when handling DNS requests. The ID is removed from the
  // active transactions queue when we build a response for the identified query
  const uint16_t id = static_cast<uint16_t>(incoming_.id);
  if (std::find(active_transactions_.begin(), active_transactions_.end(), id) !=
      active_transactions_.end()) {
    ENVOY_LOG(error, "The filter has already encountered ID {} in a previous request", id);
    return false;
  }

  // Double check that this ID is not already being handled.
  if (queries_.find(id) != queries_.end()) {
    ENVOY_LOG(
        error,
        "There are queries matching ID {} from a previous request for which we have not responded",
        id);
    return false;
  }

  active_transactions_.push_back(id);

  // Almost always, we will have only one query here
  for (auto index = 0; index < incoming_.questions; index++) {
    ENVOY_LOG(trace, "Parsing [{}/{}] questions", index, incoming_.questions);
    auto rec = parseDnsQueryRecord(buffer, &offset);
    if (rec == nullptr) {
      ENVOY_LOG(error, "Couldn't parse query record from buffer");
      return false;
    }
    storeQueryRecord(std::move(rec));
  }

  return true;
}

const std::string DnsMessageParser::parseDnsNameRecord(const Buffer::InstancePtr& buffer,
                                                       uint64_t* available_bytes,
                                                       uint64_t* name_offset) {
  std::stringstream name_ss{};
  unsigned char c;

  do {
    // Read the name segment length or flag;
    c = buffer->peekBEInt<unsigned char>(*name_offset);
    *name_offset += sizeof(unsigned char);
    *available_bytes -= sizeof(unsigned char);

    if (c == 0xc0) {
      // This is a compressed response. Get the offset in the query record where the domain name
      // begins. This is done to reduce the name duplication in DNS answer buffers.
      c = buffer->peekBEInt<unsigned char>(*name_offset);

      // We will restart the loop from this offset and read until we encounter a null byte
      // signifying the end of the name
      *name_offset = static_cast<uint64_t>(c);

      continue;

    } else if (c == 0x00) {
      // We've reached the end of the query.
      ENVOY_LOG(trace, "End of name: [{}] {}", name_ss.str(), *name_offset);
      break;
    }

    const uint64_t segment_length = static_cast<uint64_t>(c);

    // Verify that we have enough data to read the segment length
    if (segment_length > *available_bytes) {
      ENVOY_LOG(error,
                "Insufficient data in buffer for name segment. "
                "available bytes: {}  segment length: {}",
                *available_bytes, segment_length);
      return EMPTY_STRING;
    }

    // Add the name separator if we have already accumulated name data
    if (name_ss.tellp()) {
      name_ss << '.';
    }

    *available_bytes -= segment_length;

    // The value read is a name segment length
    for (uint64_t index = 0; index < segment_length; index++) {
      c = buffer->peekBEInt<unsigned char>(*name_offset);
      *name_offset += sizeof(unsigned char);
      name_ss << c;
    }

  } while (c != 0x00);

  const std::string name = name_ss.str();

  return name;
}

DnsQueryRecordPtr DnsMessageParser::parseDnsQueryRecord(const Buffer::InstancePtr& buffer,
                                                        uint64_t* offset) {
  uint64_t name_offset = *offset;
  uint64_t available_bytes = buffer->length() - name_offset;

  const std::string record_name = parseDnsNameRecord(buffer, &available_bytes, &name_offset);
  if (record_name.empty()) {
    ENVOY_LOG(error, "Unable to parse name record from buffer");
    return nullptr;
  }

  if (available_bytes < 2 * sizeof(uint16_t)) {
    ENVOY_LOG(error, "Insufficient data in buffer to read query record type and class. ");
    return nullptr;
  }

  // Read the record type (A or AAAA)
  uint16_t record_type;
  record_type = buffer->peekBEInt<uint16_t>(name_offset);
  name_offset += sizeof(record_type);

  // Read the record class. This value is almost always 1 for internet address records
  uint16_t record_class;
  record_class = buffer->peekBEInt<uint16_t>(name_offset);
  name_offset += sizeof(record_class);

  // This is shared because we use the query from a list when building the response.
  // Using a shared pointer avoids duplicating this data in the asynchronous resolution path
  auto rec = std::make_shared<DnsQueryRecord>(static_cast<uint16_t>(incoming_.id), record_name,
                                              record_type, record_class);

  // stop reading he buffer here since we aren't parsing additional records
  ENVOY_LOG(trace, "Extracted query record. Name: {} type: {} class: {}", rec->name_, rec->type_,
            rec->class_);

  *offset = name_offset;

  return rec;
}

void DnsMessageParser::storeQueryRecord(DnsQueryRecordPtr rec) {

  const uint16_t id = rec->id_;
  const auto& query_iter = queries_.find(id);

  if (query_iter == queries_.end()) {
    std::list<DnsQueryRecordPtr> query_list{};
    query_list.push_back(std::move(rec));
    queries_.emplace(id, std::move(query_list));
  } else {
    // There should really be only one record here, but allow adding others since the
    // protocol allows it.
    query_iter->second.push_back(std::move(rec));
  }
}

} // namespace DnsFilter
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
