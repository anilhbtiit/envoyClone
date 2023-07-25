#include "source/common/io/io_uring_impl.h"
#include "source/common/network/address_impl.h"

#include "test/mocks/server/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Io {
namespace {

class IoUringImplTest : public ::testing::Test {
public:
  IoUringImplTest() : api_(Api::createApiForTest()), should_skip_(!isIoUringSupported()) {
    if (!should_skip_) {
      factory_ = std::make_unique<IoUringFactoryImpl>(2, false, context_.threadLocal());
      factory_->onServerInitialized();
      io_uring_ = factory_->getOrCreate();
    }
  }

  void SetUp() override {
    if (should_skip_) {
      GTEST_SKIP();
    }
  }

  void TearDown() override {
    if (should_skip_) {
      return;
    }

    if (io_uring_->isEventfdRegistered()) {
      io_uring_->unregisterEventfd();
    }
  }

  Api::ApiPtr api_;
  testing::NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  std::unique_ptr<IoUringFactoryImpl> factory_{};
  OptRef<IoUring> io_uring_{};
  const bool should_skip_{};
};

class IoUringImplParamTest
    : public IoUringImplTest,
      public testing::WithParamInterface<std::function<IoUringResult(IoUring&, os_fd_t)>> {};

INSTANTIATE_TEST_SUITE_P(InvalidPrepareMethodParamsTest, IoUringImplParamTest,
                         testing::Values(
                             [](IoUring& uring, os_fd_t fd) -> IoUringResult {
                               return uring.prepareAccept(fd, nullptr, nullptr, nullptr);
                             },
                             [](IoUring& uring, os_fd_t fd) -> IoUringResult {
                               auto address =
                                   std::make_shared<Network::Address::EnvoyInternalInstance>(
                                       "test");
                               return uring.prepareConnect(fd, address, nullptr);
                             },
                             [](IoUring& uring, os_fd_t fd) -> IoUringResult {
                               return uring.prepareReadv(fd, nullptr, 0, 0, nullptr);
                             },
                             [](IoUring& uring, os_fd_t fd) -> IoUringResult {
                               return uring.prepareWritev(fd, nullptr, 0, 0, nullptr);
                             },
                             [](IoUring& uring, os_fd_t fd) -> IoUringResult {
                               return uring.prepareClose(fd, nullptr);
                             }));

TEST_P(IoUringImplParamTest, InvalidParams) {
  os_fd_t fd;
  SET_SOCKET_INVALID(fd);
  auto dispatcher = api_->allocateDispatcher("test_thread");

  os_fd_t event_fd = io_uring_->registerEventfd();
  const Event::FileTriggerType trigger = Event::PlatformDefaultTriggerType;
  int32_t completions_nr = 0;
  auto file_event = dispatcher->createFileEvent(
      event_fd,
      [this, &completions_nr](uint32_t) {
        io_uring_->forEveryCompletion([&completions_nr](void*, int32_t res, bool) {
          EXPECT_TRUE(res < 0);
          completions_nr++;
        });
      },
      trigger, Event::FileReadyType::Read);

  auto prepare_method = GetParam();
  IoUringResult res = prepare_method(*io_uring_, fd);
  EXPECT_EQ(res, IoUringResult::Ok);
  res = prepare_method(*io_uring_, fd);
  EXPECT_EQ(res, IoUringResult::Ok);
  res = prepare_method(*io_uring_, fd);
  EXPECT_EQ(res, IoUringResult::Failed);
  res = io_uring_->submit();
  EXPECT_EQ(res, IoUringResult::Ok);
  res = io_uring_->submit();
  EXPECT_EQ(res, IoUringResult::Ok);

  dispatcher->run(Event::Dispatcher::RunType::NonBlock);
  EXPECT_EQ(completions_nr, 2);
}

TEST_F(IoUringImplTest, InjectCompletion) {
  auto dispatcher = api_->allocateDispatcher("test_thread");

  os_fd_t fd = 11;
  os_fd_t event_fd = io_uring_->registerEventfd();
  const Event::FileTriggerType trigger = Event::PlatformDefaultTriggerType;
  int32_t completions_nr = 0;

  auto file_event = dispatcher->createFileEvent(
      event_fd,
      [this, &fd, &completions_nr](uint32_t) {
        io_uring_->forEveryCompletion(
            [&fd, &completions_nr](void* user_data, int32_t res, bool injected) {
              EXPECT_TRUE(injected);
              EXPECT_EQ(&fd, user_data);
              EXPECT_EQ(-11, res);
              completions_nr++;
            });
      },
      trigger, Event::FileReadyType::Read);

  io_uring_->injectCompletion(fd, &fd, -11);

  file_event->activate(Event::FileReadyType::Read);

  dispatcher->run(Event::Dispatcher::RunType::NonBlock);
  EXPECT_EQ(completions_nr, 1);
}

TEST_F(IoUringImplTest, NestInjectCompletion) {
  auto dispatcher = api_->allocateDispatcher("test_thread");

  os_fd_t fd = 11;
  os_fd_t fd2 = 11;
  os_fd_t event_fd = io_uring_->registerEventfd();
  const Event::FileTriggerType trigger = Event::PlatformDefaultTriggerType;
  int32_t completions_nr = 0;

  auto file_event = dispatcher->createFileEvent(
      event_fd,
      [this, &fd, &fd2, &completions_nr](uint32_t) {
        io_uring_->forEveryCompletion(
            [this, &fd, &fd2, &completions_nr](void* user_data, int32_t res, bool injected) {
              EXPECT_TRUE(injected);
              if (completions_nr == 0) {
                EXPECT_EQ(&fd, user_data);
                EXPECT_EQ(-11, res);
                io_uring_->injectCompletion(fd2, &fd2, -22);
              } else {
                EXPECT_EQ(&fd2, user_data);
                EXPECT_EQ(-22, res);
              }

              completions_nr++;
            });
      },
      trigger, Event::FileReadyType::Read);

  io_uring_->injectCompletion(fd, &fd, -11);

  file_event->activate(Event::FileReadyType::Read);

  dispatcher->run(Event::Dispatcher::RunType::NonBlock);
  EXPECT_EQ(completions_nr, 2);
}

TEST_F(IoUringImplTest, RemoveInjectCompletion) {
  auto dispatcher = api_->allocateDispatcher("test_thread");

  os_fd_t fd = 11;
  os_fd_t fd2 = 22;
  os_fd_t event_fd = io_uring_->registerEventfd();
  const Event::FileTriggerType trigger = Event::PlatformDefaultTriggerType;
  int32_t completions_nr = 0;

  auto file_event = dispatcher->createFileEvent(
      event_fd,
      [this, &fd, &completions_nr](uint32_t) {
        io_uring_->forEveryCompletion(
            [&fd, &completions_nr](void* user_data, int32_t res, bool injected) {
              EXPECT_TRUE(injected);
              EXPECT_EQ(&fd, user_data);
              EXPECT_EQ(-11, res);
              completions_nr++;
            });
      },
      trigger, Event::FileReadyType::Read);

  io_uring_->injectCompletion(fd, &fd, -11);
  io_uring_->injectCompletion(fd2, &fd2, -22);
  io_uring_->removeInjectedCompletion(
      fd2, [fd2](void* user_data) { EXPECT_EQ(fd2, *reinterpret_cast<os_fd_t*>(user_data)); });
  file_event->activate(Event::FileReadyType::Read);

  dispatcher->run(Event::Dispatcher::RunType::NonBlock);
  EXPECT_EQ(completions_nr, 1);
}

TEST_F(IoUringImplTest, NestRemoveInjectCompletion) {
  auto dispatcher = api_->allocateDispatcher("test_thread");

  os_fd_t fd = 11;
  os_fd_t fd2 = 22;
  os_fd_t event_fd = io_uring_->registerEventfd();
  const Event::FileTriggerType trigger = Event::PlatformDefaultTriggerType;
  int32_t completions_nr = 0;

  auto file_event = dispatcher->createFileEvent(
      event_fd,
      [this, &fd, &fd2, &completions_nr](uint32_t) {
        io_uring_->forEveryCompletion(
            [this, &fd, &fd2, &completions_nr](void* user_data, int32_t res, bool injected) {
              EXPECT_TRUE(injected);
              if (completions_nr == 0) {
                EXPECT_EQ(&fd, user_data);
                EXPECT_EQ(-11, res);
              } else {
                io_uring_->removeInjectedCompletion(fd2, [](void*) {});
              }
              completions_nr++;
            });
      },
      trigger, Event::FileReadyType::Read);

  io_uring_->injectCompletion(fd, &fd, -11);
  io_uring_->injectCompletion(fd2, &fd2, -22);

  file_event->activate(Event::FileReadyType::Read);

  dispatcher->run(Event::Dispatcher::RunType::NonBlock);
  EXPECT_EQ(completions_nr, 2);
}

TEST_F(IoUringImplTest, RegisterEventfd) {
  EXPECT_FALSE(io_uring_->isEventfdRegistered());
  io_uring_->registerEventfd();
  EXPECT_TRUE(io_uring_->isEventfdRegistered());
  io_uring_->unregisterEventfd();
  EXPECT_FALSE(io_uring_->isEventfdRegistered());
  EXPECT_DEATH(io_uring_->unregisterEventfd(), "");
}

TEST_F(IoUringImplTest, PrepareReadvAllDataFitsOneChunk) {
  std::string test_file =
      TestEnvironment::writeStringToFileForTest("prepare_readv", "test text", true);
  os_fd_t fd = open(test_file.c_str(), O_RDONLY);
  ASSERT_TRUE(fd >= 0);

  auto dispatcher = api_->allocateDispatcher("test_thread");

  uint8_t buffer[4096]{};
  struct iovec iov;
  iov.iov_base = buffer;
  iov.iov_len = 4096;

  os_fd_t event_fd = io_uring_->registerEventfd();

  const Event::FileTriggerType trigger = Event::PlatformDefaultTriggerType;
  int32_t completions_nr = 0;
  auto file_event = dispatcher->createFileEvent(
      event_fd,
      [this, &completions_nr, d = dispatcher.get()](uint32_t) {
        io_uring_->forEveryCompletion([&completions_nr](void*, int32_t res, bool) {
          completions_nr++;
          EXPECT_EQ(res, strlen("test text"));
        });
        d->exit();
      },
      trigger, Event::FileReadyType::Read);

  io_uring_->prepareReadv(fd, &iov, 1, 0, nullptr);
  EXPECT_STREQ(static_cast<char*>(iov.iov_base), "");
  io_uring_->submit();

  dispatcher->run(Event::Dispatcher::RunType::Block);

  // Check that the completion callback has been actually called.
  EXPECT_EQ(completions_nr, 1);
  // The file's content is in the read buffer now.
  EXPECT_STREQ(static_cast<char*>(iov.iov_base), "test text");
}

TEST_F(IoUringImplTest, PrepareReadvQueueOverflow) {
  std::string test_file =
      TestEnvironment::writeStringToFileForTest("prepare_readv_overflow", "abcdefhg", true);
  os_fd_t fd = open(test_file.c_str(), O_RDONLY);
  ASSERT_TRUE(fd >= 0);

  auto dispatcher = api_->allocateDispatcher("test_thread");

  uint8_t buffer1[2]{};
  struct iovec iov1;
  iov1.iov_base = buffer1;
  iov1.iov_len = 2;
  uint8_t buffer2[2]{};
  struct iovec iov2;
  iov2.iov_base = buffer2;
  iov2.iov_len = 2;
  uint8_t buffer3[2]{};
  struct iovec iov3;
  iov3.iov_base = buffer3;
  iov3.iov_len = 2;

  os_fd_t event_fd = io_uring_->registerEventfd();
  const Event::FileTriggerType trigger = Event::PlatformDefaultTriggerType;
  int32_t completions_nr = 0;
  auto file_event = dispatcher->createFileEvent(
      event_fd,
      [this, &completions_nr](uint32_t) {
        io_uring_->forEveryCompletion([&completions_nr](void* user_data, int32_t res, bool) {
          EXPECT_TRUE(user_data != nullptr);
          EXPECT_EQ(res, 2);
          completions_nr++;
          // Note: generally events are not guaranteed to complete in the same order
          // we submit them, but for this case of reading from a single file it's ok
          // to expect the same order.
          EXPECT_EQ(reinterpret_cast<int64_t>(user_data), completions_nr);
        });
      },
      trigger, Event::FileReadyType::Read);

  IoUringResult res = io_uring_->prepareReadv(fd, &iov1, 1, 0, reinterpret_cast<void*>(1));
  EXPECT_EQ(res, IoUringResult::Ok);
  res = io_uring_->prepareReadv(fd, &iov2, 1, 2, reinterpret_cast<void*>(2));
  EXPECT_EQ(res, IoUringResult::Ok);
  res = io_uring_->prepareReadv(fd, &iov3, 1, 4, reinterpret_cast<void*>(3));
  // Expect the submission queue overflow.
  EXPECT_EQ(res, IoUringResult::Failed);
  res = io_uring_->submit();
  EXPECT_EQ(res, IoUringResult::Ok);

  // Even though we haven't been notified about ops completion the buffers
  // are filled already.
  EXPECT_EQ(static_cast<char*>(iov1.iov_base)[0], 'a');
  EXPECT_EQ(static_cast<char*>(iov1.iov_base)[1], 'b');
  EXPECT_EQ(static_cast<char*>(iov2.iov_base)[0], 'c');
  EXPECT_EQ(static_cast<char*>(iov2.iov_base)[1], 'd');

  dispatcher->run(Event::Dispatcher::RunType::NonBlock);

  // Only 2 completions are expected because the completion queue can contain
  // no more than 2 entries.
  EXPECT_EQ(completions_nr, 2);

  // Check a new event gets handled in the next dispatcher run.
  res = io_uring_->prepareReadv(fd, &iov3, 1, 4, reinterpret_cast<void*>(3));
  EXPECT_EQ(res, IoUringResult::Ok);
  res = io_uring_->submit();
  EXPECT_EQ(res, IoUringResult::Ok);

  EXPECT_EQ(static_cast<char*>(iov3.iov_base)[0], 'e');
  EXPECT_EQ(static_cast<char*>(iov3.iov_base)[1], 'f');

  dispatcher->run(Event::Dispatcher::RunType::NonBlock);
  // Check the completion callback was called actually.
  EXPECT_EQ(completions_nr, 3);
}

} // namespace
} // namespace Io
} // namespace Envoy
