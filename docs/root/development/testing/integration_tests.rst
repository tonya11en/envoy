.. _integration_testing_guide:

Integration Testing Guide
=========================

Introduction
------------

Envoy integration tests are designed to verify the end-to-end behavior of Envoy by simulating
real-world traffic patterns. The framework facilitates communication between a **Downstream**
client, the **Envoy** instance under test, and one or more **Upstream** fake servers.

Glossary
--------

.. glossary::

   Downstream
     The client initiating the connection to Envoy. In integration tests, this is typically
     represented by a test client provided by the framework.

   Envoy
     The proxy instance being tested. The framework manages its lifecycle, configuration,
     and networking.

   Upstream
     The fake server(s) that Envoy connects to. These servers simulate the backend services
     Envoy would proxy to in a production environment.

   Codec Client
     The internal test client (``IntegrationCodecClient``) used to communicate with Envoy.
     It provides high-level APIs for sending requests and receiving responses over various
     protocols (HTTP/1.1, HTTP/2, etc.).

Core Classes
------------

Integration tests are built upon a hierarchy of classes that provide increasing levels of
abstraction:

* **BaseIntegrationTest**: The foundational class for all integration tests. It manages the
  Envoy process lifecycle, port allocation, and basic networking setup.
* **HttpIntegrationTest**: A specialized base class for testing HTTP and gRPC flows. It
  provides high-level helpers for creating HTTP connections and managing streams.

The Standard Sequence
---------------------

A typical integration test follows a standard initialization and execution sequence:

1. **initialize()**: This method starts the fake upstream servers and then launches the Envoy
   instance with the configured bootstrap and filter chain.
2. **makeHttpConnection()**: Establishes a connection from the **Codec Client** to Envoy's
   listening port.
3. **sendRequest()**: Initiates test traffic by sending headers (and optionally a body) from
   the client through Envoy to the upstream.

Configuration & Startup
-----------------------

Integration tests often require modifying the default Envoy configuration to test specific
features or filter behaviors. Configuration changes **must** happen before calling
``initialize()``.

Config Modification Strategies
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

There are two primary ways to modify the configuration:

Option A: ConfigHelper
^^^^^^^^^^^^^^^^^^^^^^

The ``ConfigHelper`` (available via ``config_helper_``) provides high-level utilities for common
alterations. It is the preferred method for standard tasks such as:

*  **Adding Filters**: ``config_helper_.prependFilter(config)`` or ``config_helper_.addFilter(config)``.
*  **Setting Protocols**: ``config_helper_.setDownstreamProtocol(Http::CodecType::HTTP2)``.
*  **TLS Configuration**: Using ``ServerSslOptions`` to configure SSL contexts.

Option B: addConfigModifier
^^^^^^^^^^^^^^^^^^^^^^^^^^^

For custom Protobuf tweaks that aren't covered by ``ConfigHelper``'s dedicated methods, use
``addConfigModifier``. This allows you to provide a lambda that directly manipulates the
underlying Envoy configuration (Bootstrap, HttpConnectionManager, etc.).

.. code-block:: cpp

   config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
     // Direct modification of the bootstrap proto
     bootstrap.mutable_node()->set_id("test_node");
   });

Discovery Guidance
~~~~~~~~~~~~~~~~~~

To find available configuration fields, explore the Proto definitions in the ``api/``
directory. Each Envoy extension and core component defines its configuration schema using
Protocol Buffers.

Upstreams & Clients
-------------------

The integration framework provides specialized classes for simulating backends (Upstreams)
and clients (Downstream).

Upstream Types
~~~~~~~~~~~~~~

Choosing the right upstream type depends on the level of control your test requires:

*  **FakeUpstream (Control)**: Use this when you need precise timing, specific request
   ordering, or manual response encoding. It requires explicit calls to wait for and handle
   requests (e.g., ``waitForNextUpstreamRequest()``).
*  **AutonomousUpstream (Simplicity)**: Ideal for basic success flows or when the exact
   timing of upstream interaction is not critical. It automatically responds to incoming
   requests based on pre-defined logic, reducing boilerplate code.

Common Client Methods
~~~~~~~~~~~~~~~~~~~~~

The ``IntegrationCodecClient`` provides several methods for initiating traffic:

*  **makeHttpConnection(lookupPort("http"))**: Establishes the connection to Envoy. Always
   use ``lookupPort()`` instead of hardcoded port numbers.
*  **makeRequestWithBody(headers, body)**: Sends a request and returns a handle for
   tracking the response.
*  **sendRequestAndWaitForResponse(headers, body, ...)**: A unified helper that sends a
   request and waits for the full response, ideal for simple unary flows.

Common Pitfalls
---------------

When writing integration tests, developers often encounter the following issues:

Content-Length Mismatch
  **Symptom**: ``waitForEndStream()`` or ``sendRequestAndWaitForResponse()`` times out.
  **Cause**: The response body length sent from the fake upstream does not match the
  ``content-length`` header.
  **Fix**: Ensure that the data size in ``encodeData()`` matches the header value.

Protocol Mismatch
  **Symptom**: The connection fails or is reset immediately.
  **Cause**: Inconsistent protocol configuration between the client, Envoy listener, and
  upstream (e.g., trying to speak HTTP/1 to an HTTP/2 listener).
  **Fix**: Use ``setDownstreamProtocol()`` and ``setUpstreamProtocol()`` to ensure all
  components are aligned.
