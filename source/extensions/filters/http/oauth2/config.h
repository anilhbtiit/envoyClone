#pragma once

#include <string>

#include "envoy/extensions/filters/http/oauth2/v3alpha/oauth.pb.h"
#include "envoy/extensions/filters/http/oauth2/v3alpha/oauth.pb.validate.h"

#include "source/extensions/filters/http/common/factory_base.h"
#include "source/extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Oauth2 {

class OAuth2Config : public Extensions::HttpFilters::Common::FactoryBase<
                         envoy::extensions::filters::http::oauth2::v3alpha::OAuth2> {
public:
  OAuth2Config() : FactoryBase(HttpFilterNames::get().OAuth) {}

  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::oauth2::v3alpha::OAuth2&, const std::string&,
      Server::Configuration::FactoryContext&) override;
};

} // namespace Oauth2
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
