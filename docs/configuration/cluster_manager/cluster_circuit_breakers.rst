.. _config_cluster_manager_cluster_circuit_breakers:

Circuit breakers
================

Circuit breaking :ref:`architecture overview <arch_overview_circuit_break>`.

Circuit breaking settings can be specified individually for each defined priority. How the
different priorities are used are documented in the sections of the configuration guide that use
them.

.. code-block:: json

  {
    "default": "{...}",
    "high": "{...}"
  }

default
  *(optional, object)* Settings object for default priority.

high
  *(optional, object)* Settings object for high priority.

Per priority settings
---------------------

.. code-block:: json

  {
    "max_connections": "...",
    "max_pending_requests": "...",
    "max_requests": "...",
    "max_retries": "...",
  }

max_connections
  *(optional, integer)* The maximum number of connections that Envoy will make to the upstream
  cluster. If not specified, the default is 1024. See the :ref:`circuit breaking overview
  <arch_overview_circuit_break>` for more information.

max_pending_requests
  *(optional, integer)* The maximum number of pending requests that Envoy will allow to the upstream
  cluster. If not specified, the default is 1024. See the :ref:`circuit breaking overview
  <arch_overview_circuit_break>` for more information.

max_requests
  *(optional, integer)* The maximum number of parallel requests that Envoy will make to the upstream
  cluster. If not specified, the default is 1024. See the :ref:`circuit breaking overview
  <arch_overview_circuit_break>` for more information.

max_retries
  *(optional, integer)* The maximum number of parallel retries that Envoy will allow to the upstream
  cluster. If not specified, the default is 3. See the :ref:`circuit breaking overview
  <arch_overview_circuit_break>` for more information.
