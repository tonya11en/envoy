# Roadmap: Envoy Developer Testing Documentation

This roadmap outlines the phases for creating a comprehensive developer guide for testing in the Envoy codebase.

## Phases

- [x] **Phase 1: Foundations & Bazel (Internal/Build)** - Documenting the Bazel macros and core test utilities.
- [ ] **Phase 2: Unit Testing Deep Dive** - Comprehensive guide on unit testing with GTest/GMock and common mocks.
- [ ] **Phase 3: Integration Testing & Case Studies** - End-to-end testing guides and detailed walkthroughs of existing tests.
- [ ] **Phase 4: Review & Refinement** - Peer review and finalizing the documentation structure.

## Phase Details

### Phase 1: Foundations & Bazel (Internal/Build)
**Goal**: Developers understand the build infrastructure and core utilities for Envoy tests.
**Depends on**: Nothing
**Requirements**: FR-4, FR-7
**Success Criteria**:
1. A developer can identify the correct Bazel macro (`envoy_cc_test`, `envoy_cc_test_library`, etc.) for their test type.
2. A developer can use `TestUtility` and `SimulatedTimeSystem` in a new test case based on the documentation.
3. The Bazel macros reference documentation is complete and accurate.
**Plans**:
- [x] 1-01-PLAN.md — Foundations, Bazel macros, and core utilities reference.

### Phase 2: Unit Testing Deep Dive
**Goal**: Developers can write high-quality unit tests using Envoy's mocking patterns.
**Depends on**: Phase 1
**Requirements**: FR-1, FR-2, FR-6
**Success Criteria**:
1. A developer can create a unit test using `StrictMock` or `NiceMock` appropriately based on the guide.
2. The unit testing guide provides clear examples of common mocking scenarios (e.g., mocking `Dispatcher`).
3. The centralized "how-to" entry point is established and links to unit testing resources.
**Plans**:
- [x] 2-01-PLAN.md — Foundation & Mock Catalog.
- [x] 2-02-PLAN.md — Gold Standard Annotations & Troubleshooting.

### Phase 3: Integration Testing & Case Studies
**Goal**: Developers can write complex integration tests and learn from high-quality existing examples.
**Depends on**: Phase 2
**Requirements**: FR-3, FR-5
**Success Criteria**:
1. A developer can follow the step-by-step guide to write a functional integration test for an HTTP filter.
2. Three detailed case studies (e.g., RateLimit, TLS Inspector) are documented with explanatory walkthroughs.
3. The integration testing framework components (e.g., `IntegrationTestServer`) are clearly explained.
**Plans**: TBD

### Phase 4: Review & Refinement
**Goal**: The documentation is verified for accuracy, consistency, and accessibility.
**Depends on**: Phase 3
**Requirements**: V-1, V-2, NFR-1, NFR-2, NFR-3
**Success Criteria**:
1. Maintainers have reviewed and approved the documentation (V-1).
2. A developer successfully writes a sample test following the guides and provides feedback (V-2).
3. Documentation follows Envoy style (NFR-1) and is easily navigable (NFR-3).
**Plans**: TBD

## Progress

| Phase | Plans Complete | Status | Completed |
|-------|----------------|--------|-----------|
| 1: Foundations & Bazel | 1/1 | Completed | Mar 20, 2026 |
| 2: Unit Testing Deep Dive | 0/2 | In Progress | - |
| 3: Integration Testing & Case Studies | 0/0 | Not started | - |
| 4: Review & Refinement | 0/0 | Not started | - |
