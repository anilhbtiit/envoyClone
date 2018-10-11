#include "extensions/access_loggers/file/config.h"

#include "envoy/config/accesslog/v2/file.pb.validate.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "common/access_log/access_log_formatter.h"
#include "common/common/logger.h"
#include "common/protobuf/protobuf.h"

#include "extensions/access_loggers/file/file_access_log_impl.h"
#include "extensions/access_loggers/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace File {

AccessLog::InstanceSharedPtr
FileAccessLogFactory::createAccessLogInstance(const Protobuf::Message& config,
                                              AccessLog::FilterPtr&& filter,
                                              Server::Configuration::FactoryContext& context) {
  const auto& fal_config =
      MessageUtil::downcastAndValidate<const envoy::config::accesslog::v2::FileAccessLog&>(config);
  AccessLog::FormatterPtr formatter;

  if (fal_config.access_log_format_case() == envoy::config::accesslog::v2::FileAccessLog::kFormat ||
      fal_config.access_log_format_case() ==
          envoy::config::accesslog::v2::FileAccessLog::ACCESS_LOG_FORMAT_NOT_SET) {
    if (fal_config.format().empty()) {
      formatter = AccessLog::AccessLogFormatUtils::defaultAccessLogFormatter();
    } else {
      formatter.reset(new AccessLog::FormatterImpl(fal_config.format()));
    }
  } else if (fal_config.access_log_format_case() ==
             envoy::config::accesslog::v2::FileAccessLog::kJsonFormat) {
    const auto& json_format_map = this->convert_json_format_to_map(fal_config.json_format());
    formatter.reset(new AccessLog::JsonFormatterImpl(json_format_map));
  } else {
    throw EnvoyException(
        "Invalid access_log format provided. Only 'format' and 'json_format' are supported.");
  }

  return std::make_shared<FileAccessLog>(fal_config.path(), std::move(filter), std::move(formatter),
                                         context.accessLogManager());
}

ProtobufTypes::MessagePtr FileAccessLogFactory::createEmptyConfigProto() {
  return ProtobufTypes::MessagePtr{new envoy::config::accesslog::v2::FileAccessLog()};
}

std::string FileAccessLogFactory::name() const { return AccessLogNames::get().File; }

std::map<const std::string, const std::string>
FileAccessLogFactory::convert_json_format_to_map(ProtobufWkt::Struct json_format) {
  std::map<const std::string, const std::string> output;
  for (const auto& pair : json_format.fields()) {
    if (pair.second.kind_case() != ProtobufWkt::Value::kStringValue) {
      throw EnvoyException("Only string values are supported in the JSON access log format.");
    }
    output.emplace(pair.first, pair.second.string_value());
  }
  return output;
}

/**
 * Static registration for the file access log. @see RegisterFactory.
 */
static Registry::RegisterFactory<FileAccessLogFactory,
                                 Server::Configuration::AccessLogInstanceFactory>
    register_;

} // namespace File
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
