.. _config_connection_balance_dlb:

Dlb Connection Balancer
=======================

* :ref:`v3 API reference <envoy_v3_api_msg_extensions.network.connection_balance.dlb.v3alpha.Dlb>`


This connection balancer extension provides Envoy with low latency networking by integrating with `Intel DLB <https://networkbuilders.intel.com/solutionslibrary/queue-management-and-load-balancing-on-intel-architecture>`_ through the libdlb library.

The Dlb connection balancer is only included in :ref:`contrib images <install_contrib>`

Example configuration
---------------------

An example for Dlb connection balancer configuration is:

.. literalinclude:: _include/dlb.yaml
    :language: yaml


How it works
------------

If enabled, the Dlb connection balancer will:

- attach Dlb hardware
- create a queue for balancing
- create one port to send and one port to receive for each worker thread
- create one eventfd for each worker thread and attach each eventfd to corresponding customer
- register each eventfd to corresponding customer and Dlb hardware

When new connections come, one worker thread will accept it and send it to Dlb hardware. Dlb hardware
does balancing then trigger one worker thread to receive via libevent.

Installing Dlb
--------------

Download DLB driver release tarball from `dlb website <https://www.intel.com/content/www/us/en/download/686372/intel-dynamic-load-balancer.html>`_, then install it refer to `the getting started guide <https://downloadmirror.intel.com/727424/DLB_Driver_User_Guide.pdf>`_.


Using Dlb
---------

Create a config file to make Envoy listen 10000 port as proxy, the upstream server listens 12000 port.

.. literalinclude:: _include/dlb_example_config.yaml
    :language: yaml

Run the upstream service:

.. code-block:: bash

$ docker run -d -p 12000:80 nginx

Run Envoy with dlb enabled:

.. code-block:: bash

$ ./envoy --concurrency 2 -c dlb-config.yaml

Test:

.. code-block:: bash

$ curl localhost:10000

You should get output from Nginx like below:
.. code-block:: text
<!DOCTYPE html>
<html>
<head>
<title>Welcome to nginx!</title>
<style>
html { color-scheme: light dark; }
body { width: 35em; margin: 0 auto;
font-family: Tahoma, Verdana, Arial, sans-serif; }
</style>
</head>
<body>
<h1>Welcome to nginx!</h1>
<p>If you see this page, the nginx web server is successfully installed and
working. Further configuration is required.</p>

<p>For online documentation and support please refer to
<a href="http://nginx.org/">nginx.org</a>.<br/>
Commercial support is available at
<a href="http://nginx.com/">nginx.com</a>.</p>

<p><em>Thank you for using nginx.</em></p>
</body>
</html>
