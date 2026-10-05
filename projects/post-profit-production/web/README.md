<!-- SPDX-License-Identifier: MIT -->
# Production web view

Open [post-profit-production.html](../dist/post-profit-production.html) in a
browser that permits local HTML and WebAssembly workers. Everything needed for
simulation is embedded. The page has no remote dependencies, automatic file
access, network requests or persistent browser storage.

The production unit remains a configured instance of the general engine. The
web view does not introduce products, industries or a separate JavaScript
production model. It compiles the same C++ simulation and experiment runner as
the native command-line programs. The [simulation contract](../simulation/CONTRACT.md)
and [experiment guide](../experiments/README.md) remain authoritative.

1. Edit the complete configuration JSON, load an explicit `.cfg` file, or restore
   the abstract research example. Run the simulation to validate the raw text and
   obtain a trajectory. Duplicate keys and numeric token types are checked by the
   C++ parser; the editor does not normalize them before validation. File loading
   rejects invalid UTF-8 and preserves the loaded text for export. A single
   leading UTF-8 document marker is supported. The worker rejects actual null
   characters and unpaired Unicode surrogates before its C string call.
2. Inspect full-horizon job outcomes, resource balances and fixed compensation
   due. Select a recorded tick to inspect stage progress, waiting reasons,
   closing stocks, available and used pools, and events. Charts preserve each
   resource's and pool's configured units. Tick selection only changes the view.
3. In the scheduling experiment workspace, edit or load a complete plan. The
   bundled comparison uses the declared cases, policies and seeds. Retained
   reports show checks, skipped cases and all-seed outcome denominators. Select
   any recorded run for the same trajectory inspector. A verification failure
   is retained for inspection and gates exploration according to the runner.
4. Export the complete result and save the editor text explicitly. Cancellation
   terminates the worker and preserves the last completed report; an input error
   likewise leaves the earlier result available. Edits require a new run.

Timeline cells and curves are DOM/SVG, with tables for exact quantities. The
timeline's waiting reason is the first blocking check encountered by the model,
not a complete causal attribution. Completion marks required work done; declared
output loss may still leave no usable output. Risk values are supplied synthetic
assumptions, not fitted operating frequencies. Compensation is an obligation in
minor currency units, including idle and interrupted ticks; settlement, funding,
economic viability, worker control and human benefit are not inferred.

The download includes a corresponding source ZIP, root MIT license, JSON
provenance/license and compiler-runtime notices. Production does not compile or
bundle the restricted pricing engine. Follow the [container build guide](../environment/README.md)
to rebuild it from the repository. The source ZIP omits the external SDK and
shared container launcher; rebuilding that extracted source needs the official
Emscripten 4.0.15 SDK in Linux, at the shared path or `PRODUCTION_EMSDK`.
Runtime parity and packaging checks run inside that container;
they do not themselves establish browser layout or accessibility quality.

A separate ignored `noindex` review entry permits the Codex selector's injected
style elements while keeping scripts strict and style attributes prohibited.
That entry is excluded from the release and source bundle. Do not change the
release CSP for local review.
