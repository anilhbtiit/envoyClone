#include "extensions/filters/network/zookeeper_proxy/zookeeper_utils.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ZooKeeperProxy {

int32_t BufferHelper::peekInt32(Buffer::Instance& buffer, uint64_t& offset) {
  int32_t val = buffer.peekBEInt<int32_t>(offset);
  offset += sizeof(int32_t);
  return val;
}

int64_t BufferHelper::peekInt64(Buffer::Instance& buffer, uint64_t& offset) {
  int64_t val = buffer.peekBEInt<int64_t>(offset);
  offset += sizeof(int64_t);
  return val;
}

bool BufferHelper::peekBool(Buffer::Instance& buffer, uint64_t& offset) {
  char byte = buffer.peekInt<char, ByteOrder::Host, 1>(offset);
  bool val = static_cast<bool>(byte & 255);
  offset += 1;
  return val;
}

std::string BufferHelper::peekString(Buffer::Instance& buffer, uint64_t& offset) {
  std::string val;
  int32_t len = peekInt32(buffer, offset);

  if (len == 0) {
    return val;
  }

  if (buffer.length() < (offset + len)) {
    throw EnvoyException("buffer is too small");
  }

  std::unique_ptr<char[]> data(new char[len]);
  buffer.copyOut(offset, len, data.get());
  val.assign(data.get(), len);
  offset += len;

  return val;
}

} // namespace ZooKeeperProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
