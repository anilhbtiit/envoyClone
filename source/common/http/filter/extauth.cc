#include "common/http/filter/extauth.h"

#include "common/common/assert.h"
#include "common/common/enum_to_int.h"
#include "common/http/message_impl.h"
#include "common/http/utility.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Http {

namespace {

const LowerCaseString header_to_add() {
  CONSTRUCT_ON_FIRST_USE(LowerCaseString, "x-ambassador-calltype");
}

const std::string value_to_add() { CONSTRUCT_ON_FIRST_USE(std::string, "extauth-request"); }

} // namespace

ExtAuth::ExtAuth(ExtAuthConfigConstSharedPtr config) : config_(std::move(config)) {}

ExtAuth::~ExtAuth() { ASSERT(!auth_request_); }

void ExtAuth::dumpHeaders(const char* what, HeaderMap* headers) {
#ifndef NVLOG
  ENVOY_STREAM_LOG(trace, "ExtAuth headers ({}):", *callbacks_, what);
  if (headers) {
    headers->iterate(
        [](const HeaderEntry& header, void* context) -> HeaderMap::Iterate {
          ENVOY_STREAM_LOG(trace, "  '{}':'{}'",
                           *static_cast<StreamDecoderFilterCallbacks*>(context),
                           header.key().c_str(), header.value().c_str());
          return HeaderMap::Iterate::Continue;
        },
        static_cast<void*>(callbacks_));
  }
#endif
}

// TODO(gsagula): at the end of this PR, most of the comments inside member functions should be
// removed.
FilterHeadersStatus ExtAuth::decodeHeaders(HeaderMap& headers, bool) {

  // decodeHeaders is called at the point that the HTTP machinery handling
  // the request has parsed the HTTP headers for this request.
  // Our primary job here is to construct the request to the auth service
  // and start it executing, but we also have to be sure to save a pointer
  // to the incoming request headers in case we need to modify them in
  // flight.

  // Remember that we have _not_ finished talking to the auth service...

  auth_complete_ = false;

  // ...and hang onto a pointer to the original request headers.
  //
  // Note that using a reference here won't work. Move semantics are the
  // root of this issue, I think.

  request_headers_ = &headers;

  // Debugging.
  dumpHeaders("decodeHeaders", request_headers_);

  // OK, time to get the auth-service request set up. Create a
  // RequestMessageImpl to hold all the details, and start it off as a
  // copy of the incoming request's headers.

  MessagePtr request_message{new RequestMessageImpl{HeaderMapPtr{new HeaderMapImpl{headers}}}};

  // We do need to tweak a couple of things. To start with, has a change
  // to the path we hand to the auth service been configured?

  if (!config_->path_prefix_.empty()) {
    // Yes, it has. Go ahead and prepend it to the request_message path.
    std::string path;
    absl::StrAppend(&path, config_->path_prefix_,
                    request_message->headers().insertPath().value().getString());
    request_message->headers().insertPath().value(path);
  }

  // https://github.com/datawire/ambassador/issues/154
  // We used to reset the Host: header to match the cluster name we're about
  // to send the auth request to. That, however, causes trouble for anyone
  // who wants to make auth decisions based on the host to which the client
  // started out trying to talk to.
  //
  // We may need to make this configurable later, so I'm leaving this line
  // in for reference.
  // request_message->headers().insertHost().value(config_->cluster_);

  // After setting up whatever headers we need to, make sure the body is
  // correctly marked as empty.

  request_message->headers().insertContentLength().value(uint64_t(0));

  // Finally, we mark the request as being an Ambassador auth request.

  request_message->headers().addReference(header_to_add(), value_to_add());

  // Fire the request up. When it's finished, we'll get a call to
  // either onSuccess() or onFailure().

  ENVOY_STREAM_LOG(trace, "ExtAuth contacting auth server", *callbacks_);

  auth_request_ = config_->cm_.httpAsyncClientForCluster(config_->cluster_)
                      .send(std::move(request_message), *this,
                            Optional<std::chrono::milliseconds>(config_->timeout_));

  // It'll take some time for our auth call to complete. Stop
  // filtering while we wait for it.

  return FilterHeadersStatus::StopIteration;
}

FilterDataStatus ExtAuth::decodeData(Buffer::Instance&, bool) {
  // decodeHeaders is called at the point that the HTTP machinery handling
  // the request has parsed the HTTP body for this request. We don't need
  // to do anything special here; we just need to make sure that we don't
  // let things proceed until our auth call is done.
  if (auth_complete_) {
    return FilterDataStatus::Continue;
  }
  return FilterDataStatus::StopIterationAndBuffer;
}

FilterTrailersStatus ExtAuth::decodeTrailers(HeaderMap&) {
  // decodeTrailers is called at the point that the HTTP machinery handling
  // the request has parsed the HTTP trailers for this request. We don't need
  // to do anything special here; we just need to make sure that we don't
  // let things proceed until our auth call is done.
  if (auth_complete_) {
    return FilterTrailersStatus::Continue;
  }
  return FilterTrailersStatus::StopIteration;
}

ExtAuthStats ExtAuth::generateStats(const std::string& prefix, Stats::Scope& scope) {
  std::string final_prefix = prefix + "extauth.";
  return {ALL_EXTAUTH_STATS(POOL_COUNTER_PREFIX(scope, final_prefix))};
}

void ExtAuth::onSuccess(Http::MessagePtr&& response) {

  // onSuccess is called when our asynch auth request succeeds, meaning
  // "the HTTP protocol was successfully followed to completion" -- it
  // could still be the case that the auth server gave us a failure
  // response.

  // We're done with our auth request, so make sure it gets shredded.
  auth_request_ = nullptr;

  dumpHeaders("onSuccess", request_headers_);

  // What did we get back from the auth server?

  uint64_t response_code = Http::Utility::getResponseStatus(response->headers());
  std::string response_body = response->bodyAsString();

  ENVOY_STREAM_LOG(trace, "ExtAuth Auth responded with code {}", *callbacks_, response_code);

  if (!response->body()->length()) {
    ENVOY_STREAM_LOG(trace, "ExtAuth Auth said: {}", *callbacks_, response->bodyAsString());
  }

  // By definition, any response code other than 200, "OK", means we deny
  // this request.

  if (response_code != enumToInt(Http::Code::OK)) {
    ENVOY_STREAM_LOG(debug, "ExtAuth rejecting request", *callbacks_);

    // Bump the rejection count...

    config_->stats_.rq_rejected_.inc();

    // ...and ditch our pointer to the request headers.

    request_headers_ = nullptr;

    // Whatever the auth server replied, we're going to hand that back to the
    // original requestor. That means both the header and the body; start by
    // copying the headers...

    Http::HeaderMapPtr response_headers{new HeaderMapImpl(response->headers())};

    callbacks_->encodeHeaders(std::move(response_headers), response_body.empty());

    // ...and then copy the body, as well, if there is one.

    if (!response_body.empty()) {
      Buffer::OwnedImpl buffer(response_body);
      callbacks_->encodeData(buffer, true);
    }

    // ...ahhhhnd we're done.

    return;
  }

  ENVOY_STREAM_LOG(debug, "ExtAuth accepting request", *callbacks_);

  // OK, we're going to approve this request, great! Next up: the filter can
  // be configured to copy headers from the auth server to the requester.
  // If that's configured, we need to take care of that now -- and if we actually
  // copy any headers, we'll need to be sure to invalidate the route cache.
  // (If we don't copy any headers, we should leave the route cache alone.)

  bool addedHeaders = false;

  // Do we have any headers configured to copy?

  if (!config_->allowed_headers_.empty()) {
    // Yup. Let's see if any of them are present.

    for (const std::string& allowed_header : config_->allowed_headers_) {
      LowerCaseString key(allowed_header);

      // OK, do we have this header?

      const HeaderEntry* hdr = response->headers().get(key);

      if (hdr) {
        // Well, the header exists at all. Does it have a value?

        const HeaderString& value = hdr->value();

        if (!value.empty()) {
          // Not empty! Copy it into our request_headers_.

          std::string valstr{value.c_str()};

          ENVOY_STREAM_LOG(trace, "ExtAuth allowing response header {}: {}", *callbacks_,
                           allowed_header, valstr);
          addedHeaders = true;
          request_headers_->addCopy(key, valstr);
        }
      }
    }
  }

  if (addedHeaders) {
    // Yup, we added headers. Invalidate the route cache in case any of
    // the headers will affect routing decisions.

    dumpHeaders("ExtAuth invalidating route cache", request_headers_);

    callbacks_->clearRouteCache();
  }

  // Finally done. Bump the "passed" stat...
  config_->stats_.rq_passed_.inc();

  // ...remember that auth is done...
  auth_complete_ = true;

  // ...clear our request-header pointer now that we're finished with
  // this request...
  request_headers_ = nullptr;

  // ...and allow everything to continue.
  callbacks_->continueDecoding();
}

void ExtAuth::onFailure(Http::AsyncClient::FailureReason) {
  auth_request_ = nullptr;
  request_headers_ = nullptr;
  ENVOY_STREAM_LOG(warn, "ExtAuth Auth request failed", *callbacks_);
  config_->stats_.rq_failed_.inc();
  Http::Utility::sendLocalReply(*callbacks_, false, Http::Code::ServiceUnavailable,
                                std::string("Auth request failed."));
}

void ExtAuth::onDestroy() {
  if (auth_request_) {
    auth_request_->cancel();
    auth_request_ = nullptr;
  }
}

void ExtAuth::setDecoderFilterCallbacks(StreamDecoderFilterCallbacks& callbacks) {
  callbacks_ = &callbacks;
}

} // namespace Http
} // namespace Envoy
