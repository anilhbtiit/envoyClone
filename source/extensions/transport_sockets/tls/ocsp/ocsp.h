#pragma once

#include <iomanip>
#include <sstream>
#include <vector>

#include "envoy/common/exception.h"
#include "envoy/common/time.h"

#include "absl/types/optional.h"
#include "openssl/bytestring.h"
#include "openssl/ssl.h"

/**
 * Data structures and functions for unmarshaling OCSP responses
 * according to the RFC6960 B.2 spec. See: https://tools.ietf.org/html/rfc6960#appendix-B
 */

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {
namespace Ocsp {

/**
 * Reflection of the ASN.1 OcspResponseStatus enumeration.
 * The possible statuses that can accompany an OCSP response.
 */
enum class OcspResponseStatus {
  // OCSPResponseStatus ::= ENUMERATED {
  //    successful            (0),  -- Response has valid confirmations
  //    malformedRequest      (1),  -- Illegal confirmation request
  //    internalError         (2),  -- Internal error in issuer
  //    tryLater              (3),  -- Try again later
  //                                -- (4) is not used
  //    sigRequired           (5),  -- Must sign the request
  //    unauthorized          (6)   -- Request unauthorized
  // }
  Successful = 0,
  MalformedRequest = 1,
  InternalError = 2,
  TryLater = 3,
  SigRequired = 5,
  Unauthorized = 6
};

/**
 * Reflection of the ASN.1 CertStatus enumeration.
 * The status of a single SSL certificate in an OCSP response.
 */
enum class CertStatus {
  // The certificate is known to be valid
  GOOD,
  // The certificate has been revoked
  REVOKED,
  // The responder has no record of the certificate and cannot confirm its validity
  UNKNOWN
};

/**
 * Reflection of the ASN.1 CertId structure.
 * Contains the information to uniquely identify an SSL Certificate.
 * Serial numbers are guaranteed to be
 * unique per issuer but not necessarily universally.
 */
struct CertId {
  CertId(std::string serial_number, std::string alg_oid, std::string issuer_name_hash,
         std::string issuer_public_key_hash);

  std::string serial_number_;
  std::string alg_oid_;
  std::string issuer_name_hash_;
  std::string issuer_public_key_hash_;
};

/**
 * Reflection of the ASN.1 SingleResponse structure.
 * Contains information about the OCSP status of a single certificate.
 * An OCSP request may request the status of multiple certificates and
 * therefore responses may contain multiple SingleResponses.
 *
 * this_update_ and next_update_ reflect the validity period for this response.
 * If next_update_ is not present, the OCSP responder always has new information
 * available. In this case the response would be considered immediately expired
 * and invalid for stapling.
 */
struct SingleResponse {
  SingleResponse(CertId cert_id, CertStatus status, Envoy::SystemTime this_update,
                 absl::optional<Envoy::SystemTime> next_update);

  const CertId cert_id_;
  const CertStatus status_;
  const Envoy::SystemTime this_update_;
  const absl::optional<Envoy::SystemTime> next_update_;
};

/**
 * Reflection of the ASN.1 ResponseData structure.
 * Contains an OCSP response for each certificate in a given request
 * as well as the time at which the response was produced.
 */
struct ResponseData {
  ResponseData(Envoy::SystemTime produced_at, std::vector<SingleResponse> single_responses);

  const Envoy::SystemTime produced_at_;
  const std::vector<SingleResponse> single_responses_;
};

/**
 * An abstract type for OCSP response formats. Which variant of |Response| is
 * used in an |OcspResponse| is indicated by the structure's OID.
 *
 * We currently enforce that OCSP responses must be for a single certificate
 * only. The methods on this class extract the relevant information for the
 * single certificate contained in the response.
 */
class Response {
public:
  virtual ~Response() = default;

  /**
   * @return The number of certs reported on by this response.
   */
  virtual size_t getNumCerts() PURE;

  /**
   * @return The revocation status of the certificate.
   */
  virtual CertStatus getCertRevocationStatus() PURE;

  /**
   * @return The serial number of the certificate.
   */
  virtual const std::string& getCertSerialNumber() PURE;

  /**
   * @return The beginning of the validity window for this response.
   */
  virtual const Envoy::SystemTime& getThisUpdate() PURE;

  /**
   * The time at which this response is considered to expire. If
   * |nullopt|, then there is assumed to always be more up-to-date
   * information available and the response is always considered expired.
   *
   * @return The end of the validity window for this response.
   */
  virtual const absl::optional<Envoy::SystemTime>& getNextUpdate() PURE;
};

using ResponsePtr = std::unique_ptr<Response>;

/**
 * Reflection of the ASN.1 BasicOcspResponse structure.
 * Contains the full data of an OCSP response and a signature/signature
 * algorithm to verify the OCSP responder.
 *
 * BasicOcspResponse is the only supported Response type in RFC 6960.
 */
class BasicOcspResponse : public Response {
public:
  BasicOcspResponse(ResponseData data, std::string signature_alg, std::vector<uint8_t> signature);

  // Response
  size_t getNumCerts() override { return data_.single_responses_.size(); }
  CertStatus getCertRevocationStatus() override { return data_.single_responses_[0].status_; }
  const std::string& getCertSerialNumber() override {
    return data_.single_responses_[0].cert_id_.serial_number_;
  }
  const Envoy::SystemTime& getThisUpdate() override {
    return data_.single_responses_[0].this_update_;
  }
  const absl::optional<Envoy::SystemTime>& getNextUpdate() override {
    return data_.single_responses_[0].next_update_;
  }

  const static std::string OID;

private:
  const ResponseData data_;
  const std::string signature_alg_;
  const std::vector<uint8_t> signature_;
};

/**
 * Reflection of the ASN.1 OcspResponse structure.
 * This is the top-level data structure for OCSP responses.
 */
struct OcspResponse {
  OcspResponse(OcspResponseStatus status, ResponsePtr&& response);

  OcspResponseStatus status_;
  ResponsePtr response_;
};

/**
 * A wrapper used to own and query an OCSP response in DER-encoded format.
 */
class OcspResponseWrapper {
public:
  OcspResponseWrapper(std::string der_response, TimeSource& time_source);

  /**
   * @return std::string& a reference to the underlying bytestring representation
   * of the OCSP response
   */
  const std::string& rawBytes() { return raw_bytes_; }

  /**
   * @return OcspResponseStatus whether the OCSP response was successfully created
   * or a status indicating an error in the OCSP process
   */
  OcspResponseStatus getResponseStatus() { return response_->status_; }

  /**
   * @returns CertStatus for the single SSL certificate reported on by this response
   */
  CertStatus getCertRevocationStatus() { return response_->response_->getCertRevocationStatus(); }

  /**
   * @param cert a X509& SSL certificate
   * @returns bool whether this OCSP response contains the revocation status of |cert|
   */
  bool matchesCertificate(X509& cert);

  /**
   * Determines whether the OCSP response can no longer be considered valid.
   * This can be true if the nextUpdate field of the response has passed
   * or is not present, indicating that there is always more updated information
   * available.
   *
   * @returns bool if the OCSP response is expired.
   */
  bool isExpired();

private:
  const std::string raw_bytes_;
  const std::unique_ptr<OcspResponse> response_;
  TimeSource& time_source_;
};

using OcspResponseWrapperPtr = std::unique_ptr<OcspResponseWrapper>;

/**
 * ASN.1 DER-encoded parsing functions similar to Asn1Utility but specifically
 * for structures related to OCSP.
 *
 * Each function must advance `cbs` across the element it refers to.
 */
class Asn1OcspUtility {
public:
  /**
   * @param cbs a CBS& that refers to an ASN.1 OcspResponse element
   * @returns std::unique_ptr<OcspResponse> the OCSP response encoded in `cbs`
   * @throws Envoy::EnvoyException if `cbs` does not contain a well-formed OcspResponse
   * element.
   */
  static std::unique_ptr<OcspResponse> parseOcspResponse(CBS& cbs);

  /**
   * @param cbs a CBS& that refers to an ASN.1 OcspResponseStatus element
   * @returns OcspResponseStatus the OCSP response encoded in `cbs`
   * @throws Envoy::EnvoyException if `cbs` does not contain a well-formed
   * OcspResponseStatus element.
   */
  static OcspResponseStatus parseResponseStatus(CBS& cbs);

  /**
   * @param cbs a CBS& that refers to an ASN.1 Response element
   * @returns Response containing the content of an OCSP response
   * @throws Envoy::EnvoyException if `cbs` does not contain a well-formed
   * structure that is a valid Response type.
   */
  static ResponsePtr parseResponseBytes(CBS& cbs);

  /**
   * @param cbs a CBS& that refers to an ASN.1 BasicOcspResponse element
   * @returns BasicOcspResponse containing the content of an OCSP response
   * @throws Envoy::EnvoyException if `cbs` does not contain a well-formed
   * BasicOcspResponse element.
   */
  static std::unique_ptr<BasicOcspResponse> parseBasicOcspResponse(CBS& cbs);

  /**
   * @param cbs a CBS& that refers to an ASN.1 ResponseData element
   * @returns ResponseData containing the content of an OCSP response relating
   * to certificate statuses.
   * @throws Envoy::EnvoyException if `cbs` does not contain a well-formed
   * ResponseData element.
   */
  static ResponseData parseResponseData(CBS& cbs);

  /**
   * @param cbs a CBS& that refers to an ASN.1 SingleResponse element
   * @returns SingleResponse containing the id and revocation status of
   * a single certificate.
   * @throws Envoy::EnvoyException if `cbs` does not contain a well-formed
   * SingleResponse element.
   */
  static SingleResponse parseSingleResponse(CBS& cbs);

  /**
   * @param cbs a CBS& that refers to an ASN.1 CertId element
   * @returns CertId containing the information necessary to uniquely identify
   * an SSL certificate.
   * @throws Envoy::EnvoyException if `cbs` does not contain a well-formed
   * CertId element.
   */
  static CertId parseCertId(CBS& cbs);

  /**
   * @param cbs a CBS& that refers to an ASN.1 CertStatus element
   * @returns CertStatus the revocation status of a given certificate.
   * @throws Envoy::EnvoyException if `cbs` does not contain a well-formed
   * CertStatus element.
   */
  static CertStatus parseCertStatus(CBS& cbs);
};

} // namespace Ocsp
} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
