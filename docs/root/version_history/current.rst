1.22.0 (pending)
================

Incompatible Behavior Changes
-----------------------------
*Changes that are expected to cause an incompatibility if applicable; deployment changes are likely required*

* tls: set TLS v1.2 as the default minimal version for servers. Users can still explicitly opt-in to 1.0 and 1.1 using :ref:`tls_minimum_protocol_version <envoy_v3_api_field_extensions.transport_sockets.tls.v3.TlsParameters.tls_minimum_protocol_version>`.

Minor Behavior Changes
----------------------
*Changes that may cause incompatibilities for some users, but should not for most*

* dynamic_forward_proxy: if a DNS resolution fails, failing immediately with a specific resolution error, rather than finishing up all local filters and failing to select an upstream host.
* ext_authz: added requested server name in ext_authz network filter for auth review.
* file: changed disk based files to truncate files which are not being appended to. This behavioral change can be temporarily reverted by setting runtime guard ``envoy.reloadable_features.append_or_truncate`` to false.
* grpc: flip runtime guard ``envoy.reloadable_features.enable_grpc_async_client_cache`` to be default enabled. async grpc client created through getOrCreateRawAsyncClient will be cached by default.
* http: avoiding delay-close for HTTP/1.0 responses framed by connection: close as well as HTTP/1.1 if the request is fully read. This behavior can be temporarily reverted by setting ``envoy.reloadable_features.skip_delay_close`` to false.
* http: now the max concurrent streams of http2 connection can not only be adjusted down according to the SETTINGS frame but also can be adjusted up, of course, it can not exceed the configured upper bounds. This fix is guarded by ``envoy.reloadable_features.http2_allow_capacity_increase_by_settings``.

Bug Fixes
---------
*Changes expected to improve the state of the world and are unlikely to have negative effects*

* access_log: fix memory leak when reopening an access log fails. Access logs will now try to be reopened on each subsequent flush attempt after a failure.
* data plane: fixing error handling where writing to a socket failed while under the stack of processing. This should genreally affect HTTP/3. This behavioral change can be reverted by setting ``envoy.reloadable_features.allow_upstream_inline_write`` to false.
* eds: fix the eds cluster update by allowing update on the locality of the cluster endpoints. This behavioral change can be temporarily reverted by setting runtime guard ``envoy.reloadable_features.support_locality_update_on_eds_cluster_endpoints`` to false.
* tls: fix a bug while matching a certificate SAN with an exact value in ``match_typed_subject_alt_names`` of a listener where wildcard ``*`` character is not the only character of the dns label. Example, ``baz*.example.net`` and ``*baz.example.net`` and ``b*z.example.net`` will match ``baz1.example.net`` and ``foobaz.example.net`` and ``buzz.example.net``, respectively.
* xray: fix the AWS X-Ray tracer extension to not sample the trace if ``sampled=`` keyword is not present in the header ``x-amzn-trace-id``.

Removed Config or Runtime
-------------------------
*Normally occurs at the end of the* :ref:`deprecation period <deprecated>`

* access_log: removed ``envoy.reloadable_features.unquote_log_string_values`` and legacy code paths.
* grpc_bridge_filter: removed ``envoy.reloadable_features.grpc_bridge_stats_disabled`` and legacy code paths.
* http: removed ``envoy.reloadable_features.hash_multiple_header_values`` and legacy code paths.
* http: removed ``envoy.reloadable_features.no_chunked_encoding_header_for_304`` and legacy code paths.
* http: removed ``envoy.reloadable_features.preserve_downstream_scheme`` and legacy code paths.
* http: removed ``envoy.reloadable_features.require_strict_1xx_and_204_response_headers`` and ``envoy.reloadable_features.send_strict_1xx_and_204_response_headers`` and legacy code paths.
* http: removed ``envoy.reloadable_features.strip_port_from_connect`` and legacy code paths.
* http: removed ``envoy.reloadable_features.use_observable_cluster_name`` and legacy code paths.
* http: removed ``envoy.reloadable_features.http_transport_failure_reason_in_body`` and legacy code paths.
* http: removed ``envoy.reloadable_features.allow_response_for_timeout`` and legacy code paths.
* http: removed ``envoy.reloadable_features.http2_consume_stream_refused_errors`` and legacy code paths.
* http: removed ``envoy.reloadable_features.internal_redirects_with_body`` and legacy code paths.
* udp: removed ``envoy.reloadable_features.udp_per_event_loop_read_limit`` and legacy code paths.
* upstream: removed ``envoy.reloadable_features.health_check.graceful_goaway_handling`` and legacy code paths.

New Features
------------
* access log: added :ref:`custom_tags <envoy_v3_api_field_extensions.access_loggers.grpc.v3.CommonGrpcAccessLogConfig.custom_tags>` to annotate log entries with custom tags.
* access log: added :ref:`grpc_stream_retry_policy <envoy_v3_api_field_extensions.access_loggers.grpc.v3.CommonGrpcAccessLogConfig.grpc_stream_retry_policy>` to the gRPC logger to reconnect when a connection fails to be established.
* access_log: added :ref:`METADATA<envoy_v3_api_msg_extensions.formatter.metadata.v3.Metadata>` token to handle all types of metadata (DYNAMIC, CLUSTER, ROUTE).
* access_log: added a CEL extension filter to enable filtering of access logs based on Envoy attribute expressions.
* access_log: added new access_log command operator ``%UPSTREAM_REQUEST_ATTEMPT_COUNT%`` to retrieve the number of times given request got attempted upstream.
* access_log: added new access_log command operator ``%VIRTUAL_CLUSTER_NAME%`` to retrieve the matched Virtual Cluster name.
* api: added support for *xds.type.v3.TypedStruct* in addition to the now-deprecated *udpa.type.v1.TypedStruct* proto message, which is a wrapper proto used to encode typed JSON data in a *google.protobuf.Any* field.
* aws_request_signing_filter: added :ref:`match_excluded_headers <envoy_v3_api_field_extensions.filters.http.aws_request_signing.v3.AwsRequestSigning.match_excluded_headers>` to the signing filter to optionally exclude request headers from signing.
* bootstrap: added :ref:`typed_dns_resolver_config <envoy_v3_api_field_config.bootstrap.v3.Bootstrap.typed_dns_resolver_config>` in the bootstrap to support DNS resolver as an extension.
* cluster: added :ref:`typed_dns_resolver_config <envoy_v3_api_field_config.cluster.v3.Cluster.typed_dns_resolver_config>` in the cluster to support DNS resolver as an extension.
* config: added :ref:`environment_variable <envoy_v3_api_field_config.core.v3.datasource.environment_variable>` to the :ref:`DataSource <envoy_v3_api_msg_config.core.v3.datasource>`.
* decompressor: added :ref:`ignore_no_transform_header <envoy_v3_api_field_extensions.filters.http.decompressor.v3.Decompressor.CommonDirectionConfig.ignore_no_transform_header>` to run decompression regardless of the value of the *no-transform* cache control header.
* dns: added :ref:`ALL <envoy_v3_api_enum_value_config.cluster.v3.Cluster.DnsLookupFamily.ALL>` option to return both IPv4 and IPv6 addresses.
* dns_cache: added :ref:`typed_dns_resolver_config <envoy_v3_api_field_extensions.common.dynamic_forward_proxy.v3.DnsCacheConfig.typed_dns_resolver_config>` in the dns_cache to support DNS resolver as an extension.
* dns_filter: added :ref:`typed_dns_resolver_config <envoy_v3_api_field_extensions.filters.udp.dns_filter.v3.DnsFilterConfig.ClientContextConfig.typed_dns_resolver_config>` in the dns_filter to support DNS resolver as an extension.
* dns_resolver: added :ref:`CaresDnsResolverConfig<envoy_v3_api_msg_extensions.network.dns_resolver.cares.v3.CaresDnsResolverConfig>` to support c-ares DNS resolver as an extension.
* dns_resolver: added :ref:`use_resolvers_as_fallback<envoy_v3_api_field_extensions.network.dns_resolver.cares.v3.CaresDnsResolverConfig.use_resolvers_as_fallback>` to the c-ares DNS resolver.
* dns_resolver: added :ref:`use_resolvers_as_fallback<envoy_v3_api_field_extensions.network.dns_resolver.cares.v3.CaresDnsResolverConfig.filter_unroutable_families>` to the c-ares DNS resolver.
* dns_resolver: added :ref:`AppleDnsResolverConfig<envoy_v3_api_msg_extensions.network.dns_resolver.apple.v3.AppleDnsResolverConfig>` to support apple DNS resolver as an extension.
* ext_authz: added :ref:`query_parameters_to_set <envoy_v3_api_field_service.auth.v3.OkHttpResponse.query_parameters_to_set>` and :ref:`query_parameters_to_remove <envoy_v3_api_field_service.auth.v3.OkHttpResponse.query_parameters_to_remove>` for adding and removing query string parameters when using a gRPC authorization server.
* grpc_http_bridge: added :ref:`upgrade_protobuf_to_grpc <envoy_v3_api_field_extensions.filters.http.grpc_http1_bridge.v3.Config.upgrade_protobuf_to_grpc>` option for automatically framing protobuf payloads as gRPC requests.
* grpc_json_transcoder: added support for matching unregistered custom verb :ref:`match_unregistered_custom_verb <envoy_v3_api_field_extensions.filters.http.grpc_json_transcoder.v3.GrpcJsonTranscoder.match_unregistered_custom_verb>`.
* http: added support for %REQUESTED_SERVER_NAME% to extract SNI as a custom header.
* http: added support for %VIRTUAL_CLUSTER_NAME% to extract the matched Virtual Cluster name as a custom header.
* http: added support for :ref:`retriable health check status codes <envoy_v3_api_field_config.core.v3.HealthCheck.HttpHealthCheck.retriable_statuses>`.
* http: added timing information about upstream connection and encryption establishment to stream info. These can currently be accessed via custom access loggers.
* http: added support for :ref:`forwarding HTTP1 reason phrase <envoy_v3_api_field_extensions.http.header_formatters.preserve_case.v3.PreserveCaseFormatterConfig.forward_reason_phrase>`.
* listener: added API for extensions to access :ref:`typed_filter_metadata <envoy_v3_api_field_config.core.v3.Metadata.typed_filter_metadata>` configured in the listener's :ref:`metadata <envoy_v3_api_field_config.listener.v3.Listener.metadata>` field.
* listener: added support for :ref:`MPTCP <envoy_v3_api_field_config.listener.v3.Listener.enable_mptcp>` (multipath TCP).
* listener: added support for opting out listeners from the globally set downstream connection limit via :ref:`ignore_global_conn_limit <envoy_v3_api_field_config.listener.v3.Listener.ignore_global_conn_limit>`.
* matcher: added support for *xds.type.matcher.v3.IPMatcher* IP trie matching.
* oauth filter: added :ref:`cookie_names <envoy_v3_api_field_extensions.filters.http.oauth2.v3.OAuth2Credentials.cookie_names>` to allow overriding (default) cookie names (``BearerToken``, ``OauthHMAC``, and ``OauthExpires``) set by the filter.
* oauth filter: setting IdToken and RefreshToken cookies if they are provided by Identity provider along with AccessToken.
* perf: added support for [Perfetto](https://perfetto.dev) performance tracing.
* router: added support for the :ref:`config_http_conn_man_headers_x-forwarded-host` header.
* stats: added text_readouts query parameter to prometheus stats to return gauges made from text readouts.
* tcp: added a :ref:`FilterState <envoy_v3_api_msg_type.v3.HashPolicy.FilterState>` :ref:`hash policy <envoy_v3_api_msg_type.v3.HashPolicy>`, used by :ref:`TCP proxy <envoy_v3_api_field_extensions.filters.network.tcp_proxy.v3.TcpProxy.hash_policy>` to allow hashing load balancer algorithms to hash on objects in filter state.
* tcp_proxy: added support to populate upstream http connect header values from stream info.
* thrift_proxy: add header to metadata filter for turning headers into dynamic metadata.
* thrift_proxy: add upstream response zone metrics in the form ``cluster.cluster_name.zone.local_zone.upstream_zone.thrift.upstream_resp_success``.
* thrift_proxy: add upstream metrics to show decoding errors and whether exception is from local or remote, e.g. ``cluster.cluster_name.thrift.upstream_resp_exception_remote``.
* thrift_proxy: add host level success/error metrics where success is a reply of type success and error is any other response to a call.
* thrift_proxy: support header flags.
* thrift_proxy: support subset lb when using request or route metadata.
* tls: added support for :ref:`match_typed_subject_alt_names <envoy_v3_api_field_extensions.transport_sockets.tls.v3.CertificateValidationContext.match_typed_subject_alt_names>` for subject alternative names to enforce specifying the subject alternative name type for the matcher to prevent matching against an unintended type in the certificate.
* tls: added support for only verifying the leaf CRL in the certificate chain with :ref:`only_verify_leaf_cert_crl <envoy_v3_api_field_extensions.transport_sockets.tls.v3.CertificateValidationContext.only_verify_leaf_cert_crl>`.
* tls: support loading certificate chain and private key via :ref:`pkcs12 <envoy_v3_api_field_extensions.transport_sockets.tls.v3.TlsCertificate.pkcs12>`.
* tls_inspector filter: added :ref:`enable_ja3_fingerprinting <envoy_v3_api_field_extensions.filters.listener.tls_inspector.v3.TlsInspector.enable_ja3_fingerprinting>` to create JA3 fingerprint hash from Client Hello message.
* transport_socket: added :ref:`envoy.transport_sockets.tcp_stats <envoy_v3_api_msg_extensions.transport_sockets.tcp_stats.v3.Config>` which generates additional statistics gathered from the OS TCP stack.
* udp: add support for multiple listener filters.
* udp_proxy: added :ref:`matcher <envoy_v3_api_field_extensions.filters.udp.udp_proxy.v3.UdpProxyConfig.matcher>` to support matching and routing to different clusters.
* udp_proxy: added :ref:`use_per_packet_load_balancing <envoy_v3_api_field_extensions.filters.udp.udp_proxy.v3.UdpProxyConfig.use_per_packet_load_balancing>` option to enable per packet load balancing (selection of upstream host on each data chunk).
* upstream: added the ability to :ref:`configure max connection duration <envoy_v3_api_field_config.core.v3.HttpProtocolOptions.max_connection_duration>` for upstream clusters.
* vcl_socket_interface: added VCL socket interface extension for fd.io VPP integration to :ref:`contrib images <install_contrib>`. This can be enabled via :ref:`VCL <envoy_v3_api_msg_extensions.vcl.v3alpha.VclSocketInterface>` configuration.
* xds: re-introduced unified delta and sotw xDS multiplexers that share most of the implementation. Added a new runtime config ``envoy.reloadable_features.unified_mux`` (disabled by default) that when enabled, switches xDS to use unified multiplexers.
* access_log: make consistent access_log format fields ``%(DOWN|DIRECT_DOWN|UP)STREAM_(LOCAL|REMOTE)_*%`` to provide all combinations of local & remote addresses for upstream & downstream connections.
* cors: add dynamic support for headers ``access-control-allow-methods`` and ``access-control-allow-headers`` in cors.
* http: added random_value_specifier in :ref:`weighted_clusters <envoy_v3_api_field_config.route.v3.RouteAction.weighted_clusters>` to allow random value to be specified from configuration proto.
* http: added support for :ref:`proxy_status_config <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.proxy_status_config>` for configuring `Proxy-Status <https://datatracker.ietf.org/doc/html/draft-ietf-httpbis-proxy-status-08>`_ HTTP response header fields.
* http: make consistent custom header format fields ``%(DOWN|DIRECT_DOWN|UP)STREAM_(LOCAL|REMOTE)_*%`` to provide all combinations of local & remote addresses for upstream & downstream connections.
* http3: downstream HTTP/3 support is now GA! Upstream HTTP/3 also GA for specific deployments. See :ref:`here <arch_overview_http3>` for details.
* http3: supports upstream HTTP/3 retries. Automatically retry `0-RTT safe requests <https://www.rfc-editor.org/rfc/rfc7231#section-4.2.1>`_ if they are rejected because they are sent `too early <https://datatracker.ietf.org/doc/html/rfc8470#section-5.2>`_. And automatically retry 0-RTT safe requests if connect attempt fails later on and the cluster is configured with TCP fallback. And add retry on ``http3-post-connect-failure`` policy which allows retry of failed HTTP/3 requests with TCP fallback even after handshake if the cluster is configured with TCP fallback. This feature is guarded by ``envoy.reloadable_features.conn_pool_new_stream_with_early_data_and_http3``.
* matching: the matching API can now express a match tree that will always match by omitting a matcher at the top level.

Deprecated
----------

* http: removing support for long-deprecated old style filter names, e.g. envoy.router, envoy.lua.
