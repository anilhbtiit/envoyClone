#include "source/extensions/credentials/generic/generic_impl.h"

namespace Envoy {
namespace Extensions {
namespace Credentials {
namespace Generic {

bool GenericCredentialInjector::inject(Http::RequestHeaderMap& headers, bool overwrite) {
  if (!overwrite && !headers.get(Http::LowerCaseString(header_)).empty()) {
    return false;
  }

  headers.setCopy(Http::LowerCaseString(header_), secret_reader_->credential());
  return true;
}

} // namespace Generic
} // namespace Credentials
} // namespace Extensions
} // namespace Envoy
