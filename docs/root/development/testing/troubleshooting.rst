Deciphering GMock Failures
==========================

This guide helps you understand and resolve common Google Mock (GMock) error messages encountered in Envoy's CI and local testing.

.. contents::
  :local:

Common Failure Modes
--------------------

GMock failures are often descriptive, but their verbosity can be overwhelming. This section breaks down the most frequent errors into a "Symptom -> Cause -> Fix" format.

1. Unmatched Call
~~~~~~~~~~~~~~~~~

**Symptom**:
A test fails with a message like:
``Actual function call count doesn't match EXPECT_CALL(*mock, Method(...))...``
or
``Actual: [call] (no matching expectation)``

**Cause**:
The System Under Test (SUT) called a method on a mock object, but no ``EXPECT_CALL`` was defined for that specific call, or the arguments provided did not match the defined expectation.

**Fix**:
*   **Check Arguments**: Ensure that the arguments in your ``EXPECT_CALL`` match what the SUT is providing. Use ``testing::_`` for arguments you don't care about.
*   **Missing Expectation**: Add an ``EXPECT_CALL`` for the method. If the call is expected but its return value doesn't matter, ensure you at least define the call.
*   **Check Call Count**: If using ``.Times(N)``, verify that the SUT actually calls the method exactly N times.

2. Strictness Violation
~~~~~~~~~~~~~~~~~~~~~~~

**Symptom**:
The test fails with a message indicating an "uninteresting mock function call," and the mock object is a ``StrictMock``.

**Cause**:
You are using ``testing::StrictMock<MockClass>``, which treats any call not explicitly covered by an ``EXPECT_CALL`` as a failure.

**Fix**:
*   **Add Expectation**: If the call is relevant to the test, add an ``EXPECT_CALL``.
*   **Use NiceMock**: If the call is "noise" (e.g., logging, stats increment) and not relevant to the current test case, change the mock to ``testing::NiceMock<MockClass>``.
*   **Review Strictness**: Ensure you are following the :ref:`Mock Selection Strategy <unit_testing_mock_selection>`.

3. Unexpected Return (Return value undefined)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

**Symptom**:
A crash or a GMock warning: ``The mock function has no default action set, and its return type has no default value.``

**Cause**:
A mock method with a non-void return type was called, but no ``.WillOnce(Return(...))`` or ``.WillRepeatedly(Return(...))`` was specified. GMock doesn't know what to return to the SUT.

**Fix**:
Add a return action to your expectation:
.. code-block:: cpp

  EXPECT_CALL(*mock, getValue()).WillOnce(Return(42));

4. Memory Leaks in Mocks
~~~~~~~~~~~~~~~~~~~~~~~~

**Symptom**:
LSAN (Leak Sanitizer) reports a leak in a test, often pointing to GMock internal structures or the mock object itself.

**Cause**:
GMock stores expectations in the mock object. If a mock object is not destroyed at the end of a test, or if it's held in a static variable, the expectations (and any captured lambda state) may appear as leaks.

**Fix**:
*   **Fixture Cleanup**: Ensure mock objects are members of the test fixture or are wrapped in ``std::unique_ptr`` that gets reset in ``TearDown()``.
*   **Verify Destruction**: If a mock is passed to the SUT via a ``shared_ptr``, ensure the SUT actually releases its reference.

Advanced Debugging
------------------

If you're still stuck, consider these techniques:

*   **GMock Verbosity**: Run the test with ``--gmock_verbose=info`` to see a detailed log of every call and how it matched (or didn't match) expectations.
*   **Check for Overlapping Expectations**: If multiple ``EXPECT_CALL`` statements match the same call, GMock uses them in reverse order of definition. Ensure your expectations aren't shadowing each other.
*   **Inspect Parameter Matchers**: If using complex matchers like ``Field()`` or ``Property()``, simplify them to ``_`` temporarily to see if the matching is the issue.
