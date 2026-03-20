Testing Utilities and Mock Mappings
====================================

This document provides a catalog of core testing utilities, common helper classes, and standard mock mappings used in the Envoy codebase.

.. contents::
   :local:
   :depth: 2

Common/General Utilities
------------------------

These utilities provide foundational support for Envoy testing, including configuration loading, protobuf comparison, and time control.

TestUtility
~~~~~~~~~~~

The `TestUtility` class (located in `test/test_common/utility.h`) is a central repository for miscellaneous testing helpers.

**Greatest Hits:**

*   ``TestUtility::loadFromYaml(const std::string& yaml, Message& message)``: 
    Populates a protobuf message from a YAML string. This is the preferred way to initialize complex configurations in tests.
*   ``TestUtility::protoEqual(const Message& msg1, const Message& msg2)``: 
    Performs a deep comparison of two protobuf messages, which is essential for verifying configuration output.
*   ``TestUtility::readFile(const std::string& path)``: 
    A simple helper for reading a file into a string, often used for loading large test datasets.

**Reference:** `test/test_common/utility.h <https://github.com/envoyproxy/envoy/blob/main/test/test_common/utility.h>`_

SimulatedTimeSystem
~~~~~~~~~~~~~~~~~~~

The `SimulatedTimeSystem` (located in `test/test_common/simulated_time_system.h`) is used for deterministic time-based testing. It allows tests to advance time without real-world delays.

**Greatest Hits:**

*   ``advanceTimeAndRun(const std::chrono::milliseconds& duration)``: 
    Advances the clock by the specified duration and executes any pending timers or events in the dispatcher.

**Reference:** `test/test_common/simulated_time_system.h <https://github.com/envoyproxy/envoy/blob/main/test/test_common/simulated_time_system.h>`_

Network Utilities
-----------------

NetworkUtility
~~~~~~~~~~~~~~

The `NetworkUtility` (located in `test/test_common/network_utility.h`) provides helpers for network-related testing, such as port management and IP address handling.

**Greatest Hits:**

*   ``NetworkUtility::findOrCheckFreePort(const Address::InstanceConstSharedPtr& address, Address::IpVersion type)``: 
    Finds an available port for a test server to bind to, ensuring tests are isolated and don't conflict with other processes.

**Reference:** `test/test_common/network_utility.h <https://github.com/envoyproxy/envoy/blob/main/test/test_common/network_utility.h>`_

Filesystem Utilities
--------------------

FileSystem
~~~~~~~~~~

The `FileSystem` abstraction (located in `test/test_common/file_system_for_test.h`) provides a mockable interface for filesystem operations, allowing tests to simulate various disk states and errors.

**Reference:** `test/test_common/file_system_for_test.h <https://github.com/envoyproxy/envoy/blob/main/test/test_common/file_system_for_test.h>`_

Integration Testing Helpers
---------------------------

These helpers are primarily used in the Envoy integration testing framework.

AutonomousUpstream
~~~~~~~~~~~~~~~~~~

The `AutonomousUpstream` (located in `test/integration/autonomous_upstream.h`) provides a simplified mock upstream that automatically responds to requests with a default 200 OK.

**Greatest Hits:**

*   ``AutonomousStream``: 
    A stream that handles request/response cycles automatically, reducing the need for explicit `EXPECT_CALL` for simple backend behavior.

**Reference:** `test/integration/autonomous_upstream.h <https://github.com/envoyproxy/envoy/blob/main/test/integration/autonomous_upstream.h>`_

IntegrationTestServer
~~~~~~~~~~~~~~~~~~~~~

The `IntegrationTestServer` (located in `test/integration/integration.h`) provides helpers for interacting with the Envoy server during an integration test.

**Greatest Hits:**

*   ``findCounter(const std::string& name)``: 
    Locates a statistic counter by name, which is essential for verifying that certain events occurred during the test.

**Reference:** `test/integration/integration.h <https://github.com/envoyproxy/envoy/blob/main/test/integration/integration.h>`_

Specialized Libraries
---------------------

*   ``DelegatingRouteUtility``: 
    Used in HTTP route testing to delegate routing logic and verify complex matching scenarios.
*   ``Environment``: 
    Provides access to environment-specific settings, such as build paths and temporary directories.

Mock Mappings
-------------

Envoy follows a consistent pattern of defining interfaces in `include/envoy/` and their corresponding mocks in `test/mocks/`.

+------------------------------+-------------------------------------+
| Core Interface               | Standard Mock Implementation        |
+==============================+=====================================+
| ``Network::Filter``          | ``Network::MockFilter``             |
+------------------------------+-------------------------------------+
| ``Http::StreamDecoderFilter``| ``Http::MockStreamDecoderFilter``   |
+------------------------------+-------------------------------------+
| ``Upstream::ClusterManager`` | ``Upstream::MockClusterManager``    |
+------------------------------+-------------------------------------+
| ``Event::Dispatcher``        | ``Event::MockDispatcher``           |
+------------------------------+-------------------------------------+
| ``StreamInfo::StreamInfo``   | ``StreamInfo::MockStreamInfo``      |
+------------------------------+-------------------------------------+
