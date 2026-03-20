.. _testing_review_checklist:

Maintainer Review Checklist
===========================

This checklist is designed to help Envoy maintainers and contributors review new or
updated testing documentation. Adherence to these criteria ensures that the guide remains
technically accurate, consistent with Envoy's standards, and accessible to developers.

Review Criteria
---------------

Technical Accuracy
^^^^^^^^^^^^^^^^^^

* [ ] **Framework Versions**: Does the guide reflect the current versions of GTest, GMock, and Bazel used in Envoy?
* [ ] **Code Snippets**: Are all code examples syntactically correct and follow Envoy's C++ style?
* [ ] **Mock Usage**: Does the guide correctly distinguish between ``StrictMock``, ``NiceMock``, and ``Mock``?
* [ ] **Initialization**: Is the ``initialize()`` sequence correctly described for integration tests?
* [ ] **Bazel Macros**: Are the correct macros (e.g., ``envoy_cc_test``, ``envoy_cc_mock``) referenced for their respective purposes?

Style & Clarity
^^^^^^^^^^^^^^^

* [ ] **Header Hierarchy**: Does the document follow the standard reStructuredText header hierarchy (``=``, ``-``, ``^``, ``*``)?
* [ ] **Inclusive Language**: Is the language neutral and inclusive (avoiding ``whitelist``, ``blacklist``, ``master``, ``slave``)?
* [ ] **Glossary Usage**: Are technical terms correctly linked to the central glossary or explained in context?
* [ ] **Formatting**: Are code blocks, notes, and warnings used effectively and consistently?

Accessibility & Navigation
^^^^^^^^^^^^^^^^^^^^^^^^^^

* [ ] **Broken Links**: Have all ``:ref:``, ``:doc:``, and external links been verified to work?
* [ ] **Index Integration**: Is the new document correctly added to the ``index.rst`` toctree?
* [ ] **Prerequisites**: Are any required background concepts clearly stated or linked?

Maintainer Responsibilities
---------------------------

Reviewers should be assigned based on their specific expertise and roles:

*   **Senior Maintainer**: Responsible for the core architecture of the testing guide and final merge.
*   **Domain Expert**: Focuses on the technical accuracy of specific testing guides (e.g., Unit vs. Integration testing).
*   **Cross-Org Approver**: Ensures the documentation remains vendor-neutral and does not include organization-specific shortcuts.

Common Pitfalls to Avoid
------------------------

*   **DCO Sign-off**: All commits must include a valid Developer Certificate of Origin (DCO) sign-off (``git commit -s``).
*   **Grammar/Punctuation**: Envoy maintainers prioritize high-quality English and correct punctuation in documentation.
*   **Sphinx Warnings**: Ensure the build produces no warnings for internal ``:ref:`` or ``:doc:`` tags.

Maintainer Workflow
-------------------

The process for updating this documentation is as follows:

1.  **Drafting**: A contributor drafts a new guide or update in a local branch.
2.  **Self-Audit**: The author performs a self-audit using the criteria above.
3.  **PR Submission**: Submit a Pull Request (PR) to the Envoy repository.
4.  **Tagging**: Tag appropriate maintainers (e.g., those from the ``@envoyproxy/maintainers-testing`` team if applicable).
5.  **Iteration**: Respond to feedback and refine the documentation as needed.
6.  **Sign-off**: A maintainer provides a formal "Approve" review, ensuring all checklist items are addressed.

Review Etiquette
----------------

*   **Be Specific**: When identifying issues, provide clear examples or alternative suggestions.
*   **Constructive Feedback**: Focus on making the documentation more useful for the community.
*   **Promptness**: Maintainers should aim to review documentation changes with the same priority as code changes to avoid bottlenecks.
*   **PR Stality**: Contributors should actively work on open PRs. PRs with no activity for more than 7 days may be closed.
