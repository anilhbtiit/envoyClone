#include "extensions/tracers/dynamic_ot/config.h"

#include "envoy/registry/registry.h"

#include "common/common/utility.h"
#include "common/tracing/http_tracer_impl.h"

#include "extensions/tracers/dynamic_ot/dynamic_opentracing_driver_impl.h"
#include "extensions/tracers/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace DynamicOt {

Tracing::HttpTracerPtr DynamicOpenTracingTracerFactory::createHttpTracer(
    const envoy::config::trace::v2::Tracing& configuration, Server::Instance& server) {
  ProtobufTypes::MessagePtr config_ptr =
      ProtobufTypes::MessagePtr{new envoy::config::trace::v2::DynamicOtConfig()};

  if (configuration.http().has_config()) {
    MessageUtil::jsonConvert(configuration.http().config(), *config_ptr);
  }

  const auto& dynaot_config =
      dynamic_cast<const envoy::config::trace::v2::DynamicOtConfig&>(*config_ptr);

  const std::string library = dynaot_config.library();
  Json::ObjectSharedPtr json_config = MessageUtil::getJsonObjectFromMessage(dynaot_config.config());
  const std::string config = json_config->asJsonString();
  Tracing::DriverPtr dynamic_driver{
      std::make_unique<DynamicOpenTracingDriver>(server.stats(), library, config)};
  return std::make_unique<Tracing::HttpTracerImpl>(std::move(dynamic_driver), server.localInfo());
}

std::string DynamicOpenTracingTracerFactory::name() { return TracerNames::get().DynamicOt; }

/**
 * Static registration for the dynamic opentracing tracer. @see RegisterFactory.
 */
static Registry::RegisterFactory<DynamicOpenTracingTracerFactory,
                                 Server::Configuration::TracerFactory>
    register_;

} // namespace DynamicOt
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
