Unit Testing
============

This guide covers core unit testing patterns and principles in Envoy. Envoy uses `Google Test (GTest) <https://google.github.io/googletest/>`_ and `Google Mock (GMock) <https://google.github.io/googletest/gmock_for_dummies.html>`_ as its primary testing framework.

.. contents::
  :local:

Foundations
-----------

Envoy unit tests are typically located in the ``test/`` directory, mirroring the structure of the ``source/`` directory. For example, tests for ``source/common/http/`` are located in ``test/common/http/``.

All unit tests are built and run using Bazel. Refer to the :doc:`Bazel Testing Guide <bazel>` for information on the macros used to define tests (e.g., ``envoy_cc_test``).

.. _unit_testing_mock_selection:

Mock Selection Strategy (Strictness Baseline)
---------------------------------------------

Choosing between ``StrictMock``, ``NiceMock``, and ``Mock`` is a critical part of writing maintainable tests. Envoy follows a "Strictness Baseline" to ensure tests are neither too fragile nor too permissive.

StrictMock (Default for SUT Collaborators)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

By default, use ``StrictMock`` for the **System Under Test (SUT)** and its **direct collaborators**.

*   **Why**: It ensures that any unexpected calls to the collaborator are treated as failures. This helps catch logic errors where the SUT interacts with its dependencies in unintended ways.
*   **Usage**: ``testing::StrictMock<MockClass> collaborator;``

NiceMock (For "Noise" Dependencies)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Use ``NiceMock`` for dependencies that are necessary for the SUT to function but whose specific interactions are not being tested in the current scope. These are often referred to as "noise" dependencies.

*   **Common Examples**:
    *   ``Api::Api``
    *   ``Stats::Store``
    *   ``Runtime::Loader``
    *   ``LocalInfo::LocalInfo``
*   **Why**: It suppresses warnings about "uninteresting" calls, keeping test output clean and focusing only on relevant interactions.
*   **Usage**: ``testing::NiceMock<MockClass> noise_dependency;``

Mock Injection Patterns
-----------------------

Mocks should be injected into the SUT using one of the following patterns:

Constructor Injection
~~~~~~~~~~~~~~~~~~~~~

The most common pattern. Pass mock objects (usually by reference or shared pointer) to the SUT's constructor.

.. code-block:: cpp

  NiceMock<Runtime::MockLoader> runtime;
  StrictMock<MockCollaborator> collaborator;
  SUT sut(runtime, collaborator);

Factory Injection
~~~~~~~~~~~~~~~~~

For dependencies created at runtime, use a factory interface that can be mocked to return your mock objects.

Test Fixture Design
-------------------

To reduce boilerplate and promote reuse, Envoy uses Mixins and Base classes for complex test setups.

Mixin and Base Classes
~~~~~~~~~~~~~~~~~~~~~~

A common pattern is to define a ``*Mixin`` class that implements a configuration interface (e.g., ``ConnectionManagerConfig``) and contains all the necessary mocks and setup logic.

The actual test fixture then inherits from both the Mixin and ``testing::Test``.

.. code-block:: cpp

  class MyComponentTest : public MyComponentMixin, public testing::Test {
    // Test cases go here
  };

Example: ``HttpConnectionManagerImplMixin`` in ``test/common/http/conn_manager_impl_test_base.h`` is a canonical example of this pattern.

Time Manipulation
-----------------

For logic that depends on time (timeouts, intervals, etc.), use the ``SimulatedTimeSystem``. This allows you to advance time manually without relying on the system clock, making tests deterministic and fast.

.. code-block:: cpp

  Envoy::Event::SimulatedTimeSystem test_time;
  // ...
  test_time.advanceTimeWait(std::chrono::milliseconds(100));

For more details, see the :doc:`Testing Utilities <utilities>` guide.

Gold Standard Examples
----------------------

Envoy's codebase contains thousands of tests. The following examples are considered "Gold Standard" because they demonstrate clean, maintainable, and effective testing patterns.

1. Simple State Validation: IsolatedStoreImpl
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

**File**: `test/common/stats/isolated_store_impl_test.cc <https://github.com/envoyproxy/envoy/blob/main/test/common/stats/isolated_store_impl_test.cc>`_

This test is an excellent example of testing a component with minimal dependencies. It focuses on validating that the internal state of the ``IsolatedStoreImpl`` correctly reflects the operations performed on it.

.. code-block:: cpp

  TEST_F(StatsIsolatedStoreImplTest, All) {
    EXPECT_TRUE(store_->fixedTags().empty());
    ScopeSharedPtr scope1 = scope_->createScope("scope1.");
    Counter& c1 = scope_->counterFromString("c1");
    
    // Validate name concatenation in scopes
    EXPECT_EQ("c1", c1.name());
    EXPECT_EQ("scope1.c2", c2.name());

    // Basic state transition: increment and verify
    c1.add(100);
    auto found_counter = scope_->findCounter(c1_name.statName());
    ASSERT_TRUE(found_counter.has_value());
    EXPECT_EQ(100, found_counter->get().value());
  }

**Key Patterns**:
* **Minimal Mocking**: Since the component is relatively self-contained, the test relies on real objects where possible, reducing the risk of "testing the mock" instead of the implementation.
* **Direct Assertion**: Uses ``EXPECT_EQ`` and ``ASSERT_TRUE`` to verify the exact state of the system after each operation.

2. Complex State Machines: Connection Manager
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

**File**: `test/common/http/conn_manager_impl_test.cc <https://github.com/envoyproxy/envoy/blob/main/test/common/http/conn_manager_impl_test.cc>`_

The HttpConnectionManager (HCM) is one of Envoy's most complex components. Its tests use a robust fixture and Mixin pattern to manage this complexity.

.. code-block:: cpp

  TEST_F(HttpConnectionManagerImplTest, HeaderOnlyRequestAndResponse) {
    setup(SetupOpts().setTracing(false)); // Use a setup helper for boilerplate

    std::shared_ptr<MockStreamDecoderFilter> filter(new NiceMock<MockStreamDecoderFilter>());

    // Set expectations for the filter interaction
    EXPECT_CALL(*filter, decodeHeaders(_, true))
        .WillRepeatedly(Invoke([&](RequestHeaderMap& headers, bool) -> FilterHeadersStatus {
          return FilterHeadersStatus::StopIteration;
        }));

    // Trigger the state machine via codec dispatch
    EXPECT_CALL(*codec_, dispatch(_))
        .WillRepeatedly(Invoke([&](Buffer::Instance& data) -> Http::Status {
          decoder_ = &conn_manager_->newStream(response_encoder_);
          // ... simulate headers arriving ...
          return Http::okStatus();
        }));
  }

**Key Patterns**:
* **Test Fixture Mixins**: Inherits from ``HttpConnectionManagerImplTestBase`` to share complex setup logic (codecs, dispatchers, filters) across dozens of test files.
* **Protocol-Level Simulation**: Instead of calling methods on the HCM directly, the test simulates data arriving over the network via the ``codec_`` mock, exercising the full state machine.

3. Filter Lifecycle: Filter Manager
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

**File**: `test/common/http/filter_manager_test.cc <https://github.com/envoyproxy/envoy/blob/main/test/common/http/filter_manager_test.cc>`_

This test demonstrates how to verify the interaction between a manager and the filters it controls.

.. code-block:: cpp

  TEST_F(FilterManagerTest, RequestHeadersOrResponseHeadersAccess) {
    initialize();

    auto decoder_filter = std::make_shared<NiceMock<MockStreamDecoderFilter>>();
    
    // Verify that the factory correctly adds the filter to the chain
    EXPECT_CALL(filter_factory_, createFilterChain(_))
        .WillOnce(Invoke([&](FilterChainFactoryCallbacks& callbacks) -> bool {
          callbacks.addStreamDecoderFilter(decoder_filter);
          return true;
        }));

    // ... execute the chain and verify results ...
  }

**Key Patterns**:
* **Factory Mocking**: Mocks the ``FilterChainFactory`` to inject specific mock filters into the manager.
* **Interaction Verification**: Uses ``EXPECT_CALL`` to ensure that filters are called in the correct order and with the expected arguments.
