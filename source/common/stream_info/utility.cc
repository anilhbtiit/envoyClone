#include "source/common/stream_info/utility.h"

#include <string>

#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"

#include "absl/strings/str_format.h"

namespace Envoy {
namespace StreamInfo {

const std::string ResponseFlagUtils::toShortString(const StreamInfo& stream_info) {
  // We don't expect more than 4 flags are set. Relax to 16 since the vector is allocated on stack
  // anyway.
  absl::InlinedVector<absl::string_view, 16> flag_strings;
  for (const auto& [flag_string, flag] : ALL_RESPONSE_STRING_FLAGS) {
    if (stream_info.hasResponseFlag(flag)) {
      flag_strings.push_back(flag_string);
    }
  }
  if (flag_strings.empty()) {
    return std::string(NONE);
  }
  return absl::StrJoin(flag_strings, ",");
}

absl::flat_hash_map<std::string, ResponseFlag> ResponseFlagUtils::getFlagMap() {
  static_assert(ResponseFlag::LastFlag == 0x2000000,
                "A flag has been added. Add the new flag to ALL_RESPONSE_STRING_FLAGS.");
  absl::flat_hash_map<std::string, ResponseFlag> res;
  for (auto [str, flag] : ResponseFlagUtils::ALL_RESPONSE_STRING_FLAGS) {
    res.emplace(str, flag);
  }
  return res;
}

absl::optional<ResponseFlag> ResponseFlagUtils::toResponseFlag(absl::string_view flag) {
  // This `MapType` is introduce because CONSTRUCT_ON_FIRST_USE doesn't like template.
  using MapType = absl::flat_hash_map<std::string, ResponseFlag>;
  const auto& flag_map = []() {
    CONSTRUCT_ON_FIRST_USE(MapType, ResponseFlagUtils::getFlagMap());
  }();
  const auto& it = flag_map.find(flag);
  if (it != flag_map.end()) {
    return absl::make_optional<ResponseFlag>(it->second);
  }
  return absl::nullopt;
}

const absl::optional<uint32_t>
ProxyStatusUtils::recommendedHttpStatusCode(const ProxyStatusError proxy_status) {
  // This switch statement was derived from the mapping from proxy error type to
  // recommended HTTP status code in
  // https://datatracker.ietf.org/doc/html/draft-ietf-httpbis-proxy-status-05#section-2.3 and below.
  //
  // TODO(ambuc): Replace this with the non-draft URL when finalized.
  switch (proxy_status) {
  case ProxyStatusError::DnsTimeout:
    return 504;
  case ProxyStatusError::DnsError:
    return 502;
  case ProxyStatusError::DestinationNotFound:
    return 500;
  case ProxyStatusError::DestinationUnavailable:
    return 503;
  case ProxyStatusError::DestinationIpProhibited:
    return 502;
  case ProxyStatusError::DestinationIpUnroutable:
    return 502;
  case ProxyStatusError::ConnectionRefused:
    return 502;
  case ProxyStatusError::ConnectionTerminated:
    return 502;
  case ProxyStatusError::ConnectionTimeout:
    return 504;
  case ProxyStatusError::ConnectionReadTimeout:
    return 504;
  case ProxyStatusError::ConnectionWriteTimeout:
    return 504;
  case ProxyStatusError::ConnectionLimitReached:
    return 503;
  case ProxyStatusError::TlsProtocolError:
    return 502;
  case ProxyStatusError::TlsCertificateError:
    return 502;
  case ProxyStatusError::TlsAlertReceived:
    return 502;
  case ProxyStatusError::HttpRequestDenied:
    return 403;
  case ProxyStatusError::HttpResponseIncomplete:
    return 502;
  case ProxyStatusError::HttpResponseHeaderSectionSize:
    return 502;
  case ProxyStatusError::HttpResponseHeaderSize:
    return 502;
  case ProxyStatusError::HttpResponseBodySize:
    return 502;
  case ProxyStatusError::HttpResponseTrailerSectionSize:
    return 502;
  case ProxyStatusError::HttpResponseTrailerSize:
    return 502;
  case ProxyStatusError::HttpResponseTransferCoding:
    return 502;
  case ProxyStatusError::HttpResponseContentCoding:
    return 502;
  case ProxyStatusError::HttpResponseTimeout:
    return 504;
  case ProxyStatusError::HttpUpgradeFailed:
    return 502;
  case ProxyStatusError::HttpProtocolError:
    return 502;
  case ProxyStatusError::ProxyInternalError:
    return 500;
  case ProxyStatusError::ProxyConfigurationError:
    return 500;
  case ProxyStatusError::ProxyLoopDetected:
    return 502;
  case ProxyStatusError::ProxyInternalResponse:
  case ProxyStatusError::HttpRequestError:
  default:
    return absl::nullopt;
  }
}

const std::string
ProxyStatusUtils::toString(const StreamInfo& stream_info, const ProxyStatusError error,
                           absl::string_view server_name,
                           const envoy::extensions::filters::network::http_connection_manager::v3::
                               HttpConnectionManager::ProxyStatusConfig& proxy_status_config) {
  std::string retval = "";

  switch (proxy_status_config.proxy_name()) {
  case envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager::
      ProxyStatusConfig::SERVER_NAME: {
    retval.append(std::string(server_name));
    break;
  }
  case envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager::
      ProxyStatusConfig::ENVOY_LITERAL:
  default: {
    retval.append("envoy");
    break;
  }
  }

  retval.append(absl::StrFormat("; error=%s", proxyStatusErrorToString(error)));

  if (!proxy_status_config.remove_details() && stream_info.responseCodeDetails().has_value()) {
    retval.append(absl::StrFormat("; details='%s'", stream_info.responseCodeDetails().value()));
  }

  return retval;
}

const absl::string_view
ProxyStatusUtils::proxyStatusErrorToString(const ProxyStatusError proxy_status) {
  switch (proxy_status) {
  case ProxyStatusError::DnsTimeout:
    return DNS_TIMEOUT;
  case ProxyStatusError::DnsError:
    return DNS_ERROR;
  case ProxyStatusError::DestinationNotFound:
    return DESTINATION_NOT_FOUND;
  case ProxyStatusError::DestinationUnavailable:
    return DESTINATION_UNAVAILABLE;
  case ProxyStatusError::DestinationIpProhibited:
    return DESTINATION_IP_PROHIBITED;
  case ProxyStatusError::DestinationIpUnroutable:
    return DESTINATION_IP_UNROUTABLE;
  case ProxyStatusError::ConnectionRefused:
    return CONNECTION_REFUSED;
  case ProxyStatusError::ConnectionTerminated:
    return CONNECTION_TERMINATED;
  case ProxyStatusError::ConnectionTimeout:
    return CONNECTION_TIMEOUT;
  case ProxyStatusError::ConnectionReadTimeout:
    return CONNECTION_READ_TIMEOUT;
  case ProxyStatusError::ConnectionWriteTimeout:
    return CONNECTION_WRITE_TIMEOUT;
  case ProxyStatusError::ConnectionLimitReached:
    return CONNECTION_LIMIT_REACHED;
  case ProxyStatusError::TlsProtocolError:
    return TLS_PROTOCOL_ERORR;
  case ProxyStatusError::TlsCertificateError:
    return TLS_CERTIFICATE_ERORR;
  case ProxyStatusError::TlsAlertReceived:
    return TLS_ALERT_RECEIVED;
  case ProxyStatusError::HttpRequestError:
    return HTTP_REQUEST_ERROR;
  case ProxyStatusError::HttpRequestDenied:
    return HTTP_REQUEST_DENIED;
  case ProxyStatusError::HttpResponseIncomplete:
    return HTTP_RESPONSE_INCOMPLETE;
  case ProxyStatusError::HttpResponseHeaderSectionSize:
    return HTTP_RESPONSE_HEADER_SECTION_SIZE;
  case ProxyStatusError::HttpResponseHeaderSize:
    return HTTP_RESPONSE_HEADER_SIZE;
  case ProxyStatusError::HttpResponseBodySize:
    return HTTP_RESPONSE_BODY_SIZE;
  case ProxyStatusError::HttpResponseTrailerSectionSize:
    return HTTP_RESPONSE_TRAILER_SECTION_SIZE;
  case ProxyStatusError::HttpResponseTrailerSize:
    return HTTP_RESPONSE_TRAILER_SIZE;
  case ProxyStatusError::HttpResponseTransferCoding:
    return HTTP_RESPONSE_TRANSFER_CODING;
  case ProxyStatusError::HttpResponseContentCoding:
    return HTTP_RESPONSE_CONTENT_CODING;
  case ProxyStatusError::HttpResponseTimeout:
    return HTTP_RESPONSE_TIMEOUT;
  case ProxyStatusError::HttpUpgradeFailed:
    return HTTP_UPGRADE_FAILED;
  case ProxyStatusError::HttpProtocolError:
    return HTTP_PROTOCOL_ERROR;
  case ProxyStatusError::ProxyInternalResponse:
    return PROXY_INTERNAL_RESPONSE;
  case ProxyStatusError::ProxyInternalError:
    return PROXY_INTERNAL_ERROR;
  case ProxyStatusError::ProxyConfigurationError:
    return PROXY_CONFIGURATION_ERROR;
  case ProxyStatusError::ProxyLoopDetected:
    return PROXY_LOOP_DETECTED;
  }
  return "-";
}

const absl::optional<ProxyStatusError>
ProxyStatusUtils::fromStreamInfo(const StreamInfo& stream_info) {
  // NB: This mapping from Envoy-specific ResponseFlag enum to Proxy-Status
  // error enum is lossy, since ResponseFlag is really a bitset of many
  // ResponseFlag enums. Here, we search the list of all known ResponseFlag values in
  // enum order, returning the first matching ProxyStatusError.
  if (stream_info.hasResponseFlag(ResponseFlag::FailedLocalHealthCheck)) {
    return ProxyStatusError::DestinationUnavailable;
  } else if (stream_info.hasResponseFlag(ResponseFlag::NoHealthyUpstream)) {
    return ProxyStatusError::DestinationUnavailable;
  } else if (stream_info.hasResponseFlag(ResponseFlag::UpstreamRequestTimeout)) {
    return ProxyStatusError::ConnectionTimeout;
  } else if (stream_info.hasResponseFlag(ResponseFlag::LocalReset)) {
    return ProxyStatusError::ConnectionTimeout;
  } else if (stream_info.hasResponseFlag(ResponseFlag::UpstreamRemoteReset)) {
    return ProxyStatusError::ConnectionTerminated;
  } else if (stream_info.hasResponseFlag(ResponseFlag::UpstreamConnectionFailure)) {
    return ProxyStatusError::ConnectionRefused;
  } else if (stream_info.hasResponseFlag(ResponseFlag::UpstreamConnectionTermination)) {
    return ProxyStatusError::ConnectionTerminated;
  } else if (stream_info.hasResponseFlag(ResponseFlag::UpstreamOverflow)) {
    return ProxyStatusError::ConnectionLimitReached;
  } else if (stream_info.hasResponseFlag(ResponseFlag::NoRouteFound)) {
    return ProxyStatusError::DestinationNotFound;
  } else if (stream_info.hasResponseFlag(ResponseFlag::RateLimited)) {
    return ProxyStatusError::ConnectionLimitReached;
  } else if (stream_info.hasResponseFlag(ResponseFlag::RateLimitServiceError)) {
    return ProxyStatusError::ConnectionLimitReached;
  } else if (stream_info.hasResponseFlag(ResponseFlag::UpstreamRetryLimitExceeded)) {
    return ProxyStatusError::ConnectionTerminated;
  } else if (stream_info.hasResponseFlag(ResponseFlag::StreamIdleTimeout)) {
    return ProxyStatusError::HttpResponseTimeout;
  } else if (stream_info.hasResponseFlag(ResponseFlag::InvalidEnvoyRequestHeaders)) {
    return ProxyStatusError::HttpRequestError;
  } else if (stream_info.hasResponseFlag(ResponseFlag::DownstreamProtocolError)) {
    return ProxyStatusError::HttpRequestError;
  } else if (stream_info.hasResponseFlag(ResponseFlag::UpstreamMaxStreamDurationReached)) {
    return ProxyStatusError::HttpResponseTimeout;
  } else if (stream_info.hasResponseFlag(ResponseFlag::NoFilterConfigFound)) {
    return ProxyStatusError::ProxyConfigurationError;
  } else if (stream_info.hasResponseFlag(ResponseFlag::UpstreamProtocolError)) {
    return ProxyStatusError::HttpProtocolError;
  } else if (stream_info.hasResponseFlag(ResponseFlag::NoClusterFound)) {
    return ProxyStatusError::DestinationIpUnroutable;
  } else {
    return absl::nullopt;
  }
}

const std::string&
Utility::formatDownstreamAddressNoPort(const Network::Address::Instance& address) {
  if (address.type() == Network::Address::Type::Ip) {
    return address.ip()->addressAsString();
  } else {
    return address.asString();
  }
}

const std::string
Utility::formatDownstreamAddressJustPort(const Network::Address::Instance& address) {
  std::string port;
  if (address.type() == Network::Address::Type::Ip) {
    port = std::to_string(address.ip()->port());
  }
  return port;
}

} // namespace StreamInfo
} // namespace Envoy
