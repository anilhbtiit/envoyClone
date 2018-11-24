#include "extensions/filters/network/mysql_proxy/mysql_filter.h"
#include "extensions/filters/network/well_known_names.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/assert.h"
#include "common/common/logger.h"

#include "include/sqlparser/SQLParser.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {

class DynamicMetadataKeys {
  public:
    const std::string MessagesField{"messages"};
    const std::string OperationField{"operation"};
    const std::string ResourceField{"resource"};
};
typedef ConstSingleton<DynamicMetadataKeys> DynamicMetadataKeysSingleton;
  
MySQLFilterConfig::MySQLFilterConfig(const std::string& stat_prefix, Stats::Scope& scope)
    : scope_(scope), stat_prefix_(stat_prefix), stats_(generateStats(stat_prefix, scope)) {}

MySQLFilter::MySQLFilter(MySQLFilterConfigSharedPtr config) : config_(std::move(config)) {}

void MySQLFilter::initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) {
  read_callbacks_ = &callbacks;
}

Network::FilterStatus MySQLFilter::onWrite(Buffer::Instance& data, bool end_stream) {
  return Process(data, end_stream);
}

Network::FilterStatus MySQLFilter::onData(Buffer::Instance& data, bool end_stream) {
  return Process(data, end_stream);
}

Network::FilterStatus MySQLFilter::Process(Buffer::Instance& data, bool end_stream) {
  ENVOY_CONN_LOG(trace, "onData, len {}, end_stream {}", read_callbacks_->connection(),
                 data.length(), end_stream);
  if (!data.length()) {
    ENVOY_CONN_LOG(trace, "no data, return ", read_callbacks_->connection());
    return Network::FilterStatus::Continue;
  }

  // Run the mysql state machine
  switch (session_.GetState()) {

  // expect Server Challenge packet
  case MySQLSession::State::MYSQL_INIT: {
    ServerGreeting greeting{};
    greeting.Decode(data);
    if (greeting.GetSeq() != GREETING_SEQ_NUM) {
      config_->stats_.protocol_errors_.inc();
      break;
    }
    session_.SetState(MySQLSession::State::MYSQL_CHALLENGE_REQ);
    break;
  }

  // Process Client Handshake Response
  case MySQLSession::State::MYSQL_CHALLENGE_REQ: {
    config_->stats_.login_attempts_.inc();
    ClientLogin client_login{};
    client_login.Decode(data);
    if (client_login.GetSeq() != CHALLENGE_SEQ_NUM) {
      config_->stats_.protocol_errors_.inc();
      break;
    }
    if (client_login.IsSSLRequest()) {
      session_.SetState(MySQLSession::State::MYSQL_SSL_PT);
      config_->stats_.upgraded_to_ssl_.inc();
    } else if (client_login.IsResponse41()) {
      session_.SetState(MySQLSession::State::MYSQL_CHALLENGE_RESP_41);
    } else {
      session_.SetState(MySQLSession::State::MYSQL_CHALLENGE_RESP_320);
    }
    break;
  }

  case MySQLSession::State::MYSQL_SSL_PT:
    return Network::FilterStatus::Continue;

  case MySQLSession::State::MYSQL_CHALLENGE_RESP_41:
  case MySQLSession::State::MYSQL_CHALLENGE_RESP_320: {
    ClientLoginResponse client_login_resp{};
    client_login_resp.Decode(data);
    if (client_login_resp.GetSeq() != CHALLENGE_RESP_SEQ_NUM) {
      config_->stats_.protocol_errors_.inc();
      break;
    }
    if (client_login_resp.GetRespCode() == MYSQL_RESP_OK) {
      session_.SetState(MySQLSession::State::MYSQL_REQ);
    } else if (client_login_resp.GetRespCode() == MYSQL_RESP_AUTH_SWITCH) {
      config_->stats_.auth_switch_request_.inc();
      session_.SetState(MySQLSession::State::MYSQL_AUTH_SWITCH_RESP);
      session_.SetExpectedSeq(client_login_resp.GetSeq() + 1);
    } else if (client_login_resp.GetRespCode() == MYSQL_RESP_ERR) {
      config_->stats_.login_failures_.inc();
      session_.SetState(MySQLSession::State::MYSQL_ERROR);
    } else {
      session_.SetState(MySQLSession::State::MYSQL_NOT_HANDLED);
    }
    break;
  }

  case MySQLSession::State::MYSQL_AUTH_SWITCH_RESP: {
    ClientSwitchResponse client_switch_resp{};
    client_switch_resp.Decode(data);
    if ((client_switch_resp.GetSeq() != session_.GetExpectedSeq())) {
      config_->stats_.protocol_errors_.inc();
      break;
    }
    session_.SetState(MySQLSession::State::MYSQL_AUTH_SWITCH_MORE);
    session_.SetExpectedSeq(client_switch_resp.GetSeq() + 1);
    break;
  }

  case MySQLSession::State::MYSQL_AUTH_SWITCH_MORE: {
    ClientLoginResponse client_login_resp{};
    client_login_resp.Decode(data);
    if (client_login_resp.GetSeq() != session_.GetExpectedSeq()) {
      config_->stats_.protocol_errors_.inc();
      break;
    }
    if (client_login_resp.GetRespCode() == MYSQL_RESP_OK) {
      session_.SetState(MySQLSession::State::MYSQL_REQ);
    } else if (client_login_resp.GetRespCode() == MYSQL_RESP_MORE) {
      session_.SetState(MySQLSession::State::MYSQL_AUTH_SWITCH_RESP);
      session_.SetExpectedSeq(client_login_resp.GetSeq() + 1);
    } else if (client_login_resp.GetRespCode() == MYSQL_RESP_ERR) {
      config_->stats_.login_failures_.inc();
      session_.SetState(MySQLSession::State::MYSQL_ERROR);
    } else {
      session_.SetState(MySQLSession::State::MYSQL_NOT_HANDLED);
    }
    break;
  }

  // Process Command
  case MySQLSession::State::MYSQL_REQ: {
    Command command{};
    command.Decode(data);
    session_.SetState(MySQLSession::State::MYSQL_REQ_RESP);
    if (!command.RunQueryParser()) {
      // some mysql commands don't have a string to parse
      break;
    }
    // parse a given query
    hsql::SQLParserResult result;
    hsql::SQLParser::parse(command.GetData(), &result);

    ENVOY_CONN_LOG(warn, "mysql msg processed {}", read_callbacks_->connection(),
                   command.GetData());

    // check whether the parsing was successful
    if (result.isValid()) {
      auto& dynamic_metadata = const_cast<envoy::api::v2::core::Metadata&>(read_callbacks_->connection().streamInfo().dynamicMetadata());

      ProtobufWkt::Struct metadata((*dynamic_metadata.mutable_filter_metadata())[NetworkFilterNames::get().MySQLProxy]);
      auto& fields = *metadata.mutable_fields();
      auto& list = *fields[DynamicMetadataKeysSingleton::get().MessagesField].mutable_list_value();

      for (auto i = 0u; i < result.size(); ++i) {
        hsql::TableAccessMap table_access_map;
        result.getStatement(i)->tablesAccessed(table_access_map);
        for (auto it = table_access_map.begin(); it != table_access_map.end(); ++it) {
          auto& message = *list.add_values()->mutable_struct_value()->mutable_fields();
          message[DynamicMetadataKeysSingleton::get().ResourceField].set_string_value(it->first);
          auto& operations = *message[DynamicMetadataKeysSingleton::get().OperationField].mutable_list_value();
          for (auto ot = it->second.begin(); ot != it->second.end(); ++ot) {
            operations.add_values()->set_string_value(*ot);
          }
        }
      }

      read_callbacks_->connection().streamInfo().setDynamicMetadata(NetworkFilterNames::get().MySQLProxy, metadata);
    }
    break;
  }

  // Process Command Response
  case MySQLSession::State::MYSQL_REQ_RESP: {
    CommandResp command_resp{};
    command_resp.Decode(data);
    session_.SetState(MySQLSession::State::MYSQL_REQ);
    break;
  }

  case MySQLSession::State::MYSQL_ERROR:
  case MySQLSession::State::MYSQL_NOT_HANDLED:
  default:
    break;
  }

  ENVOY_CONN_LOG(trace, "mysql msg processed, session in state {}", read_callbacks_->connection(),
                 static_cast<int>(session_.GetState()));

  return Network::FilterStatus::Continue;
}

Network::FilterStatus MySQLFilter::onNewConnection() {
  config_->stats_.sessions_.inc();
  session_.SetId(read_callbacks_->connection().id());
  return Network::FilterStatus::Continue;
}

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
