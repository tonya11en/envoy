Testing Envoy
=============

Envoy's testing infrastructure is designed to provide high confidence in the correctness and 
performance of the codebase while maintaining a fast, deterministic developer workflow. 
Writing tests in Envoy is not just about coverage; it's about ensuring that complex, 
highly-concurrent networking logic behaves predictably under all conditions.

Testing Philosophy
------------------

The Envoy testing framework is built on several core principles that every developer 
should understand:

1. **Determinism over Realism**: While we strive for realistic testing scenarios, we 
   prioritize determinism. This means we often prefer simulated time and mocked 
   interfaces over real-world system calls that can introduce non-deterministic 
   behavior (flakiness).
2. **Shared-Nothing Isolation**: Tests are designed to be isolated. We avoid shared 
   global state and ensure that each test case can run independently and in parallel 
   with others.
3. **Simulated Time**: One of Envoy's most powerful testing features is the 
   ``SimulatedTimeSystem``. This allows us to test time-dependent logic (like timeouts 
   and retries) instantly and deterministically without waiting for real-world 
   wall-clock time.
4. **Mocking by Default**: Envoy uses `GMock <https://github.com/google/googletest>`_ 
   extensively. By mocking core interfaces like ``Dispatcher``, ``Network::Connection``, 
   and ``Http::StreamEncoder``, we can verify component behavior in isolation with 
   high precision.

Developer Guides
----------------

These guides provide the conceptual foundation and practical steps for writing various 
types of tests in Envoy.

.. toctree::
  :maxdepth: 2

  bazel
  unit_tests
  integration_tests
  utilities
  mock_catalog
  troubleshooting
  review_checklist

Case Studies
------------

To see these principles in action, explore these detailed walkthroughs of 
high-quality existing tests. These case studies illustrate how to handle common 
challenges like complex configuration, TLS handshakes, and raw TCP streams.

.. toctree::
  :maxdepth: 1

  case_studies/ratelimit
  case_studies/tls_inspector
  case_studies/tcp_proxy

Future Work & Maintenance
-------------------------

As Envoy's testing framework evolves, this documentation must be kept up to date.

Updating Existing Guides
^^^^^^^^^^^^^^^^^^^^^^^^

If you identify a gap or inaccuracy in an existing guide, please submit a PR with the
necessary corrections. All changes should follow the :ref:`testing_review_checklist`.

Adding New Guides
^^^^^^^^^^^^^^^^^

When adding documentation for a new testing utility or a major framework change:

1.  **File Naming**: Use lowercase, snake_case for all file names (e.g., ``new_utility_guide.rst``).
2.  **RST Format**: Follow the standard header hierarchy and Sphinx directive patterns used in existing docs.
3.  **Cross-Linking**: Ensure the new guide is linked from the main ``index.rst`` and references related utilities or case studies.
4.  **Verification**: If possible, perform a :doc:`Verification Walkthrough <verification_walkthrough>` to ensure the new content is actionable for other developers.
