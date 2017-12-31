#include <fstream>

#include "common/http/message_impl.h"
#include "common/profiler/profiler.h"

#include "server/http/admin.h"

#include "test/mocks/server/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;
using testing::_;

namespace Envoy {
namespace Server {

class AdminFilterTest : public testing::TestWithParam<Network::Address::IpVersion> {
public:
  // TODO(mattklein123): Switch to mocks and do not bind to a real port.
  AdminFilterTest()
      : admin_("/dev/null", TestEnvironment::temporaryPath("envoy.prof"),
               TestEnvironment::temporaryPath("admin.address"),
               Network::Test::getCanonicalLoopbackAddress(GetParam()), server_, listener_scope_),
        filter_(admin_), request_headers_{{":path", "/"}} {
    filter_.setDecoderFilterCallbacks(callbacks_);
  }

  NiceMock<MockInstance> server_;
  Stats::IsolatedStoreImpl listener_scope_;
  AdminImpl admin_;
  AdminFilter filter_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> callbacks_;
  Http::TestHeaderMapImpl request_headers_;
};

INSTANTIATE_TEST_CASE_P(IpVersions, AdminFilterTest,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

TEST_P(AdminFilterTest, HeaderOnly) {
  EXPECT_CALL(callbacks_, encodeHeaders_(_, false));
  filter_.decodeHeaders(request_headers_, true);
}

TEST_P(AdminFilterTest, Body) {
  filter_.decodeHeaders(request_headers_, false);
  Buffer::OwnedImpl data("hello");
  EXPECT_CALL(callbacks_, encodeHeaders_(_, false));
  filter_.decodeData(data, true);
}

TEST_P(AdminFilterTest, Trailers) {
  filter_.decodeHeaders(request_headers_, false);
  Buffer::OwnedImpl data("hello");
  filter_.decodeData(data, false);
  EXPECT_CALL(callbacks_, encodeHeaders_(_, false));
  filter_.decodeTrailers(request_headers_);
}

class AdminInstanceTest : public testing::TestWithParam<Network::Address::IpVersion> {
public:
  AdminInstanceTest()
      : address_out_path_(TestEnvironment::temporaryPath("admin.address")),
        cpu_profile_path_(TestEnvironment::temporaryPath("envoy.prof")),
        admin_("/dev/null", cpu_profile_path_, address_out_path_,
               Network::Test::getCanonicalLoopbackAddress(GetParam()), server_, listener_scope_) {
    EXPECT_EQ(std::chrono::milliseconds(100), admin_.drainTimeout());
    admin_.tracingStats().random_sampling_.inc();
  }

  std::string address_out_path_;
  std::string cpu_profile_path_;
  NiceMock<MockInstance> server_;
  Stats::IsolatedStoreImpl listener_scope_;
  AdminImpl admin_;
};

INSTANTIATE_TEST_CASE_P(IpVersions, AdminInstanceTest,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));
// Can only get code coverage of AdminImpl::handlerCpuProfiler stopProfiler with
// a real profiler linked in (successful call to startProfiler). startProfiler
// requies tcmalloc.
#ifdef TCMALLOC

TEST_P(AdminInstanceTest, AdminProfiler) {
  Buffer::OwnedImpl data;
  Http::HeaderMapImpl header_map;
  EXPECT_EQ(Http::Code::OK, admin_.runCallback("/cpuprofiler?enable=y", header_map, data));
  EXPECT_TRUE(Profiler::Cpu::profilerEnabled());
  EXPECT_EQ(Http::Code::OK, admin_.runCallback("/cpuprofiler?enable=n", header_map, data));
  EXPECT_FALSE(Profiler::Cpu::profilerEnabled());
}

#endif

TEST_P(AdminInstanceTest, AdminBadProfiler) {
  Buffer::OwnedImpl data;
  AdminImpl admin_bad_profile_path(
      "/dev/null", TestEnvironment::temporaryPath("some/unlikely/bad/path.prof"), "",
      Network::Test::getCanonicalLoopbackAddress(GetParam()), server_, listener_scope_);
  Http::HeaderMapImpl header_map;
  admin_bad_profile_path.runCallback("/cpuprofiler?enable=y", header_map, data);
  EXPECT_FALSE(Profiler::Cpu::profilerEnabled());
}

TEST_P(AdminInstanceTest, WriteAddressToFile) {
  std::ifstream address_file(address_out_path_);
  std::string address_from_file;
  std::getline(address_file, address_from_file);
  EXPECT_EQ(admin_.socket().localAddress()->asString(), address_from_file);
}

TEST_P(AdminInstanceTest, AdminBadAddressOutPath) {
  std::string bad_path = TestEnvironment::temporaryPath("some/unlikely/bad/path/admin.address");
  AdminImpl admin_bad_address_out_path("/dev/null", cpu_profile_path_, bad_path,
                                       Network::Test::getCanonicalLoopbackAddress(GetParam()),
                                       server_, listener_scope_);
  EXPECT_FALSE(std::ifstream(bad_path));
}

TEST_P(AdminInstanceTest, CustomHandler) {
  auto callback = [&](const std::string&, Http::HeaderMap&, Buffer::Instance&) -> Http::Code {
    return Http::Code::Accepted;
  };

  // Test removable handler.
  EXPECT_TRUE(admin_.addHandler("/foo/bar", "hello", callback, true));
  Http::HeaderMapImpl header_map;
  Buffer::OwnedImpl response;
  EXPECT_EQ(Http::Code::Accepted, admin_.runCallback("/foo/bar", header_map, response));

  // Test that removable handler gets removed.
  EXPECT_TRUE(admin_.removeHandler("/foo/bar"));
  EXPECT_EQ(Http::Code::NotFound, admin_.runCallback("/foo/bar", header_map, response));
  EXPECT_FALSE(admin_.removeHandler("/foo/bar"));

  // Add non removable handler.
  EXPECT_TRUE(admin_.addHandler("/foo/bar", "hello", callback, false));
  EXPECT_EQ(Http::Code::Accepted, admin_.runCallback("/foo/bar", header_map, response));

  // Add again and make sure it is not there twice.
  EXPECT_FALSE(admin_.addHandler("/foo/bar", "hello", callback, false));

  // Try to remove non removable handler, and make sure it is not removed.
  EXPECT_FALSE(admin_.removeHandler("/foo/bar"));
  EXPECT_EQ(Http::Code::Accepted, admin_.runCallback("/foo/bar", header_map, response));
}

TEST_P(AdminInstanceTest, RejectHandlerWithXss) {
  auto callback = [&](const std::string&, Http::HeaderMap&, Buffer::Instance&) -> Http::Code {
    return Http::Code::Accepted;
  };
  EXPECT_FALSE(admin_.addHandler("/foo<script>alert('hi')</script>", "hello", callback, true));
}

TEST_P(AdminInstanceTest, RejectHandlerWithEmbeddedQuery) {
  auto callback = [&](const std::string&, Http::HeaderMap&, Buffer::Instance&) -> Http::Code {
    return Http::Code::Accepted;
  };
  EXPECT_FALSE(admin_.addHandler("/bar?queryShouldNotBeInPrefix", "hello", callback, true));
}

TEST_P(AdminInstanceTest, EscapeHelpTextWithPunctuation) {
  auto callback = [&](const std::string&, Http::HeaderMap&, Buffer::Instance&) -> Http::Code {
    return Http::Code::Accepted;
  };

  // It's OK to have help text with HTML characters in it, but when we render the home
  // page they need to be escaped.
  const std::string kPlanets = "jupiter>saturn>mars";
  EXPECT_TRUE(admin_.addHandler("/planets", kPlanets, callback, true));

  Http::HeaderMapImpl header_map;
  Buffer::OwnedImpl response;
  EXPECT_EQ(Http::Code::OK, admin_.runCallback("/", header_map, response));
  Http::HeaderString& content_type = header_map.ContentType()->value();
  EXPECT_TRUE(content_type.find("text/html")) << content_type.c_str();
  EXPECT_EQ(-1, response.search(kPlanets.data(), kPlanets.size(), 0));
  const std::string kEscapedPlanets = "jupiter&gt;saturn&gt;mars";
  EXPECT_NE(-1, response.search(kEscapedPlanets.data(), kEscapedPlanets.size(), 0));
}

} // namespace Server
} // namespace Envoy
