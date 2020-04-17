#include "envoy/extensions/filters/http/grpc_json_transcoder/v3/transcoder.pb.h"

#include "common/config/utility.h"
#include "common/config/version_converter.h"
#include "common/http/message_impl.h"
#include "common/protobuf/protobuf.h"
#include "common/protobuf/utility.h"

#include "test/fuzz/utility.h"
#include "test/mocks/buffer/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/proto/bookstore.pb.h"
#include "test/test_common/utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {

class UberFilterFuzzer {
public:
  UberFilterFuzzer() {
    // Need to set for both a decoder filter and an encoder/decoder filter.
    ON_CALL(filter_callback_, addStreamDecoderFilter(_))
        .WillByDefault(
            Invoke([&](std::shared_ptr<Envoy::Http::StreamDecoderFilter> filter) -> void {
              filter_ = filter;
              filter_->setDecoderFilterCallbacks(callbacks_);
            }));
    ON_CALL(filter_callback_, addStreamFilter(_))
        .WillByDefault(
            Invoke([&](std::shared_ptr<Envoy::Http::StreamDecoderFilter> filter) -> void {
              filter_ = filter;
              filter_->setDecoderFilterCallbacks(callbacks_);
            }));
    setExpectations();
  }

  void setExpectations() {
    // Ext-authz setup
    prepareExtAuthz();
    prepareCache();
    prepareTap();
  }

  void prepareExtAuthz() {
    // Preparing the expectations for the ext_authz filter.
    addr_ = std::make_shared<Network::Address::Ipv4Instance>("1.2.3.4", 1111);
    ON_CALL(connection_, remoteAddress()).WillByDefault(testing::ReturnRef(addr_));
    ON_CALL(connection_, localAddress()).WillByDefault(testing::ReturnRef(addr_));
    ON_CALL(callbacks_, connection()).WillByDefault(testing::Return(&connection_));
    ON_CALL(callbacks_, activeSpan())
        .WillByDefault(testing::ReturnRef(Tracing::NullSpan::instance()));
    callbacks_.stream_info_.protocol_ = Envoy::Http::Protocol::Http2;
  }

  void prepareCache() {
    // Prepare expectations for dynamic forward proxy.
    ON_CALL(factory_context_.dispatcher_, createDnsResolver(_, _))
        .WillByDefault(testing::Return(resolver_));
  }

  void prepareTap() {
    ON_CALL(factory_context_.admin_, addHandler(_, _, _, _, _))
        .WillByDefault(testing::Return(true));
    ON_CALL(factory_context_.admin_, removeHandler(_)).WillByDefault(testing::Return(true));
  }

  void addProtoDescriptor(absl::string_view filter_name, Protobuf::Message* message) {
    if (filter_name.find("grpc_json_transcoder") != absl::string_view::npos) {
      envoy::extensions::filters::http::grpc_json_transcoder::v3::GrpcJsonTranscoder& config =
          dynamic_cast<
              envoy::extensions::filters::http::grpc_json_transcoder::v3::GrpcJsonTranscoder&>(
              *message);
      config.clear_services();
      config.add_services("bookstore.Bookstore");

      Protobuf::FileDescriptorSet descriptor_set;
      for (const auto& file : {"google/api/http.proto", "google/protobuf/descriptor.proto",
                               "google/api/annotations.proto", "google/protobuf/any.proto",
                               "google/api/httpbody.proto", "google/protobuf/empty.proto",
                               "google/protobuf/struct.proto", "test/proto/bookstore.proto"}) {
        const auto* file_descriptor =
            Protobuf::DescriptorPool::generated_pool()->FindFileByName(file);
        ASSERT(file_descriptor != nullptr);
        file_descriptor->CopyTo(descriptor_set.add_file());
      }
      descriptor_set.SerializeToString(config.mutable_proto_descriptor_bin());
    }
  }

  // This executes the decode methods to be fuzzed.
  void decode(Http::StreamDecoderFilter* filter, const test::fuzz::HttpData& data) {
    bool end_stream = false;

    auto headers = Fuzz::fromHeaders<Http::TestRequestHeaderMapImpl>(data.headers());
    if (headers.Path() == nullptr) {
      headers.setPath("/foo");
    }
    if (headers.Method() == nullptr) {
      headers.setMethod("GET");
    }
    if (headers.Host() == nullptr) {
      headers.setHost("foo.com");
    }

    if (data.data().empty() && !data.has_trailers()) {
      end_stream = true;
    }
    ENVOY_LOG_MISC(debug, "Decoding headers: {} ", data.headers().DebugString());
    const auto& headersStatus = filter->decodeHeaders(headers, end_stream);
    if (headersStatus != Http::FilterHeadersStatus::Continue &&
        headersStatus != Http::FilterHeadersStatus::StopIteration) {
      return;
    }

    for (int i = 0; i < data.data().size(); i++) {
      if (i == data.data().size() - 1 && !data.has_trailers()) {
        end_stream = true;
      }
      Buffer::OwnedImpl buffer(data.data().Get(i));
      ENVOY_LOG_MISC(debug, "Decoding data: {} ", buffer.toString());
      if (filter->decodeData(buffer, end_stream) != Http::FilterDataStatus::Continue) {
        return;
      }
    }

    if (data.has_trailers()) {
      ENVOY_LOG_MISC(debug, "Decoding trailers: {} ", data.trailers().DebugString());
      auto trailers = Fuzz::fromHeaders<Http::TestRequestTrailerMapImpl>(data.trailers());
      filter->decodeTrailers(trailers);
    }
  }

  // This creates the filter config and runs decode.
  void fuzz(const envoy::extensions::filters::network::http_connection_manager::v3::HttpFilter&
                proto_config,
            const test::fuzz::HttpData& data) {
    try {
      // Try to create the filter. Exit early if the config is invalid or violates PGV constraints.
      ENVOY_LOG_MISC(info, "filter name {}", proto_config.name());
      auto& factory = Config::Utility::getAndCheckFactoryByName<
          Server::Configuration::NamedHttpFilterConfigFactory>(proto_config.name());
      ProtobufTypes::MessagePtr message = Config::Utility::translateToFactoryConfig(
          proto_config, factory_context_.messageValidationVisitor(), factory);
      // Add a valid service and proto descriptor.
      addProtoDescriptor(proto_config.name(), message.get());
      cb_ = factory.createFilterFactoryFromProto(*message, "stats", factory_context_);
      cb_(filter_callback_);
    } catch (const EnvoyException& e) {
      ENVOY_LOG_MISC(debug, "Controlled exception {}", e.what());
      return;
    }

    decode(filter_.get(), data);
    reset();
  }

  void reset() {
    if (filter_ != nullptr) {
      filter_->onDestroy();
    }
    filter_.reset();
  }

  NiceMock<Server::Configuration::MockFactoryContext> factory_context_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> callbacks_;
  NiceMock<Http::MockFilterChainFactoryCallbacks> filter_callback_;
  std::shared_ptr<Network::MockDnsResolver> resolver_{std::make_shared<Network::MockDnsResolver>()};
  std::shared_ptr<Http::StreamDecoderFilter> filter_;
  Http::FilterFactoryCb cb_;
  NiceMock<Envoy::Network::MockConnection> connection_;
  Network::Address::InstanceConstSharedPtr addr_;
};

} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
