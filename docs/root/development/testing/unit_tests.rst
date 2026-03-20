Unit Testing
============

This guide covers core unit testing patterns and principles in Envoy. Envoy uses `Google Test (GTest) <https://google.github.io/googletest/>`_ and `Google Mock (GMock) <https://google.github.io/googletest/gmock_for_dummies.html>`_ as its primary testing framework.

.. contents::
  :local:

Foundations
-----------

Envoy unit tests are typically located in the ``test/`` directory, mirroring the structure of the ``source/`` directory. For example, tests for ``source/common/http/`` are located in ``test/common/http/``.

All unit tests are built and run using Bazel. Refer to the :doc:`Bazel Testing Guide <bazel>` for information on the macros used to define tests (e.g., ``envoy_cc_test``).

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
