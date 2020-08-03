#include "test/extensions/filters/listener/common/fuzz/listener_filter_fakes.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {

Network::IoHandle& FakeConnectionSocket::ioHandle() { return *io_handle_; }

const Network::IoHandle& FakeConnectionSocket::ioHandle() const { return *io_handle_; }

void FakeConnectionSocket::setLocalAddress(
    const Network::Address::InstanceConstSharedPtr& local_address) {
  local_address_ = local_address;
  if (local_address_ != nullptr) {
    addr_type_ = local_address_->type();
  }
}

void FakeConnectionSocket::setRemoteAddress(
    const Network::Address::InstanceConstSharedPtr& remote_address) {
  remote_address_ = remote_address;
}

const Network::Address::InstanceConstSharedPtr& FakeConnectionSocket::localAddress() const {
  return local_address_;
}

const Network::Address::InstanceConstSharedPtr& FakeConnectionSocket::remoteAddress() const {
  return remote_address_;
}

Network::Address::Type FakeConnectionSocket::addressType() const { return addr_type_; }

absl::optional<Network::Address::IpVersion> FakeConnectionSocket::ipVersion() const {
  if (local_address_ == nullptr || addr_type_ != Network::Address::Type::Ip) {
    return absl::nullopt;
  }

  return local_address_->ip()->version();
}

void FakeConnectionSocket::setRequestedApplicationProtocols(
    const std::vector<absl::string_view>& protocols) {
  application_protocols_.clear();
  for (const auto& protocol : protocols) {
    application_protocols_.emplace_back(protocol);
  }
}

const std::vector<std::string>& FakeConnectionSocket::requestedApplicationProtocols() const {
  return application_protocols_;
}

Api::SysCallIntResult FakeConnectionSocket::getSocketOption(int level, int, void* optval,
                                                            socklen_t*) const {
  switch (level) {
  case SOL_IPV6:
    static_cast<sockaddr_storage*>(optval)->ss_family = AF_INET6;
    break;
  case SOL_IP:
    static_cast<sockaddr_storage*>(optval)->ss_family = AF_INET;
    break;
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }

  return Api::SysCallIntResult{0, 0};
}

} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
