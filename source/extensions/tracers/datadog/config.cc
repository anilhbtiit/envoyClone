#include "source/extensions/tracers/datadog/config.h"

#include <datadog/tracer_config.h>

#include <memory>

#include "envoy/config/trace/v3/datadog.pb.h"
#include "envoy/config/trace/v3/datadog.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/common/version/version.h"
#include "source/extensions/tracers/datadog/dd.h"
#include "source/extensions/tracers/datadog/tracer.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Datadog {

DatadogTracerFactory::DatadogTracerFactory() : FactoryBase("envoy.tracers.datadog") {}

dd::TracerConfig DatadogTracerFactory::makeConfig() {
  dd::TracerConfig config;
  config.defaults.version = "envoy " + Envoy::VersionInfo::version();
  config.defaults.name = "envoy.proxy";
  config.defaults.service = "envoy";
  return config;
}

std::string DatadogTracerFactory::makeCollectorReferenceHost(
    const envoy::config::trace::v3::DatadogConfig& proto_config) {
  std::string collector_reference_host = proto_config.collector_hostname();
  if (collector_reference_host.empty()) {
    collector_reference_host = proto_config.collector_cluster();
  }
  return collector_reference_host;
}

Tracing::DriverSharedPtr DatadogTracerFactory::createTracerDriverTyped(
    const envoy::config::trace::v3::DatadogConfig& proto_config,
    Server::Configuration::TracerFactoryContext& context) {
  return std::make_shared<Tracer>(
      proto_config.collector_cluster(), makeCollectorReferenceHost(proto_config), makeConfig(),
      context.serverFactoryContext().clusterManager(), context.serverFactoryContext().scope(),
      context.serverFactoryContext().threadLocal());
}

/**
 * Static registration for the Datadog tracer. @see RegisterFactory.
 */
REGISTER_FACTORY(DatadogTracerFactory, Server::Configuration::TracerFactory);

} // namespace Datadog
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
