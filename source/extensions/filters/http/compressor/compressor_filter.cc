#include "extensions/filters/http/compressor/compressor_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Compressor {

CompressorFilterConfig::CompressorFilterConfig(
    const envoy::extensions::filters::http::compressor::v3::Compressor& generic_compressor,
    const std::string& stats_prefix, Stats::Scope& scope, Runtime::Loader& runtime,
    CompressorFactoryPtr compressor_factory)
    : Common::Compressors::CompressorFilterConfig(
          generic_compressor, stats_prefix + compressor_factory->statsPrefix(), scope, runtime,
          compressor_factory->contentEncoding()),
      compressor_factory_(std::move(compressor_factory)) {}

std::unique_ptr<Envoy::Compressor::Compressor> CompressorFilterConfig::makeCompressor() {
  return compressor_factory_->createCompressor();
}

} // namespace Compressor
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
