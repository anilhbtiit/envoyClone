#include "source/server/hot_restarting_child.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/utility.h"
#include "source/common/network/utility.h"

namespace Envoy {
namespace Server {

using HotRestartMessage = envoy::HotRestartMessage;

void HotRestartingChild::UdpForwardingContext::registerListener(
    Network::Address::InstanceConstSharedPtr address,
    std::shared_ptr<Network::UdpListenerConfig> listener_config) {
  const bool inserted =
      listener_map_.try_emplace(address->asString(), ForwardEntry{address, listener_config}).second;
  ASSERT(inserted, "Two udp listeners on the same address shouldn't be possible");
}

absl::optional<HotRestartingChild::UdpForwardingContext::ForwardEntry>
HotRestartingChild::UdpForwardingContext::getListenerForDestination(
    const Network::Address::Instance& address) {
  auto it = listener_map_.find(address.asString());
  if (it == listener_map_.end()) {
    // If no listener on the specific address was found, check for a default route.
    // If the address is IPv6, check default route IPv6 only, otherwise check default
    // route IPv4 then default route IPv6, as either can potentially receive an IPv4
    // packet.
    uint32_t port = address.ip()->port();
    if (address.ip()->version() == Network::Address::IpVersion::v6) {
      it = listener_map_.find(absl::StrCat("[::]:", port));
    } else {
      it = listener_map_.find(absl::StrCat("0.0.0.0:", port));
      if (it == listener_map_.end()) {
        it = listener_map_.find(absl::StrCat("[::]:", port));
        if (it != listener_map_.end() && it->second.first->ip()->ipv6()->v6only()) {
          // If there is a default IPv6 route but it's set v6only, don't use it.
          it = listener_map_.end();
        }
      }
    }
  }
  if (it == listener_map_.end()) {
    return absl::nullopt;
  }
  return it->second;
}

HotRestartingChild::HotRestartingChild(int base_id, int restart_epoch,
                                       const std::string& socket_path, mode_t socket_mode)
    : HotRestartingBase(base_id), restart_epoch_(restart_epoch) {
  main_rpc_stream_.initDomainSocketAddress(&parent_address_);
  std::string socket_path_udp = socket_path + "_udp";
  udp_forwarding_rpc_stream_.initDomainSocketAddress(&parent_address_udp_forwarding_);
  if (restart_epoch_ != 0) {
    parent_address_ = main_rpc_stream_.createDomainSocketAddress(restart_epoch_ + -1, "parent",
                                                                 socket_path, socket_mode);
    parent_address_udp_forwarding_ = udp_forwarding_rpc_stream_.createDomainSocketAddress(
        restart_epoch_ + -1, "parent", socket_path_udp, socket_mode);
  }
  main_rpc_stream_.bindDomainSocket(restart_epoch_, "child", socket_path, socket_mode);
  udp_forwarding_rpc_stream_.bindDomainSocket(restart_epoch_, "child", socket_path_udp,
                                              socket_mode);
}

void HotRestartingChild::initialize(Event::Dispatcher& dispatcher) {
  socket_event_udp_forwarding_ = dispatcher.createFileEvent(
      udp_forwarding_rpc_stream_.domain_socket_,
      [this](uint32_t events) -> void {
        ASSERT(events == Event::FileReadyType::Read);
        onSocketEventUdpForwarding();
      },
      Event::FileTriggerType::Edge, Event::FileReadyType::Read);
}

void HotRestartingChild::shutdown() { socket_event_udp_forwarding_.reset(); }

void HotRestartingChild::onForwardedUdpPacket(uint32_t worker_index, Network::UdpRecvData&& data) {
  auto addr_and_listener =
      udp_forwarding_context_.getListenerForDestination(*data.addresses_.local_);
  if (addr_and_listener.has_value()) {
    auto [addr, listener_config] = *addr_and_listener;
    listener_config->listenerWorkerRouter(*addr).deliver(worker_index, std::move(data));
  }
}

int HotRestartingChild::duplicateParentListenSocket(const std::string& address,
                                                    uint32_t worker_index) {
  if (restart_epoch_ == 0 || parent_terminated_) {
    return -1;
  }

  HotRestartMessage wrapped_request;
  wrapped_request.mutable_request()->mutable_pass_listen_socket()->set_address(address);
  wrapped_request.mutable_request()->mutable_pass_listen_socket()->set_worker_index(worker_index);
  main_rpc_stream_.sendHotRestartMessage(parent_address_, wrapped_request);

  std::unique_ptr<HotRestartMessage> wrapped_reply =
      main_rpc_stream_.receiveHotRestartMessage(RpcStream::Blocking::Yes);
  if (!main_rpc_stream_.replyIsExpectedType(wrapped_reply.get(),
                                            HotRestartMessage::Reply::kPassListenSocket)) {
    return -1;
  }
  return wrapped_reply->reply().pass_listen_socket().fd();
}

std::unique_ptr<HotRestartMessage> HotRestartingChild::getParentStats() {
  if (restart_epoch_ == 0 || parent_terminated_) {
    return nullptr;
  }

  HotRestartMessage wrapped_request;
  wrapped_request.mutable_request()->mutable_stats();
  main_rpc_stream_.sendHotRestartMessage(parent_address_, wrapped_request);

  std::unique_ptr<HotRestartMessage> wrapped_reply =
      main_rpc_stream_.receiveHotRestartMessage(RpcStream::Blocking::Yes);
  RELEASE_ASSERT(
      main_rpc_stream_.replyIsExpectedType(wrapped_reply.get(), HotRestartMessage::Reply::kStats),
      "Hot restart parent did not respond as expected to get stats request.");
  return wrapped_reply;
}

void HotRestartingChild::drainParentListeners() {
  if (restart_epoch_ == 0 || parent_terminated_) {
    return;
  }
  // No reply expected.
  HotRestartMessage wrapped_request;
  wrapped_request.mutable_request()->mutable_drain_listeners();
  main_rpc_stream_.sendHotRestartMessage(parent_address_, wrapped_request);
}

void HotRestartingChild::registerUdpForwardingListener(
    Network::Address::InstanceConstSharedPtr address,
    std::shared_ptr<Network::UdpListenerConfig> listener_config) {
  ASSERT_IS_MAIN_OR_TEST_THREAD();
  udp_forwarding_context_.registerListener(address, listener_config);
}

absl::optional<HotRestart::AdminShutdownResponse>
HotRestartingChild::sendParentAdminShutdownRequest() {
  if (restart_epoch_ == 0 || parent_terminated_) {
    return absl::nullopt;
  }

  HotRestartMessage wrapped_request;
  wrapped_request.mutable_request()->mutable_shutdown_admin();
  main_rpc_stream_.sendHotRestartMessage(parent_address_, wrapped_request);

  std::unique_ptr<HotRestartMessage> wrapped_reply =
      main_rpc_stream_.receiveHotRestartMessage(RpcStream::Blocking::Yes);
  RELEASE_ASSERT(main_rpc_stream_.replyIsExpectedType(wrapped_reply.get(),
                                                      HotRestartMessage::Reply::kShutdownAdmin),
                 "Hot restart parent did not respond as expected to ShutdownParentAdmin.");
  return HotRestart::AdminShutdownResponse{
      static_cast<time_t>(
          wrapped_reply->reply().shutdown_admin().original_start_time_unix_seconds()),
      wrapped_reply->reply().shutdown_admin().enable_reuse_port_default()};
}

void HotRestartingChild::sendParentTerminateRequest() {
  if (restart_epoch_ == 0 || parent_terminated_) {
    return;
  }
  HotRestartMessage wrapped_request;
  wrapped_request.mutable_request()->mutable_terminate();
  main_rpc_stream_.sendHotRestartMessage(parent_address_, wrapped_request);
  parent_terminated_ = true;

  // Note that the 'generation' counter needs to retain the contribution from
  // the parent.
  stat_merger_->retainParentGaugeValue(hot_restart_generation_stat_name_);

  // Now it is safe to forget our stat transferral state.
  //
  // This destruction is actually important far beyond memory efficiency. The
  // scope-based temporary counter logic relies on the StatMerger getting
  // destroyed once hot restart's stat merging is all done. (See stat_merger.h
  // for details).
  stat_merger_.reset();
}

void HotRestartingChild::mergeParentStats(Stats::Store& stats_store,
                                          const HotRestartMessage::Reply::Stats& stats_proto) {
  if (!stat_merger_) {
    stat_merger_ = std::make_unique<Stats::StatMerger>(stats_store);
    hot_restart_generation_stat_name_ = hotRestartGeneration(*stats_store.rootScope()).statName();
  }

  // Convert the protobuf for serialized dynamic spans into the structure
  // required by StatMerger.
  Stats::StatMerger::DynamicsMap dynamics;
  for (const auto& iter : stats_proto.dynamics()) {
    Stats::DynamicSpans& spans = dynamics[iter.first];
    for (int i = 0; i < iter.second.spans_size(); ++i) {
      const HotRestartMessage::Reply::Span& span_proto = iter.second.spans(i);
      spans.push_back(Stats::DynamicSpan(span_proto.first(), span_proto.last()));
    }
  }
  stat_merger_->mergeStats(stats_proto.counter_deltas(), stats_proto.gauges(), dynamics);
}

void HotRestartingChild::onSocketEventUdpForwarding() {
  std::unique_ptr<HotRestartMessage> wrapped_request;
  while ((wrapped_request =
              udp_forwarding_rpc_stream_.receiveHotRestartMessage(RpcStream::Blocking::No))) {
    if (wrapped_request->requestreply_case() == HotRestartMessage::kReply) {
      ENVOY_LOG(
          error,
          "HotRestartMessage reply received on UdpForwarding (we want only requests); ignoring.");
      continue;
    }
    switch (wrapped_request->request().request_case()) {
    case HotRestartMessage::Request::kForwardedUdpPacket: {
      const auto& req = wrapped_request->request().forwarded_udp_packet();
      Network::UdpRecvData data;
      data.addresses_.local_ = Network::Utility::parseInternetAddressAndPort(req.local_addr());
      data.addresses_.peer_ = Network::Utility::parseInternetAddressAndPort(req.peer_addr());
      data.receive_time_ =
          MonotonicTime(std::chrono::microseconds{req.receive_time_epoch_microseconds()});
      data.buffer_ = std::make_unique<Buffer::OwnedImpl>(req.packet());
      onForwardedUdpPacket(req.worker_index(), std::move(data));
      break;
    }
    default: {
      ENVOY_LOG(
          error,
          "child sent a request other than ForwardedUdpPacket on udp forwarding socket; ignoring.");
      break;
    }
    }
  }
}

} // namespace Server
} // namespace Envoy
