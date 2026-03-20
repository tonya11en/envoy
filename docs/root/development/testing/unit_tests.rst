Unit Testing
============

Unit testing is the bedrock of Envoy's stability. Because Envoy is a complex,
multi-threaded network proxy, we prioritize component-level isolation and
deterministic behavior. This guide explains how to design for testability and
leverage Envoy's robust mocking infrastructure to write high-quality unit tests.

.. contents::
  :local:

Designing for Testability
-------------------------

In Envoy, "testable code" is code that is modular, decoupled from its
dependencies, and deterministic. When writing new features, consider the following
patterns to ensure your code can be effectively unit tested:

1. **Dependency Injection**: Always pass dependencies (like ``Dispatcher``, 
   ``Runtime::Loader``, or ``Api::Api``) into your component's constructor or via 
   factory methods. This allows tests to inject mocks instead of relying on real 
   system resources.
2. **Interface-Based Design**: Define clear interfaces for collaborators. This 
   makes it easy to create mocks using ``GMock`` that satisfy those interfaces.
3. **Avoid Global State**: Global variables or static singletons make tests 
   difficult to isolate and can lead to non-deterministic failures when running 
   tests in parallel.
4. **Use Simulated Time**: If your logic depends on timers or timeouts, always 
   interact with the ``Dispatcher`` or ``TimeSource``. This allows tests to 
   manually advance time using the ``SimulatedTimeSystem``.

.. tip::
   If you find yourself struggling to write a unit test because of complex 
   initialization or hard-to-mock dependencies, it's often a sign that your 
   component needs to be refactored for better decoupling.

Mocking Strategy
----------------

Envoy uses `Google Mock (GMock) <https://google.github.io/googletest/gmock_for_dummies.html>`_ 
extensively. Our mocking strategy is designed to balance test precision with 
maintainability.

.. note::
   We follow a "Strictness Baseline." By default, use ``StrictMock`` for the 
   collaborators you are actively testing. Use ``NiceMock`` for "noise" 
   dependencies that are required for the component to run but are not the 
   focus of the current test.

StrictMock (Default for SUT Collaborators)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Use ``StrictMock`` for any object where you want to verify specific interactions. 
Any unexpected call to a ``StrictMock`` will cause the test to fail.

*   **Why**: It forces you to be explicit about what interactions your component 
    has with its environment. This catches regressions where the component 
    starts calling methods it shouldn't.
*   **Usage**: ``testing::StrictMock<MockClass> collaborator;``

NiceMock (For "Noise" Dependencies)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Use ``NiceMock`` for infrastructure-level dependencies that provide common 
services but whose specific methods are not being verified in this test.

*   **Common Examples**:
    *   ``Api::Api`` (for filesystem or socket access)
    *   ``Stats::Store`` (for counter/gauge registration)
    *   ``Runtime::Loader`` (for feature flags)
*   **Why**: It suppresses "Uninteresting mock function call" warnings, keeping 
    the test output focused on what matters.
*   **Usage**: ``testing::NiceMock<MockClass> noise_dependency;``

Foundations
-----------

Envoy unit tests are typically located in the ``test/`` directory, mirroring the 
structure of the ``source/`` directory. For example, tests for 
``source/common/http/`` are located in ``test/common/http/``.

All unit tests are built and run using Bazel. Refer to the 
:doc:`Bazel Testing Guide <bazel>` for information on the macros used to define 
tests (e.g., ``envoy_cc_test``).

Common Includes & Dependencies
------------------------------

When writing unit tests for Envoy, you will typically need to include the 
following headers and their corresponding Bazel targets in your ``BUILD`` file:

+-------------------------------------+-------------------------------------+---------------------------------------------------------+
| **Header**                          | **Bazel Target**                    | **Purpose**                                             |
+=====================================+=====================================+=========================================================+
| ``test/test_common/utility.h``      | ``//test/test_common:utility_lib``  | Essential test utilities (e.g., ``EXPECT_THROW_WITH_MESSAGE``) |
+-------------------------------------+-------------------------------------+---------------------------------------------------------+
| ``test/mocks/http/mocks.h``         | ``//test/mocks/http:http_mocks``    | Mocks for HTTP codecs, streams, and filters.            |
+-------------------------------------+-------------------------------------+---------------------------------------------------------+
| ``test/mocks/network/mocks.h``      | ``//test/mocks/network:network_mocks``| Mocks for network connections, listeners, and filters.  |
+-------------------------------------+-------------------------------------+---------------------------------------------------------+
| ``test/mocks/upstream/mocks.h``     | ``//test/mocks/upstream:upstream_mocks``| Mocks for clusters, hosts, and load balancers.        |
+-------------------------------------+-------------------------------------+---------------------------------------------------------+
| ``test/mocks/stats/mocks.h``        | ``//test/mocks/stats:stats_mocks``  | Mocks for stat stores and counters.                   |
+-------------------------------------+-------------------------------------+---------------------------------------------------------+

Mock Injection Patterns
-----------------------

Mocks should be injected into the SUT using one of the following patterns:

Constructor Injection
^^^^^^^^^^^^^^^^^^^^^

The most common and preferred pattern. Pass mock objects (usually by reference or 
shared pointer) to the SUT's constructor. This makes dependencies explicit and 
easy to identify.

.. code-block:: cpp

  NiceMock<Runtime::MockLoader> runtime;
  StrictMock<MockCollaborator> collaborator;
  SUT sut(runtime, collaborator);

Factory Injection
^^^^^^^^^^^^^^^^^

For dependencies created at runtime (e.g., a new HTTP connection), use a factory 
interface that can be mocked to return your mock objects. This is often used 
when testing managers or orchestrators.

Test Fixture Design
-------------------

To reduce boilerplate and promote reuse, Envoy uses Mixins and Base classes for 
complex test setups.

Mixin and Base Classes
^^^^^^^^^^^^^^^^^^^^^^

A common pattern is to define a ``*Mixin`` class that implements a 
configuration interface (e.g., ``ConnectionManagerConfig``) and contains all the 
necessary mocks and setup logic.

The actual test fixture then inherits from both the Mixin and ``testing::Test``.

.. code-block:: cpp

  class MyComponentTest : public MyComponentMixin, public testing::Test {
    // Test cases go here
  };

Example: ``HttpConnectionManagerImplMixin`` in ``test/common/http/conn_manager_impl_test_base.h`` 
is a canonical example of this pattern.

Gold Standard Examples
----------------------

Envoy's codebase contains thousands of tests. The following examples are 
considered "Gold Standard" because they demonstrate clean, maintainable, and 
effective testing patterns that tell a clear story of component behavior.

1. Simple State Validation: IsolatedStoreImpl
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

**File**: `test/common/stats/isolated_store_impl_test.cc <https://github.com/envoyproxy/envoy/blob/main/test/common/stats/isolated_store_impl_test.cc>`_

This test is an excellent example of testing a component with minimal 
dependencies. It focuses on validating that the internal state of the 
``IsolatedStoreImpl`` correctly reflects the operations performed on it.

**Key Insight**: Minimal mocking is used here because the component is 
relatively self-contained. The test relies on real objects to verify the 
actual implementation rather than its interactions with mocks.

2. Complex State Machines: Connection Manager
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

**File**: `test/common/http/conn_manager_impl_test.cc <https://github.com/envoyproxy/envoy/blob/main/test/common/http/conn_manager_impl_test.cc>`_

The HttpConnectionManager (HCM) is one of Envoy's most complex components. Its 
tests use a robust fixture and Mixin pattern to manage this complexity.

**Key Insight**: Instead of calling methods on the HCM directly, the test 
simulates data arriving over the network via the ``codec_`` mock, exercising 
the full state machine. This "protocol-level" simulation is a powerful way to 
test complex logic.

3. Filter Lifecycle: Filter Manager
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

**File**: `test/common/http/filter_manager_test.cc <https://github.com/envoyproxy/envoy/blob/main/test/common/http/filter_manager_test.cc>`_

This test demonstrates how to verify the interaction between a manager and the 
filters it controls.

**Key Insight**: By mocking the ``FilterChainFactory``, the test can inject 
specific mock filters into the manager and verify their interaction lifecycle 
with high precision.
