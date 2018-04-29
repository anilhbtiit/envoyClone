#pragma once

#include <functional>
#include <string>

#include "envoy/buffer/buffer.h"
#include "envoy/common/pure.h"
#include "envoy/http/codes.h"
#include "envoy/http/filter.h"
#include "envoy/http/header_map.h"
#include "envoy/network/listen_socket.h"
#include "envoy/server/config_tracker.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Server {

class AdminStream {
public:
  virtual ~AdminStream() {}

  /**
   * @param end_stream set to false for streaming response. Default is true, which will close
   * connection on socket
   */
  virtual void setEndStreamOnComplete(const bool& end_stream) PURE;

  /**
   * @param cb callback to be added to the list of callbacks invoked by onDestroy() when connection
   * is dropped
   */
  virtual void addOnDestroyCallback(std::function<void()> cb) PURE;

  /**
   * @return Http::StreamDecoderFilterCallbacks* to be used by the handler to get socket for data
   * streaming
   */
  virtual const Http::StreamDecoderFilterCallbacks& getDecoderFilterCallbacks() const PURE;

  /**
   * @return Http::HeaderMap* to be used by handler to parse header information sent with the
   * request
   */
  virtual const Http::HeaderMap& getRequestHeaders() const PURE;
};
/**
 * This macro is used to add handlers to the Admin HTTP Endpoint. It builds
 * a callback that executes X when the specified admin handler is hit. This macro can be
 * used to add static handlers as in source/server/http/admin.cc and also dynamic handlers as
 * done in the RouteConfigProviderManagerImpl constructor in source/common/router/rds_impl.cc.
 */
#define MAKE_ADMIN_HANDLER(X)                                                                      \
  [this](absl::string_view path_and_query, Http::HeaderMap& response_headers,                      \
         Buffer::Instance& data, AdminStream& admin_filter) -> Http::Code {                        \
    return X(path_and_query, response_headers, data, admin_filter);                                \
  }

/**
 * Global admin HTTP endpoint for the server.
 */
class Admin {
public:
  virtual ~Admin() {}

  /**
   * Callback for admin URL handlers.
   * @param path_and_query supplies the URL prefix to install the handler for.
   * @param response_headers enables setting of http headers (eg content-type, cache-control) in the
   * handler.
   * @param response supplies the buffer to fill in with the response body.
   * @param admin_stream supplies the filter which invoked the handler, enables the handler to use
   * its data
   * @return Http::Code the response code.
   */
  typedef std::function<Http::Code(absl::string_view path_and_query,
                                   Http::HeaderMap& response_headers, Buffer::Instance& response,
                                   AdminStream& admin_stream)>

      HandlerCb;

  /**
   * Add an admin handler.
   * @param prefix supplies the URL prefix to handle.
   * @param help_text supplies the help text for the handler.
   * @param callback supplies the callback to invoke when the prefix matches.
   * @param removable if true allows the handler to be removed via removeHandler.
   * @param mutates_server_state indicates whether callback will mutate server state.
   * @return bool true if the handler was added, false if it was not added.
   */
  virtual bool addHandler(const std::string& prefix, const std::string& help_text,
                          HandlerCb callback, bool removable, bool mutates_server_state) PURE;

  /**
   * Remove an admin handler if it is removable.
   * @param prefix supplies the URL prefix of the handler to delete.
   * @return bool true if the handler was removed, false if it was not removed.
   */
  virtual bool removeHandler(const std::string& prefix) PURE;

  /**
   * Obtain socket the admin endpoint is bound to.
   * @return Network::Socket& socket reference.
   */
  virtual const Network::Socket& socket() PURE;

  /**
   * @return ConfigTracker& tracker for /config_dump endpoint.
   */
  virtual ConfigTracker& getConfigTracker() PURE;
};

} // namespace Server
} // namespace Envoy
