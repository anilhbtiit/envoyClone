#include "source/server/active_internal_listener.h"

#include "envoy/network/filter.h"
#include "envoy/stats/scope.h"

#include "source/common/network/address_impl.h"
#include "source/common/stats/timespan_impl.h"

#include "active_stream_listener_base.h"

namespace Envoy {
namespace Server {

ActiveInternalListener::ActiveInternalListener(Network::ConnectionHandler& conn_handler,
                                               Event::Dispatcher& dispatcher,
                                               Network::ListenerConfig& config)
    : OwnedActiveStreamListenerBase(
          conn_handler, dispatcher,
          std::make_unique<ActiveInternalListener::NetworkInternalListener>(), config) {}
ActiveInternalListener::ActiveInternalListener(Network::ConnectionHandler& conn_handler,
                                               Event::Dispatcher& dispatcher,
                                               Network::ListenerPtr listener,
                                               Network::ListenerConfig& config)
    : OwnedActiveStreamListenerBase(conn_handler, dispatcher, std::move(listener), config) {}

ActiveInternalListener::~ActiveInternalListener() {
  is_deleting_ = true;
  // Purge sockets that have not progressed to connections. This should only happen when
  // a listener filter stops iteration and never resumes.
  while (!sockets_.empty()) {
    auto removed = sockets_.front()->removeFromList(sockets_);
    dispatcher().deferredDelete(std::move(removed));
  }

  for (auto& [chain, active_connections] : connections_by_context_) {
    ASSERT(active_connections != nullptr);
    auto& connections = active_connections->connections_;
    while (!connections.empty()) {
      connections.front()->connection_->close(Network::ConnectionCloseType::NoFlush);
    }
  }
  dispatcher().clearDeferredDeleteList();
}

// void ActiveInternalListener::removeConnection(ActiveInternalConnection& connection) {
//   ENVOY_CONN_LOG(debug, "adding to cleanup list", *connection.connection_);
//   ActiveConnections& active_connections = connection.active_connections_;
//   ActiveInternalConnectionPtr removed =
//   connection.removeFromList(active_connections.connections_);
//   dispatcher().deferredDelete(std::move(removed));
//   // Delete map entry only iff connections becomes empty.
//   if (active_connections.connections_.empty()) {
//     auto iter = connections_by_context_.find(&active_connections.filter_chain_);
//     ASSERT(iter != connections_by_context_.end());
//     // To cover the lifetime of every single connection, Connections need to be deferred deleted
//     // because the previously contained connection is deferred deleted.
//     dispatcher().deferredDelete(std::move(iter->second));
//     // The erase will break the iteration over the connections_by_context_ during the deletion.
//     if (!is_deleting_) {
//       connections_by_context_.erase(iter);
//     }
//   }
// }

// void ActiveInternalListener::newConnection(Network::ConnectionSocketPtr&& socket,
//                                            std::unique_ptr<StreamInfo::StreamInfo> stream_info) {

//   // Find matching filter chain.
//   const auto filter_chain = config_->filterChainManager().findFilterChain(*socket);
//   if (filter_chain == nullptr) {
//     ENVOY_LOG(debug, "closing connection: no matching filter chain found");
//     stats_.no_filter_chain_match_.inc();
//     stream_info->setResponseFlag(StreamInfo::ResponseFlag::NoRouteFound);
//     stream_info->setResponseCodeDetails(StreamInfo::ResponseCodeDetails::get().FilterChainNotFound);
//     emitLogs(*config_, *stream_info);
//     socket->close();
//     return;
//   }

//   stream_info->setFilterChainName(filter_chain->name());
//   auto transport_socket = filter_chain->transportSocketFactory().createTransportSocket(nullptr);
//   stream_info->setDownstreamSslConnection(transport_socket->ssl());
//   auto& active_connections = getOrCreateActiveConnections(*filter_chain);
//   auto server_conn_ptr = dispatcher().createServerConnection(
//       std::move(socket), std::move(transport_socket), *stream_info);
//   if (const auto timeout = filter_chain->transportSocketConnectTimeout();
//       timeout != std::chrono::milliseconds::zero()) {
//     server_conn_ptr->setTransportSocketConnectTimeout(timeout);
//   }
//   ActiveInternalConnectionPtr active_connection(
//       new ActiveInternalConnection(active_connections, std::move(server_conn_ptr),
//                                    dispatcher().timeSource(), std::move(stream_info)));
//   active_connection->connection_->setBufferLimits(config_->perConnectionBufferLimitBytes());

//   const bool empty_filter_chain = !config_->filterChainFactory().createNetworkFilterChain(
//       *active_connection->connection_, filter_chain->networkFilterFactories());
//   if (empty_filter_chain) {
//     ENVOY_CONN_LOG(debug, "closing connection: no filters", *active_connection->connection_);
//     active_connection->connection_->close(Network::ConnectionCloseType::NoFlush);
//   }

//   // If the connection is already closed, we can just let this connection immediately die.
//   if (active_connection->connection_->state() != Network::Connection::State::Closed) {
//     ENVOY_CONN_LOG(debug, "new connection", *active_connection->connection_);
//     active_connection->connection_->addConnectionCallbacks(*active_connection);
//     LinkedList::moveIntoList(std::move(active_connection), active_connections.connections_);
//   }
// }

void ActiveInternalListener::updateListenerConfig(Network::ListenerConfig& config) {
  ENVOY_LOG(trace, "replacing listener ", config_->listenerTag(), " by ", config.listenerTag());
  config_ = &config;
}

// ActiveInternalConnections::ActiveInternalConnections(ActiveInternalListener& listener,
//                                                      const Network::FilterChain& filter_chain)
//     : listener_(listener), filter_chain_(filter_chain) {}

// ActiveInternalConnections::~ActiveInternalConnections() {
//   // connections should be defer deleted already.
//   ASSERT(connections_.empty());
// }

// ActiveInternalConnection::ActiveInternalConnection(
//     ActiveInternalConnections& active_connections, Network::ConnectionPtr&& new_connection,
//     TimeSource& time_source, std::unique_ptr<StreamInfo::StreamInfo>&& stream_info)
//     : stream_info_(std::move(stream_info)), active_connections_(active_connections),
//       connection_(std::move(new_connection)),
//       conn_length_(new Stats::HistogramCompletableTimespanImpl(
//           active_connections_.listener_.stats_.downstream_cx_length_ms_, time_source)) {
//   // We just universally set no delay on connections. Theoretically we might at some point want
//   // to make this configurable.
//   connection_->noDelay(true);
//   auto& listener = active_connections_.listener_;
//   listener.stats_.downstream_cx_total_.inc();
//   listener.stats_.downstream_cx_active_.inc();
//   listener.per_worker_stats_.downstream_cx_total_.inc();
//   listener.per_worker_stats_.downstream_cx_active_.inc();
//   stream_info_->setConnectionID(connection_->id());

//   // Active connections on the handler (not listener). The per listener connections have already
//   // been incremented at this point either via the connection balancer or in the socket accept
//   // path if there is no configured balancer.
//   listener.parent_.incNumConnections();
// }

// ActiveInternalConnection::~ActiveInternalConnection() {
//   ActiveInternalListener::emitLogs(*active_connections_.listener_.config_, *stream_info_);
//   auto& listener = active_connections_.listener_;
//   listener.stats_.downstream_cx_active_.dec();
//   listener.stats_.downstream_cx_destroy_.inc();
//   listener.per_worker_stats_.downstream_cx_active_.dec();
//   conn_length_->complete();

//   // Active listener connections (not handler).
//   listener.decNumConnections();

//   // Active handler connections (not listener).
//   listener.parent_.decNumConnections();
// }

void ActiveInternalListener::onAccept(Network::ConnectionSocketPtr&& socket) {
  // Unlike tcp listener, no rebalancer is applied and won't call pickTargetHandler to account
  // connections.
  incNumConnections();

  auto active_socket = std::make_unique<ActiveTcpSocket>(
      *this, std::move(socket), false /* do not handle off at internal listener */);
  // TODO(lambdai): restore address from either socket options, or from listener config.
  active_socket->socket_->connectionInfoProvider().restoreLocalAddress(
      std::make_shared<Network::Address::Ipv4Instance>("255.255.255.255", 0));
  active_socket->socket_->connectionInfoProvider().setRemoteAddress(
      std::make_shared<Network::Address::Ipv4Instance>("255.255.255.254", 0));

  onSocketAccepted(std::move(active_socket));
}

void ActiveInternalListener::newActiveConnection(
    const Network::FilterChain& filter_chain, Network::ServerConnectionPtr server_conn_ptr,
    std::unique_ptr<StreamInfo::StreamInfo> stream_info) {
  auto& active_connections = getOrCreateActiveConnections(filter_chain);
  auto active_connection =
      std::make_unique<ActiveTcpConnection>(active_connections, std::move(server_conn_ptr),
                                            dispatcher().timeSource(), std::move(stream_info));
  // If the connection is already closed, we can just let this connection immediately die.
  if (active_connection->connection_->state() != Network::Connection::State::Closed) {
    ENVOY_CONN_LOG(debug, "new connection from {}", *active_connection->connection_,
                   active_connection->connection_->connectionInfoProvider().remoteAddress()->asString());
    active_connection->connection_->addConnectionCallbacks(*active_connection);
    LinkedList::moveIntoList(std::move(active_connection), active_connections.connections_);
  }
}
} // namespace Server
} // namespace Envoy
