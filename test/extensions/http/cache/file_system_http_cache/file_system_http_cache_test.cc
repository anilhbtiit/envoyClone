#include "envoy/http/header_map.h"
#include "envoy/registry/registry.h"
#include "envoy/singleton/manager.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/filesystem/directory.h"
#include "source/extensions/filters/http/cache/cache_entry_utils.h"
#include "source/extensions/filters/http/cache/cache_headers_utils.h"
#include "source/extensions/http/cache/file_system_http_cache/cache_file_header_proto_util.h"
#include "source/extensions/http/cache/file_system_http_cache/file_system_http_cache.h"

#include "test/extensions/common/async_files/mocks.h"
#include "test/extensions/filters/http/cache/common.h"
#include "test/extensions/filters/http/cache/http_cache_implementation_test_common.h"
#include "test/mocks/server/factory_context.h"
#include "test/test_common/environment.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "absl/cleanup/cleanup.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace FileSystemHttpCache {
namespace {

using Common::AsyncFiles::AsyncFileHandle;
using Common::AsyncFiles::MockAsyncFileContext;
using Common::AsyncFiles::MockAsyncFileHandle;
using Common::AsyncFiles::MockAsyncFileManager;
using Common::AsyncFiles::MockAsyncFileManagerFactory;
using ::envoy::extensions::filters::http::cache::v3::CacheConfig;
using ::testing::HasSubstr;
using ::testing::NiceMock;
using ::testing::StrictMock;

absl::string_view yaml_config = R"(
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.http.cache.file_system_http_cache.v3.FileSystemHttpCacheConfig
    manager_config:
      thread_pool:
        thread_count: 1
    cache_path: /tmp/
)";

class FileSystemCacheTestContext {
public:
  FileSystemCacheTestContext() {
    cache_path_ = absl::StrCat(env_.temporaryDirectory(), "/");
    ConfigProto cfg = testConfig();
    deleteCacheFiles(cfg.cache_path());
    auto cache_config = cacheConfig(cfg);
    const std::string type{
        TypeUtil::typeUrlToDescriptorFullName(cache_config.typed_config().type_url())};
    http_cache_factory_ = Registry::FactoryRegistry<HttpCacheFactory>::getFactoryByType(type);
    if (http_cache_factory_ == nullptr) {
      throw EnvoyException(
          fmt::format("Didn't find a registered implementation for type: '{}'", type));
    }
  }

  void initCache() {
    cache_ = std::dynamic_pointer_cast<FileSystemHttpCache>(
        http_cache_factory_->getCache(cacheConfig(testConfig()), context_));
  }

  ConfigProto testConfig() {
    envoy::extensions::filters::http::cache::v3::CacheConfig cache_config;
    TestUtility::loadFromYaml(std::string(yaml_config), cache_config);
    ConfigProto cfg;
    MessageUtil::unpackTo(cache_config.typed_config(), cfg);
    cfg.set_cache_path(cache_path_);
    return cfg;
  }

  envoy::extensions::filters::http::cache::v3::CacheConfig cacheConfig(ConfigProto cfg) {
    envoy::extensions::filters::http::cache::v3::CacheConfig cache_config;
    cache_config.mutable_typed_config()->PackFrom(cfg);
    return cache_config;
  }

protected:
  void deleteCacheFiles(std::string path) {
    for (const auto& it : ::Envoy::Filesystem::Directory(path)) {
      if (absl::StartsWith(it.name_, "cache-")) {
        env_.removePath(absl::StrCat(path, it.name_));
      }
    }
  }

  ::Envoy::TestEnvironment env_;
  std::string cache_path_;
  NiceMock<Server::Configuration::MockFactoryContext> context_;
  std::shared_ptr<FileSystemHttpCache> cache_;
  LogLevelSetter log_level_ = LogLevelSetter(spdlog::level::debug);
  HttpCacheFactory* http_cache_factory_;
};

class FileSystemHttpCacheTest : public FileSystemCacheTestContext, public ::testing::Test {
  void SetUp() override { initCache(); }
};

TEST_F(FileSystemHttpCacheTest, ExceptionOnTryingToCreateCachesWithDistinctConfigsOnSamePath) {
  ConfigProto cfg = testConfig();
  cfg.mutable_manager_config()->mutable_thread_pool()->set_thread_count(2);
  EXPECT_ANY_THROW(http_cache_factory_->getCache(cacheConfig(cfg), context_));
}

TEST_F(FileSystemHttpCacheTest, IdenticalCacheConfigReturnsSameCacheInstance) {
  ConfigProto cfg = testConfig();
  auto second_cache = http_cache_factory_->getCache(cacheConfig(cfg), context_);
  EXPECT_EQ(cache_, second_cache);
}

TEST_F(FileSystemHttpCacheTest, CacheConfigsWithDifferentPathsReturnDistinctCacheInstances) {
  ConfigProto cfg = testConfig();
  cfg.set_cache_path(env_.temporaryDirectory());
  auto second_cache = http_cache_factory_->getCache(cacheConfig(cfg), context_);
  EXPECT_NE(cache_, second_cache);
}

CacheConfig varyAllowListConfig() {
  CacheConfig config;
  config.add_allowed_vary_headers()->set_exact("accept");
  return config;
}

class MockSingletonManager : public Singleton::ManagerImpl {
public:
  MockSingletonManager() : Singleton::ManagerImpl(Thread::threadFactoryForTest()) {
    // By default just act like a real SingletonManager, but allow overrides.
    ON_CALL(*this, get(_, _))
        .WillByDefault(std::bind(&MockSingletonManager::realGet, this, std::placeholders::_1,
                                 std::placeholders::_2));
  }

  MOCK_METHOD(Singleton::InstanceSharedPtr, get,
              (const std::string& name, Singleton::SingletonFactoryCb cb));
  Singleton::InstanceSharedPtr realGet(const std::string& name, Singleton::SingletonFactoryCb cb) {
    return Singleton::ManagerImpl::get(name, cb);
  }
};

class FileSystemHttpCacheTestWithMockFiles : public FileSystemHttpCacheTest {
public:
  FileSystemHttpCacheTestWithMockFiles() {
    ON_CALL(context_, singletonManager()).WillByDefault(ReturnRef(mock_singleton_manager_));
    ON_CALL(mock_singleton_manager_, get(HasSubstr("async_file_manager_factory_singleton"), _))
        .WillByDefault(Return(mock_async_file_manager_factory_));
    ON_CALL(*mock_async_file_manager_factory_, getAsyncFileManager(_, _))
        .WillByDefault(Return(mock_async_file_manager_));
    request_headers_.setMethod("GET");
    request_headers_.setHost("example.com");
    request_headers_.setScheme("https");
    request_headers_.setCopy(Http::CustomHeaders::get().CacheControl, "max-age=3600");
    request_headers_.setPath("/");
    expect_false_callback_ = [this](bool result) {
      EXPECT_FALSE(result);
      false_callbacks_called_++;
    };
    expect_true_callback_ = [this](bool result) {
      EXPECT_TRUE(result);
      true_callbacks_called_++;
    };
    trailers_size_ = bufferFromProto(protoFromTrailers(response_trailers_)).length();
    key_ = LookupRequest{request_headers_, time_system_.systemTime(), vary_allow_list_}.key();
    headers_size_ =
        headerProtoSize(protoFromHeadersAndMetadata(key_, response_headers_, metadata_));
  }

  void SetUp() override { initCache(); }

  LookupContextPtr testLookupContext() {
    auto request = LookupRequest{request_headers_, time_system_.systemTime(), vary_allow_list_};
    key_ = request.key();
    return cache_->makeLookupContext(std::move(request), decoder_callbacks_);
  }

  InsertContextPtr testInserter() {
    auto ret = cache_->makeInsertContext(testLookupContext(), encoder_callbacks_);
    return ret;
  }

  LookupResult testLookupResult() {
    auto lookup = testLookupContext();
    absl::Cleanup destroy_lookup([&lookup]() { lookup->onDestroy(); });
    LookupResult result;
    lookup->getHeaders([&](LookupResult&& r) { result = std::move(r); });
    return result;
  }

  void insertTestCacheRecord() {
    {
      InsertContextPtr inserter = testInserter();
      absl::Cleanup destroy_inserter([&inserter]() { inserter->onDestroy(); });
      EXPECT_CALL(*mock_async_file_manager_, createAnonymousFile(_, _));
      EXPECT_CALL(*mock_async_file_handle_, write(_, _, _)).Times(5);
      EXPECT_CALL(*mock_async_file_handle_, createHardLink(_, _));
      inserter->insertHeaders(response_headers_, metadata_, expect_true_callback_, false);
      const absl::string_view body = "beepbeep";
      inserter->insertBody(Buffer::OwnedImpl(body), expect_true_callback_, false);
      inserter->insertTrailers(response_trailers_, expect_true_callback_);
      mock_async_file_manager_->nextActionCompletes(
          absl::StatusOr<AsyncFileHandle>(mock_async_file_handle_));
      // Empty pre-header
      mock_async_file_manager_->nextActionCompletes(
          absl::StatusOr<size_t>(CacheFileFixedBlock::size()));
      // Body
      mock_async_file_manager_->nextActionCompletes(absl::StatusOr<size_t>(body.size()));
      // Trailers
      mock_async_file_manager_->nextActionCompletes(absl::StatusOr<size_t>(trailers_size_));
      // Headers
      mock_async_file_manager_->nextActionCompletes(absl::StatusOr<size_t>(headers_size_));
      // Updated pre-header
      mock_async_file_manager_->nextActionCompletes(
          absl::StatusOr<size_t>(CacheFileFixedBlock::size()));
      // createHardLink
      mock_async_file_manager_->nextActionCompletes(absl::OkStatus());
      // Should have been 3 callbacks; insertHeaders, insertBody, insertTrailers.
      EXPECT_EQ(true_callbacks_called_, 3);
      // Clear the callback counter for the actual test.
      true_callbacks_called_ = 0;
    }
    // Replace the file handle because the first one has been used and closed.
    mock_async_file_handle_ = std::make_shared<MockAsyncFileContext>(mock_async_file_manager_);
    // Consume the close from the queue.
    mock_async_file_manager_->nextActionCompletes(absl::OkStatus());
  }

protected:
  ::testing::NiceMock<MockSingletonManager> mock_singleton_manager_;
  std::shared_ptr<MockAsyncFileManagerFactory> mock_async_file_manager_factory_ =
      std::make_shared<NiceMock<MockAsyncFileManagerFactory>>();
  std::shared_ptr<MockAsyncFileManager> mock_async_file_manager_ =
      std::make_shared<NiceMock<MockAsyncFileManager>>();
  MockAsyncFileHandle mock_async_file_handle_ =
      std::make_shared<StrictMock<MockAsyncFileContext>>(mock_async_file_manager_);
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;
  Event::SimulatedTimeSystem time_system_;
  Http::TestRequestHeaderMapImpl request_headers_;
  VaryAllowList vary_allow_list_{varyAllowListConfig().allowed_vary_headers()};
  DateFormatter formatter_{"%a, %d %b %Y %H:%M:%S GMT"};
  Http::TestResponseHeaderMapImpl response_headers_{
      {":status", "200"},
      {"date", formatter_.fromTime(time_system_.systemTime())},
      {"cache-control", "public,max-age=3600"},
  };
  Http::TestResponseTrailerMapImpl response_trailers_{{"fruit", "banana"}};
  const ResponseMetadata metadata_{time_system_.systemTime()};
  Key key_;
  int false_callbacks_called_ = 0;
  int true_callbacks_called_ = 0;
  std::function<void(bool result)> expect_false_callback_;
  std::function<void(bool result)> expect_true_callback_;
  size_t headers_size_;
  size_t trailers_size_;
};

TEST_F(FileSystemHttpCacheTestWithMockFiles, FailedWriteOfVaryNodeJustClosesTheFile) {
  auto inserter = testInserter();
  absl::Cleanup destroy_inserter{[&inserter]() { inserter->onDestroy(); }};
  Http::TestResponseHeaderMapImpl response_headers{
      {":status", "200"},
      {"date", formatter_.fromTime(time_system_.systemTime())},
      {"cache-control", "public,max-age=3600"},
      {"vary", "accept"}};
  // one file created for the vary node, one for the actual write.
  EXPECT_CALL(*mock_async_file_manager_, createAnonymousFile(_, _)).Times(2);
  inserter->insertHeaders(
      response_headers, metadata_, [&](bool result) { EXPECT_FALSE(result); }, true);
  EXPECT_CALL(*mock_async_file_handle_, write(_, _, _));
  // File handle for the vary node.
  // (This triggers the expected write call.)
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<AsyncFileHandle>{mock_async_file_handle_});
  // Fail to create file for the cache entry node.
  // (This provokes the false callback to insertHeaders.)
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<AsyncFileHandle>{absl::UnknownError("open failure")});
  // Fail to write for the vary node.
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<size_t>(absl::UnknownError("write failure")));
}

TEST_F(FileSystemHttpCacheTestWithMockFiles, LookupDuringAnotherInsertPreventsInserts) {
  auto inserter = testInserter();
  absl::Cleanup destroy_inserter{[&inserter]() { inserter->onDestroy(); }};
  // First inserter will try to create a file.
  EXPECT_CALL(*mock_async_file_manager_, createAnonymousFile(_, _));
  inserter->insertHeaders(
      response_headers_, metadata_, [&](bool result) { EXPECT_FALSE(result); }, false);

  auto inserter2 = testInserter();
  absl::Cleanup destroy_inserter2{[&inserter2]() { inserter2->onDestroy(); }};
  // Allow the first inserter to complete after the second lookup was made.
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<AsyncFileHandle>{absl::UnknownError("intentionally failed to open file")});
  inserter2->insertHeaders(response_headers_, metadata_, expect_false_callback_, false);
  inserter2->insertBody(Buffer::OwnedImpl("boop"), expect_false_callback_, false);
  inserter2->insertTrailers(response_trailers_, expect_false_callback_);
  EXPECT_EQ(false_callbacks_called_, 3);
  // The file handle didn't actually get used in this test, but is expected to be closed.
  EXPECT_OK(mock_async_file_handle_->close([](absl::Status) {}));
}

TEST_F(FileSystemHttpCacheTestWithMockFiles, DuplicateInsertWhileInsertInProgressIsPrevented) {
  auto inserter = testInserter();
  absl::Cleanup destroy_inserter{[&inserter]() { inserter->onDestroy(); }};
  auto inserter2 = testInserter();
  absl::Cleanup destroy_inserter2{[&inserter2]() { inserter2->onDestroy(); }};
  // First inserter will try to create a file.
  EXPECT_CALL(*mock_async_file_manager_, createAnonymousFile(_, _));
  inserter->insertHeaders(response_headers_, metadata_, expect_false_callback_, false);
  inserter2->insertHeaders(response_headers_, metadata_, expect_false_callback_, false);
  // Allow the first inserter to complete after the second insert was called.
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<AsyncFileHandle>{absl::UnknownError("intentionally failed to open file")});
  inserter2->insertBody(Buffer::OwnedImpl("boop"), expect_false_callback_, false);
  inserter2->insertTrailers(response_trailers_, expect_false_callback_);
  EXPECT_EQ(false_callbacks_called_, 4);
  // The file handle didn't actually get used in this test, but is expected to be closed.
  EXPECT_OK(mock_async_file_handle_->close([](absl::Status) {}));
}

// The documentation for cache_filter suggests it will wait for ready_for_next_chunk to be
// called before sending another chunk, but it does not. This test verifies that the cache
// doesn't rely on the documented behavior, and can cope with receiving two insertBody
// calls without completion callbacks being called in between.
TEST_F(FileSystemHttpCacheTestWithMockFiles, InsertWithMultipleChunksBeforeCallbackWorks) {
  auto inserter = testInserter();
  absl::Cleanup destroy_inserter{[&inserter]() { inserter->onDestroy(); }};
  EXPECT_CALL(*mock_async_file_manager_, createAnonymousFile(_, _));
  inserter->insertHeaders(response_headers_, metadata_, expect_true_callback_, false);
  absl::string_view body1 = "herp";
  absl::string_view body2 = "derp";
  inserter->insertBody(Buffer::OwnedImpl(body1), expect_true_callback_, false);
  inserter->insertBody(Buffer::OwnedImpl(body2), expect_true_callback_, false);
  inserter->insertTrailers(response_trailers_, expect_true_callback_);
  EXPECT_EQ(0, true_callbacks_called_);
  EXPECT_CALL(*mock_async_file_handle_, write(_, _, _)).Times(6);
  // Open file
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<AsyncFileHandle>{mock_async_file_handle_});
  // Empty pre-header
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<size_t>(CacheFileFixedBlock::size()));
  // Body1
  mock_async_file_manager_->nextActionCompletes(absl::StatusOr<size_t>(body1.size()));
  // Body2
  mock_async_file_manager_->nextActionCompletes(absl::StatusOr<size_t>(body2.size()));
  // Trailers
  mock_async_file_manager_->nextActionCompletes(absl::StatusOr<size_t>(trailers_size_));
  // Headers
  mock_async_file_manager_->nextActionCompletes(absl::StatusOr<size_t>(headers_size_));
  // Updated pre-header (which triggers createHardLink)
  EXPECT_CALL(*mock_async_file_handle_, createHardLink(_, _));
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<size_t>(CacheFileFixedBlock::size()));
  // createHardLink
  mock_async_file_manager_->nextActionCompletes(absl::OkStatus());
  // Should have been 4 callbacks; insertHeaders, insertBody, insertBody, insertTrailers.
  EXPECT_EQ(true_callbacks_called_, 4);
}

TEST_F(FileSystemHttpCacheTestWithMockFiles, FailedOpenForReadInvalidatesTheCacheEntry) {
  insertTestCacheRecord();
  auto lookup = testLookupContext();
  absl::Cleanup destroy_lookup([&lookup]() { lookup->onDestroy(); });
  LookupResult result;
  lookup->getHeaders([&](LookupResult&& r) { result = std::move(r); });
  EXPECT_CALL(*mock_async_file_manager_, openExistingFile(_, _, _));
  EXPECT_CALL(*mock_async_file_manager_, unlink(_, _));
  lookup->getBody(AdjustedByteRange(0, 8),
                  [&](Buffer::InstancePtr body) { EXPECT_EQ(body.get(), nullptr); });
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<AsyncFileHandle>(absl::UnknownError("Intentionally failed to open file")));
  mock_async_file_manager_->nextActionCompletes(absl::OkStatus());
  // File handle didn't get used but is expected to be closed.
  EXPECT_OK(mock_async_file_handle_->close([](absl::Status) {}));
}

TEST_F(FileSystemHttpCacheTestWithMockFiles, FailedReadOfBodyInvalidatesTheCacheEntry) {
  insertTestCacheRecord();
  auto lookup = testLookupContext();
  absl::Cleanup destroy_lookup([&lookup]() { lookup->onDestroy(); });
  LookupResult result;
  lookup->getHeaders([&](LookupResult&& r) { result = std::move(r); });
  EXPECT_CALL(*mock_async_file_manager_, openExistingFile(_, _, _));
  EXPECT_CALL(*mock_async_file_handle_, read(_, _, _));
  lookup->getBody(AdjustedByteRange(0, 8),
                  [&](Buffer::InstancePtr body) { EXPECT_EQ(body.get(), nullptr); });
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<AsyncFileHandle>(mock_async_file_handle_));
  EXPECT_CALL(*mock_async_file_manager_, unlink(_, _));
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<Buffer::InstancePtr>(absl::UnknownError("intentional failure to read")));
  mock_async_file_manager_->nextActionCompletes(
      absl::UnknownError("intentionally failed to unlink, for coverage"));
}

TEST_F(FileSystemHttpCacheTestWithMockFiles, FailedReadOfTrailersInvalidatesTheCacheEntry) {
  insertTestCacheRecord();
  auto lookup = testLookupContext();
  absl::Cleanup destroy_lookup([&lookup]() { lookup->onDestroy(); });
  LookupResult result;
  lookup->getHeaders([&](LookupResult&& r) { result = std::move(r); });
  EXPECT_CALL(*mock_async_file_manager_, openExistingFile(_, _, _));
  EXPECT_CALL(*mock_async_file_handle_, read(_, _, _));
  lookup->getBody(AdjustedByteRange(0, 8),
                  [&](Buffer::InstancePtr body) { EXPECT_EQ(body->toString(), "beepbeep"); });
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<AsyncFileHandle>(mock_async_file_handle_));
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<Buffer::InstancePtr>(std::make_unique<Buffer::OwnedImpl>("beepbeep")));
  EXPECT_CALL(*mock_async_file_handle_, read(_, _, _));
  // No point validating that the trailers are empty since that's not even particularly
  // desirable behavior - it's a quirk of the filter that we can't properly signify an error.
  lookup->getTrailers([&](Http::ResponseTrailerMapPtr) {});
  EXPECT_CALL(*mock_async_file_manager_, unlink(_, _));
  mock_async_file_manager_->nextActionCompletes(absl::StatusOr<Buffer::InstancePtr>(
      absl::UnknownError("intentional failure to read trailers")));
  mock_async_file_manager_->nextActionCompletes(
      absl::UnknownError("intentionally failed to unlink, for coverage"));
}

TEST_F(FileSystemHttpCacheTestWithMockFiles, ReadWithMultipleBlocksWorksCorrectly) {
  insertTestCacheRecord();
  auto lookup = testLookupContext();
  LookupResult result;
  lookup->getHeaders([&](LookupResult&& r) { result = std::move(r); });
  EXPECT_CALL(*mock_async_file_manager_, openExistingFile(_, _, _));
  EXPECT_CALL(*mock_async_file_handle_, read(CacheFileFixedBlock::offsetToBody(), 4, _));
  EXPECT_CALL(*mock_async_file_handle_, read(CacheFileFixedBlock::offsetToBody() + 4, 4, _));
  lookup->getBody(AdjustedByteRange(0, 4),
                  [&](Buffer::InstancePtr body) { EXPECT_EQ(body->toString(), "beep"); });
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<AsyncFileHandle>(mock_async_file_handle_));
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<Buffer::InstancePtr>(std::make_unique<Buffer::OwnedImpl>("beep")));
  lookup->getBody(AdjustedByteRange(4, 8),
                  [&](Buffer::InstancePtr body) { EXPECT_EQ(body->toString(), "boop"); });
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<Buffer::InstancePtr>(std::make_unique<Buffer::OwnedImpl>("boop")));
  // While we're here, incidentally test the behavior of aborting a lookup in progress
  // while no file actions are in flight.
  lookup->onDestroy();
  lookup.reset();
  // There should be a file-close in the queue.
  mock_async_file_manager_->nextActionCompletes(absl::OkStatus());
}

TEST_F(FileSystemHttpCacheTestWithMockFiles, DestroyingALookupWithFileActionInFlightCancelsAction) {
  insertTestCacheRecord();
  auto lookup = testLookupContext();
  absl::Cleanup destroy_lookup([&lookup]() { lookup->onDestroy(); });
  LookupResult result;
  lookup->getHeaders([&](LookupResult&& r) { result = std::move(r); });
  EXPECT_CALL(*mock_async_file_manager_, openExistingFile(_, _, _));
  EXPECT_CALL(*mock_async_file_manager_, mockCancel());
  lookup->getBody(AdjustedByteRange(0, 4),
                  [&](Buffer::InstancePtr body) { EXPECT_EQ(body.get(), nullptr); });
  // File wasn't used in this test but is expected to be closed.
  EXPECT_OK(mock_async_file_handle_->close([](absl::Status) {}));
}

TEST_F(FileSystemHttpCacheTestWithMockFiles,
       InsertAbortsOnFailureToWriteEmptyHeaderBlockAndCancelsEntireQueue) {
  auto inserter = testInserter();
  absl::Cleanup destroy_inserter([&inserter]() { inserter->onDestroy(); });
  EXPECT_CALL(*mock_async_file_manager_, createAnonymousFile(_, _));
  EXPECT_CALL(*mock_async_file_handle_, write(_, _, _));
  inserter->insertHeaders(response_headers_, metadata_, expect_false_callback_, false);
  inserter->insertBody(Buffer::OwnedImpl("woop"), expect_false_callback_, false);
  inserter->insertBody(Buffer::OwnedImpl("woop"), expect_false_callback_, false);
  inserter->insertBody(Buffer::OwnedImpl("woop"), expect_false_callback_, true);
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<AsyncFileHandle>(mock_async_file_handle_));
  mock_async_file_manager_->nextActionCompletes(absl::StatusOr<size_t>(
      absl::UnknownError("intentionally failed write to empty header block")));
  EXPECT_EQ(testLookupResult().cache_entry_status_, CacheEntryStatus::Unusable);
  EXPECT_EQ(false_callbacks_called_, 4);
}

TEST_F(FileSystemHttpCacheTestWithMockFiles, InsertAbortsOnFailureToWriteBodyChunk) {
  auto inserter = testInserter();
  absl::Cleanup destroy_inserter([&inserter]() { inserter->onDestroy(); });
  EXPECT_CALL(*mock_async_file_manager_, createAnonymousFile(_, _));
  EXPECT_CALL(*mock_async_file_handle_, write(_, _, _)).Times(2);
  inserter->insertHeaders(response_headers_, metadata_, expect_true_callback_, false);
  inserter->insertBody(Buffer::OwnedImpl("woop"), expect_false_callback_, false);
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<AsyncFileHandle>(mock_async_file_handle_));
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<size_t>(CacheFileFixedBlock::size()));
  // Intentionally undersized write of body chunk.
  mock_async_file_manager_->nextActionCompletes(absl::StatusOr<size_t>(1));
  EXPECT_EQ(testLookupResult().cache_entry_status_, CacheEntryStatus::Unusable);
  EXPECT_EQ(false_callbacks_called_, 1);
}

TEST_F(FileSystemHttpCacheTestWithMockFiles, InsertAbortsOnFailureToWriteTrailerChunk) {
  auto inserter = testInserter();
  absl::Cleanup destroy_inserter([&inserter]() { inserter->onDestroy(); });
  EXPECT_CALL(*mock_async_file_manager_, createAnonymousFile(_, _));
  EXPECT_CALL(*mock_async_file_handle_, write(_, _, _)).Times(3);
  inserter->insertHeaders(response_headers_, metadata_, expect_true_callback_, false);
  const absl::string_view body = "woop";
  inserter->insertBody(Buffer::OwnedImpl(body), expect_true_callback_, false);
  inserter->insertTrailers(response_trailers_, expect_false_callback_);
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<AsyncFileHandle>(mock_async_file_handle_));
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<size_t>(CacheFileFixedBlock::size()));
  mock_async_file_manager_->nextActionCompletes(absl::StatusOr<size_t>(body.size()));
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<size_t>(absl::UnknownError("intentionally failed write of trailer chunk")));
  EXPECT_EQ(testLookupResult().cache_entry_status_, CacheEntryStatus::Unusable);
  EXPECT_EQ(false_callbacks_called_, 1);
}

TEST_F(FileSystemHttpCacheTestWithMockFiles, InsertAbortsOnFailureToWriteHeaderChunk) {
  auto inserter = testInserter();
  absl::Cleanup destroy_inserter([&inserter]() { inserter->onDestroy(); });
  EXPECT_CALL(*mock_async_file_manager_, createAnonymousFile(_, _));
  EXPECT_CALL(*mock_async_file_handle_, write(_, _, _)).Times(4);
  inserter->insertHeaders(response_headers_, metadata_, expect_true_callback_, false);
  const absl::string_view body = "woop";
  inserter->insertBody(Buffer::OwnedImpl(body), expect_true_callback_, false);
  inserter->insertTrailers(response_trailers_, expect_false_callback_);
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<AsyncFileHandle>(mock_async_file_handle_));
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<size_t>(CacheFileFixedBlock::size()));
  mock_async_file_manager_->nextActionCompletes(absl::StatusOr<size_t>(body.size()));
  mock_async_file_manager_->nextActionCompletes(absl::StatusOr<size_t>(trailers_size_));
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<size_t>(absl::UnknownError("intentionally failed write of header chunk")));
  EXPECT_EQ(testLookupResult().cache_entry_status_, CacheEntryStatus::Unusable);
  EXPECT_EQ(false_callbacks_called_, 1);
}

TEST_F(FileSystemHttpCacheTestWithMockFiles, InsertAbortsOnFailureToWriteUpdatedHeaderBlock) {
  auto inserter = testInserter();
  absl::Cleanup destroy_inserter([&inserter]() { inserter->onDestroy(); });
  EXPECT_CALL(*mock_async_file_manager_, createAnonymousFile(_, _));
  EXPECT_CALL(*mock_async_file_handle_, write(_, _, _)).Times(5);
  inserter->insertHeaders(response_headers_, metadata_, expect_true_callback_, false);
  const absl::string_view body = "woop";
  inserter->insertBody(Buffer::OwnedImpl(body), expect_true_callback_, false);
  inserter->insertTrailers(response_trailers_, expect_false_callback_);
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<AsyncFileHandle>(mock_async_file_handle_));
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<size_t>(CacheFileFixedBlock::size()));
  mock_async_file_manager_->nextActionCompletes(absl::StatusOr<size_t>(body.size()));
  mock_async_file_manager_->nextActionCompletes(absl::StatusOr<size_t>(trailers_size_));
  mock_async_file_manager_->nextActionCompletes(absl::StatusOr<size_t>(headers_size_));
  mock_async_file_manager_->nextActionCompletes(absl::StatusOr<size_t>(
      absl::UnknownError("intentionally failed write of updated header block")));
  EXPECT_EQ(testLookupResult().cache_entry_status_, CacheEntryStatus::Unusable);
  EXPECT_EQ(false_callbacks_called_, 1);
}

TEST_F(FileSystemHttpCacheTestWithMockFiles, InsertAbortsOnFailureToLinkFile) {
  auto inserter = testInserter();
  absl::Cleanup destroy_inserter([&inserter]() { inserter->onDestroy(); });
  EXPECT_CALL(*mock_async_file_manager_, createAnonymousFile(_, _));
  EXPECT_CALL(*mock_async_file_handle_, write(_, _, _)).Times(5);
  EXPECT_CALL(*mock_async_file_handle_, createHardLink(_, _));
  inserter->insertHeaders(response_headers_, metadata_, expect_true_callback_, false);
  const absl::string_view body = "woop";
  inserter->insertBody(Buffer::OwnedImpl(body), expect_true_callback_, false);
  inserter->insertTrailers(response_trailers_, expect_false_callback_);
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<AsyncFileHandle>(mock_async_file_handle_));
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<size_t>(CacheFileFixedBlock::size()));
  mock_async_file_manager_->nextActionCompletes(absl::StatusOr<size_t>(body.size()));
  mock_async_file_manager_->nextActionCompletes(absl::StatusOr<size_t>(trailers_size_));
  mock_async_file_manager_->nextActionCompletes(absl::StatusOr<size_t>(headers_size_));
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<size_t>(CacheFileFixedBlock::size()));
  mock_async_file_manager_->nextActionCompletes(
      absl::UnknownError("intentionally failed to link cache file"));
  EXPECT_EQ(testLookupResult().cache_entry_status_, CacheEntryStatus::Unusable);
  EXPECT_EQ(false_callbacks_called_, 1);
}

TEST_F(FileSystemHttpCacheTestWithMockFiles, UpdateHeadersStillWorksIfFileOpenFailed) {
  insertTestCacheRecord();
  time_system_.advanceTimeWait(Seconds(3601));
  Http::TestResponseHeaderMapImpl response_headers{
      {":status", "200"},
      {"date", formatter_.fromTime(time_system_.systemTime())},
      {"x-whatever", "updated"},
      {"cache-control", "public,max-age=3600"},
  };
  auto lookup_context = testLookupContext();
  EXPECT_CALL(*mock_async_file_manager_, openExistingFile(_, _, _));
  cache_->updateHeaders(*lookup_context, response_headers, {time_system_.systemTime()});
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<AsyncFileHandle>(absl::UnknownError("Intentionally failed to open file")));
  lookup_context->onDestroy();
  response_headers.setReferenceKey(Http::LowerCaseString("age"), "0");
  EXPECT_THAT(testLookupResult().headers_.get(), HeaderMapEqualIgnoreOrder(&response_headers));
  // File is not used in this test, but is expected to be closed.
  EXPECT_OK(mock_async_file_handle_->close([](absl::Status) {}));
}

TEST_F(FileSystemHttpCacheTestWithMockFiles, UpdateHeadersStillWorksIfHeaderWriteToFileFailed) {
  insertTestCacheRecord();
  time_system_.advanceTimeWait(Seconds(3601));
  Http::TestResponseHeaderMapImpl response_headers{
      {":status", "200"},
      {"date", formatter_.fromTime(time_system_.systemTime())},
      {"x-whatever", "updated"},
      {"cache-control", "public,max-age=3600"},
  };
  auto lookup_context = testLookupContext();
  EXPECT_CALL(*mock_async_file_manager_, openExistingFile(_, _, _));
  EXPECT_CALL(*mock_async_file_handle_, write(_, _, _));
  cache_->updateHeaders(*lookup_context, response_headers, {time_system_.systemTime()});
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<AsyncFileHandle>(mock_async_file_handle_));
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<size_t>(absl::UnknownError("Intentionally failed to write headers")));
  lookup_context->onDestroy();
  response_headers.setReferenceKey(Http::LowerCaseString("age"), "0");
  EXPECT_THAT(testLookupResult().headers_.get(), HeaderMapEqualIgnoreOrder(&response_headers));
}

TEST_F(FileSystemHttpCacheTestWithMockFiles,
       UpdateHeadersStillWorksIfHeaderBlockWriteToFileFailed) {
  insertTestCacheRecord();
  time_system_.advanceTimeWait(Seconds(3601));
  const ResponseMetadata metadata{time_system_.systemTime()};
  Http::TestResponseHeaderMapImpl response_headers{
      {":status", "200"},
      {"date", formatter_.fromTime(time_system_.systemTime())},
      {"x-whatever", "updated"},
      {"cache-control", "public,max-age=3600"},
  };
  auto lookup_context = testLookupContext();
  EXPECT_CALL(*mock_async_file_manager_, openExistingFile(_, _, _));
  EXPECT_CALL(*mock_async_file_handle_, write(_, _, _)).Times(2);
  cache_->updateHeaders(*lookup_context, response_headers, metadata);
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<AsyncFileHandle>(mock_async_file_handle_));
  mock_async_file_manager_->nextActionCompletes(absl::StatusOr<size_t>(
      headerProtoSize(protoFromHeadersAndMetadata(key_, response_headers, metadata))));
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<size_t>(absl::UnknownError("Intentionally failed to write headers block")));
  lookup_context->onDestroy();
  response_headers.setReferenceKey(Http::LowerCaseString("age"), "0");
  EXPECT_THAT(testLookupResult().headers_.get(), HeaderMapEqualIgnoreOrder(&response_headers));
}

// For the standard cache tests from http_cache_implementation_test_common.cc
// These will be run with the real file system, and therefore only cover the
// "no file errors" paths.
class FileSystemHttpCacheTestDelegate : public HttpCacheTestDelegate,
                                        public FileSystemCacheTestContext {
public:
  FileSystemHttpCacheTestDelegate() { initCache(); }
  std::shared_ptr<HttpCache> cache() override { return cache_; }
  bool validationEnabled() const override { return true; }
};

// For the standard cache tests from http_cache_implementation_test_common.cc
INSTANTIATE_TEST_SUITE_P(FileSystemHttpCacheTest, HttpCacheImplementationTest,
                         testing::Values(std::make_unique<FileSystemHttpCacheTestDelegate>),
                         [](const testing::TestParamInfo<HttpCacheImplementationTest::ParamType>&) {
                           return "FileSystemHttpCache";
                         });

TEST(Registration, GetCacheFromFactory) {
  HttpCacheFactory* factory = Registry::FactoryRegistry<HttpCacheFactory>::getFactoryByType(
      "envoy.extensions.http.cache.file_system_http_cache.v3.FileSystemHttpCacheConfig");
  ASSERT_NE(factory, nullptr);
  envoy::extensions::filters::http::cache::v3::CacheConfig cache_config;
  NiceMock<Server::Configuration::MockFactoryContext> factory_context;
  TestUtility::loadFromYaml(std::string(yaml_config), cache_config);
  EXPECT_EQ(factory->getCache(cache_config, factory_context)->cacheInfo().name_,
            "envoy.extensions.http.cache.file_system_http_cache");
}

} // namespace
} // namespace FileSystemHttpCache
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
