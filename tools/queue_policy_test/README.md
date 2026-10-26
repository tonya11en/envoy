# Envoy Queue Policy Latency Benchmark Suite

Repeatable synthetic HTTP benchmark harness that measures client-observed request latencies across Envoy upstream connection pool queue policies (`envoy.queue_policy`).

## Architecture

```
+-----------------------------------------------------------------------+
|                       Nighthawk HTTP Clients                          |
|                                                                       |
|  +-----------------------+   +-----------------------+                |
|  | Well-Behaved Clients  |   | Noisy Neighbor Client |                |
|  | (wb1, wb2, wb3)       |   | (noisy)               |                |
|  | 60 RPS each, t=0..30s |   | 500 RPS, t=10..25s    |                |
|  | x-client-id: wb1..3   |   | x-client-id: noisy    |                |
|  +-----------+-----------+   +-----------+-----------+                |
|              |                           |                            |
|              +-------------+-------------+                            |
|                            | HTTP/1.1 (Port 10000)                    |
|                            v                                          |
|              +---------------------------+                            |
|              |        Envoy Proxy        |                            |
|              |     (--concurrency 1)     |                            |
|              |  pending_stream_queue_    |                            |
|              |  max_connections: 4       |                            |
|              +-------------+-------------+                            |
|                            | HTTP/1.1 (Port 10001)                    |
|                            v                                          |
|              +---------------------------+                            |
|              |   Nighthawk Test Server   |                            |
|              |  dynamic-delay: 0.010s    |                            |
|              |  (400 RPS Total Capacity) |                            |
|              +---------------------------+                            |
+-----------------------------------------------------------------------+
```

### How Queueing Is Induced

1. **Deterministic Upstream Capacity**: Envoy connects to `nighthawk_test_server` over HTTP/1.1 with `circuit_breakers.thresholds.max_connections: 4` and `max_pending_requests: 10000`. The backend applies a fixed `10ms` (`0.010s`) static delay per request, yielding a total upstream capacity of **400 RPS** (`4 connections * 100 RPS/conn`).
2. **Baseline Phase (`0s–10s` and `25s–30s`)**: Three well-behaved clients (`wb1`, `wb2`, `wb3`) each send `60 RPS` (`180 RPS` combined, below the `400 RPS` capacity). Requests do not wait in `pending_stream_queue_`, and client-observed latencies remain near the `10ms` service time.
3. **Noisy-Neighbor Burst (`10s–25s`)**: The noisy neighbor client (`noisy`) sends `500 RPS` with `120` concurrent connections, pushing total arrival rate to `680 RPS` and building a standing backlog in `ConnPoolImplBase::pending_stream_queue_`.

## Running the Benchmark

Build `envoy-static` and run the suite:

```bash
bazel build //source/exe:envoy-static
./tools/queue_policy_test/run_queue_benchmark.sh
```

To regenerate plots from existing JSON results without re-running traffic:

```bash
./tools/queue_policy_test/run_queue_benchmark.sh --plot-only
```

## Generated Conference Talk Graphs

All outputs are saved to `tools/queue_policy_test/results/` in both 300-DPI `.png` and vector `.pdf` formats:

- `latency_cdf_by_policy.{png,pdf}`: Side-by-side panels per queue policy showing the full client-observed latency CDF (`0%–99.5%`, log scale) for all clients.
- `latency_percentiles_bar.{png,pdf}`: Grouped bar chart comparing `P50`, `P90`, `P99`, and `P99.9` client latencies across clients and policies.
- `latency_cdf_cross_policy.{png,pdf}`: Cross-policy CDF overlay comparing Well-Behaved clients vs. the Noisy Neighbor across queue policies.
- `queue_and_latency_timeseries.{png,pdf}`: Three-row timeseries showing per-client P90 latency, Envoy pending queue depth (`upstream_rq_pending_active`), and per-client throughput (`RPS`) across the 30-second run.
- `queue_policy_summary_dashboard.{png,pdf}`: Combined 4-row overview figure.

## Adding the Fair Queue Extension

Edit `tools/queue_policy_test/scenario.yaml` and append your new policy under `policies:`:

```yaml
policies:
- id: fifo
  label: "FIFO Queue"
  queuing_policy:
    name: envoy.queue_policy.fifo
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.queue_policy.fifo.v3.FifoQueuePolicyConfig

- id: fair_queue
  label: "Fair Queue"
  queuing_policy:
    name: envoy.queue_policy.fair_queue
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.queue_policy.fair_queue.v3.FairQueuePolicyConfig
```

You can also customize client counts, RPS, burst timing, and per-client headers under `clients:` in `scenario.yaml`.
