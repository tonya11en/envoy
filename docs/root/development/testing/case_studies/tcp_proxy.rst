.. _tcp_proxy_case_study:

Case Study: TCP Proxy Integration Test
======================================

This case study explores the integration testing patterns for raw TCP traffic in Envoy, using the
TCP Proxy filter as the primary example. Unlike HTTP-based tests that operate on streams and
headers, TCP integration tests focus on the flow of raw bytes between a downstream client and an
upstream server.

Day in the Life of a TCP Request
--------------------------------

The following narrative traces a "hello" message as it moves from the test client through Envoy's
TCP Proxy to the fake upstream.

1. **Initialization**: The test environment is set up by calling ``initialize()``, which starts
   Envoy with a TCP Proxy configuration and prepares the fake upstreams.
2. **Connection**: The test creates a downstream connection using ``makeTcpConnection(lookupPort("tcp_proxy"))``.
3. **Downstream Write**: The client sends raw bytes by calling ``tcp_client->write("hello")``.
4. **Upstream Reception**: Envoy's TCP Proxy filter receives the bytes and forwards them to the
   configured upstream cluster. The test waits for this connection to be established on the
   upstream side using ``fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection)``.
5. **Data Verification**: The test confirms the upstream received the expected data using
   ``fake_upstream_connection->waitForData(5)``.
6. **Upstream Response**: The fake upstream echoes data back (e.g., "world") using
   ``fake_upstream_connection->write("world")``.
7. **Client Reception**: Finally, the test verifies the client received the response using
   ``tcp_client->waitForData("world")``.

Component-by-Component Breakdown
--------------------------------

The TCP integration framework consists of three primary components, each playing a specific role
in the byte flow.

IntegrationTcpClient (Downstream)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The ``IntegrationTcpClient`` simulates a raw TCP client. It is responsible for:
- Initiating the connection to Envoy's listener.
- Writing raw string or buffer data to the socket.
- Providing blocking calls like ``waitForData()`` and ``waitForHalfClose()`` to synchronize the
  test with asynchronous network events.

Envoy (TCP Proxy)
^^^^^^^^^^^^^^^^^

Envoy acts as the intermediary, running the ``TcpProxy`` network filter. Its role is to:
- Accept the downstream connection.
- Perform load balancing to select an upstream host.
- Tunnel all subsequent bytes bidirectionally between the downstream and upstream sockets without
  inspecting the application-level protocol.

FakeRawConnection / FakeUpstream (Upstream)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The ``FakeUpstream`` serves as the backend service. For TCP tests, it typically provides a
``FakeRawConnection`` which allows the test to:
- Acknowledge new connections from Envoy.
- Read bytes directly from the upstream socket.
- Write response bytes back to Envoy.

Common Pitfalls
---------------

When testing TCP flows, developers frequently encounter these two synchronization issues:

Missing Connection Wait
  **Symptom**: The test hangs or fails when trying to read/write to the upstream.
  **Cause**: Forgetting to call ``waitForRawConnection()``. Unlike HTTP tests where
  ``waitForNextUpstreamRequest()`` handles the connection implicitly, TCP tests require
  explicitly waiting for the raw connection handle.
  **Fix**: Always use ``ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection))``
  before interacting with the upstream.

Premature Client Checks
  **Symptom**: ``tcp_client->data()`` is empty when it should contain the response.
  **Cause**: Calling ``data()`` before the bytes have actually arrived over the network.
  **Fix**: Always use ``waitForData("expected_string")`` to ensure the test blocks until the
  necessary data has been received and buffered by the client.

Example Pattern
---------------

The following snippet illustrates a standard bidirectional TCP test pattern found in
``test/integration/tcp_proxy_integration_test.cc``:

.. code-block:: cpp

   // 1. Start Envoy and the upstreams
   initialize();

   // 2. Connect the client to Envoy
   IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));

   // 3. Write data from the client
   ASSERT_TRUE(tcp_client->write("hello"));

   // 4. Wait for Envoy to connect to the upstream
   FakeRawConnectionPtr fake_upstream_connection;
   ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

   // 5. Verify the upstream received the data and respond
   ASSERT_TRUE(fake_upstream_connection->waitForData(5));
   ASSERT_TRUE(fake_upstream_connection->write("world"));

   // 6. Verify the client received the response
   tcp_client->waitForData("world");
   tcp_client->close();
