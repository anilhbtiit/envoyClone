#include "source/extensions/filters/network/thrift_proxy/router/shadow_writer_impl.h"

#include <memory>

#include "envoy/upstream/cluster_manager.h"
#include "envoy/upstream/thread_local_cluster.h"

#include "source/common/common/utility.h"
#include "source/extensions/filters/network/thrift_proxy/app_exception_impl.h"
#include "source/extensions/filters/network/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {
namespace Router {

bool NullResponseDecoder::onData(Buffer::Instance& data) {
  upstream_buffer_.move(data);

  bool underflow = false;
  decoder_->onData(upstream_buffer_, underflow);
  ASSERT(complete_ || underflow);
  return complete_;
}

FilterStatus NullResponseDecoder::messageBegin(MessageMetadataSharedPtr metadata) {
  metadata_ = metadata;
  first_reply_field_ =
      (metadata->hasMessageType() && metadata->messageType() == MessageType::Reply);

  return FilterStatus::Continue;
}

FilterStatus NullResponseDecoder::fieldBegin(absl::string_view, FieldType&, int16_t& field_id) {
  if (first_reply_field_) {
    // Reply messages contain a struct where field 0 is the call result and fields 1+ are
    // exceptions, if defined. At most one field may be set. Therefore, the very first field we
    // encounter in a reply is either field 0 (success) or not (IDL exception returned).
    // If first fieldType is FieldType::Stop then it is a void success and handled in messageEnd()
    // because decoder state machine does not call decoder event callback fieldBegin on
    // FieldType::Stop.
    success_ = (field_id == 0);
    first_reply_field_ = false;
  }

  return FilterStatus::Continue;
}

FilterStatus NullResponseDecoder::messageEnd() {
  if (first_reply_field_) {
    // When the response is thrift void type there is never a fieldBegin call on a success
    // because the response struct has no fields and so the first field type is FieldType::Stop.
    // The decoder state machine handles FieldType::Stop by going immediately to structEnd,
    // skipping fieldBegin callback. Therefore if we are still waiting for the first reply field
    // at end of message then it is a void success.
    success_ = true;
    first_reply_field_ = false;
  }

  return FilterStatus::Continue;
}

FilterStatus NullResponseDecoder::transportEnd() {
  ASSERT(metadata_ != nullptr);
  complete_ = true;
  // TODO: bump stats.
  return FilterStatus::Continue;
}

absl::optional<std::reference_wrapper<ShadowRequestHandle>>
ShadowWriterImpl::submit(const std::string& cluster_name, MessageMetadataSharedPtr metadata,
                         TransportType original_transport, ProtocolType original_protocol) {
  Upstream::ThreadLocalCluster* cluster = cm_.getThreadLocalCluster(cluster_name);
  if (!cluster) {
    ENVOY_LOG(debug, "cluster not found for shadow request '{}'", cluster_name);
    return absl::nullopt;
  }

  Upstream::ClusterInfoConstSharedPtr cluster_info = cluster->info();

  ENVOY_LOG(debug, "shadow request to cluster '{}', for method '{}'", cluster_name,
            metadata->methodName());

  switch (metadata->messageType()) {
  case MessageType::Call:
    incClusterScopeCounter(*cluster_info, {upstream_rq_call_});
    break;
  case MessageType::Oneway:
    incClusterScopeCounter(*cluster_info, {upstream_rq_oneway_});
    break;
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }

  if (cluster_info->maintenanceMode()) {
    ENVOY_LOG(debug, "maintenance mode for cluster '{}' during shadow request", cluster_name);
    return absl::nullopt;
  }

  const std::shared_ptr<const ProtocolOptionsConfig> options =
      cluster_info->extensionProtocolOptionsTyped<ProtocolOptionsConfig>(
          NetworkFilterNames::get().ThriftProxy);

  const TransportType transport =
      options ? options->transport(original_transport) : original_transport;
  ASSERT(transport != TransportType::Auto);

  const ProtocolType protocol = options ? options->protocol(original_protocol) : original_protocol;
  ASSERT(protocol != ProtocolType::Auto);

  auto conn_pool_data = cluster->tcpConnPool(Upstream::ResourcePriority::Default, this);
  if (!conn_pool_data) {
    ENVOY_LOG(debug, "no healthy upstream for shadow request to '{}'", cluster_name);
    return absl::nullopt;
  }

  // We are ready to go: create shadow request.
  auto request_ptr = std::make_unique<ShadowRequest>(
      *this, std::move(cluster_info), *conn_pool_data, metadata, transport, protocol);
  LinkedList::moveIntoList(std::move(request_ptr), active_requests_);
  auto& request = *active_requests_.front();
  request.start();

  return request;
}

ShadowRequest::ShadowRequest(ShadowWriterImpl& parent,
                             Upstream::ClusterInfoConstSharedPtr&& cluster_info,
                             Upstream::TcpPoolData& pool, MessageMetadataSharedPtr& metadata,
                             TransportType transport, ProtocolType protocol)
    : parent_(parent), conn_pool_data_(pool),
      transport_(NamedTransportConfigFactory::getFactory(transport).createTransport()),
      protocol_(NamedProtocolConfigFactory::getFactory(protocol).createProtocol()),
      cluster_(std::move(cluster_info)), metadata_(metadata) {
  ProtocolConverter::initProtocolConverter(*protocol_, request_buffer_);
  response_decoder_ = std::make_unique<NullResponseDecoder>(*transport_, *protocol_);
}

ShadowRequest::~ShadowRequest() {
  if (conn_pool_handle_) {
    conn_pool_handle_->cancel(Tcp::ConnectionPool::CancelPolicy::Default);
  }
}

void ShadowWriterImpl::incClusterScopeCounter(const Upstream::ClusterInfo& cluster,
                                              const Stats::StatNameVec& names) const {
  const Stats::SymbolTable::StoragePtr stat_name_storage = symbol_table_.join(names);
  cluster.statsScope().counterFromStatName(Stats::StatName(stat_name_storage.get())).inc();
}

void ShadowWriterImpl::recordClusterScopeHistogram(const Upstream::ClusterInfo& cluster,
                                                   const Stats::StatNameVec& names,
                                                   Stats::Histogram::Unit unit,
                                                   uint64_t count) const {
  const Stats::SymbolTable::StoragePtr stat_name_storage = symbol_table_.join(names);
  cluster.statsScope()
      .histogramFromStatName(Stats::StatName(stat_name_storage.get()), unit)
      .recordValue(count);
}

void ShadowRequest::start() {
  Tcp::ConnectionPool::Cancellable* handle = conn_pool_data_.newConnection(*this);
  if (handle) {
    conn_pool_handle_ = handle;
  }
}

void ShadowRequest::onPoolFailure(ConnectionPool::PoolFailureReason reason,
                                  Upstream::HostDescriptionConstSharedPtr host) {
  conn_pool_handle_ = nullptr;
  upstream_host_ = host;
  onResetStream(reason);
  maybeCleanup();
}

void ShadowRequest::onPoolReady(Tcp::ConnectionPool::ConnectionDataPtr&& conn_data,
                                Upstream::HostDescriptionConstSharedPtr host) {

  upstream_host_ = host;
  conn_data_ = std::move(conn_data);
  conn_data_->addUpstreamCallbacks(*this);
  conn_pool_handle_ = nullptr;

  conn_state_ = conn_data_->connectionStateTyped<ThriftConnectionState>();
  if (conn_state_ == nullptr) {
    conn_data_->setConnectionState(std::make_unique<ThriftConnectionState>());
    conn_state_ = conn_data_->connectionStateTyped<ThriftConnectionState>();
  }

  // Now that we have a connection, fetch the next available sequence id.
  metadata_->setSequenceId(conn_state_->nextSequenceId());

  if (request_ready_) {
    writeRequest();
  }
}

FilterStatus ShadowRequest::transportBegin(MessageMetadataSharedPtr) {
  return FilterStatus::Continue;
}

FilterStatus ShadowRequest::transportEnd() { return FilterStatus::Continue; }

void ShadowRequest::tryWriteRequest() {
  ENVOY_LOG(debug, "shadow request writing");

  if (conn_data_ != nullptr) {
    writeRequest();
  } else {
    // Wait until the connection becomes ready.
    // However, don't bother if it already failed.
    if (!reset_stream_) {
      request_ready_ = true;
    }
  }
}

// TODO: set response timeout.
void ShadowRequest::writeRequest() {
  // TODO(rgs1): is this even needed? The original upstream request does this...
  metadata_->setProtocol(protocol_->type());

  // Stitch everything together.
  Buffer::OwnedImpl message_buffer;
  protocol_->writeMessageBegin(message_buffer, *metadata_);
  message_buffer.move(request_buffer_);

  Buffer::OwnedImpl transport_buffer;
  transport_->encodeFrame(transport_buffer, *metadata_, message_buffer);

  parent_.recordClusterScopeHistogram(*cluster_, {parent_.upstream_rq_size_},
                                      Stats::Histogram::Unit::Bytes, transport_buffer.length());
  conn_data_->connection().write(transport_buffer, false);
  request_sent_ = true;

  if (metadata_->messageType() == MessageType::Oneway) {
    // No response expected
    releaseConnection(false);
    maybeCleanup();
  }
}

void ShadowRequest::onUpstreamData(Buffer::Instance& data, bool end_stream) {
  ENVOY_LOG(debug, "Shadow request upstream end_stream: {}", end_stream);

  response_size_ += data.length();

  try {
    const bool complete = response_decoder_->onData(data);
    if (complete || end_stream) {
      ENVOY_LOG(debug, "Shadow request complete: {}", complete);

      if (response_decoder_->metadata_ != nullptr) {
        parent_.recordClusterScopeHistogram(*cluster_, {parent_.upstream_resp_size_},
                                            Stats::Histogram::Unit::Bytes, response_size_);

        switch (response_decoder_->metadata_->messageType()) {
        case MessageType::Reply:
          parent_.incClusterScopeCounter(*cluster_, {parent_.upstream_resp_reply_});
          if (response_decoder_->success_.value_or(false)) {
            parent_.incClusterScopeCounter(*cluster_, {parent_.upstream_resp_reply_success_});
          } else {
            parent_.incClusterScopeCounter(*cluster_, {parent_.upstream_resp_reply_error_});
          }
          break;
        case MessageType::Exception:
          parent_.incClusterScopeCounter(*cluster_, {parent_.upstream_resp_exception_});
          break;
        default:
          NOT_REACHED_GCOVR_EXCL_LINE;
        }
      }

      releaseConnection(!complete);
      maybeCleanup();
    }
  } catch (const AppException& ex) {
    ENVOY_LOG(debug, "thrift shadow response application error: {}", ex.what());
    // TODO: bump stats.
    releaseConnection(true);
    maybeCleanup();
  } catch (const EnvoyException& ex) {
    ENVOY_LOG(debug, "thrift shadow response error: {}", ex.what());
    // TODO: bump stats.
    releaseConnection(true);
    maybeCleanup();
  }
}

bool ShadowRequest::requestInProgress() {
  // Connection open and message sent.
  if (conn_data_ != nullptr && request_sent_) {
    return true;
  }

  // Connection in progress and request buffered.
  if (conn_pool_handle_ != nullptr && request_ready_) {
    return true;
  }

  return false;
}

void ShadowRequest::tryReleaseConnection() {
  if (requestInProgress()) {
    // Mark the shadow request to be destroyed when the response gets back
    // or the upstream connection finally fails.
    original_request_done_ = true;
  } else {
    // We are done.
    releaseConnection(false);
    cleanup();
  }
}

bool ShadowRequest::waitingForConnection() const { return conn_pool_handle_ != nullptr; }

void ShadowRequest::onEvent(Network::ConnectionEvent event) {
  switch (event) {
  case Network::ConnectionEvent::RemoteClose:
    ENVOY_LOG(debug, "shadow request upstream remote close");
    onResetStream(ConnectionPool::PoolFailureReason::RemoteConnectionFailure);
    maybeCleanup();
    break;
  case Network::ConnectionEvent::LocalClose:
    ENVOY_LOG(debug, "upstream local close");
    onResetStream(ConnectionPool::PoolFailureReason::LocalConnectionFailure);
    maybeCleanup();
    break;
  default:
    // Connected is consumed by the connection pool.
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

void ShadowRequest::onResetStream(ConnectionPool::PoolFailureReason) {
  reset_stream_ = true;
  releaseConnection(false);
}

void ShadowRequest::releaseConnection(const bool close) {
  if (conn_pool_handle_) {
    conn_pool_handle_->cancel(Tcp::ConnectionPool::CancelPolicy::Default);
    conn_pool_handle_ = nullptr;
  }

  conn_state_ = nullptr;

  // The event triggered by close will also release this connection so clear conn_data_ before
  // closing.
  auto conn_data = std::move(conn_data_);
  if (close && conn_data != nullptr) {
    conn_data->connection().close(Network::ConnectionCloseType::NoFlush);
  }
}

void ShadowRequest::resetStream() { releaseConnection(true); }

void ShadowRequest::cleanup() {
  if (inserted()) {
    removeFromList(parent_.active_requests_);
  }
}

void ShadowRequest::maybeCleanup() {
  if (original_request_done_) {
    cleanup();
  }
}

} // namespace Router
} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
