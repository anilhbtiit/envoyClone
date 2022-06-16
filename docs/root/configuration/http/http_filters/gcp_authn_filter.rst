.. _config_http_filters_gcp_authn:

GCP Authentication Filter
=========================
This filter is used to fetch authentication tokens from GCP compute metadata server(https://cloud.google.com/run/docs/securing/service-identity#identity_tokens).
In multiple services architecture where these services likely need to communicate with each other, authenticating service-to-service(https://cloud.google.com/run/docs/authenticating/service-to-service) is required because many of these services may be private and require credentials for access.

Configuration
-------------
This filter should be configured with the name ``envoy.filters.http.gcp_authn``.

The filter configuration :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.gcp_authn.v3.GcpAuthnFilterConfig>` has three fields:

* Field ``http_uri`` specifies the HTTP URI for fetching the from `GCE Metadata Server <https://cloud.google.com/compute/docs/metadata/overview>`_. The URL format is ``http://metadata.google.internal/computeMetadata/v1/instance/service-accounts/default/identity?audience=[AUDIENCE]``. The ``AUDIENCE`` field is provided by configuration, please see more details below.

* Field ``retry_policy`` specifies the retry policy if fetching tokens failed. This field is optional. If it is not configured, the filter will be fail-closed (i.e., reject the requests).

* Field ``cache_config`` specifies the configuration for the token cache which is used to avoid the duplicated queries to GCE metadata server for the same request.

The audience configuration :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.gcp_authn.v3.Audience>` is the URL of the destionation service, which is the receving service that calling service is invoking. This information is provided through cluster metadata :ref:`Metadata<envoy_v3_api_msg_config.core.v3.metadata>`

The token cache configuration :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.gcp_authn.v3.Token>` is used to avoid the redundant queries to authentication server (GCE metadata server in the context of this filter) for duplicated tokens.

Configuration example
--------------------
Static and dynamic resouce configuration example:

.. literalinclude:: _include/gcp-authn-resource-configuration.yaml
   :language: yaml
   :lines: 1-75
   :linenos:
   :lineno-start: 21
   :caption: :download:`gcp-authn-resource-configuration.yaml <_include/gcp-authn-resource-configuration.yaml>`

Http filter configuration example in the filter chain:

.. literalinclude:: _include/gcp-authn-filter-configuration.yaml
   :language: yaml
   :lines: 17-24
   :linenos:
   :lineno-start: 17
   :caption: :download:`gcp-authn-filter-configuration.yaml <_include/gcp-authn-filter-configuration.yaml>`
