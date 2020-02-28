#include "envoy/extensions/filters/listener/http_inspector/v3/http_inspector.pb.h"
#include "envoy/extensions/filters/listener/http_inspector/v3/http_inspector.pb.validate.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "extensions/filters/listener/http_inspector/http_inspector.h"
#include "extensions/filters/listener/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace HttpInspector {

/**
 * Config registration for the Http inspector filter. @see NamedNetworkFilterConfigFactory.
 */
class HttpInspectorConfigFactory : public Server::Configuration::NamedListenerFilterConfigFactory {
public:
  // NamedListenerFilterConfigFactory
  Network::ListenerFilterFactoryCb createListenerFilterFactoryFromProto(
      const Protobuf::Message&, Network::ListenerFilterConfigSharedPtr lf_config,

      Server::Configuration::ListenerFactoryContext& context) override {
    ConfigSharedPtr config(std::make_shared<Config>(context.scope()));
    return [lf_config, config](Network::ListenerFilterManager& filter_manager) -> void {
      filter_manager.addAcceptFilter(lf_config, std::make_unique<Filter>(config));
    };
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::filters::listener::http_inspector::v3::HttpInspector>();
  }

  std::string name() const override { return ListenerFilterNames::get().HttpInspector; }
};

/**
 * Static registration for the http inspector filter. @see RegisterFactory.
 */
REGISTER_FACTORY(HttpInspectorConfigFactory,
                 Server::Configuration::NamedListenerFilterConfigFactory);

} // namespace HttpInspector
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
