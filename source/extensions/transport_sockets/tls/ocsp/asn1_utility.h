#pragma once

#include <iomanip>
#include <sstream>
#include <vector>

#include "envoy/common/exception.h"
#include "envoy/common/time.h"

#include "common/common/assert.h"

#include "absl/types/optional.h"
#include "openssl/bn.h"
#include "openssl/bytestring.h"
#include "openssl/ssl.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {
namespace Ocsp {

/**
 * Construct a |T| from the data contained in the CBS&. Functions
 * of this type must advance the input CBS& over the element.
 */
template <typename T> using Asn1ParsingFunc = std::function<T(CBS&)>;

/**
 * Utility functions for parsing DER-encoded ASN.1 objects.
 * This relies heavily on the 'openssl/bytestring' API which
 * is BoringSSL's recommended interface for parsing DER-encoded
 * ASN.1 data when there is not an existing wrapper.
 * This is not a complete library for ASN.1 parsing and primarily
 * serves as abstractions for the OCSP module, but can be
 * extended and moved into a general utility to support parsing of
 * additional ASN.1 objects.
 *
 * Each function adheres to the invariant that given a reference
 * to a crypto bytestring (CBS&), it will parse the specified
 * ASN.1 element and advance |cbs| over it.
 *
 * An exception is thrown if the bytestring is malformed or does
 * not match the specified ASN.1 object. The position
 * of |cbs| is not reliable after an exception is thrown.
 */
class Asn1Utility {
public:
  ~Asn1Utility() = default;

  /**
   * Extracts the full contents of |cbs| as a string.
   * This copies the data in |cbs|.
   *
   * @param cbs a CBS& that refers to the current document position
   * @returns std::string containing the contents of |cbs|
   */
  static std::string cbsToString(CBS& cbs);

  /**
   * Parses all elements of an ASN.1 SEQUENCE OF. |parseElement| must
   * advance its input CBS& over the entire element.
   *
   * @param cbs a CBS& that refers to an ASN.1 SEQUENCE OF object
   * @param parseElement an Asn1ParsingFunc<T> used to parse each element
   * @returns std::vector<T> containing the parsed elements of the sequence.
   * @throws Envoy::EnvoyException if |cbs| does not point to a well-formed
   * SEQUENCE OF object.
   */
  template <typename T>
  static std::vector<T> parseSequenceOf(CBS& cbs, Asn1ParsingFunc<T> parseElement);

  /**
   * Checks if an explicitly tagged optional element of |tag| is present and
   * if so parses its value with |parseData|. If the element is not present,
   * |cbs| is not advanced.
   *
   * @param cbs a CBS& that refers to the current document position
   * @param parseData an Asn1ParsingFunc<T> used to parse the data if present
   * @return absl::optional<T> with a T if |cbs| is of the specified tag,
   * else nullopt
   */
  template <typename T>
  static absl::optional<T> parseOptional(CBS& cbs, Asn1ParsingFunc<T> parseData, unsigned tag);

  /**
   * Returns whether or not an element explicitly tagged with |tag| is present
   * at |cbs|. If so, |cbs| is advanced over the optional and assigns
   * |data| to the inner element, if |data| is not nullptr.
   * If |cbs| does not contain |tag|, |cbs| remains at the same position.
   *
   * @param cbs a CBS& that refers to the current document position
   * @param data a CBS& that is set to the contents of |cbs|
   * @param an explicit tag indicating an optional value
   *
   * @returns bool whether |cbs| points to an element tagged with |tag|
   * @throws Envoy::EnvoyException if |cbs| is a malformed TLV bytestring
   */
  static bool isOptionalPresent(CBS& cbs, CBS* data, unsigned tag);

  /**
   * @param cbs a CBS& that refers to an ASN.1 OBJECT IDENTIFIER element
   * @returns std::string the OID encoded in |cbs|
   * @throws Envoy::EnvoyException if |cbs| does not point to a well-formed
   * OBJECT IDENTIFIER
   */
  static std::string parseOid(CBS& cbs);

  /**
   * @param cbs a CBS& that refers to an ASN.1 GENERALIZEDTIME element
   * @returns Envoy::SystemTime the UTC timestamp encoded in |cbs|
   * @throws Envoy::EnvoyException if |cbs| does not point to a well-formed
   * GENERALIZEDTIME
   */
  static Envoy::SystemTime parseGeneralizedTime(CBS& cbs);

  /**
   * Parses an ASN.1 INTEGER type into its hex string representation.
   * ASN.1 INTEGER types are arbitrary precision.
   * If you're SURE the integer fits into a fixed-size int,
   * use CBS_get_asn1_* functions for the given integer type instead.
   *
   * @param cbs a CBS& that refers to an ASN.1 INTEGER element
   * @returns std::string a hex representation of the integer
   * @throws Envoy::EnvoyException if |cbs| does not point to a well-formed
   * INTEGER
   */
  static std::string parseInteger(CBS& cbs);

  /**
   * Parses an ASN.1 AlgorithmIdentifier. Currently ignores algorithm params
   * and only returns the OID of the algorithm.
   *
   * @param cbs a CBS& that refers to an ASN.1 AlgorithmIdentifier element
   * @returns std::string the OID of the algorithm
   * @throws Envoy::EnvoyException if |cbs| does not point to a well-formed
   * AlgorithmIdentifier
   */
  static std::string parseAlgorithmIdentifier(CBS& cbs);

  /**
   * @param cbs a CBS& that refers to an ASN.1 OCTETSTRING element
   * @returns std::string of the octets in |cbs|
   * @throws Envoy::EnvoyException if |cbs| does not point to a well-formed
   * OCTETSTRING
   */
  static std::string parseOctetString(CBS& cbs);

  /**
   * Parses an ASN.1 BITSTRING into a byte vector. The first byte
   * of the vector indicates the number of unused bits at the end of
   * the last byte. The second byte up through part of the last byte
   * contain the contents of the bit string.
   *
   * @param cbs a CBS& that refers to an ASN.1 BITSTRING element
   * @returns std::vector<uint8_t> of the bitstring packed into bytes.
   * @throws Envoy::EnvoyException if |cbs| does not point to a well-formed BITSTRING
   */
  static std::vector<uint8_t> parseBitString(CBS& cbs);

  /**
   * Advance |cbs| over an ASN.1 value of the class |tag| if that
   * value is present. Otherwise, |cbs| stays in the same position.
   *
   * @param cbs a CBS& that refers to the current document position
   * @param tag the tag of the value to skip
   * @throws Envoy::EnvoyException if |cbs| is a malformed TLV bytestring
   */
  static void skipOptional(CBS& cbs, unsigned tag);
};

template <typename T>
std::vector<T> Asn1Utility::parseSequenceOf(CBS& cbs, Asn1ParsingFunc<T> parseElement) {
  CBS seq_elem;
  std::vector<T> vec;

  // Initialize seq_elem to first element in sequence.
  if (!CBS_get_asn1(&cbs, &seq_elem, CBS_ASN1_SEQUENCE)) {
    throw Envoy::EnvoyException("Expected sequence of ASN.1 elements.");
  }

  while (CBS_data(&seq_elem) < CBS_data(&cbs)) {
    // parseElement MUST advance seq_elem
    vec.push_back(parseElement(seq_elem));
  }

  RELEASE_ASSERT(CBS_data(&cbs) == CBS_data(&seq_elem),
                 "Sequence tag length must match actual length or element parsing would fail");

  return vec;
}

template <typename T>
absl::optional<T> Asn1Utility::parseOptional(CBS& cbs, Asn1ParsingFunc<T> parseData, unsigned tag) {
  CBS data;
  if (isOptionalPresent(cbs, &data, tag)) {
    return parseData(data);
  }

  return absl::nullopt;
}

} // namespace Ocsp
} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
