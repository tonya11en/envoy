Bazel Macros for Testing
========================

Envoy uses custom Bazel macros to simplify the definition of tests and ensure consistent behavior (such as default mock behavior and sanitizer integration) across the codebase.

Common Tasks
------------

Adding a new unit test for an extension
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Use ``envoy_cc_test`` for standard C++ unit tests.

.. code-block:: python

   load("//bazel:envoy_build_system.bzl", "envoy_cc_test")

   envoy_cc_test(
       name = "my_extension_test",
       srcs = ["my_extension_test.cc"],
       deps = [
           "//source/extensions/my_extension:my_extension_lib",
           "//test/test_common:utility_lib",
       ],
   )

**Canonical Example:** `test/common/common/base64_test.cc <https://github.com/envoyproxy/envoy/blob/main/test/common/common/base64_test.cc>`_

Creating a test library
~~~~~~~~~~~~~~~~~~~~~~~

Use ``envoy_cc_test_library`` for code that is shared across multiple test targets.

**Canonical Example:** ``//test/test_common:utility_lib``

Defining a mock
~~~~~~~~~~~~~~~

Use ``envoy_cc_mock`` to define libraries containing mocks. This macro is a wrapper around ``envoy_cc_test_library`` with PCH (Precompiled Headers) disabled to avoid common mocking issues.

**Canonical Example:** ``//test/mocks/http:http_mocks``

Adding a fuzz test
~~~~~~~~~~~~~~~~~~

Use ``envoy_cc_fuzz_test`` to define a libFuzzer-based fuzz test.

**Canonical Example:** `test/common/common/base64_fuzz_test.cc <https://github.com/envoyproxy/envoy/blob/main/test/common/common/base64_fuzz_test.cc>`_

Writing a shell test
~~~~~~~~~~~~~~~~~~~~

Use ``envoy_sh_test`` for tests written in Bash. If ``coverage = True`` (default), the macro generates a C++ wrapper to ensure the shell test contributes to coverage reports.

**Canonical Example:** `test/server/hot_restart_test.sh <https://github.com/envoyproxy/envoy/blob/main/test/server/hot_restart_test.sh>`_

Adding an integration test
~~~~~~~~~~~~~~~~~~~~~~~~~~

Integration tests also use ``envoy_cc_test`` but typically depend on integration-specific libraries.

**Canonical Example:** `test/extensions/filters/http/ratelimit/ratelimit_integration_test.cc <https://github.com/envoyproxy/envoy/blob/main/test/extensions/filters/http/ratelimit/ratelimit_integration_test.cc>`_

Why use these macros?
---------------------

* **Strict Mock Behavior:** By default, ``envoy_cc_test`` passes ``--gmock_default_mock_behavior=2`` to the test binary. This ensures that all mocks behave as ``StrictMock``, forcing developers to explicitly define expected behaviors and preventing "uninteresting call" warnings from masking bugs.
* **Sanitizer Integration:** The macros automatically handle the necessary compiler and linker flags for AddressSanitizer (ASAN), ThreadSanitizer (TSAN), and other sanitizers when they are enabled via Bazel config.
* **Hermeticity:** They ensure that tests are run in a hermetic environment with properly defined data dependencies.

Troubleshooting & Common Errors
-------------------------------

Visibility
~~~~~~~~~~

If your test cannot find a header from a library it depends on, check the ``visibility`` of that library in its ``BUILD`` file. Most Envoy libraries are restricted to specific packages.

Dependency Resolution
~~~~~~~~~~~~~~~~~~~~~

* **deps:** Used for internal Envoy targets.
* **external_deps:** Used for third-party dependencies defined in ``bazel/repository_locations.bzl``.

Common Sanitizer Issues
~~~~~~~~~~~~~~~~~~~~~~~

If a test fails under ASAN but passes normally, it often indicates a memory leak or use-after-free. The macros ensure that ``llvm-symbolizer`` is available in the test environment to provide readable stack traces.
