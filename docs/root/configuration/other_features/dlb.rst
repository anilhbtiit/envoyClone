.. _config_connection_balance_dlb:

DLB Connection Balancer
=======================

* :ref:`v3 API reference <envoy_v3_api_msg_extensions.network.connection_balance.dlb.v3alpha.Dlb>`


This connection balancer extension provides Envoy with low latency networking by integrating with `Intel DLB <https://networkbuilders.intel.com/solutionslibrary/queue-management-and-load-balancing-on-intel-architecture>`_ through the libdlb library.

The DLB connection balancer is only included in :ref:`contrib images <install_contrib>`.

Example configuration
---------------------

An example for DLB connection balancer configuration is:

.. literalinclude:: _include/dlb.yaml
    :language: yaml


How it works
------------

If enabled, the DLB connection balancer will:

- attach DLB hardware
- create a queue for balancing
- create one port to send and one port to receive for each worker thread
- create one eventfd for each worker thread and attach each eventfd to corresponding customer
- register each eventfd to corresponding customer and DLB hardware

When new connections come, one worker thread will accept it and send it to DLB hardware. DLB hardware
does balancing then trigger one worker thread to receive via libevent.

Installing DLB
--------------

You can download the DLB driver release tarball from the `DLB website <https://www.intel.com/content/www/us/en/download/686372/intel-dynamic-load-balancer.html>`_. To install it refer to `the getting started guide <https://downloadmirror.intel.com/727424/DLB_Driver_User_Guide.pdf>`_.


Using DLB
---------

With the example configuration Envoy listens on port 10000 and proxies to an upstream server listening on port 12000.

.. literalinclude:: _include/dlb_example_config.yaml
 :language: yaml
 :lines: 7-11
 :lineno-start: 7
 :linenos:
 :caption: :download:`dlb_example_config.yaml <_include/dlb_example_config.yaml>`

Run the upstream service:

.. code-block:: console

 $ docker run -d -p 12000:80 nginx

Run Envoy with DLB connection balancer enabled:

.. code-block:: console

 $ ./envoy --concurrency 2 -c dlb_example_config.yaml

Test:

.. code-block:: console

 $ curl localhost:10000
