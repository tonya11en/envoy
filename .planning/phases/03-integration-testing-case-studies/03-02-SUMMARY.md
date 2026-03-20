---
phase: 03-integration-testing-case-studies
plan: 02
subsystem: docs
tags: [integration-testing, case-studies, ratelimit, tls-inspector]
dependency_graph:
  requires: [03-01]
  provides: [03-02]
  affects: [docs]
tech_stack: [RST, Integration Testing]
key_files:
  - docs/root/development/testing/case_studies/ratelimit.rst
  - docs/root/development/testing/case_studies/tls_inspector.rst
decisions:
  - Used "Dual Narrative Flow" (D-01, D-02) for both case studies to provide both high-level and component-level context.
  - Included specific "Symptom -> Cause -> Fix" troubleshooting tables for each filter type.
metrics:
  duration: 15m
  completed_date: "2026-03-20"
---

# Phase 03 Plan 02: Integration Testing Case Studies Summary

This plan delivered two detailed case studies for high-quality integration tests in Envoy: the RateLimit HTTP filter and the TLS Inspector listener filter. These case studies serve as practical guides for developers, demonstrating how to test complex gRPC interactions and low-level connection sniffing.

## Key Accomplishments

### 1. RateLimit Case Study
- Created `docs/root/development/testing/case_studies/ratelimit.rst`.
- Detailed the "Day in the Life of a Request" for a RateLimit check, including the asynchronous gRPC call to the external service.
- Explained the roles of `FakeUpstream` vs. `AutonomousUpstream` in the context of gRPC service emulation.
- Provided troubleshooting steps for common issues like unexpected 429s and gRPC timeouts.

### 2. TLS Inspector Case Study
- Created `docs/root/development/testing/case_studies/tls_inspector.rst`.
- Walked through the listener filter lifecycle, focusing on how the inspector peeks at bytes before a filter chain is selected.
- Highlighted the importance of testing SNI and ALPN extraction for correct filter chain matching.
- Included troubleshooting for `FilterChainNotFound` errors and listener filter timeouts.

## Deviations from Plan

None - plan executed exactly as written.

## Known Stubs

None.

## Self-Check: PASSED

1. Created files exist:
   - `docs/root/development/testing/case_studies/ratelimit.rst`: FOUND
   - `docs/root/development/testing/case_studies/tls_inspector.rst`: FOUND
2. Commits exist:
   - `15c5dc13b0`: FOUND
   - `217dcf9b7e`: FOUND
