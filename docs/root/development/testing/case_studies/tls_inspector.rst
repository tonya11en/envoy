.. _testing_case_study_tls_inspector:

Case Study: TLS Inspector Listener Filter Integration Test
===========================================================

This case study examines the integration tests for the TLS Inspector listener filter,
highlighting how to test "pre-handshake" logic and filter chain selection in Envoy.

The source code for this test can be found in
``test/extensions/filters/listener/tls_inspector/tls_inspector_integration_test.cc``.

Narrative A: Day in the Life
----------------------------

The TLS Inspector is unique because it operates on the raw connection bytes *before* a
filter chain or transport socket is even selected.

1.  **Connection Initiation**: A test client (e.g., using ``Ssl::createClientSslTransportSocketFactory``)
    initiates a TCP connection to Envoy.
2.  **Listener Filter Execution**: Upon acceptance, Envoy starts running the configured
    listener filters. The ``tls_inspector`` is typically one of the first to run.
3.  **ClientHello Sniffing**: The inspector peeks at the incoming data stream, looking for
    a TLS ClientHello. It does not "consume" the data; the bytes remain in the buffer for
    the eventual TLS handshake.
4.  **SNI and ALPN Extraction**: The filter parses the ClientHello to extract the
    Server Name Indication (SNI) and Application-Layer Protocol Negotiation (ALPN)
    values.
5.  **State Population**: The extracted SNI and ALPN are stored in the connection's
    internal state, making them available for the next phase.
6.  **Filter Chain Selection**: Once the inspector finishes (or times out), Envoy uses the
    captured SNI and ALPN to find a matching filter chain. For example, a chain might be
    selected only if the ALPN is ``h2``.
7.  **Handshake Completion**: The selected transport socket (usually a TLS socket) then
    performs the actual cryptographic handshake using the same ClientHello bytes.

Narrative B: Component-by-Component
-----------------------------------

The Test Client (TLS-Enabled)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
Unlike basic HTTP tests, the client here must be capable of performing a TLS handshake.
The test uses ``Ssl::createClientSslTransportSocketFactory`` to create a real SSL socket
that sends a valid ClientHello to Envoy.

Envoy Listener (The Entry Point)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
The listener is configured with the ``tls_inspector`` filter. Crucially, it also
defines multiple ``filter_chains`` with varying ``filter_chain_match`` criteria:
*   **Match by SNI**: Choosing a chain based on the domain requested.
*   **Match by ALPN**: Choosing a chain based on the protocol (e.g., ``envoyalpn``).

The Listener Filter Manager
^^^^^^^^^^^^^^^^^^^^^^^^^^^
This component manages the execution and lifecycle of listener filters. In the test
``RequestedBufferSizeCanStartBig``, we see how multiple listener filters (like a
custom ``LargeBufferListenerFilter``) can interact and how the manager handles
buffer growth.

Common Pitfalls
---------------

Initial Data Read
  The TLS Inspector relies on the client sending the ClientHello immediately. If the
  client waits for a server greeting (like in some SMTP flows), the inspector will
  timeout unless configured otherwise.

Listener vs. Network/HTTP Filters
  Listener filters run on the raw socket. They do not have access to HTTP-level
  constructs like headers or paths. Tests for listener filters should focus on
  socket-level data and filter chain selection logic.

Buffer Size Limits
  If a ClientHello is exceptionally large (e.g., due to many extensions or large SNI),
  it might exceed the inspector's ``initial_read_buffer_size``. The test
  ``RequestedBufferSizeCanGrow`` demonstrates how Envoy handles this by expanding
  the read buffer to accommodate the full ClientHello.

Troubleshooting
---------------

.. list-table::
   :widths: 20 40 40
   :header-rows: 1

   * - Symptom
     - Cause
     - Fix
   * - ``FilterChainNotFound`` in access logs
     - The inspector failed to extract the expected SNI/ALPN, or the values did not
       match any configured filter chain.
     - Verify the client's TLS configuration (SNI/ALPN) and ensure the listener's
       ``filter_chain_match`` blocks are correctly defined.
   * - Connection timeout
     - The client did not send enough data for the inspector to identify the TLS
       handshake, or the network was too slow.
     - Check the ``listener_filters_timeout`` setting in the bootstrap and ensure
       the client initiates the handshake immediately after connecting.
   * - Handshake failure after successful inspection
     - The inspector correctly identified the SNI/ALPN, but the selected transport
       socket has a configuration error (e.g., missing certificate).
     - Check the ``common_tls_context`` of the *selected* filter chain for errors
       unrelated to the inspector itself.
