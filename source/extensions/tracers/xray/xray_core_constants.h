#pragma once

#include <string>

#include "envoy/http/header_map.h"

#include "common/singleton/const_singleton.h"

namespace Envoy {
    namespace Extensions {
        namespace Tracers {
            namespace XRay {

                class XRayCoreConstantValues {
                public:
                    const std::string HTTP_HOST = "http.host";
                    const std::string HTTP_METHOD = "http.method";
                    const std::string HTTP_PATH = "http.path";
                    const std::string HTTP_URL = "http.url";
                    const std::string HTTP_STATUS_CODE = "http.status_code";
                    const std::string HTTP_REQUEST_SIZE = "request.size";
                    const std::string HTTP_RESPONSE_SIZE = "response.size";
                    const std::string HTTP_USER_AGENT = "user_agent";

                    const std::string ERROR = "error";

                    // XRay trace header
                    const std::string XRAY_HEADER_KEY = "X-Amzn-Trace-Id";
                    const std::string ROOT_PREFIX = "Root=";
                    const std::string PARENT_PREFIX = "Parent=";
                    const std::string SAMPLED_PREFIX = "Sampled=";
                    const std::string SELF_PREFIX = "Self=";
                    const std::string SAMPLED = "1";
                    const std::string NOT_SAMPLED = "0";
                    const std::string UNKNOWN = "";

                    // XRay headers
                    const Http::LowerCaseString XRAY_TRACE_ID{"XRay-TraceId"};
                    const Http::LowerCaseString XRAY_PARENT_ID{"XRay-ParentId"};
                    const Http::LowerCaseString XRAY_SAMPLED{"XRay-Sampled"};

                    const std::string DEFAULT_DAEMON_ENDPOINT = "127.0.0.1:2000";
                };

                typedef ConstSingleton<XRayCoreConstantValues> XRayCoreConstants;

            } // namespace XRay
        } // namespace Tracers
    } // namespace Extensions
} // namespace Envoy
