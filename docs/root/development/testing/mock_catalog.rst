Mock Catalog
============

This catalog provides a mapping of common Envoy interfaces to their standard mock implementations. This table is designed to help developers find the correct mocks and see examples of how they are typically used in tests.

.. contents::
   :local:

Common Mocks
------------

The following table maps core interfaces to their corresponding mock classes.

.. list-table::
   :widths: 25 25 25 25
   :header-rows: 1

   * - Interface
     - Mock Class
     - Header Location
     - Canonical Usage Example
   * - ``Event::Dispatcher``
     - ``Event::MockDispatcher``
     - ``test/mocks/event/mocks.h``
     - ``test/common/memory/heap_shrinker_test.cc``
   * - ``Network::Connection``
     - ``Network::MockConnection``
     - ``test/mocks/network/connection.h``
     - ``test/common/network/listener_impl_test.cc``
   * - ``Http::FilterManagerCallbacks``
     - ``Http::MockFilterManagerCallbacks``
     - ``test/mocks/http/mocks.h``
     - ``test/common/http/filter_manager_test.cc``
   * - ``Api::Api``
     - ``Api::MockApi``
     - ``test/mocks/api/mocks.h``
     - ``test/common/access_log/access_log_manager_impl_test.cc``
   * - ``Stats::Store``
     - ``Stats::MockStore``
     - ``test/mocks/stats/mocks.h``
     - ``test/common/tracing/tracer_impl_test.cc``

Mocking Reference (Bazel Targets)
---------------------------------

The following table provides a quick reference for common Envoy mocks and their associated Bazel targets.

+-----------------------------+-------------------------------------+---------------------------------------------------------+
| **Component to Mock**       | **Header File**                     | **Bazel Target**                                        |
+=============================+=====================================+=========================================================+
| HTTP Codecs/Streams/Filters | ``test/mocks/http/mocks.h``         | ``//test/mocks/http:http_mocks``                        |
+-----------------------------+-------------------------------------+---------------------------------------------------------+
| Network Connections/Filters | ``test/mocks/network/mocks.h``      | ``//test/mocks/network:network_mocks``                  |
+-----------------------------+-------------------------------------+---------------------------------------------------------+
| Upstream Clusters/Hosts     | ``test/mocks/upstream/mocks.h``     | ``//test/mocks/upstream:upstream_mocks``                |
+-----------------------------+-------------------------------------+---------------------------------------------------------+
| Event Dispatchers/Timers    | ``test/mocks/event/mocks.h``        | ``//test/mocks/event:event_mocks``                      |
+-----------------------------+-------------------------------------+---------------------------------------------------------+
| Statistics Store            | ``test/mocks/stats/mocks.h``        | ``//test/mocks/stats:stats_mocks``                      |
+-----------------------------+-------------------------------------+---------------------------------------------------------+
| Runtime Loader              | ``test/mocks/runtime/mocks.h``      | ``//test/mocks/runtime:runtime_mocks``                  |
+-----------------------------+-------------------------------------+---------------------------------------------------------+
| API / Filesystem            | ``test/mocks/api/mocks.h``          | ``//test/mocks/api:api_mocks``                          |
+-----------------------------+-------------------------------------+---------------------------------------------------------+

Finding Mocks
-------------

Envoy follows a consistent pattern for organizing its mocks:

1.  **Interface Definition**: Interfaces are defined in headers under ``include/envoy/``. For example, ``include/envoy/event/dispatcher.h``.
2.  **Mock Implementation**: Corresponding mock classes are located in mirroring directories under ``test/mocks/``. For example, ``test/mocks/event/mocks.h``.

If you cannot find a mock for a specific interface, check the ``test/mocks/`` directory structure. Most core subsystems (Event, Network, Http, Upstream, etc.) have a central ``mocks.h`` file in their respective subdirectory.
