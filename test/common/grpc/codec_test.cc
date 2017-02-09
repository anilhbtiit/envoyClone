#include "common/buffer/buffer_impl.h"
#include "common/grpc/codec.h"

#include "test/generated/helloworld.pb.h"

namespace Grpc {

TEST(CodecTest, encodeHeader) {
  Encoder encoder;
  std::array<uint8_t, 5> buffer;

  encoder.newFrame(GRPC_FH_DEFAULT, 1, buffer);
  EXPECT_EQ(buffer[0], GRPC_FH_DEFAULT);
  EXPECT_EQ(buffer[1], 0);
  EXPECT_EQ(buffer[2], 0);
  EXPECT_EQ(buffer[3], 0);
  EXPECT_EQ(buffer[4], 1);

  encoder.newFrame(GRPC_FH_COMPRESSED, 1, buffer);
  EXPECT_EQ(buffer[0], GRPC_FH_COMPRESSED);
  EXPECT_EQ(buffer[1], 0);
  EXPECT_EQ(buffer[2], 0);
  EXPECT_EQ(buffer[3], 0);
  EXPECT_EQ(buffer[4], 1);

  encoder.newFrame(GRPC_FH_DEFAULT, 0x100, buffer);
  EXPECT_EQ(buffer[0], GRPC_FH_DEFAULT);
  EXPECT_EQ(buffer[1], 0);
  EXPECT_EQ(buffer[2], 0);
  EXPECT_EQ(buffer[3], 1);
  EXPECT_EQ(buffer[4], 0);

  encoder.newFrame(GRPC_FH_DEFAULT, 0x10000, buffer);
  EXPECT_EQ(buffer[0], GRPC_FH_DEFAULT);
  EXPECT_EQ(buffer[1], 0);
  EXPECT_EQ(buffer[2], 1);
  EXPECT_EQ(buffer[3], 0);
  EXPECT_EQ(buffer[4], 0);

  encoder.newFrame(GRPC_FH_DEFAULT, 0x1000000, buffer);
  EXPECT_EQ(buffer[0], GRPC_FH_DEFAULT);
  EXPECT_EQ(buffer[1], 1);
  EXPECT_EQ(buffer[2], 0);
  EXPECT_EQ(buffer[3], 0);
  EXPECT_EQ(buffer[4], 0);
}

TEST(CodecTest, decodeInvalidFrame) {
  helloworld::HelloRequest request;
  request.set_name("hello");

  Buffer::OwnedImpl buffer;
  std::array<uint8_t, 5> header;
  Encoder encoder;
  encoder.newFrame(0b10u, request.ByteSize(), header);
  buffer.add(header.data(), 5);
  buffer.add(request.SerializeAsString());

  std::vector<Frame> frames;
  Decoder decoder;
  EXPECT_FALSE(decoder.decode(buffer, frames));
}

TEST(CodecTest, decodeSingleFrame) {
  helloworld::HelloRequest request;
  request.set_name("hello");

  Buffer::OwnedImpl buffer;
  std::array<uint8_t, 5> header;
  Encoder encoder;
  encoder.newFrame(GRPC_FH_DEFAULT, request.ByteSize(), header);
  buffer.add(header.data(), 5);
  buffer.add(request.SerializeAsString());

  std::vector<Frame> frames;
  Decoder decoder;
  EXPECT_TRUE(decoder.decode(buffer, frames));
  EXPECT_EQ(frames.size(), static_cast<uint64_t>(1));
  EXPECT_EQ(GRPC_FH_DEFAULT, frames[0].flags_);
  EXPECT_EQ(static_cast<uint64_t>(request.ByteSize()), frames[0].length_);

  helloworld::HelloRequest result;
  result.ParseFromArray(frames[0].data_->linearize(frames[0].data_->length()),
                        frames[0].data_->length());
  EXPECT_EQ("hello", result.name());
}

TEST(CodecTest, decodeMultipleFrame) {
  helloworld::HelloRequest request;
  request.set_name("hello");

  Buffer::OwnedImpl buffer;
  std::array<uint8_t, 5> header;
  Encoder encoder;
  encoder.newFrame(GRPC_FH_DEFAULT, request.ByteSize(), header);
  for (int i = 0; i < 1009; i++) {
    buffer.add(header.data(), 5);
    buffer.add(request.SerializeAsString());
  }

  std::vector<Frame> frames;
  Decoder decoder;
  EXPECT_TRUE(decoder.decode(buffer, frames));
  EXPECT_EQ(frames.size(), static_cast<uint64_t>(1009));
  for (Frame& frame : frames) {
    EXPECT_EQ(GRPC_FH_DEFAULT, frame.flags_);
    EXPECT_EQ(static_cast<uint64_t>(request.ByteSize()), frame.length_);

    helloworld::HelloRequest result;
    result.ParseFromArray(frame.data_->linearize(frame.data_->length()), frame.data_->length());
    EXPECT_EQ("hello", result.name());
  }
}

} // Grpc
