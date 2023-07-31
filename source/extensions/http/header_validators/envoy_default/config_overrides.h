#pragma once

#include "envoy/server/factory_context.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace HeaderValidators {
namespace EnvoyDefault {

struct ConfigOverrides {
  ConfigOverrides() = default;
  ConfigOverrides(const Envoy::Runtime::Snapshot& snapshot)
      : preserve_url_encoded_case_(
            snapshot.getBoolean("envoy.uhv.preserve_url_encoded_case", true)),
        allow_non_compliant_characters_in_path_(snapshot.getBoolean("envoy.uhv.allow_non_compliant_characters_in_path", true)) {}

  // This flag enables preservation of the case of percent-encoded triplets in URL path for
  // compatibility with legacy path normalization.
  // https://datatracker.ietf.org/doc/html/rfc3986#section-2.1 mandates that uppercase
  // hexadecimal digits (A through F) are equivalent to lowercase.
  // However to make path matching of percent-encoded triplets easier path normalization changes all
  // hexadecimal digits to uppercase.
  //
  // This option currently is `true` by default and can be overridden using the
  // "envoy.uhv.preserve_url_encoded_case" runtime value. Note that the default value
  // will be changed to `false` in the future to make it easier to write path matchers that
  // look for percent-encoded triplets.
  const bool preserve_url_encoded_case_{true};

  // This flag enables validation of the :path header charcter set compatible with legacy Envoy codecs.
  // When this flag is false header validator checks the URL path in accordance with
  // the https://datatracker.ietf.org/doc/html/rfc3986#section-3.3 RFC.
  //
  // This option currently is `true` by default and can be overridden using the
  // "envoy.uhv.allow_non_compliant_characters_in_path" runtime value. Note that the default value
  // will be changed to `false` in the future to make Envoy behavior standard compliant and
  // consistent across all HTTP protocol versions.
  //
  // In the relaxed mode header validator allows the following additional characters:
  // HTTP/1 protocol: " < > [ ] ^ ` { } \ | #
  // HTTP/2 and HTTP/3 protocols: all characters allowed for HTTP/1, space, TAB, all extended ASCII
  // (>= 0x80)
  //
  // In addition when this flag is true AND path normalization is enabled, Envoy will do the
  // following:
  // 1. all additionally allowed characters with the exception of the [] and \ characters are percent
  // encoded in the path segment of the URL only. These characters in query or fragment will remain unencoded.
  // 2. \ character is translated to / in path segment.
  //
  // This option provides backward compatibility with the existing (pre header validator) Envoy
  // behavior. Envoy's legacy codecs were not compliant with the
  // https://datatracker.ietf.org/doc/html/rfc3986#section-3.3
  //
  // With the `envoy.uhv.allow_non_compliant_characters_in_path` set to false the header validator
  // rejects requests with characters not allowed by the RFC in the :path header.
  const bool allow_non_compliant_characters_in_path_{true};
};

} // namespace EnvoyDefault
} // namespace HeaderValidators
} // namespace Http
} // namespace Extensions
} // namespace Envoy
