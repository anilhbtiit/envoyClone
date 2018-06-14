#pragma once

#include <string>
#include <tuple>
#include <vector>

#include "envoy/config/filter/http/header_to_metadata/v2/header_to_metadata.pb.h"
#include "envoy/server/filter_config.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace HeaderToMetadataFilter {

enum class MetadataType { String, Number };

struct MetadataKeyValue {
  std::string metadataNamespace;
  std::string key;
  std::string value;
  MetadataType type;
};

struct Rule {
  std::string header;
  MetadataKeyValue onHeaderPresent;
  MetadataKeyValue onHeaderMissing;
  bool remove;
};

typedef std::map<std::string, ProtobufWkt::Struct> StructMap;
typedef std::vector<Rule> HeaderToMetadataRules;
typedef Protobuf::RepeatedPtrField<envoy::config::filter::http::header_to_metadata::v2::Rule>
    ProtobufRepeatedRule;

/**
 *  Encapsulates the filter configuration with STL containers and provides an area for any custom
 *  configuration logic.
 */
class Config {
public:
  Config(const envoy::config::filter::http::header_to_metadata::v2::Config config);

  // getters
  HeaderToMetadataRules requestRules() const { return request_rules_; }
  HeaderToMetadataRules responseRules() const { return response_rules_; }
  bool doResponse() const { return response_set_; }
  bool doRequest() const { return request_set_; }

private:
  HeaderToMetadataRules request_rules_;
  HeaderToMetadataRules response_rules_;
  bool response_set_;
  bool request_set_;

  /**
   *  configToVector is a helper function for converting from configuration (protobuf types) into
   *  STL containers for usage elsewhere.
   *
   *  @param config A protobuf repeated field of metadata that specifies what headers to convert to
   *         metadata
   *  @param vector A vector that will be populated with the configuration data from config
   *  @return true if any configuration data was added to the vector, false otherwise. Can be used
   *          to validate whether the configuration was empty.
   */
  static bool configToVector(const ProtobufRepeatedRule&, HeaderToMetadataRules&);

  static MetadataType
  toType(const envoy::config::filter::http::header_to_metadata::v2::ValueType& vtype);
  static MetadataKeyValue
  toKeyValue(const envoy::config::filter::http::header_to_metadata::v2::KeyValuePair& keyValPair);
  static Rule toRule(const envoy::config::filter::http::header_to_metadata::v2::Rule& entry);
  const std::string& decideNamespace(const std::string& nspace) const;
};

typedef std::shared_ptr<Config> ConfigSharedPtr;

/**
 * Header-To-Metadata examines request/response headers and either copies or
 * moves the values into request metadata based on configuration information.
 */
class HeaderToMetadataFilter : public Http::StreamFilter {
public:
  HeaderToMetadataFilter(const ConfigSharedPtr config);
  ~HeaderToMetadataFilter();

  // Http::StreamFilterBase
  void onDestroy() override {}

  // StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::HeaderMap& headers, bool) override;
  Http::FilterDataStatus decodeData(Buffer::Instance&, bool) override {
    return Http::FilterDataStatus::Continue;
  }
  Http::FilterTrailersStatus decodeTrailers(Http::HeaderMap&) override {
    return Http::FilterTrailersStatus::Continue;
  }
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override;

  // StreamEncoderFilter
  Http::FilterHeadersStatus encode100ContinueHeaders(Http::HeaderMap&) override {
    return Http::FilterHeadersStatus::Continue;
  }
  Http::FilterHeadersStatus encodeHeaders(Http::HeaderMap& headers, bool) override;
  Http::FilterDataStatus encodeData(Buffer::Instance&, bool) override {
    return Http::FilterDataStatus::Continue;
  }
  Http::FilterTrailersStatus encodeTrailers(Http::HeaderMap&) override {
    return Http::FilterTrailersStatus::Continue;
  }
  void setEncoderFilterCallbacks(Http::StreamEncoderFilterCallbacks& callbacks) override;

private:
  const ConfigSharedPtr config_;
  Http::StreamDecoderFilterCallbacks* decoder_callbacks_{};
  Http::StreamEncoderFilterCallbacks* encoder_callbacks_{};

  /**
   *  writeHeaderToMetadata encapsulates (1) searching for the header and (2) writing it to the
   *  request metadata.
   *  @param headers the map of key-value headers to look through. These could be response or
   *                 request headers depending on whether this is called from the encode state or
   *                 decode state.
   *  @param rules the header-to-metadata mapping set in configuration.
   *  @param callbacks the callback used to fetch the RequestInfo (which is then used to get
   *                   metadata). Callable with both encoder_callbacks_ and decoder_callbacks_.
   */
  void writeHeaderToMetadata(Http::HeaderMap& headers, const HeaderToMetadataRules& rules,
                             Http::StreamFilterCallbacks& callbacks);
  bool addMetadata(StructMap&, const std::string&, const std::string&, const std::string&,
                   MetadataType) const;
  const std::string& decideNamespace(const std::string& nspace) const;
};

} // namespace HeaderToMetadataFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
