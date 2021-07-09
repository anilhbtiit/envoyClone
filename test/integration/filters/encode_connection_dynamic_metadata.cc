#include <string>

#include "envoy/http/filter.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "test/extensions/filters/http/common/empty_http_filter_config.h"
#include "test/integration/filters/common.h"

namespace Envoy {

// A test filter that adds body data to a request/response without body payload.
class EncodeConnectionDynamicMetadataStreamFilter : public Http::PassThroughFilter {
public:
  constexpr static char name[] = "encode-connection-dynamic-metadata-filter";

  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override {

    return Http::FilterHeadersStatus::Continue;
  }

  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override {
    return Http::FilterDataStatus::Continue;
  }

  Http::FilterDataStatus encodeData(Buffer::Instance& data, bool end_stream) override {
    // Ensure that encodeData is only called for HTTP/3 (where protocol is set at the
    // connection level). In HTTP/3 the FIN arrives separately so we will get
    // encodeData() with an empty body.
    ASSERT(!end_stream || decoder_callbacks_->connection()->streamInfo().protocol());
    data.add("body");
    return Http::FilterDataStatus::Continue;
  }

  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap& headers,
                                          bool end_stream) override {

    return Http::FilterHeadersStatus::Continue;
  }
};

constexpr char AddBodyStreamFilter::name[];

static Registry::RegisterFactory<SimpleFilterConfig<AddBodyStreamFilter>,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    encoder_register_;
} // namespace Envoy
