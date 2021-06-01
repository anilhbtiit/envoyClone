#include "server/active_tcp_listener.h"

#include <chrono>

#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/network/filter.h"
#include "envoy/stats/scope.h"

#include "common/event/deferred_task.h"
#include "common/network/connection_impl.h"
#include "common/network/utility.h"
#include "common/stats/timespan_impl.h"

namespace Envoy {
namespace Server {

ActiveTcpListener::ActiveTcpListener(Network::TcpConnectionHandler& parent,
                                     Network::ListenerConfig& config)
    : TypedActiveStreamListenerBase<ActiveTcpConnection>(
          parent, parent.dispatcher(),
          parent.dispatcher().createListener(config.listenSocketFactory().getListenSocket(), *this,
                                             config.bindToPort(), config.tcpBacklogSize()),
          config),
      tcp_conn_handler_(parent) {
  config.connectionBalancer().registerHandler(*this);
}

ActiveTcpListener::ActiveTcpListener(Network::TcpConnectionHandler& parent,
                                     Network::ListenerPtr&& listener,
                                     Network::ListenerConfig& config)
    : TypedActiveStreamListenerBase<ActiveTcpConnection>(parent, parent.dispatcher(),
                                                         std::move(listener), config),
      tcp_conn_handler_(parent) {
  config.connectionBalancer().registerHandler(*this);
}

ActiveTcpListener::~ActiveTcpListener() {
  config_->connectionBalancer().unregisterHandler(*this);

  cleanupConnections();

  // By the time a listener is destroyed, in the common case, there should be no connections.
  // However, this is not always true if there is an in flight rebalanced connection that is
  // being posted. This assert is extremely useful for debugging the common path so we will leave it
  // for now. If it becomes a problem (developers hitting this assert when using debug builds) we
  // can revisit. This case, if it happens, should be benign on production builds. This case is
  // covered in ConnectionHandlerTest::RemoveListenerDuringRebalance.
  ASSERT(num_listener_connections_ == 0, fmt::format("destroyed listener {} has {} connections",
                                                     config_->name(), numConnections()));
}

void ActiveTcpListener::removeConnection(ActiveTcpConnection& connection) {
  ENVOY_CONN_LOG(debug, "adding to cleanup list", *connection.connection_);
  ActiveConnections& active_connections = connection.active_connections_;
  ActiveTcpConnectionPtr removed = connection.removeFromList(active_connections.connections_);
  dispatcher().deferredDelete(std::move(removed));
  // Delete map entry only iff connections becomes empty.
  if (active_connections.connections_.empty()) {
    auto iter = connections_by_context_.find(&active_connections.filter_chain_);
    ASSERT(iter != connections_by_context_.end());
    // To cover the lifetime of every single connection, Connections need to be deferred deleted
    // because the previously contained connection is deferred deleted.
    dispatcher().deferredDelete(std::move(iter->second));
    // The erase will break the iteration over the connections_by_context_ during the deletion.
    if (!is_deleting_) {
      connections_by_context_.erase(iter);
    }
  }
}

void ActiveTcpListener::updateListenerConfig(Network::ListenerConfig& config) {
  ENVOY_LOG(trace, "replacing listener ", config_->listenerTag(), " by ", config.listenerTag());
  ASSERT(&config_->connectionBalancer() == &config.connectionBalancer());
  config_ = &config;
}

void ActiveTcpListener::onAccept(Network::ConnectionSocketPtr&& socket) {
  if (listenerConnectionLimitReached()) {
    ENVOY_LOG(trace, "closing connection: listener connection limit reached for {}",
              config_->name());
    socket->close();
    stats_.downstream_cx_overflow_.inc();
    return;
  }

  onAcceptWorker(std::move(socket), config_->handOffRestoredDestinationConnections(), false);
}

void ActiveTcpListener::onReject(RejectCause cause) {
  switch (cause) {
  case RejectCause::GlobalCxLimit:
    stats_.downstream_global_cx_overflow_.inc();
    break;
  case RejectCause::OverloadAction:
    stats_.downstream_cx_overload_reject_.inc();
    break;
  }
}

void ActiveTcpListener::onAcceptWorker(Network::ConnectionSocketPtr&& socket,
                                       bool hand_off_restored_destination_connections,
                                       bool rebalanced) {
  if (!rebalanced) {
    Network::BalancedConnectionHandler& target_handler =
        config_->connectionBalancer().pickTargetHandler(*this);
    if (&target_handler != this) {
      target_handler.post(std::move(socket));
      return;
    }
  }

  auto active_socket = std::make_unique<ActiveStreamSocket>(
      *this, std::move(socket), hand_off_restored_destination_connections);

  onSocketAccepted(std::move(active_socket));
}

Network::BalancedConnectionHandlerOptRef
ActiveTcpListener::getBalancedHandlerByAddress(const Network::Address::Instance& address) {
  return tcp_conn_handler_.getBalancedHandlerByAddress(address);
}

void ActiveTcpListener::pauseListening() {
  if (listener_ != nullptr) {
    listener_->disable();
  }
}

void ActiveTcpListener::resumeListening() {
  if (listener_ != nullptr) {
    listener_->enable();
  }
}

void ActiveTcpListener::newConnection(Network::ConnectionSocketPtr&& socket,
                                      std::unique_ptr<StreamInfo::StreamInfo> stream_info) {

  // Find matching filter chain.
  const auto filter_chain = config_->filterChainManager().findFilterChain(*socket);
  if (filter_chain == nullptr) {
    ENVOY_LOG(debug, "closing connection: no matching filter chain found");
    stats_.no_filter_chain_match_.inc();
    stream_info->setResponseFlag(StreamInfo::ResponseFlag::NoRouteFound);
    stream_info->setResponseCodeDetails(StreamInfo::ResponseCodeDetails::get().FilterChainNotFound);
    emitLogs(*config_, *stream_info);
    socket->close();
    return;
  }

  stream_info->setFilterChainName(filter_chain->name());
  auto transport_socket = filter_chain->transportSocketFactory().createTransportSocket(nullptr);
  stream_info->setDownstreamSslConnection(transport_socket->ssl());
  auto& active_connections = getOrCreateActiveConnections(*filter_chain);
  auto server_conn_ptr = dispatcher().createServerConnection(
      std::move(socket), std::move(transport_socket), *stream_info);
  if (const auto timeout = filter_chain->transportSocketConnectTimeout();
      timeout != std::chrono::milliseconds::zero()) {
    server_conn_ptr->setTransportSocketConnectTimeout(timeout);
  }
  ActiveTcpConnectionPtr active_connection(
      new ActiveTcpConnection(active_connections, std::move(server_conn_ptr),
                              dispatcher().timeSource(), std::move(stream_info)));
  active_connection->connection_->setBufferLimits(config_->perConnectionBufferLimitBytes());

  const bool empty_filter_chain = !config_->filterChainFactory().createNetworkFilterChain(
      *active_connection->connection_, filter_chain->networkFilterFactories());
  if (empty_filter_chain) {
    ENVOY_CONN_LOG(debug, "closing connection: no filters", *active_connection->connection_);
    active_connection->connection_->close(Network::ConnectionCloseType::NoFlush);
  }

  // If the connection is already closed, we can just let this connection immediately die.
  if (active_connection->connection_->state() != Network::Connection::State::Closed) {
    ENVOY_CONN_LOG(debug, "new connection", *active_connection->connection_);
    active_connection->connection_->addConnectionCallbacks(*active_connection);
    LinkedList::moveIntoList(std::move(active_connection), active_connections.connections_);
  }
}

ActiveConnections&
ActiveTcpListener::getOrCreateActiveConnections(const Network::FilterChain& filter_chain) {
  ActiveConnectionsPtr& connections = connections_by_context_[&filter_chain];
  if (connections == nullptr) {
    connections = std::make_unique<ActiveConnections>(*this, filter_chain);
  }
  return *connections;
}

void ActiveTcpListener::post(Network::ConnectionSocketPtr&& socket) {
  // It is not possible to capture a unique_ptr because the post() API copies the lambda, so we must
  // bundle the socket inside a shared_ptr that can be captured.
  // TODO(mattklein123): It may be possible to change the post() API such that the lambda is only
  // moved, but this is non-trivial and needs investigation.
  RebalancedSocketSharedPtr socket_to_rebalance = std::make_shared<RebalancedSocket>();
  socket_to_rebalance->socket = std::move(socket);

  dispatcher().post([socket_to_rebalance, tag = config_->listenerTag(),
                     &tcp_conn_handler = tcp_conn_handler_,
                     handoff = config_->handOffRestoredDestinationConnections()]() {
    auto balanced_handler = tcp_conn_handler.getBalancedHandlerByTag(tag);
    if (balanced_handler.has_value()) {
      balanced_handler->get().onAcceptWorker(std::move(socket_to_rebalance->socket), handoff, true);
      return;
    }
  });
}

ActiveConnections::ActiveConnections(ActiveTcpListener& listener,
                                     const Network::FilterChain& filter_chain)
    : listener_(listener), filter_chain_(filter_chain) {}

ActiveConnections::~ActiveConnections() {
  // connections should be defer deleted already.
  ASSERT(connections_.empty());
}

ActiveTcpConnection::ActiveTcpConnection(ActiveConnections& active_connections,
                                         Network::ConnectionPtr&& new_connection,
                                         TimeSource& time_source,
                                         std::unique_ptr<StreamInfo::StreamInfo>&& stream_info)
    : stream_info_(std::move(stream_info)), active_connections_(active_connections),
      connection_(std::move(new_connection)),
      conn_length_(new Stats::HistogramCompletableTimespanImpl(
          active_connections_.listener_.stats_.downstream_cx_length_ms_, time_source)) {
  // We just universally set no delay on connections. Theoretically we might at some point want
  // to make this configurable.
  connection_->noDelay(true);
  auto& listener = active_connections_.listener_;
  listener.stats_.downstream_cx_total_.inc();
  listener.stats_.downstream_cx_active_.inc();
  listener.per_worker_stats_.downstream_cx_total_.inc();
  listener.per_worker_stats_.downstream_cx_active_.inc();
  stream_info_->setConnectionID(connection_->id());

  // Active connections on the handler (not listener). The per listener connections have already
  // been incremented at this point either via the connection balancer or in the socket accept
  // path if there is no configured balancer.
  listener.parent_.incNumConnections();
}

ActiveTcpConnection::~ActiveTcpConnection() {
  ActiveStreamListenerBase::emitLogs(*active_connections_.listener_.config_, *stream_info_);
  auto& listener = active_connections_.listener_;
  listener.stats_.downstream_cx_active_.dec();
  listener.stats_.downstream_cx_destroy_.inc();
  listener.per_worker_stats_.downstream_cx_active_.dec();
  conn_length_->complete();

  // Active listener connections (not handler).
  listener.decNumConnections();

  // Active handler connections (not listener).
  listener.parent_.decNumConnections();
}

// Network::ConnectionCallbacks
void ActiveTcpConnection::onEvent(Network::ConnectionEvent event) {
  FANCY_LOG(info, "lambdai: tcp connection on event {}", static_cast<int>(event));

  // Any event leads to destruction of the connection.
  if (event == Network::ConnectionEvent::LocalClose ||
      event == Network::ConnectionEvent::RemoteClose) {
    active_connections_.listener_.removeConnection(*this);
  }
}

} // namespace Server
} // namespace Envoy
