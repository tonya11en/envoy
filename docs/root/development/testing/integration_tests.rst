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
