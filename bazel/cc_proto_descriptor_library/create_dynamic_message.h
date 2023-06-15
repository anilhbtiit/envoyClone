#pragma once

#include <memory>

#include "google/protobuf/io/tokenizer.h"
#include "google/protobuf/io/zero_copy_stream.h"
#include "google/protobuf/io/zero_copy_stream_impl_lite.h"
#include "google/protobuf/message.h"
#include "google/protobuf/message_lite.h"

namespace cc_proto_descriptor_library {

// Forward declare to make the friendship handshake easier.
class TextFormatTranscoder;

// Creates a DynamicMessage based off the passed in MessageLite. DynamicMessage
// gives you access to the Descriptor for the message. The message must have
// been registered with the passed in TextFormatTranscoder. Returned messages
// cannot outlive transcoder.
std::unique_ptr<google::protobuf::Message> CreateDynamicMessage(
    const TextFormatTranscoder& transcoder, const google::protobuf::MessageLite& message,
    google::protobuf::io::ErrorCollector* error_collector = nullptr);

}  // namespace cc_proto_descriptor_library
