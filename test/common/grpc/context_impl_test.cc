#include <arpa/inet.h>

#include "common/grpc/context_impl.h"
#include "common/http/headers.h"
#include "common/http/message_impl.h"
#include "common/http/utility.h"
#include "common/stats/fake_symbol_table_impl.h"

#include "test/mocks/upstream/mocks.h"
#include "test/proto/helloworld.pb.h"
#include "test/test_common/global.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Grpc {

TEST(GrpcContextTest, GetGrpcStatus) {
  Http::TestHeaderMapImpl ok_trailers{{"grpc-status", "0"}};
  EXPECT_EQ(Status::Ok, ContextImpl::getGrpcStatus(ok_trailers).value());

  Http::TestHeaderMapImpl no_status_trailers{{"foo", "bar"}};
  EXPECT_FALSE(ContextImpl::getGrpcStatus(no_status_trailers));

  Http::TestHeaderMapImpl aborted_trailers{{"grpc-status", "10"}};
  EXPECT_EQ(Status::Aborted, ContextImpl::getGrpcStatus(aborted_trailers).value());

  Http::TestHeaderMapImpl unauth_trailers{{"grpc-status", "16"}};
  EXPECT_EQ(Status::Unauthenticated, ContextImpl::getGrpcStatus(unauth_trailers).value());

  Http::TestHeaderMapImpl invalid_trailers{{"grpc-status", "-1"}};
  EXPECT_EQ(Status::InvalidCode, ContextImpl::getGrpcStatus(invalid_trailers).value());
}

TEST(GrpcContextTest, GetGrpcMessage) {
  Http::TestHeaderMapImpl empty_trailers;
  EXPECT_EQ("", ContextImpl::getGrpcMessage(empty_trailers));

  Http::TestHeaderMapImpl error_trailers{{"grpc-message", "Some error"}};
  EXPECT_EQ("Some error", ContextImpl::getGrpcMessage(error_trailers));

  Http::TestHeaderMapImpl empty_error_trailers{{"grpc-message", ""}};
  EXPECT_EQ("", ContextImpl::getGrpcMessage(empty_error_trailers));
}

TEST(GrpcContextTest, GetGrpcTimeout) {
  Http::TestHeaderMapImpl empty_headers;
  EXPECT_EQ(std::chrono::milliseconds(0), ContextImpl::getGrpcTimeout(empty_headers));

  Http::TestHeaderMapImpl empty_grpc_timeout{{"grpc-timeout", ""}};
  EXPECT_EQ(std::chrono::milliseconds(0), ContextImpl::getGrpcTimeout(empty_grpc_timeout));

  Http::TestHeaderMapImpl missing_unit{{"grpc-timeout", "123"}};
  EXPECT_EQ(std::chrono::milliseconds(0), ContextImpl::getGrpcTimeout(missing_unit));

  Http::TestHeaderMapImpl illegal_unit{{"grpc-timeout", "123F"}};
  EXPECT_EQ(std::chrono::milliseconds(0), ContextImpl::getGrpcTimeout(illegal_unit));

  Http::TestHeaderMapImpl unit_hours{{"grpc-timeout", "1H"}};
  EXPECT_EQ(std::chrono::milliseconds(60 * 60 * 1000), ContextImpl::getGrpcTimeout(unit_hours));

  Http::TestHeaderMapImpl unit_minutes{{"grpc-timeout", "1M"}};
  EXPECT_EQ(std::chrono::milliseconds(60 * 1000), ContextImpl::getGrpcTimeout(unit_minutes));

  Http::TestHeaderMapImpl unit_seconds{{"grpc-timeout", "1S"}};
  EXPECT_EQ(std::chrono::milliseconds(1000), ContextImpl::getGrpcTimeout(unit_seconds));

  Http::TestHeaderMapImpl unit_milliseconds{{"grpc-timeout", "12345678m"}};
  EXPECT_EQ(std::chrono::milliseconds(12345678), ContextImpl::getGrpcTimeout(unit_milliseconds));

  Http::TestHeaderMapImpl unit_microseconds{{"grpc-timeout", "1000001u"}};
  EXPECT_EQ(std::chrono::milliseconds(1001), ContextImpl::getGrpcTimeout(unit_microseconds));

  Http::TestHeaderMapImpl unit_nanoseconds{{"grpc-timeout", "12345678n"}};
  EXPECT_EQ(std::chrono::milliseconds(13), ContextImpl::getGrpcTimeout(unit_nanoseconds));

  // Max 8 digits and no leading whitespace or +- signs are not enforced on decode,
  // so we don't test for them.
}

TEST(GrpcContextTest, ToGrpcTimeout) {
  Http::HeaderString value;

  ContextImpl::toGrpcTimeout(std::chrono::milliseconds(0UL), value);
  EXPECT_EQ("0m", value.getStringView());

  ContextImpl::toGrpcTimeout(std::chrono::milliseconds(1UL), value);
  EXPECT_EQ("1m", value.getStringView());

  ContextImpl::toGrpcTimeout(std::chrono::milliseconds(100000000UL), value);
  EXPECT_EQ("100000S", value.getStringView());

  ContextImpl::toGrpcTimeout(std::chrono::milliseconds(100000000000UL), value);
  EXPECT_EQ("1666666M", value.getStringView());

  ContextImpl::toGrpcTimeout(std::chrono::milliseconds(9000000000000UL), value);
  EXPECT_EQ("2500000H", value.getStringView());

  ContextImpl::toGrpcTimeout(std::chrono::milliseconds(360000000000000UL), value);
  EXPECT_EQ("99999999H", value.getStringView());

  ContextImpl::toGrpcTimeout(std::chrono::milliseconds(UINT64_MAX), value);
  EXPECT_EQ("99999999H", value.getStringView());
}

TEST(GrpcContextTest, ChargeStats) {
  NiceMock<Upstream::MockClusterInfo> cluster;
  Envoy::Test::Global<Stats::FakeSymbolTableImpl> symbol_table_;
  Stats::StatNamePool pool(*symbol_table_);
  const Stats::StatName service = pool.add("service");
  const Stats::StatName method = pool.add("method");
  ContextImpl::RequestNames request_names{service, method};
  ContextImpl context(*symbol_table_);
  context.chargeStat(cluster, request_names, true);
  EXPECT_EQ(1U, cluster.stats_store_.counter("grpc.service.method.success").value());
  EXPECT_EQ(0U, cluster.stats_store_.counter("grpc.service.method.failure").value());
  EXPECT_EQ(1U, cluster.stats_store_.counter("grpc.service.method.total").value());

  context.chargeStat(cluster, request_names, false);
  EXPECT_EQ(1U, cluster.stats_store_.counter("grpc.service.method.success").value());
  EXPECT_EQ(1U, cluster.stats_store_.counter("grpc.service.method.failure").value());
  EXPECT_EQ(2U, cluster.stats_store_.counter("grpc.service.method.total").value());

  Http::TestHeaderMapImpl trailers;
  Http::HeaderEntry& status = trailers.insertGrpcStatus();
  status.value("0", 1);
  context.chargeStat(cluster, Context::Protocol::Grpc, request_names, &status);
  EXPECT_EQ(1U, cluster.stats_store_.counter("grpc.service.method.0").value());
  EXPECT_EQ(2U, cluster.stats_store_.counter("grpc.service.method.success").value());
  EXPECT_EQ(1U, cluster.stats_store_.counter("grpc.service.method.failure").value());
  EXPECT_EQ(3U, cluster.stats_store_.counter("grpc.service.method.total").value());

  status.value("1", 1);
  context.chargeStat(cluster, Context::Protocol::Grpc, request_names, &status);
  EXPECT_EQ(1U, cluster.stats_store_.counter("grpc.service.method.0").value());
  EXPECT_EQ(1U, cluster.stats_store_.counter("grpc.service.method.1").value());
  EXPECT_EQ(2U, cluster.stats_store_.counter("grpc.service.method.success").value());
  EXPECT_EQ(2U, cluster.stats_store_.counter("grpc.service.method.failure").value());
  EXPECT_EQ(4U, cluster.stats_store_.counter("grpc.service.method.total").value());
}

TEST(GrpcContextTest, PrepareHeaders) {
  {
    Http::MessagePtr message =
        ContextImpl::prepareHeaders("cluster", "service_name", "method_name", absl::nullopt);

    EXPECT_EQ("POST", message->headers().Method()->value().getStringView());
    EXPECT_EQ("/service_name/method_name", message->headers().Path()->value().getStringView());
    EXPECT_EQ("cluster", message->headers().Host()->value().getStringView());
    EXPECT_EQ("application/grpc", message->headers().ContentType()->value().getStringView());
  }
  {
    Http::MessagePtr message = ContextImpl::prepareHeaders(
        "cluster", "service_name", "method_name", absl::optional<std::chrono::milliseconds>(1));

    EXPECT_EQ("POST", message->headers().Method()->value().getStringView());
    EXPECT_EQ("/service_name/method_name", message->headers().Path()->value().getStringView());
    EXPECT_EQ("cluster", message->headers().Host()->value().getStringView());
    EXPECT_EQ("application/grpc", message->headers().ContentType()->value().getStringView());
    EXPECT_EQ("1m", message->headers().GrpcTimeout()->value().getStringView());
  }
  {
    Http::MessagePtr message = ContextImpl::prepareHeaders("cluster", "service_name", "method_name",
                                                           absl::optional<std::chrono::seconds>(1));

    EXPECT_EQ("POST", message->headers().Method()->value().getStringView());
    EXPECT_EQ("/service_name/method_name", message->headers().Path()->value().getStringView());
    EXPECT_EQ("cluster", message->headers().Host()->value().getStringView());
    EXPECT_EQ("application/grpc", message->headers().ContentType()->value().getStringView());
    EXPECT_EQ("1000m", message->headers().GrpcTimeout()->value().getStringView());
  }
  {
    Http::MessagePtr message = ContextImpl::prepareHeaders("cluster", "service_name", "method_name",
                                                           absl::optional<std::chrono::minutes>(1));

    EXPECT_EQ("POST", message->headers().Method()->value().getStringView());
    EXPECT_EQ("/service_name/method_name", message->headers().Path()->value().getStringView());
    EXPECT_EQ("cluster", message->headers().Host()->value().getStringView());
    EXPECT_EQ("application/grpc", message->headers().ContentType()->value().getStringView());
    EXPECT_EQ("60000m", message->headers().GrpcTimeout()->value().getStringView());
  }
  {
    Http::MessagePtr message = ContextImpl::prepareHeaders("cluster", "service_name", "method_name",
                                                           absl::optional<std::chrono::hours>(1));

    EXPECT_EQ("POST", message->headers().Method()->value().getStringView());
    EXPECT_EQ("/service_name/method_name", message->headers().Path()->value().getStringView());
    EXPECT_EQ("cluster", message->headers().Host()->value().getStringView());
    EXPECT_EQ("application/grpc", message->headers().ContentType()->value().getStringView());
    EXPECT_EQ("3600000m", message->headers().GrpcTimeout()->value().getStringView());
  }
  {
    Http::MessagePtr message = ContextImpl::prepareHeaders(
        "cluster", "service_name", "method_name", absl::optional<std::chrono::hours>(100000000));

    EXPECT_EQ("POST", message->headers().Method()->value().getStringView());
    EXPECT_EQ("/service_name/method_name", message->headers().Path()->value().getStringView());
    EXPECT_EQ("cluster", message->headers().Host()->value().getStringView());
    EXPECT_EQ("application/grpc", message->headers().ContentType()->value().getStringView());
    EXPECT_EQ("99999999H", message->headers().GrpcTimeout()->value().getStringView());
  }
  {
    Http::MessagePtr message =
        ContextImpl::prepareHeaders("cluster", "service_name", "method_name",
                                    absl::optional<std::chrono::milliseconds>(100000000000));

    EXPECT_EQ("POST", message->headers().Method()->value().getStringView());
    EXPECT_EQ("/service_name/method_name", message->headers().Path()->value().getStringView());
    EXPECT_EQ("cluster", message->headers().Host()->value().getStringView());
    EXPECT_EQ("application/grpc", message->headers().ContentType()->value().getStringView());
    EXPECT_EQ("1666666M", message->headers().GrpcTimeout()->value().getStringView());
  }
}

TEST(GrpcContextTest, ResolveServiceAndMethod) {
  std::string service;
  std::string method;
  Http::HeaderMapImpl headers;
  Http::HeaderEntry& path = headers.insertPath();
  path.value(std::string("/service_name/method_name"));
  Envoy::Test::Global<Stats::FakeSymbolTableImpl> symbol_table;
  ContextImpl context(*symbol_table);
  absl::optional<ContextImpl::RequestNames> request_names = context.resolveServiceAndMethod(&path);
  EXPECT_TRUE(request_names);
  EXPECT_EQ("service_name", symbol_table->toString(request_names->service_));
  EXPECT_EQ("method_name", symbol_table->toString(request_names->method_));
  path.value(std::string(""));
  EXPECT_FALSE(context.resolveServiceAndMethod(&path));
  path.value(std::string("/"));
  EXPECT_FALSE(context.resolveServiceAndMethod(&path));
  path.value(std::string("//"));
  EXPECT_FALSE(context.resolveServiceAndMethod(&path));
  path.value(std::string("/service_name"));
  EXPECT_FALSE(context.resolveServiceAndMethod(&path));
  path.value(std::string("/service_name/"));
  EXPECT_FALSE(context.resolveServiceAndMethod(&path));
}

TEST(GrpcContextTest, GrpcToHttpStatus) {
  const std::vector<std::pair<Status::GrpcStatus, uint64_t>> test_set = {
      {Status::GrpcStatus::Ok, 200},
      {Status::GrpcStatus::Canceled, 499},
      {Status::GrpcStatus::Unknown, 500},
      {Status::GrpcStatus::InvalidArgument, 400},
      {Status::GrpcStatus::DeadlineExceeded, 504},
      {Status::GrpcStatus::NotFound, 404},
      {Status::GrpcStatus::AlreadyExists, 409},
      {Status::GrpcStatus::PermissionDenied, 403},
      {Status::GrpcStatus::ResourceExhausted, 429},
      {Status::GrpcStatus::FailedPrecondition, 400},
      {Status::GrpcStatus::Aborted, 409},
      {Status::GrpcStatus::OutOfRange, 400},
      {Status::GrpcStatus::Unimplemented, 501},
      {Status::GrpcStatus::Internal, 500},
      {Status::GrpcStatus::Unavailable, 503},
      {Status::GrpcStatus::DataLoss, 500},
      {Status::GrpcStatus::Unauthenticated, 401},
      {Status::GrpcStatus::InvalidCode, 500},
  };
  for (const auto& test_case : test_set) {
    EXPECT_EQ(test_case.second, Grpc::Utility::grpcToHttpStatus(test_case.first));
  }
}

TEST(GrpcContextTest, HttpToGrpcStatus) {
  const std::vector<std::pair<uint64_t, Status::GrpcStatus>> test_set = {
      {400, Status::GrpcStatus::Internal},         {401, Status::GrpcStatus::Unauthenticated},
      {403, Status::GrpcStatus::PermissionDenied}, {404, Status::GrpcStatus::Unimplemented},
      {429, Status::GrpcStatus::Unavailable},      {502, Status::GrpcStatus::Unavailable},
      {503, Status::GrpcStatus::Unavailable},      {504, Status::GrpcStatus::Unavailable},
      {500, Status::GrpcStatus::Unknown},
  };
  for (const auto& test_case : test_set) {
    EXPECT_EQ(test_case.second, Grpc::Utility::httpToGrpcStatus(test_case.first));
  }
}

TEST(GrpcContextTest, HasGrpcContentType) {
  {
    Http::TestHeaderMapImpl headers{};
    EXPECT_FALSE(ContextImpl::hasGrpcContentType(headers));
  }
  auto isGrpcContentType = [](const std::string& s) {
    Http::TestHeaderMapImpl headers{{"content-type", s}};
    return ContextImpl::hasGrpcContentType(headers);
  };
  EXPECT_FALSE(isGrpcContentType(""));
  EXPECT_FALSE(isGrpcContentType("application/text"));
  EXPECT_TRUE(isGrpcContentType("application/grpc"));
  EXPECT_TRUE(isGrpcContentType("application/grpc+"));
  EXPECT_TRUE(isGrpcContentType("application/grpc+foo"));
  EXPECT_FALSE(isGrpcContentType("application/grpc-"));
  EXPECT_FALSE(isGrpcContentType("application/grpc-web"));
  EXPECT_FALSE(isGrpcContentType("application/grpc-web+foo"));
}

TEST(GrpcContextTest, IsGrpcResponseHeader) {
  Http::TestHeaderMapImpl grpc_status_only{{":status", "500"}, {"grpc-status", "14"}};
  EXPECT_TRUE(ContextImpl::isGrpcResponseHeader(grpc_status_only, true));
  EXPECT_FALSE(ContextImpl::isGrpcResponseHeader(grpc_status_only, false));

  Http::TestHeaderMapImpl grpc_response_header{{":status", "200"},
                                               {"content-type", "application/grpc"}};
  EXPECT_FALSE(ContextImpl::isGrpcResponseHeader(grpc_response_header, true));
  EXPECT_TRUE(ContextImpl::isGrpcResponseHeader(grpc_response_header, false));

  Http::TestHeaderMapImpl json_response_header{{":status", "200"},
                                               {"content-type", "application/json"}};
  EXPECT_FALSE(ContextImpl::isGrpcResponseHeader(json_response_header, true));
  EXPECT_FALSE(ContextImpl::isGrpcResponseHeader(json_response_header, false));
}

TEST(GrpcContextTest, ValidateResponse) {
  {
    Http::ResponseMessageImpl response(
        Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "200"}}});
    response.trailers(Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{"grpc-status", "0"}}});
    EXPECT_NO_THROW(ContextImpl::validateResponse(response));
  }
  {
    Http::ResponseMessageImpl response(
        Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "503"}}});
    EXPECT_THROW_WITH_MESSAGE(ContextImpl::validateResponse(response), Exception,
                              "non-200 response code");
  }
  {
    Http::ResponseMessageImpl response(
        Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "200"}}});
    response.trailers(Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{"grpc-status", "100"}}});
    EXPECT_THROW_WITH_MESSAGE(ContextImpl::validateResponse(response), Exception,
                              "bad grpc-status trailer");
  }
  {
    Http::ResponseMessageImpl response(
        Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "200"}}});
    response.trailers(Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{"grpc-status", "4"}}});
    EXPECT_THROW_WITH_MESSAGE(ContextImpl::validateResponse(response), Exception, "");
  }
  {
    Http::ResponseMessageImpl response(
        Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "200"}}});
    response.trailers(Http::HeaderMapPtr{
        new Http::TestHeaderMapImpl{{"grpc-status", "4"}, {"grpc-message", "custom error"}}});
    EXPECT_THROW_WITH_MESSAGE(ContextImpl::validateResponse(response), Exception, "custom error");
  }
  {
    Http::ResponseMessageImpl response(Http::HeaderMapPtr{
        new Http::TestHeaderMapImpl{{":status", "200"}, {"grpc-status", "100"}}});
    EXPECT_THROW_WITH_MESSAGE(ContextImpl::validateResponse(response), Exception,
                              "bad grpc-status header");
  }
  {
    Http::ResponseMessageImpl response(
        Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "200"}, {"grpc-status", "4"}}});
    EXPECT_THROW_WITH_MESSAGE(ContextImpl::validateResponse(response), Exception, "");
  }
  {
    Http::ResponseMessageImpl response(Http::HeaderMapPtr{new Http::TestHeaderMapImpl{
        {":status", "200"}, {"grpc-status", "4"}, {"grpc-message", "custom error"}}});
    EXPECT_THROW_WITH_MESSAGE(ContextImpl::validateResponse(response), Exception, "custom error");
  }
}

// Ensure that the correct gPRC header is constructed for a Buffer::Instance.
TEST(GrpcContextTest, PrependGrpcFrameHeader) {
  auto buffer = std::make_unique<Buffer::OwnedImpl>();
  buffer->add("test", 4);
  std::array<char, 5> expected_header;
  expected_header[0] = 0; // flags
  const uint32_t nsize = htonl(4);
  std::memcpy(&expected_header[1], reinterpret_cast<const void*>(&nsize), sizeof(uint32_t));
  std::string header_string(&expected_header[0], 5);
  ContextImpl::prependGrpcFrameHeader(*buffer);
  EXPECT_EQ(buffer->toString(), header_string + "test");
}

} // namespace Grpc
} // namespace Envoy
