#include "extensions/filters/network/mysql_proxy/mysql_codec.h"

#include <arpa/inet.h>

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {

void BufferHelper::addUint8(Buffer::Instance& buffer, uint8_t val) {
  buffer.add(&val, sizeof(uint8_t));
}

void BufferHelper::addUint16(Buffer::Instance& buffer, uint16_t val) {
  buffer.add(&val, sizeof(uint16_t));
}

void BufferHelper::addUint32(Buffer::Instance& buffer, uint32_t val) {
  buffer.add(&val, sizeof(uint32_t));
}

void BufferHelper::addString(Buffer::Instance& buffer, const std::string& str) { buffer.add(str); }

std::string BufferHelper::toString(Buffer::Instance& buffer) {
  char* data = static_cast<char*>(buffer.linearize(buffer.length()));
  std::string s = std::string(data, buffer.length());
  return s;
}

std::string BufferHelper::encodeHdr(const std::string& cmd_str, int seq) {
  MySQLHeader mysqlhdr;
  mysqlhdr.fields.length = cmd_str.length();
  mysqlhdr.fields.seq = seq;

  Buffer::OwnedImpl buffer;
  addUint32(buffer, mysqlhdr.bits);

  std::string e_string = toString(buffer);
  e_string.append(cmd_str);
  return e_string;
}

bool BufferHelper::endOfBuffer(Buffer::Instance& buffer, uint64_t& offset) {
  return buffer.length() == offset;
}

int BufferHelper::peekUint8(Buffer::Instance& buffer, uint64_t& offset, uint8_t& val) {
  if (buffer.length() < (offset + sizeof(uint8_t))) {
    return MYSQL_FAILURE;
  }
  buffer.copyOut(offset, sizeof(uint8_t), &val);
  offset += sizeof(uint8_t);
  return MYSQL_SUCCESS;
}

int BufferHelper::peekUint16(Buffer::Instance& buffer, uint64_t& offset, uint16_t& val) {
  if (buffer.length() < (offset + sizeof(uint16_t))) {
    return MYSQL_FAILURE;
  }
  buffer.copyOut(offset, sizeof(uint16_t), &val);
  offset += sizeof(uint16_t);
  return MYSQL_SUCCESS;
}

int BufferHelper::peekUint32(Buffer::Instance& buffer, uint64_t& offset, uint32_t& val) {
  if (buffer.length() < (offset + sizeof(uint32_t))) {
    return MYSQL_FAILURE;
  }
  buffer.copyOut(offset, sizeof(uint32_t), &val);
  offset += sizeof(uint32_t);
  return MYSQL_SUCCESS;
}

int BufferHelper::peekUint64(Buffer::Instance& buffer, uint64_t& offset, uint64_t& val) {
  if (buffer.length() < (offset + sizeof(uint64_t))) {
    return MYSQL_FAILURE;
  }
  buffer.copyOut(offset, sizeof(uint64_t), &val);
  offset += sizeof(uint64_t);
  return MYSQL_SUCCESS;
}

int BufferHelper::peekBySize(Buffer::Instance& buffer, uint64_t& offset, int len, int& val) {
  if (buffer.length() < (offset + len)) {
    return MYSQL_FAILURE;
  }
  buffer.copyOut(offset, len, &val);
  offset += len;
  return MYSQL_SUCCESS;
}

// Implementation of MySQL lenenc encoder based on
// https://dev.mysql.com/doc/internals/en/integer.html#packet-Protocol::LengthEncodedInteger
int BufferHelper::peekLengthEncodedInteger(Buffer::Instance& buffer, uint64_t& offset, int& val) {
  uint8_t byte_val = 0;
  if (peekUint8(buffer, offset, byte_val) == MYSQL_FAILURE) {
    return MYSQL_FAILURE;
  }
  if (val < LENENCODINT_1BYTE) {
    val = byte_val;
    return MYSQL_SUCCESS;
  }

  int size = 0;
  if (byte_val == LENENCODINT_2BYTES) {
    size = sizeof(uint16_t);
  } else if (byte_val == LENENCODINT_3BYTES) {
    size = sizeof(uint8_t) * 3;
  } else if (byte_val == LENENCODINT_8BYTES) {
    size = sizeof(uint64_t);
  } else {
    return MYSQL_FAILURE;
  }

  if (peekBySize(buffer, offset, size, val) == MYSQL_FAILURE) {
    return MYSQL_FAILURE;
  }
  return MYSQL_SUCCESS;
}

int BufferHelper::peekBytes(Buffer::Instance& buffer, uint64_t& offset, int skip_bytes) {
  if (buffer.length() < (offset + skip_bytes)) {
    return MYSQL_FAILURE;
  }
  offset += skip_bytes;
  return MYSQL_SUCCESS;
}

int BufferHelper::peekString(Buffer::Instance& buffer, uint64_t& offset, std::string& str) {
  char end = MYSQL_STR_END;
  ssize_t index = buffer.search(&end, sizeof(end), offset);
  if (index == -1) {
    return MYSQL_FAILURE;
  }
  if (static_cast<int>(buffer.length()) < (index + 1)) {
    return MYSQL_FAILURE;
  }
  str.assign(std::string(static_cast<char*>(buffer.linearize(index)), index));
  str = str.substr(offset);
  offset = index + 1;
  return MYSQL_SUCCESS;
}

int BufferHelper::peekStringBySize(Buffer::Instance& buffer, uint64_t& offset, int len,
                                   std::string& str) {
  if (buffer.length() < (offset + len)) {
    return MYSQL_FAILURE;
  }
  str.assign(std::string(static_cast<char*>(buffer.linearize(len + offset)), len + offset));
  str = str.substr(offset);
  offset += len;
  return MYSQL_SUCCESS;
}

int BufferHelper::peekHdr(Buffer::Instance& buffer, uint64_t& offset, int& len, int& seq) {
  uint32_t val = 0;
  if (peekUint32(buffer, offset, val) != MYSQL_SUCCESS) {
    return MYSQL_FAILURE;
  }
  seq = htonl(val) & MYSQL_HDR_SEQ_MASK;
  len = val & MYSQL_HDR_PKT_SIZE_MASK;
  ENVOY_LOG(trace, "mysql_proxy: MYSQL-hdrseq {}, len {}", seq, len);
  return MYSQL_SUCCESS;
}

bool DecoderImpl::decode(Buffer::Instance& data, uint64_t& offset) {
  ENVOY_LOG(trace, "mysql_proxy: decoding {} bytes", data.length());

  int len = 0;
  int seq = 0;
  if (BufferHelper::peekHdr(data, offset, len, seq) != MYSQL_SUCCESS) {
    throw EnvoyException("error parsing mysql packet header");
  }

  // Fire the login attempt callback.
  if (session_.GetState() == MySQLSession::State::MYSQL_CHALLENGE_REQ) {
    callbacks_.onLoginAttempt();
  }

  /*
  // The sequence ID is reset on a new command.
  if (seq == MYSQL_PKT_0) {
    session_.SetExpectedSeq(MYSQL_PKT_0);
    ENVOY_LOG(trace, "mysql_proxy: received packet with sequence ID = 0");
  }
  */

  // Ignore duplicate and out-of-sync packets.
  if (seq != session_.GetExpectedSeq()) {
    callbacks_.onProtocolError();
    offset += len;
    ENVOY_LOG(info, "mysql_proxy: ignoring out-of-sync packet");
    return true;
  }
  session_.SetExpectedSeq(session_.GetExpectedSeq() + 1);

  // Ensure that the whole packet was consumed.
  const uint64_t prev_offset = offset;
  callbacks_.decode(data, offset, seq, len);
  offset = prev_offset + len;

  ENVOY_LOG(trace, "mysql_proxy: {} bytes remaining after decoding", data.length());
  return true;
}

void DecoderImpl::onData(Buffer::Instance& data) {
  // TODO(venilnoronha): handle messages over 16 mb. See
  // https://dev.mysql.com/doc/dev/mysql-server/8.0.2/page_protocol_basic_packets.html#sect_protocol_basic_packets_sending_mt_16mb.
  uint64_t offset = 0;
  while (!BufferHelper::endOfBuffer(data, offset) && decode(data, offset)) {
  }
}

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
