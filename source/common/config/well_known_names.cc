#include "common/config/well_known_names.h"

#include "absl/strings/str_replace.h"

namespace Envoy {
namespace Config {

namespace {

std::string expandRegex(const std::string& regex) {
  return absl::StrReplaceAll(
      regex, {{"<ADDRESS>",
               R"((?:\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3}_\d+|\[[_aAbBcCdDeEfF[:digit:]]+\]_\d+))"},
              {"<CIPHER>", R"([0-9A-Za-z_-]+)"},
              {"<NAME>", R"([^\.]+)"},
              {"<ROUTE_CONFIG_NAME>", R"([\w-\.]+)"}});
}

} // namespace

TagNameValues::TagNameValues() {
  // Note: the default regexes are defined below in the order that they will typically be matched
  // (see the TagExtractor class definition for an explanation of the iterative matching process).
  // This ordering is roughly from most specific to least specific. Despite the fact that these
  // regexes are defined with a particular ordering in mind, users can customize the ordering of the
  // processing of the default tag extraction regexes and include custom tags with regexes via the
  // bootstrap configuration. Because of this flexibility, these regexes are designed to not
  // interfere with one another no matter the ordering. They are tested in forward and reverse
  // ordering to ensure they will be safe in most ordering configurations.

  // To give a more user-friendly explanation of the intended behavior of each regex, each is
  // preceded by a comment with a simplified notation to explain what the regex is designed to
  // match:
  // - The text that the regex is intended to capture will be enclosed in ().
  // - Other default tags that are expected to exist in the name (and may or may not have been
  // removed before this regex has been applied) are enclosed in [].
  // - Stand-ins for a variable segment of the name (including inside capture groups) will be
  // enclosed in <>.
  // - Typical * notation will be used to denote an arbitrary set of characters.

  // *_rq(_<response_code>)
  addRe2(RESPONSE_CODE, R"(_rq(_(\d{3}))$)", "_rq_");

  // *_rq_(<response_code_class>)xx
  addRe2(RESPONSE_CODE_CLASS, R"(_rq_((\d))xx$)", "_rq_");

  // http.[<stat_prefix>.]dynamodb.table.[<table_name>.]capacity.[<operation_name>.](__partition_id=<last_seven_characters_from_partition_id>)
  addRe2(DYNAMO_PARTITION_ID,
         R"(^http\.<NAME>\.dynamodb\.table\.<NAME>\.capacity\.<NAME>(\.__partition_id=(\w{7}))$)",
         ".dynamodb.table.");

  // http.[<stat_prefix>.]dynamodb.operation.(<operation_name>.)<base_stat> or
  // http.[<stat_prefix>.]dynamodb.table.[<table_name>.]capacity.(<operation_name>.)[<partition_id>]
  addRe2(DYNAMO_OPERATION,
         R"(^http\.<NAME>\.dynamodb.(?:operation|table\.<NAME>\.capacity)(\.(<NAME>))(?:\.|$))",
         ".dynamodb.");

  // mongo.[<stat_prefix>.]collection.[<collection>.]callsite.(<callsite>.)query.<base_stat>
  addRe2(MONGO_CALLSITE, R"(^mongo\.<NAME>\.collection\.<NAME>\.callsite\.((<NAME>)\.)query\.)",
         ".collection.");

  // http.[<stat_prefix>.]dynamodb.table.(<table_name>.) or
  // http.[<stat_prefix>.]dynamodb.error.(<table_name>.)*
  addRe2(DYNAMO_TABLE, R"(^http\.<NAME>\.dynamodb.(?:table|error)\.((<NAME>)\.))", ".dynamodb.");

  // mongo.[<stat_prefix>.]collection.(<collection>.)query.<base_stat>
  addRe2(MONGO_COLLECTION, R"(^mongo\.<NAME>\.collection\.((<NAME>)\.).*?query\.)", ".collection.");

  // mongo.[<stat_prefix>.]cmd.(<cmd>.)<base_stat>
  addRe2(MONGO_CMD, R"(^mongo\.<NAME>\.cmd\.((<NAME>)\.))", ".cmd.");

  // cluster.[<route_target_cluster>.]grpc.<grpc_service>.(<grpc_method>.)*
  addRe2(GRPC_BRIDGE_METHOD, R"(^cluster\.<NAME>\.grpc\.<NAME>\.((<NAME>)\.))", ".grpc.");

  // http.[<stat_prefix>.]user_agent.(<user_agent>.)*
  addRe2(HTTP_USER_AGENT, R"(^http\.<NAME>\.user_agent\.((<NAME>)\.))", ".user_agent.");

  // vhost.[<virtual host name>.]vcluster.(<virtual_cluster_name>.)*
  addRe2(VIRTUAL_CLUSTER, R"(^vhost\.<NAME>\.vcluster\.((<NAME>)\.))", ".vcluster.");

  // http.[<stat_prefix>.]fault.(<downstream_cluster>.)*
  addRe2(FAULT_DOWNSTREAM_CLUSTER, R"(^http\.<NAME>\.fault\.((<NAME>)\.))", ".fault.");

  // listener.[<address>.]ssl.cipher.(<cipher>)
  addRe2(SSL_CIPHER, R"(^listener\..*?\.ssl\.cipher(\.(<CIPHER>))$)");

  // cluster.[<cluster_name>.]ssl.ciphers.(<cipher>)
  addRe2(SSL_CIPHER_SUITE, R"(^cluster\.<NAME>\.ssl\.ciphers(\.(<CIPHER>))$)", ".ssl.ciphers.");

  // cluster.[<route_target_cluster>.]grpc.(<grpc_service>.)*
  addRe2(GRPC_BRIDGE_SERVICE, R"(^cluster\.<NAME>\.grpc\.((<NAME>)\.))", ".grpc.");

  // tcp.(<stat_prefix>.)<base_stat>
  addRe2(TCP_PREFIX, R"(^tcp\.((<NAME>)\.))");

  // udp.(<stat_prefix>.)<base_stat>
  addRe2(UDP_PREFIX, R"(^udp\.((<NAME>)\.))");

  // auth.clientssl.(<stat_prefix>.)*
  addRe2(CLIENTSSL_PREFIX, R"(^auth\.clientssl\.((<NAME>)\.))");

  // ratelimit.(<stat_prefix>.)*
  addRe2(RATELIMIT_PREFIX, R"(^ratelimit\.((<NAME>)\.))");

  // cluster.(<cluster_name>.)*
  addRe2(CLUSTER_NAME, R"(^cluster\.((<NAME>)\.))");

  // listener.[<address>.]http.(<stat_prefix>.)*
  addRe2(HTTP_CONN_MANAGER_PREFIX, R"(^listener\..*?\.http\.((<NAME>)\.))", ".http.");

  // http.(<stat_prefix>.)*
  addRe2(HTTP_CONN_MANAGER_PREFIX, R"(^http\.((<NAME>)\.))");

  // listener.(<address>.)*
  addRe2(LISTENER_ADDRESS, R"(^listener\.((<ADDRESS>)\.))");

  // vhost.(<virtual host name>.)*
  addRe2(VIRTUAL_HOST, R"(^vhost\.((<NAME>)\.))");

  // mongo.(<stat_prefix>.)*
  addRe2(MONGO_PREFIX, R"(^mongo\.((<NAME>)\.))");

  // http.[<stat_prefix>.]rds.(<route_config_name>.)<base_stat>
  addRe2(RDS_ROUTE_CONFIG, R"(^http\.<NAME>\.rds\.((<ROUTE_CONFIG_NAME>)\.)\w+?$)", ".rds.");

  // listener_manager.(worker_<id>.)*
  addRe2(WORKER_ID, R"(^listener_manager\.((worker_\d+)\.))", "listener_manager.worker_");
}

void TagNameValues::addRegex(const std::string& name, const std::string& regex,
                             const std::string& substr) {
  descriptor_vec_.emplace_back(Descriptor{name, expandRegex(regex), substr, Regex::Type::StdRegex});
}

void TagNameValues::addRe2(const std::string& name, const std::string& regex,
                           const std::string& substr) {
  descriptor_vec_.emplace_back(Descriptor{name, expandRegex(regex), substr, Regex::Type::Re2});
}

} // namespace Config
} // namespace Envoy
