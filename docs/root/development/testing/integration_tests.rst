.. _integration_testing_guide:

Integration Testing Guide
=========================

While unit tests ensure the correctness of individual components, Envoy's 
integration tests verify the behavior of the system as a whole. Because Envoy is 
an intermediary in a distributed system, its integration tests are designed as 
a "miniature mesh" where the test framework orchestrates multiple concurrent 
actors to simulate real-world networking scenarios.

.. contents::
  :local:

The Actor Model in Tests
------------------------

To write effective integration tests, it's helpful to think of the framework 
as a set of distinct actors, each running on its own thread but coordinated 
by the main test thread:

1. **The Downstream Client**: This is the "caller." It initiates connections and 
   sends requests to Envoy. In the framework, this is typically an 
   ``IntegrationCodecClient``.
2. **Envoy (The SUT)**: The actual Envoy process (or a library-based instance) 
   running with a specific configuration. It listens for downstream connections 
   and proxies them to upstreams.
3. **The Upstream Server**: One or more "fake" servers that simulate the 
   backends Envoy would connect to in production. These can be "Fake" 
   (giving you manual control over every byte) or "Autonomous" (handling 
   requests automatically).

.. tip::
   Understanding which actor you are "playing" at any point in the test is 
   crucial. Are you sending a request from the client? Or are you waiting 
   for that request to arrive at the upstream?

Conceptual Lifecycle of a Test
------------------------------

A typical integration test tells the story of a request's journey through 
Envoy. The lifecycle follows a strict sequence to ensure determinism:

1. **The Setup (Constructor)**: The test environment is prepared. This is where 
   you define the "world" Envoy will live in, including how many upstreams 
   exist and what protocols they speak.
2. **The Blueprint (Config Modification)**: Before the Envoy process starts, 
   you must define its configuration. Use ``config_helper_`` to add filters, 
   enable TLS, or tweak timeouts. **Config must be finalized before the 
   server starts.**
3. **The Birth (initialize())**: This critical method brings the world to life. 
   It starts the fake upstreams and then launches the Envoy instance. **No 
   traffic can be sent before this call.**
4. **The Handshake (makeHttpConnection())**: The downstream client establishes 
    a connection to Envoy's listener.
5. **The Journey (sendRequest())**: The client sends headers and data. The 
   test thread then "switches roles" to the upstream to verify the request 
   arrived and send a response back.
6. **The Conclusion**: The client verifies it received the expected response 
   from Envoy.

.. note::
   Always ensure that any ``addConfigModifier`` or ``config_helper_`` calls 
   are made **before** calling ``initialize()``.

Configuration & Startup
-----------------------

Integration tests often require modifying the default Envoy configuration to 
test specific features or filter behaviors. Configuration changes **must** 
happen before calling ``initialize()``.

Config Modification Strategies
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

There are two primary ways to modify the configuration:

Option A: ConfigHelper
^^^^^^^^^^^^^^^^^^^^^^

The ``ConfigHelper`` (available via ``config_helper_``) provides high-level 
utilities for common alterations. It is the preferred method for standard 
tasks such as:

*  **Adding Filters**: ``config_helper_.prependFilter(config)`` or 
   ``config_helper_.addFilter(config)``. The ``config`` argument is typically 
   a raw YAML string defining the filter configuration.
*  **Setting Protocols**: ``config_helper_.setDownstreamProtocol(Http::CodecType::HTTP2)``.
*  **TLS Configuration**: Using ``ServerSslOptions`` to configure SSL contexts.

Option B: addConfigModifier
^^^^^^^^^^^^^^^^^^^^^^^^^^^

For custom Protobuf tweaks that aren't covered by ``ConfigHelper``'s dedicated 
methods, use ``addConfigModifier``. This allows you to provide a lambda that 
directly manipulates the underlying Envoy configuration (Bootstrap, 
HttpConnectionManager, etc.).

.. code-block:: cpp

   config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
     // Direct modification of the bootstrap proto
     bootstrap.mutable_node()->set_id("test_node");
   });

Upstreams & Clients
-------------------

Choosing the right tool for your upstreams and clients depends on the level 
of control and precision your test narrative requires.

The Choice of Upstream
^^^^^^^^^^^^^^^^^^^^^^

*  **FakeUpstream (The Precision Tool)**: Use this when you need to test race 
   conditions, specific request ordering, or malformed responses. It gives 
   you a "low-level" handle to the connection.
   * *Best For*: Protocol-level testing, timeout verification, and error handling.
*  **AutonomousUpstream (The Convenience Tool)**: Ideal for "happy path" 
   testing where the exact timing of the upstream response isn't the focus. 
   It automatically responds based on pre-defined logic.
   * *Best For*: Verifying filter logic, stats, and high-level routing.

.. note::
   Integration tests use real worker threads. To maintain determinism, the 
   framework often uses ``SimulatedTimeSystem`` to broadcast time advances 
   to all registered event loops (Server, Client, and Upstream).

Common Client Methods
^^^^^^^^^^^^^^^^^^^^^

The ``IntegrationCodecClient`` is your primary way to drive traffic:

*  **makeHttpConnection(lookupPort("http"))**: Establishes the connection. 
   The name ``"http"`` must match the listener name in your configuration.
*  **makeRequestWithBody(headers, body)**: Sends a request and returns a 
   handle for tracking the response.
*  **sendRequestAndWaitForResponse(headers, body, ...)**: A unified helper 
   that simplifies unary request/response flows.

Diagnostic Troubleshooting
--------------------------

When an integration test fails, it often tells a story of a "miscommunication" 
between the actors. Use these diagnostics to identify the root cause:

The "Silent Treatment" (Timeouts)
  **Symptom**: ``waitForEndStream()`` or ``sendRequestAndWaitForResponse()`` 
  times out.
  **Diagnosis**: This usually means one actor is waiting for data that never 
  arrived.
  **Check**:
  * Did you send the correct ``Content-Length``? If the body is smaller than 
    the header suggests, the receiver will wait forever.
  * Did you forget to call ``initialize()`` before sending traffic?

The "Immediate Rejection" (Connection Reset)
  **Symptom**: The connection fails or is reset immediately after 
  ``makeHttpConnection()``.
  **Diagnosis**: This is often a protocol or TLS mismatch at the edge.
  **Check**:
  * Is the client speaking HTTP/2 to an HTTP/1.1 listener?
  * If using TLS, do the certificates and ALPN settings match on both sides?

The "State Machine Stall"
  **Symptom**: The test hangs after the upstream receives headers but before 
  it can send a response.
  **Diagnosis**: A filter in Envoy's chain might be returning 
  ``FilterHeadersStatus::StopIteration`` without ever resuming the chain.
  **Check**: Verify that your filter's ``decodeHeaders`` logic correctly 
  resumes iteration or sends a local response.
