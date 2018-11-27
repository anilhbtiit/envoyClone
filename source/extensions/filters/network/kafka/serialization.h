#pragma once

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "envoy/buffer/buffer.h"
#include "envoy/common/exception.h"
#include "envoy/common/pure.h"

#include "common/common/byte_order.h"
#include "common/common/fmt.h"

#include "extensions/filters/network/kafka/kafka_types.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {

/**
 * Deserializer is a stateful entity that constructs a result of type T from bytes provided
 * It can be feed()-ed data until it is ready, filling the internal store
 * When ready(), it is safe to call get() to transform the internally stored bytes into result
 * Further feed()-ing should have no effect on a buffer (should return 0 and not move
 * buffer/remaining)
 * @param T type of deserialized data
 */
template <typename T> class Deserializer {
public:
  virtual ~Deserializer() = default;

  /**
   * Submit data to be processed, will consume as much data as it is necessary.
   * Invoking this method when deserializer is ready has no effect (consumes 0 bytes)
   * @param buffer data pointer, will be updated if data is consumed
   * @param remaining remaining data in buffer, will be updated if data is consumed
   * @return bytes consumed
   */
  virtual size_t feed(const char*& buffer, uint64_t& remaining) PURE;

  /**
   * Whether deserializer has consumed enough data to return result
   */
  virtual bool ready() const PURE;

  /**
   * Returns the entity that is represented by bytes stored in this deserializer
   * Should be only called when deserializer is ready
   */
  virtual T get() const PURE;
};

/**
 * Generic integer deserializer (uses array of sizeof(T) bytes)
 * After all bytes are filled in, the value is converted from network byte-order and returned
 */
template <typename T> class IntDeserializer : public Deserializer<T> {
public:
  IntDeserializer() : written_{0}, ready_(false){};

  size_t feed(const char*& buffer, uint64_t& remaining) {
    const size_t available = std::min<size_t>(sizeof(buf_) - written_, remaining);
    memcpy(buf_ + written_, buffer, available);
    written_ += available;

    if (written_ == sizeof(buf_)) {
      ready_ = true;
    }

    buffer += available;
    remaining -= available;

    return available;
  }

  bool ready() const { return ready_; }

protected:
  char buf_[sizeof(T) / sizeof(char)];
  size_t written_;
  bool ready_;
};

/**
 * Integer deserializer for int8_t
 */
class Int8Deserializer : public IntDeserializer<int8_t> {
public:
  int8_t get() const {
    int8_t result;
    memcpy(&result, buf_, sizeof(result));
    return result;
  }
};

/**
 * Integer deserializer for int16_t
 */
class Int16Deserializer : public IntDeserializer<int16_t> {
public:
  int16_t get() const {
    int16_t result;
    memcpy(&result, buf_, sizeof(result));
    return be16toh(result);
  }
};

/**
 * Integer deserializer for int32_t
 */
class Int32Deserializer : public IntDeserializer<int32_t> {
public:
  int32_t get() const {
    int32_t result;
    memcpy(&result, buf_, sizeof(result));
    return be32toh(result);
  }
};

/**
 * Integer deserializer for uint32_t
 */
class UInt32Deserializer : public IntDeserializer<uint32_t> {
public:
  uint32_t get() const {
    uint32_t result;
    memcpy(&result, buf_, sizeof(result));
    return be32toh(result);
  }
};

/**
 * Integer deserializer for uint64_t
 */
class Int64Deserializer : public IntDeserializer<int64_t> {
public:
  int64_t get() const {
    int64_t result;
    memcpy(&result, buf_, sizeof(result));
    return be64toh(result);
  }
};

/**
 * Deserializer for boolean values
 * Uses a single int8 deserializers, and just checks != 0
 * impl note: could have been a subclass of IntDeserializer<int8_t> with a different get function,
 * but it makes it harder to understand
 *
 * Boolean value is stored in a byte.
 * Values 0 and 1 are used to represent false and true respectively.
 * When reading a boolean value, any non-zero value is considered true.
 */
class BooleanDeserializer : public Deserializer<bool> {
public:
  BooleanDeserializer(){};

  size_t feed(const char*& buffer, uint64_t& remaining) { return buffer_.feed(buffer, remaining); }

  bool ready() const { return buffer_.ready(); }

  bool get() const { return 0 != buffer_.get(); }

private:
  Int8Deserializer buffer_;
};

/**
 * Deserializer of string value
 * First reads length (INT16) and then allocates the buffer of given length
 *
 * From documentation:
 * First the length N is given as an INT16.
 * Then N bytes follow which are the UTF-8 encoding of the character sequence.
 * Length must not be negative.
 */
class StringDeserializer : public Deserializer<std::string> {
public:
  /**
   * Can throw EnvoyException if given string length is not valid
   */
  size_t feed(const char*& buffer, uint64_t& remaining) {
    const size_t length_consumed = length_buf_.feed(buffer, remaining);
    if (!length_buf_.ready()) {
      // break early: we still need to fill in length buffer
      return length_consumed;
    }

    if (!length_consumed_) {
      required_ = length_buf_.get();
      if (required_ >= 0) {
        data_buf_ = std::vector<char>(required_);
      } else {
        throw EnvoyException(fmt::format("invalid STRING length: {}", required_));
      }
      length_consumed_ = true;
    }

    const size_t data_consumed = std::min<size_t>(required_, remaining);
    const size_t written = data_buf_.size() - required_;
    memcpy(data_buf_.data() + written, buffer, data_consumed);
    required_ -= data_consumed;

    buffer += data_consumed;
    remaining -= data_consumed;

    if (required_ == 0) {
      ready_ = true;
    }

    return length_consumed + data_consumed;
  }

  bool ready() const { return ready_; }

  std::string get() const { return std::string(data_buf_.begin(), data_buf_.end()); }

private:
  Int16Deserializer length_buf_;
  bool length_consumed_{false};

  int16_t required_;
  std::vector<char> data_buf_;

  bool ready_{false};
};

/**
 * Deserializer of nullable string value
 * First reads length (INT16) and then allocates the buffer of given length
 * If length was -1, buffer allocation is omitted and deserializer is immediately ready (returning
 * null value)
 *
 * From documentation:
 * For non-null strings, first the length N is given as an INT16.
 * Then N bytes follow which are the UTF-8 encoding of the character sequence.
 * A null value is encoded with length of -1 and there are no following bytes.
 */
class NullableStringDeserializer : public Deserializer<NullableString> {
public:
  /**
   * Can throw EnvoyException if given string length is not valid
   */
  size_t feed(const char*& buffer, uint64_t& remaining) {
    const size_t length_consumed = length_buf_.feed(buffer, remaining);
    if (!length_buf_.ready()) {
      // break early: we still need to fill in length buffer
      return length_consumed;
    }

    if (!length_consumed_) {
      required_ = length_buf_.get();

      if (required_ >= 0) {
        data_buf_ = std::vector<char>(required_);
      }
      if (required_ == NULL_STRING_LENGTH) {
        ready_ = true;
      }
      if (required_ < NULL_STRING_LENGTH) {
        throw EnvoyException(fmt::format("invalid NULLABLE_STRING length: {}", required_));
      }

      length_consumed_ = true;
    }

    if (ready_) {
      return length_consumed;
    }

    const size_t data_consumed = std::min<size_t>(required_, remaining);
    const size_t written = data_buf_.size() - required_;
    memcpy(data_buf_.data() + written, buffer, data_consumed);
    required_ -= data_consumed;

    buffer += data_consumed;
    remaining -= data_consumed;

    if (required_ == 0) {
      ready_ = true;
    }

    return length_consumed + data_consumed;
  }

  bool ready() const { return ready_; }

  NullableString get() const {
    return required_ >= 0 ? absl::make_optional(std::string(data_buf_.begin(), data_buf_.end()))
                          : absl::nullopt;
  }

private:
  constexpr static int16_t NULL_STRING_LENGTH{-1};

  Int16Deserializer length_buf_;
  bool length_consumed_{false};

  int16_t required_;
  std::vector<char> data_buf_;

  bool ready_{false};
};

/**
 * Deserializer of bytes value
 * First reads length (INT32) and then allocates the buffer of given length
 *
 * From documentation:
 * First the length N is given as an INT32. Then N bytes follow.
 */
class BytesDeserializer : public Deserializer<Bytes> {
public:
  /**
   * Can throw EnvoyException if given bytes length is not valid
   */
  size_t feed(const char*& buffer, uint64_t& remaining) {
    const size_t length_consumed = length_buf_.feed(buffer, remaining);
    if (!length_buf_.ready()) {
      // break early: we still need to fill in length buffer
      return length_consumed;
    }

    if (!length_consumed_) {
      required_ = length_buf_.get();
      if (required_ >= 0) {
        data_buf_ = std::vector<unsigned char>(required_);
      } else {
        throw EnvoyException(fmt::format("invalid BYTES length: {}", required_));
      }
      length_consumed_ = true;
    }

    const size_t data_consumed = std::min<size_t>(required_, remaining);
    const size_t written = data_buf_.size() - required_;
    memcpy(data_buf_.data() + written, buffer, data_consumed);
    required_ -= data_consumed;

    buffer += data_consumed;
    remaining -= data_consumed;

    if (required_ == 0) {
      ready_ = true;
    }

    return length_consumed + data_consumed;
  }

  bool ready() const { return ready_; }

  Bytes get() const { return data_buf_; }

private:
  Int32Deserializer length_buf_;
  bool length_consumed_{false};
  int32_t required_;

  std::vector<unsigned char> data_buf_;
  bool ready_{false};
};

/**
 * Deserializer of nullable bytes value
 * First reads length (INT32) and then allocates the buffer of given length
 * If length was -1, buffer allocation is omitted and deserializer is immediately ready (returning
 * null value)
 *
 * From documentation:
 * For non-null values, first the length N is given as an INT32. Then N bytes follow.
 * A null value is encoded with length of -1 and there are no following bytes.
 */
class NullableBytesDeserializer : public Deserializer<NullableBytes> {
public:
  /**
   * Can throw EnvoyException if given bytes length is not valid
   */
  size_t feed(const char*& buffer, uint64_t& remaining) {
    const size_t length_consumed = length_buf_.feed(buffer, remaining);
    if (!length_buf_.ready()) {
      // break early: we still need to fill in length buffer
      return length_consumed;
    }

    if (!length_consumed_) {
      required_ = length_buf_.get();

      if (required_ >= 0) {
        data_buf_ = std::vector<unsigned char>(required_);
      }
      if (required_ == NULL_BYTES_LENGTH) {
        ready_ = true;
      }
      if (required_ < NULL_BYTES_LENGTH) {
        throw EnvoyException(fmt::format("invalid NULLABLE_BYTES length: {}", required_));
      }

      length_consumed_ = true;
    }

    if (ready_) {
      return length_consumed;
    }

    const size_t data_consumed = std::min<size_t>(required_, remaining);
    const size_t written = data_buf_.size() - required_;
    memcpy(data_buf_.data() + written, buffer, data_consumed);
    required_ -= data_consumed;

    buffer += data_consumed;
    remaining -= data_consumed;

    if (required_ == 0) {
      ready_ = true;
    }

    return length_consumed + data_consumed;
  }

  bool ready() const { return ready_; }

  NullableBytes get() const {
    if (NULL_BYTES_LENGTH == required_) {
      return absl::nullopt;
    } else {
      return {data_buf_};
    }
  }

private:
  constexpr static int32_t NULL_BYTES_LENGTH{-1};

  Int32Deserializer length_buf_;
  bool length_consumed_{false};
  int32_t required_;

  std::vector<unsigned char> data_buf_;
  bool ready_{false};
};

/**
 * Deserializer for array of objects of the same type
 *
 * First reads the length of the array, then initializes N underlying deserializers of type
 * DeserializerType After the last of N deserializers is ready, the results of each of them are
 * gathered and put in a vector
 * @param ResponseType result type returned by deserializer of type DeserializerType
 * @param DeserializerType underlying deserializer type
 *
 * From documentation:
 * Represents a sequence of objects of a given type T. Type T can be either a primitive type (e.g.
 * STRING) or a structure. First, the length N is given as an int32_t. Then N instances of type T
 * follow. A null array is represented with a length of -1.
 */
template <typename ResponseType, typename DeserializerType>
class ArrayDeserializer : public Deserializer<std::vector<ResponseType>> {
public:
  /**
   * Can throw EnvoyException if array length is invalid or if DeserializerType can throw
   */
  size_t feed(const char*& buffer, uint64_t& remaining) {

    const size_t length_consumed = length_buf_.feed(buffer, remaining);
    if (!length_buf_.ready()) {
      // break early: we still need to fill in length buffer
      return length_consumed;
    }

    if (!length_consumed_) {
      required_ = length_buf_.get();
      if (required_ >= 0) {
        children_ = std::vector<DeserializerType>(required_);
      } else {
        throw EnvoyException(fmt::format("invalid ARRAY length: {}", required_));
      }
      length_consumed_ = true;
    }

    if (ready_) {
      return length_consumed;
    }

    size_t child_consumed{0};
    for (DeserializerType& child : children_) {
      child_consumed += child.feed(buffer, remaining);
    }

    bool children_ready_ = true;
    for (DeserializerType& child : children_) {
      children_ready_ &= child.ready();
    }
    ready_ = children_ready_;

    return length_consumed + child_consumed;
  }

  bool ready() const { return ready_; }

  std::vector<ResponseType> get() const {
    std::vector<ResponseType> result{};
    result.reserve(children_.size());
    for (const DeserializerType& child : children_) {
      const ResponseType child_result = child.get();
      result.push_back(child_result);
    }
    return result;
  }

private:
  Int32Deserializer length_buf_;
  bool length_consumed_{false};
  int32_t required_;
  std::vector<DeserializerType> children_;
  bool children_setup_{false};
  bool ready_{false};
};

/**
 * Deserializer for nullable array of objects of the same type
 *
 * First reads the length of the array, then initializes N underlying deserializers of type
 * DeserializerType After the last of N deserializers is ready, the results of each of them are
 * gathered and put in a vector
 * @param ResponseType result type returned by deserializer of type DeserializerType
 * @param DeserializerType underlying deserializer type
 *
 * From documentation:
 * Represents a sequence of objects of a given type T. Type T can be either a primitive type (e.g.
 * STRING) or a structure. First, the length N is given as an int32_t. Then N instances of type T
 * follow. A null array is represented with a length of -1.
 */
template <typename ResponseType, typename DeserializerType>
class NullableArrayDeserializer : public Deserializer<NullableArray<ResponseType>> {
public:
  /**
   * Can throw EnvoyException if array length is invalid or if DeserializerType can throw
   */
  size_t feed(const char*& buffer, uint64_t& remaining) {

    const size_t length_consumed = length_buf_.feed(buffer, remaining);
    if (!length_buf_.ready()) {
      // break early: we still need to fill in length buffer
      return length_consumed;
    }

    if (!length_consumed_) {
      required_ = length_buf_.get();

      if (required_ >= 0) {
        children_ = std::vector<DeserializerType>(required_);
      }
      if (required_ == NULL_ARRAY_LENGTH) {
        ready_ = true;
      }
      if (required_ < NULL_ARRAY_LENGTH) {
        throw EnvoyException(fmt::format("invalid NULLABLE_ARRAY length: {}", required_));
      }

      length_consumed_ = true;
    }

    if (ready_) {
      return length_consumed;
    }

    size_t child_consumed{0};
    for (DeserializerType& child : children_) {
      child_consumed += child.feed(buffer, remaining);
    }

    bool children_ready_ = true;
    for (DeserializerType& child : children_) {
      children_ready_ &= child.ready();
    }
    ready_ = children_ready_;

    return length_consumed + child_consumed;
  }

  bool ready() const { return ready_; }

  NullableArray<ResponseType> get() const {
    if (NULL_ARRAY_LENGTH != required_) {
      std::vector<ResponseType> result{};
      result.reserve(children_.size());
      for (const DeserializerType& child : children_) {
        const ResponseType child_result = child.get();
        result.push_back(child_result);
      }
      return result;
    } else {
      return absl::nullopt;
    }
  }

private:
  constexpr static int32_t NULL_ARRAY_LENGTH{-1};

  Int32Deserializer length_buf_;
  bool length_consumed_{false};
  int32_t required_;
  std::vector<DeserializerType> children_;
  bool children_setup_{false};
  bool ready_{false};
};

/**
 * Encodes provided argument in Kafka format
 * In case of primitive types, this is done explicitly as per spec
 * In case of composite types, this is done by calling 'encode' on provided argument
 *
 * This object also carries extra information that is used while traversing the request
 * structure-tree during encryping (currently api_version, as different request versions serialize
 * differently)
 */
// XXX (adam.kotwasinski) that class might be split into Request/ResponseEncodingContext in future,
// but leaving it as it is now
class EncodingContext {
public:
  EncodingContext(int16_t api_version) : api_version_{api_version} {};

  /**
   * Encode given reference in a buffer
   * @return bytes written
   */
  template <typename T> size_t encode(const T& arg, Buffer::Instance& dst);

  /**
   * Encode given array in a buffer
   * @return bytes written
   */
  template <typename T> size_t encode(const std::vector<T>& arg, Buffer::Instance& dst);

  /**
   * Encode given nullable array in a buffer
   * @return bytes written
   */
  template <typename T> size_t encode(const NullableArray<T>& arg, Buffer::Instance& dst);

  int16_t apiVersion() const { return api_version_; }

private:
  const int16_t api_version_;
};

/**
 * For non-primitive types, call `encode` on them, to delegate the serialization to the entity
 * itself
 */
template <typename T> inline size_t EncodingContext::encode(const T& arg, Buffer::Instance& dst) {
  return arg.encode(dst, *this);
}

/**
 * Template overload for int8_t
 * Encode a single byte
 */
template <> inline size_t EncodingContext::encode(const int8_t& arg, Buffer::Instance& dst) {
  dst.add(&arg, sizeof(int8_t));
  return sizeof(int8_t);
}

/**
 * Template overload for int16_t, int32_t, uint32_t, int64_t
 * Encode a N-byte integer, converting to network byte-order
 */
#define ENCODE_NUMERIC_TYPE(TYPE, CONVERTER)                                                       \
  template <> inline size_t EncodingContext::encode(const TYPE& arg, Buffer::Instance& dst) {      \
    const TYPE val = CONVERTER(arg);                                                               \
    dst.add(&val, sizeof(TYPE));                                                                   \
    return sizeof(TYPE);                                                                           \
  }

ENCODE_NUMERIC_TYPE(int16_t, htobe16);
ENCODE_NUMERIC_TYPE(int32_t, htobe32);
ENCODE_NUMERIC_TYPE(uint32_t, htobe32);
ENCODE_NUMERIC_TYPE(int64_t, htobe64);

/**
 * Template overload for bool
 * Encode boolean as a single byte
 */
template <> inline size_t EncodingContext::encode(const bool& arg, Buffer::Instance& dst) {
  int8_t val = arg;
  dst.add(&val, sizeof(int8_t));
  return sizeof(int8_t);
}

/**
 * Template overload for std::string
 * Encode string as INT16 length + N bytes
 */
template <> inline size_t EncodingContext::encode(const std::string& arg, Buffer::Instance& dst) {
  int16_t string_length = arg.length();
  size_t header_length = encode(string_length, dst);
  dst.add(arg.c_str(), string_length);
  return header_length + string_length;
}

/**
 * Template overload for NullableString
 * Encode nullable string as INT16 length + N bytes (length = -1 for null)
 */
template <>
inline size_t EncodingContext::encode(const NullableString& arg, Buffer::Instance& dst) {
  if (arg.has_value()) {
    return encode(*arg, dst);
  } else {
    const int16_t len = -1;
    return encode(len, dst);
  }
}

/**
 * Template overload for Bytes
 * Encode byte array as INT32 length + N bytes
 */
template <> inline size_t EncodingContext::encode(const Bytes& arg, Buffer::Instance& dst) {
  const int32_t data_length = arg.size();
  const size_t header_length = encode(data_length, dst);
  dst.add(arg.data(), arg.size());
  return header_length + data_length;
}

/**
 * Template overload for NullableBytes
 * Encode nullable byte array as INT32 length + N bytes (length = -1 for null)
 */
template <> inline size_t EncodingContext::encode(const NullableBytes& arg, Buffer::Instance& dst) {
  if (arg.has_value()) {
    return encode(*arg, dst);
  } else {
    const int32_t len = -1;
    return encode(len, dst);
  }
}

/**
 * Encode nullable object array to T as INT32 length + N elements
 * Each element of type T then serializes itself on its own
 */
template <typename T>
size_t EncodingContext::encode(const std::vector<T>& arg, Buffer::Instance& dst) {
  const NullableArray<T> wrapped = {arg};
  return encode(wrapped, dst);
}

/**
 * Encode nullable object array to T as INT32 length + N elements (length = -1 for null)
 * Each element of type T then serializes itself on its own
 */
template <typename T>
size_t EncodingContext::encode(const NullableArray<T>& arg, Buffer::Instance& dst) {
  if (arg.has_value()) {
    const int32_t len = arg->size();
    const size_t header_length = encode(len, dst);
    size_t written{0};
    for (const T& el : *arg) {
      // for each of array elements, resolve the correct method again
      // elements could be primitives or complex types, so calling `el.encode()` won't work
      written += encode(el, dst);
    }
    return header_length + written;
  } else {
    const int32_t len = -1;
    return encode(len, dst);
  }
}

} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
