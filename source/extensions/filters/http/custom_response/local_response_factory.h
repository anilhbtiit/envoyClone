#pragma once

#include "source/extensions/filters/http/custom_response/policy.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CustomResponse {

class LocalResponseFactory : public PolicyFactory {
public:
  ~LocalResponseFactory() override = default;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override;

  PolicySharedPtr createPolicy(const Protobuf::Message& config,
                               Envoy::Server::Configuration::ServerFactoryContext& server,
                               Stats::StatName stats_prefix) override;

  std::string name() const override;
};

} // namespace CustomResponse
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
