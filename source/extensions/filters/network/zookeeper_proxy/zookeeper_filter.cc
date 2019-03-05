#include "extensions/filters/network/zookeeper_proxy/zookeeper_filter.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/assert.h"
#include "common/common/fmt.h"
#include "common/common/logger.h"

#include "extensions/filters/network/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ZooKeeperProxy {

ZooKeeperFilterConfig::ZooKeeperFilterConfig(const std::string& stat_prefix, Stats::Scope& scope)
    : scope_(scope), stat_prefix_(stat_prefix), stats_(generateStats(stat_prefix, scope)) {}

ZooKeeperFilter::ZooKeeperFilter(ZooKeeperFilterConfigSharedPtr config)
    : config_(std::move(config)) {}

void ZooKeeperFilter::initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) {
  read_callbacks_ = &callbacks;
}

Network::FilterStatus ZooKeeperFilter::onData(Buffer::Instance& data, bool) {
  doDecode(data);
  return Network::FilterStatus::Continue;
}

Network::FilterStatus ZooKeeperFilter::onWrite(Buffer::Instance&, bool) {
  return Network::FilterStatus::Continue;
}

Network::FilterStatus ZooKeeperFilter::onNewConnection() { return Network::FilterStatus::Continue; }

void ZooKeeperFilter::doDecode(Buffer::Instance& buffer) {
  // Clear dynamic metadata.
  envoy::api::v2::core::Metadata& dynamic_metadata =
      read_callbacks_->connection().streamInfo().dynamicMetadata();
  auto& metadata =
      (*dynamic_metadata.mutable_filter_metadata())[NetworkFilterNames::get().ZooKeeperProxy];
  metadata.mutable_fields()->clear();

  if (!decoder_) {
    decoder_ = createDecoder(*this);
  }

  try {
    decoder_->onData(buffer);
  } catch (EnvoyException& e) {
    ENVOY_LOG(info, "zookeeper_proxy: decoding error: {}", e.what());
    config_->stats_.decoder_error_.inc();
  }
}

DecoderPtr ZooKeeperFilter::createDecoder(DecoderCallbacks& callbacks) {
  return std::make_unique<DecoderImpl>(callbacks);
}

void ZooKeeperFilter::setDynamicMetadata(const std::string& key, const std::string& value) {
  setDynamicMetadata({{key, value}});
}

void ZooKeeperFilter::setDynamicMetadata(
    const std::vector<std::pair<const std::string, const std::string>>& data) {
  envoy::api::v2::core::Metadata& dynamic_metadata =
      read_callbacks_->connection().streamInfo().dynamicMetadata();
  ProtobufWkt::Struct metadata(
      (*dynamic_metadata.mutable_filter_metadata())[NetworkFilterNames::get().ZooKeeperProxy]);
  auto& fields = *metadata.mutable_fields();

  for (const auto& pair : data) {
    auto val = ProtobufWkt::Value();
    val.set_string_value(pair.second);
    fields.insert({pair.first, val});
  }

  read_callbacks_->connection().streamInfo().setDynamicMetadata(
      NetworkFilterNames::get().ZooKeeperProxy, metadata);
}

void ZooKeeperFilter::onConnect(const bool readonly) {
  if (readonly) {
    config_->stats_.connect_readonly_rq_.inc();
    setDynamicMetadata("opname", "connect_readonly");
  } else {
    config_->stats_.connect_rq_.inc();
    setDynamicMetadata("opname", "connect");
  }
}

void ZooKeeperFilter::onDecodeError() {
  config_->stats_.decoder_error_.inc();
  setDynamicMetadata("opname", "error");
}

void ZooKeeperFilter::onPing() {
  config_->stats_.ping_rq_.inc();
  setDynamicMetadata("opname", "ping");
}

void ZooKeeperFilter::onAuthRequest(const std::string& scheme) {
  config_->scope_.counter(fmt::format("{}.auth.{}_rq", config_->stat_prefix_, scheme)).inc();
  setDynamicMetadata("opname", "auth");
}

void ZooKeeperFilter::onGetDataRequest(const std::string& path, const bool watch) {
  config_->stats_.getdata_rq_.inc();
  setDynamicMetadata({{"opname", "getdata"}, {"path", path}, {"watch", watch ? "true" : "false"}});
}

void ZooKeeperFilter::onCreateRequest(const std::string& path, const bool ephemeral,
                                      const bool sequence, const bool two) {
  if (!two) {
    config_->stats_.create_rq_.inc();
    setDynamicMetadata({{"opname", "create"},
                        {"path", path},
                        {"ephemeral", ephemeral ? "true" : "false"},
                        {"sequence", sequence ? "true" : "false"}});
  } else {
    config_->stats_.create2_rq_.inc();
    setDynamicMetadata({{"opname", "create2"},
                        {"path", path},
                        {"ephemeral", ephemeral ? "true" : "false"},
                        {"sequence", sequence ? "true" : "false"}});
  }
}

void ZooKeeperFilter::onSetRequest(const std::string& path) {
  config_->stats_.setdata_rq_.inc();
  setDynamicMetadata({{"opname", "setdata"}, {"path", path}});
}

void ZooKeeperFilter::onGetChildrenRequest(const std::string& path, const bool watch,
                                           const bool two) {
  if (!two) {
    config_->stats_.getchildren_rq_.inc();
    setDynamicMetadata(
        {{"opname", "getchildren"}, {"path", path}, {"watch", watch ? "true" : "false"}});
  } else {
    config_->stats_.getchildren2_rq_.inc();
    setDynamicMetadata(
        {{"opname", "getchildren2"}, {"path", path}, {"watch", watch ? "true" : "false"}});
  }
}

void ZooKeeperFilter::onDeleteRequest(const std::string& path, const int32_t version) {
  config_->stats_.remove_rq_.inc();
  setDynamicMetadata({{"opname", "remove"}, {"path", path}, {"version", std::to_string(version)}});
}

void ZooKeeperFilter::onExistsRequest(const std::string& path, const bool watch) {
  config_->stats_.exists_rq_.inc();
  setDynamicMetadata({{"opname", "exists"}, {"path", path}, {"watch", watch ? "true" : "false"}});
}

void ZooKeeperFilter::onGetAclRequest(const std::string& path) {
  config_->stats_.getacl_rq_.inc();
  setDynamicMetadata({{"opname", "getacl"}, {"path", path}});
}

void ZooKeeperFilter::onSetAclRequest(const std::string& path, const int32_t version) {
  config_->stats_.setacl_rq_.inc();
  setDynamicMetadata({{"opname", "setacl"}, {"path", path}, {"version", std::to_string(version)}});
}

void ZooKeeperFilter::onSyncRequest(const std::string& path) {
  config_->stats_.sync_rq_.inc();
  setDynamicMetadata({{"opname", "sync"}, {"path", path}});
}

void ZooKeeperFilter::onCheckRequest(const std::string&, const int32_t) {
  config_->stats_.check_rq_.inc();
}

void ZooKeeperFilter::onMultiRequest() {
  config_->stats_.multi_rq_.inc();
  setDynamicMetadata("opname", "multi");
}

void ZooKeeperFilter::onReconfigRequest() {
  config_->stats_.reconfig_rq_.inc();
  setDynamicMetadata("opname", "reconfig");
}

void ZooKeeperFilter::onSetWatchesRequest() {
  config_->stats_.setwatches_rq_.inc();
  setDynamicMetadata("opname", "setwatches");
}

void ZooKeeperFilter::onCloseRequest() {
  config_->stats_.close_rq_.inc();
  setDynamicMetadata("opname", "close");
}

} // namespace ZooKeeperProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
