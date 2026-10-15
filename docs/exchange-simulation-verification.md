<!-- SPDX-License-Identifier: MIT -->
# Exchange simulation v3 verification

## 2026-10-14: daily operator workspace

The connected application now opens on Home, with the saved position, common
actions, recent activity and attention notes drawn only from recorded facts.
Record presents one sale, delivery or expense form at a time. Stock and Activity
have separate views. Advanced contains complete accounts, obligations,
read-only configuration and expandable technical records. The application
still owns the interface; each environment supplies its data and presentation
metadata. Solid earth colors and flat sections retain the project's visual
direction, with responsive layouts and keyboard navigation.

| Check | Result |
| --- | --- |
| Operator interface | Home/default/deep links, navigation, native balances, exact decimal entry and amount previews, recent/full history, negative cash after commitments, and retained drafts passed in the managed Debian container. |
| Transaction recovery | Existing duplicate-prevention, durable outbox/reload, renewed-session retry and confirmed stale-state rejection tests passed. Pending requests remain visible from every view. |
| API/accounting integration | The existing native build passed the real HTTP transaction test in 2.06 seconds. After the concurrent database update, its fresh combined build passed **22/22 tests**, including exchange storage, transactions and HTTP integration. |
| Packaged simulator | Worker/native parity, replay, failure and source-manifest checks passed. All **142 repository source files** in the refreshed archive matched disk; private environment databases are excluded. |
| Preserved simulator | Embedded runtime SHA-256 remains `5d736b58711ae6cc2cf3a029733a92fa6558812fb267d98f31388996420fb9a1`. |
| Documentation | **44 local file links** resolved in the edited project guides; whitespace checks passed. |
| Live deployment | HTTP 200 at `http://127.0.0.1:8766/`; served HTML exactly matched the reviewed source. The same container ID and database were preserved. |

Review found and corrected a skip-link/hash-routing conflict, clearing
unrelated drafts after a save, and invalid lot identifiers remaining hidden
inside their disclosure. Browser layout and visual interaction have not been
tested in a real browser in this checkpoint; UI checks use a controlled DOM.

A subsequent wording pass replaced conversational headings with direct labels
and added a standard inline ⓘ caption control to all twelve entry fields,
account figures, relevant stock/history columns and operation settings. The
expanded interface tests passed for initial collapsed state, accessible
references, click/Escape/focus behavior and preserved drafts, with no API calls
or browser-outbox writes caused by opening help. Existing transaction and
recovery checks continued to pass. This pass changes presentation only.

Help was subsequently refined to use aligned SVG icons and one floating
caption outside table containers. The expanded deterministic UI suite passed
hover/focus/touch, focus-before-click pinning, delayed pointer crossing,
Escape/outside dismissal, single-popup ownership, viewport-edge and zoom
positioning, internal scrolling, and view/workflow/render cleanup. Hidden
source descriptions remain linked to fields and figures; no accounting or
recording behavior changed. Rendered alignment still requires visual browser
review; these checks exercise DOM interactions and positioning calculations.

The first restart caught a concurrent storage update in progress: the separate
database session had changed the adapter to call `idydb_open_at` and
`idydb_discard` before the matching library snapshot arrived. Compilation stopped.
An interim package check also rejected the old archive because the new database
test sources were not yet included. The container and records were preserved,
the database update was allowed to complete, and the refreshed source package
and fresh native tests passed before ordinary launch reused container
`1c7232e713cb2c7e8449a2b11bd7f5a15df4fc634f529cea8d8e7c15de5083a1`.
The combined test log is at
`/tmp/post-profit-idydb-aggregate-refresh/Testing/Temporary/LastTest.log`
inside the existing general development container, shared with that session.

The live environment remains at revision 0 with 80 units, 9,750 cents inventory
value, 1,000 cents obligations and 94,000 cents cash after reserve and obligations.
Its database SHA-256 is unchanged:
`0b0bb11475fc7a2dad9558c9f65e612525f0900db83a9cca90048786473c72bd`.
No test transactions were inserted into it.

The UI task did not alter native accounting, pricing, consumer behavior or
simulation defaults. Of thirteen source/configuration files captured at its
start, only the independently edited storage adapter changed. Settings remain
read-only; this redesign does not add payment processing, price publication,
refunds or settlement of existing obligations.

## 2026-10-14: recorded deliveries, sales and expenses

The native exchange application now applies paid deliveries, cash sales and new
paid/unpaid expenses, persists their original events in IdyDB, and reconstructs
current accounts by replay. Shared HTTP/UI code consumes one selected operating
environment; the environment supplies configuration, presentation metadata and
records. The simulator, its objective and default data remain unchanged.

The implementation is deliberately bounded: whole pieces, two decimal currency
units, at most 100 events and a 61,440-byte stored envelope. It does not settle
old debts, issue refunds, publish prices or initiate payments. See the
[operating contract and worked example](../projects/post-profit-exchange/doc/OPERATING_ENVIRONMENT.md).

| Check | Result |
| --- | --- |
| Fresh native build, managed Debian container | **16/16 CTest entries passed**, 5.09 seconds after the final native conflict-response change. |
| Exact account transitions | Paid receipt, FIFO sale, paid expense and new unpaid wages reconcile; invalid inputs preserve records. |
| Real HTTP → C++ → IdyDB integration | Transaction submission, reopen/replay without the original import, duplicate identity retries, stale revisions and competing clients passed. |
| Storage adapter | Atomic envelope replacement, compare-and-swap conflict handling, partial-write preservation and competing processes passed. No IdyDB internals changed. |
| Connected interface logic | Exact decimal entry, native account display, durable browser retry requests, reload recovery, session renewal, definite rejection and literal-text rendering passed in Node. |
| Saved inspector | Rendering and DOM checks passed; the example database SHA-256 stayed unchanged. |
| Packaged standalone simulator | Worker/native parity, replay and manifest checks passed. Embedded runtime bytes are unchanged; the source ZIP includes the new application without private environment databases. |
| Deployment and ordinary launch | Verified Windows HTTP 200, mount boundaries and initial accounts; ordinary up reused the same running container without provisioning. |

The four-record fixture starts with 100,000 cents cash, 9,750 stock value,
1,000 obligations and zero earned funding. A receipt of ten bread units at
120 cents, a sale of fifty at 200 cents, a paid 700-cent expense and new unpaid
300-cent wages leave cash 108,100; stock 5,850; obligations 1,300; earned
funding 3,900; net assets 112,650. These are isolated synthetic test records;
they were not inserted into the user's existing example environment.

Review identified and fixed two retry hazards: losing a response across reload
or session rotation could discard the transaction identity, while retaining
every rejection could block entry forever after a confirmed stale revision.
The browser now stores the exact pending request before POST. A matching saved
event confirms success; native conflict responses explicitly establish that the
submitted event was not recorded. Ambiguous failures retain the original ID.

The tests establish these declared transitions and retry behavior. They do not
establish production capacity, complete disaster recovery, worker authorization,
tax compliance or economic viability. The local session token protects requests;
it is not an operator login. AMPL and physical payment devices were not tested.

Build and test evidence is inside the general development container at
`/tmp/exchange-transactions-20261014/native`. The approved
[application launcher](../projects/post-profit-exchange/doc/APPLICATION_SERVICE.md)
provides one container per operating environment and leaves the shared
development container available.

The vendor-operation application was provisioned and served HTTP 200 at
`http://127.0.0.1:8766/` with container ID
`1c7232e713cb2c7e8449a2b11bd7f5a15df4fc634f529cea8d8e7c15de5083a1`.
The repository mount is read-only, the selected environment is writable at
`/operation`, and the other exchange environments are hidden by a read-only
empty tmpfs. Runtime filesystem flags and contents confirmed those boundaries.
No named volumes or GPU/device access are configured; restart policy is `no`.
Dependency setup installed zero packages. The initial database SHA-256 remained
`0b0bb11475fc7a2dad9558c9f65e612525f0900db83a9cca90048786473c72bd`.

The first launch failed because port 8765 belonged to `caatuu-dev`. Its new
exchange container never started. After explicit user approval, only that
never-started exchange container was replaced using port 8766; no environment
files or other containers were removed. The launcher now checks existing Docker
publications and Windows TCP listeners before creation/start. This corrects the
missed preflight rather than relying on a particular port remaining available.

Visual browser interaction was not automated; the interface checks above use a
controlled DOM boundary, and the deployed endpoint was checked from Windows.

## 2026-10-14: exchange layout cleanup and behavior checks

The cleanup is local to `projects/post-profit-exchange`. Its root now contains
`README.md`, `CMakeLists.txt`, `doc/`, `code/` and `web/`.
Documentation and contracts live in `doc/`; C++ sources, native tests, central
configuration and example history live in `code/`; browser source, WebAssembly
build/packaging, browser-related tests and the delivered HTML live in `web/`.
Cross-project links follow the new locations. Numerical tools remain independent.

The README states the intended vendor-run, worker-governed operation and the
role of separately funded continuity assurance. This is an implementation aim:
the current simulator does not establish a guaranteed livelihood or insurance
payout. No pricing, accounting, consumer, default-data, schema or license
semantics were changed.

| Check | Result |
| --- | --- |
| Fresh native Release build | **11/11 CTest entries passed, 1.83 seconds.** |
| Native behavior before/after cleanup | **Four complete responses matched byte for byte**: empty-history defaults, explicit synthetic history, no visits, and 60 days with seed 7. |
| Source preservation | All 14 moved C++/header/template/test/configuration/history files checked were byte-identical to their pre-cleanup versions; all three browser UI source files were unchanged. |
| Configuration interface | `--example-config` exactly matched the relocated central `.cfg`. |
| Root build integration | Repository-root CMake configured successfully with `PH_BUILD_EXCHANGE=ON`; the fresh compiled/tested build was the standalone exchange entry. |
| Fresh WebAssembly | **27 native/WASM fixtures passed** using pinned Emscripten 4.0.15. |
| Packaged standalone HTML | Worker/native parity, deterministic replay, import failures, interface logic and configuration/source archive checks passed. |
| Documentation navigation | **75 local file/heading links checked**, none unresolved. |

All builds and checks ran in the existing managed
`post-profit-economics-gpu` container. The existing Linux SDK was copied to
`/opt/post-profit-exchange/emsdk-4.0.15`; its original staging copy was preserved.
Docker's Windows transfer omitted Linux symlinks; the missing links and
executable permissions were restored from the original, and the SDK activated
and built successfully. No new SDK download, dependency installation, container
creation or container-configuration change was required.

Native evidence and four saved requests/results are under
`/tmp/exchange-layout-review-20261014/` inside the container. WebAssembly
intermediate output is under `/tmp/post-profit-exchange/wasm/`, and compiler
caches are under `/opt/post-profit-exchange/cache/`.
Build/setup scripts reject workspace-mounted toolchain and intermediate paths.

The delivered HTML is now
[`web/dist/post-profit-exchange.html`](../projects/post-profit-exchange/web/dist/post-profit-exchange.html).
Its corresponding-source ZIP uses an explicit exchange release list, including
the relocated sources, configuration, examples and license notices. Private run
records, additional configurations/histories and generated HTML are excluded.
The default relative record paths are unchanged; the moved `.cfg` now places a
default persisted run at `code/runs/example`. This directory remains ignored
and the runner still refuses existing output paths.

These checks establish preservation of the declared implementation and working
build/package paths. They do not establish real customer response, economic
viability or insurance adequacy. AMPL and visual browser interaction were not
tested in this cleanup. Use the current
[environment instructions](../projects/post-profit-exchange/doc/ENVIRONMENT.md)
for builds; the documentation-only review below records the earlier checkpoint.

## 2026-10-14: implementation review and clearer documentation

Reviewed source at `d8b060f` with documentation-only working-tree changes.
The pricing policy, consumer model, accounting, configuration defaults and
delivered HTML were not changed. The [project README](../projects/post-profit-exchange/README.md)
now explains the needs-based pricing rule, a recorded two-day example, component
boundaries and the evidence still needed for practical use.

A fresh Release build ran inside the existing managed Debian container
`post-profit-economics-gpu`, ID
`d790ed5301151c20b084a703acf6f04472234deb7cd6fd2c8c5d0b218f46b40e`.
Build and test products stayed in its filesystem at
`/tmp/exchange-doc-review-20261014/native`. No dependencies were installed.

- **11/11 CTest entries passed, 1.69 seconds.** This includes the exchange file,
  consumer/forecast and simulation tests; pricing core, enumeration, contract,
  process and backend checks; and the reusable EWMA tests.
- The unchanged default configuration ran with seed 42, empty history and a
  30-day horizon. Both paths completed, with no wage or operating arrears, no
  assurance requests and successful accounting reconciliation.
- The first two optimized days reconcile with the README example: day one adds
  4,000 cents to earned funding; day two uses adjustment 1,333 and lowers bread
  from 200 to 160 cents and beans from 300 to 240 cents.
- AMPL was not enabled in this fresh build. WebAssembly, packaged-worker and
  browser checks were not rerun; the dated earlier records below remain
  historical evidence, not results of this review.

| Default 30-day result | Balancing feedback | Fixed prices |
| --- | ---: | ---: |
| Units sold | 1,535 | 903 |
| Accumulated earned funding, cents | 1,285 | 62,910 |
| Realized economic result, cents | 7,285 | 68,910 |
| Closing cash, cents | 114,935 | 172,860 |
| Closing inventory at cost, cents | 2,100 | 5,800 |
| Closing reserve earmark, cents | 11,000 | 11,000 |

Cash, inventory and earned funding are different quantities; a reserve earmark
is part of cash, not additional assets. These are synthetic model outcomes,
not a forecast of a real store. The fixed-price comparator also has no owner
withdrawals: the comparison does not isolate the benefit of removing an owner.

The review corrected the documented day order: new reserve requirements are
scheduled before procurement. It also clarified that the 200,000-combination
limit applies after local policy and affordable-alternative filtering; forecast
contribution uses replacement costs without a separate projected spoilage
charge, whereas realized accounts expense FIFO spoilage; prices affect modeled
purchases but not visits; and EWMA history dates establish ordering without
elapsed-time aging between observations.

No material pricing or accounting defect was identified in this bounded review.
Passing the existing deterministic tests supports consistency with the declared
rules; it does not establish calibrated demand, reserve adequacy, long-run
stability or practical self-sufficiency. No real operating dataset was added.

Temporary evidence remains inside the container:
`/tmp/exchange-doc-review-20261014/native/Testing/Temporary/LastTest.log` and
`/tmp/exchange-doc-review-20261014/default-result.json`. Reproduce the native
checks using the current instructions below. The
[environment guide](../projects/post-profit-exchange/doc/ENVIRONMENT.md)
records the legacy WebAssembly storage layout that needs updating before the
next rebuild under the current container-only development rules.

## 2026-09-26: automatic example and settings-first layout

The page now loads and runs its default configuration automatically. Restoring
defaults repeats that process; resetting results preserves settings for manual
stepping. Settings appear open before the run controls. Pale section and chart
backgrounds, grouped buttons and system fonts add visual structure without
external assets. The compiled runtime and economic model are unchanged.

The packaged standalone suite passed, including embedded worker/native parity
and the existing UI, import and source checks. New tests exercise the actual
worker request/response lifecycle and result renderers with a controlled worker
boundary: loading waits for defaults, submits the configured seed and horizon
with empty history, then fills the result views. Reset followed by Step requests
day one. Defaults restoration clears imported history; failed defaults or runs
do not produce results. Static markup checks confirm open settings before controls.
These tests do not visually verify browser layout, colors or typography.

## 2026-09-26: compact standalone interface

Reorganized the page into run controls, grouped settings, a short overview,
selectable chart groups, a day/policy inspector and a compact/full ledger.
Detailed candidate checks, accounts, forecasts and raw records remain available.
No C++ logic, configuration defaults, price policy or accounting changed; the
existing compiled WebAssembly runtime was repackaged with the new interface.

- Standalone embedded worker/native parity, deterministic replay, strict imports,
  source archive and configuration checks passed.
- UI regression checks exercise the actual rendering helpers with a minimal DOM:
  every configuration leaf retains an input and round-trips unchanged; invalid
  inputs reveal closed settings; chart groups preserve the complete chart set and
  product-price series; compact/full ledgers retain every row; day/policy choices
  handle paths of different lengths; inspection leaves results unchanged. View
  event wiring and control states before, during and after a run are also checked.
- Price-explanation regressions still pass for funding arithmetic, exclusions,
  affordability evidence, ties, fixed/continuity labels and safe text insertion.
- These are automated logic/DOM checks, not a visual browser review. Layout,
  browser focus behavior and responsive appearance have not been visually verified.

## 2026-09-26: selected-day price explanations

Added the **Why this price?** view and per-candidate `price_comparison` records.
Each comparison substitutes one product's price while retaining the other
published prices; exchange-wide costs are deducted once. The C++ model supplies
eligibility and exclusion evidence. Continuity retains null certified scores;
its candidate scores are explicitly hypothetical. Pricing policy, consumer
assumptions and default configuration were unchanged.

- Native: **11/11 CTest entries passed** (3.45 seconds), including independent
  candidate arithmetic, neutral-direction exclusions, the affordable-alternative
  witness and a continuity coverage shortfall.
- Native/WASM: **27 fixtures passed**, including candidate explanations and the
  existing accounting/history/continuity cases. A new test initially distinguished
  JavaScript `-0` from integer-money `0`; normalizing its expected zero fixed the
  assertion without changing model output.
- Standalone: embedded worker/native parity and rendering-helper checks passed
  for funding needs, candidate scores and exclusions, ties, fixed/continuity
  labels, and literal display of HTML-like product text. Browser visual review
  was not performed; these checks exercise the actual DOM-building functions
  with a minimal test DOM, not browser layout or interaction.
- Before/after checksums of all existing output fields matched for the default,
  thin-cash and no-visit runs after omitting only the new comparison records.

## Previous verification snapshot

Historical evidence for the previous pricing policy. The counts, results and
scope below describe the 2026-09-21 snapshot, before the affordable-alternative
protection. The [current contract](../tools/price-optimization/docs/contract.md)
uses schema `ph.price.v3`, model `public-prices.v4`, engine `0.5.0` and objective
`operating_balance_tracking` version 2. These earlier checks do not verify that
policy change; application schema `exchange.sim.v3` remains unchanged.

Verified on 2026-09-21 in the existing approved Debian 12 optimizer container,
ID `5738ae552c575e91e0d4308b8aa2c66aa170e8dd817282fd7abd5817907d3ff9`.
Its container definition, mount, volumes, devices and published ports were unchanged.
The store remains [post-profit-exchange](../projects/post-profit-exchange/README.md);
the `post-profit-distribution` placeholder and its zero-byte README were preserved.

This record covers `exchange.sim.v3`, pricing schema `ph.price.v3`, model
`public-prices.v3`, engine `0.4.0`, and objective `operating_balance_tracking`
version 1. The browser uses the explicitly selected bounded enumeration backend;
the native AMPL interface is tested separately. The application remains MIT and
the pricing engine retains its separate license.

## Checks completed

| Check | Recorded result |
| --- | --- |
| AMPL-enabled optimizer | **8/8 CTest entries passed, 8.26 seconds.** Includes 14 general, seven feedback and three liquidity fixtures, plus insufficient-cash variants, checked against bounded enumeration. |
| Native rebuild after EWMA extraction | **11/11 CTest entries passed, 3.04 seconds.** Includes 44 file-record checks, 1,128 forecasting/consumer checks, 4,989 simulation checks and the independent exponential-smoothing suite. |
| Standalone exponential-smoothing tool | **1/1 CTest entry passed, 37 checks.** Built independently without exchange, JSON, optimizer or LibTorch dependencies. |
| Extraction compatibility | Complete cold-start and explicit synthetic-history results exactly match saved pre-extraction output, including every forecast, diagnostic, consumer outcome, price and account. |
| Native versus WebAssembly | **26 fixtures passed**, including 365 days, 12 products, imported history, censoring, continuity, insolvency and strict invalid inputs. |
| Packaged standalone HTML | **Passed** embedded-worker/native parity for history and ledgers, replay, strict JSON import helpers, source/config/license ZIP contents, static element IDs and absence of remote assets. |
| Browser visual review | **Not performed.** Browser Use rejected the local URL under its URL security policy. No browser-surface or local-server workaround was used. Node worker checks do not establish visual or interactive correctness. |

The suite checks cash, FIFO valuation, stock and net-asset reconciliation;
actual-sales feedback directions; disjoint earned credit and operating liquidity;
history-only forecasting; and rollback of unexecuted purchases and reserve
requirements after a bounded-search refusal. CTest evidence is retained in
`.build/post-profit-exchange/native/Testing/Temporary/LastTest.log` and the
optimizer build's corresponding `Testing/Temporary/LastTest.log`.

The central `.cfg` supplies all products and operation/run settings; the native
`--example-config` output matches its complete embedded document, including run ID
and record paths. Persistent records are JSON files, without a database.
The reusable MIT EWMA core now lives in `tools/exponential-smoothing`; exchange retains
its consumer, price-response, stockout and JSON adapters. Default history is
empty. The fourteen-row synthetic history remains an explicit opt-in fixture,
and no TFT training or new training dataset was introduced by this extraction.

The previously verified CLI `--config` run using the explicit synthetic fixture is preserved at
`.build/post-profit-exchange/verification-v3-final-model-run/`: **66 files**,
including daily records, retained inputs/result, assurance log and final manifest.
All JSON and manifest byte counts were checked. Replaying its exact `request.json`
reproduced the result. Repeating the persisted run was refused, with every existing
file hash unchanged. Manifest byte counts themselves are not cryptographic proofs.

## Observed behavior

Both policies completed the default 30-day run with no missing trading days and
no assurance events. The first five optimized bread/beans prices, in euro cents,
were **200/300, 160/240, 130/205, 110/175, 110/160**. Final optimized cash was
**114,935** cents and earned funding balance **1,285**; fixed-price cash was
**172,860** and funding balance **62,910**. Cold-start and supplied sample-history
runs had the same totals in this fixture, while their forecast diagnostics differed.

A no-visitor stress case with 2,500 cents opening cash and 1,000 cents daily wages
ended on **day 3**, with cash **0** and wage arrears **500**. Assurance requests
remained explicitly unfunded; no external payout or invisible financing occurred.
Forecast funding shortfalls now invoke a declared continuity rule that retains
existing public prices, replacing the previous forecast-induced trading gaps.
Continuity is labeled separately from a solver recommendation. Actual cash
exhaustion ends the policy path; technical model failure rolls back the unexecuted
day and is reported separately.

The objective minimizes expected absolute projected funding imbalance, not profit.
Earned positive history permits reductions or holding; negative history permits
contribution-preserving increases or holding; zero holds. Salaries remain fixed.
Operating liquidity can fund continuity but is neither earnings nor a price signal.
Scheduled new reserve funding remains distinct from cash earmarks and releases.
Forecast contribution uses replacement costs; realized economics uses FIFO costs.

## Repeat the native checks

From the repository root inside the documented
[managed environment](../projects/post-profit-exchange/doc/ENVIRONMENT.md):

```sh
cmake -S projects/post-profit-exchange -B /tmp/post-profit-exchange/native -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/post-profit-exchange/native --parallel 2
ctest --test-dir /tmp/post-profit-exchange/native --output-on-failure --no-tests=error
```

For a persisted `--config` run, choose a new output directory in the central
`.cfg` before executing:

```sh
/tmp/post-profit-exchange/native/exchange_simulation --config projects/post-profit-exchange/code/configs/exchange.cfg
```

The runner never overwrites an existing path. Replay a saved `request.json`
through the binary's standard input without rewriting the records. See the
[file contract](../projects/post-profit-exchange/doc/FILES.md).

Historical results retain their original build paths. Use the environment
guide's current container-local SDK/cache/output layout for WebAssembly builds
and packaged checks. The later 2026-10-14 layout cleanup rebuilt WebAssembly;
the earlier documentation-only review did not. AMPL was not enabled in either
of those fresh native builds.

## 2026-10-14 operating environment checkpoint

The [operating environment](../projects/post-profit-exchange/doc/OPERATING_ENVIRONMENT.md)
now loads one operation's central configuration, metadata and opening records,
persists an exact text envelope through IdyDB, reopens it, and renders a shared
read-only snapshot. This native path is separate from the existing simulator.
It does not record transactions, forecast operational sales or recommend new
prices yet; roadmap milestones 1 and 2 remain partial.

A fresh Debug native build in the managed Debian container at
`/tmp/exchange-environment-20261014/native` passed **14/14 CTest cases**.
The new adapter checks include exact large-integer text round trips, close/reopen,
create-once behavior and missing-file refusal. Environment checks cover
opening arithmetic, alternate operation inputs, validation, path isolation,
unchanged accounts after the opening import changes, and escaped HTML rendering.
The existing exchange, EWMA and optimizer cases also passed.

The synthetic vendor example was initialized once in
`projects/post-profit-exchange/code/environments/vendor-operation/`.
Separate CLI processes reproduced its configuration, metadata, state and accounts.
A duplicate initialize attempt returned an error without changing database bytes;
rendering also preserved those bytes. The 1,257-byte database contains the initial
envelope. Its 100,000 cash, 9,750 stock value and 1,000 obligations give net assets
108,750 minor currency units; reserve 5,000 remains part of cash.

The environment view's Node checks passed against the native-rendered HTML.
They exercise record display, exact monetary formatting, literal text handling,
download content and invalid snapshots; they are not visual browser testing.
Aggregate configuration with both IdyDB and exchange enabled, and simulator-only
configuration with `PH_EXCHANGE_BUILD_ENVIRONMENT=OFF`, both succeeded.
Database internals, simulated economic rules and existing compiled WASM were
unchanged. Private environments, database files and generated reports are
excluded from the source archive and the normal Git workflow.

## 2026-10-15 C++ application server

The connected service now uses `exchange_server` and unmodified cpp-httplib
v0.61.0, pinned to commit `c419c904da4ed0d057f169c225c58cc795c07e4d`.
The Python server was removed. Native HTTP handlers call the existing exchange
environment APIs directly; accounting, storage, pricing and simulation sources
were not changed. Python remains development/test/packaging tooling only.

A fresh Release build in the managed Debian container at
`/tmp/exchange-cpp-http-20261015/native` compiled all targets. Results:

- **9/9 HTTP integration cases** passed against the actual C++ server with
  isolated synthetic environments. Existing delivery, FIFO sale and paid/unpaid
  expense assertions were retained. Cases cover safe repeated IDs, competing
  revisions, concurrent identical retries, process restart and session renewal,
  malformed/duplicate JSON and HTTP headers, body limits, incomplete/chunked
  requests, closed routes, mismatched environment and occupied-port startup.
  Ordinary termination returned success.
- A held external writer lock produced an uncertain-completion response after
  the twenty-second budget. Releasing it and retrying the same expense ID left
  exactly one recorded expense; later retries preserved database bytes.
- **21/21 other CTest cases** passed, including exchange accounting/storage,
  simulator, EWMA, optimizer protocol and IdyDB checks. The HTTP integration
  executable suite was run separately rather than repeated through CTest.
- The connected UI's Node contract checks passed, including durable outbox,
  session renewal and uncertain-save retry behavior.
- The packaged HTML checks passed against the fresh native simulator. Existing
  WebAssembly was reused; its runtime SHA-256 remains
  `5d736b58711ae6cc2cf3a029733a92fa6558812fb267d98f31388996420fb9a1`.

The authoritative per-environment launcher reused container
`1c7232e713cb2c7e8449a2b11bd7f5a15df4fc634f529cea8d8e7c15de5083a1`,
verified the changed setup marker, and installed **zero** packages. Its existing
Debian image, binds/mask, loopback publication, device restrictions and restart
policy were preserved. On verification, PID 1 was `exchange_server`, no Python
service was running, and `http://127.0.0.1:8766/api/state` returned HTTP 200 with
`Server: ExchangeLocal/2`, environment `vendor-operation` and revision 0.
The existing environment database SHA-256 remained
`0b0bb11475fc7a2dad9558c9f65e612525f0900db83a9cca90048786473c72bd`.

Tests did not insert transactions into the vendor environment. These checks
establish local transport/account consistency, not worker authentication, remote
deployment, production capacity, backup recovery or economic outcomes.
Application wait expiry does not cancel accepted work. An external storage lock
can also delay graceful shutdown; see the documented service bounds.
Browser checks were deterministic DOM/API checks, not visual browser testing.

## 2026-10-15: encrypted environment and configuration launcher

The operation moved to
`projects/post-profit-exchange/environments/vendor_0x01/`. Its v2 `exchange.cfg`
selects the workspace, loopback endpoint and external key-file path. Runtime
settings are excluded from persisted/public business configuration. The
Windows executable delegates to the authoritative container launcher; the C++
server and CLI run in Debian. Accounting, economic policy and simulator defaults
remain unchanged.

| Executed check | Result |
| --- | --- |
| Native regression build | **23/23 CTest tests passed**, 33.13 seconds, in `/tmp/exchange-cpp-http-20261015/native`. |
| Encrypted environment | Final **9/9 integration cases passed**, 4.91 seconds, after key-file newline and port-validation corrections. The storage suite passed **129 checks**. |
| Existing HTTP behavior | **9/9 cases passed**, 20.50 seconds, including uncertainty and same-ID retry. |
| Browser contracts | Connected transaction and saved environment-view Node checks passed inside Debian. These are deterministic interface checks, not a complete visual review. |
| Launcher | Linux cross-build succeeded; **8 argument cases** and **13 PowerShell rejection cases** passed. The actual default executable action confirmed readiness and opened the browser. |

Launch testing found and fixed Windows PowerShell 5 inline-shell quoting in the
setup-marker check. The check now passes Docker arguments directly. The finished
executable's SHA-256 is
`27fa6843777e04e30f43ee512e79603a569883728765946c8ce272d50ddbd2b5`.

The explicit migration preserved revision **0**, cash **100,000**, reserve
**5,000**, **80** stock units valued at **9,750**, obligations **1,000**, and net
assets **108,750**, all monetary amounts in minor EUR units. It introduced no
trading records. The original plaintext database hash was
`0b0bb11475fc7a2dad9558c9f65e612525f0900db83a9cca90048786473c72bd`.
The encrypted migration recovery reopened to exactly the original stored payload.

New container `post-profit-exchange-vendor_0x01`, ID
`199ac623559525cebb5aadd7790d463a3f26bae3dfc86fc0235bec22c1a7f801`,
passed live readiness and restart verification at
[http://127.0.0.1:8766/](http://127.0.0.1:8766/). Its read-only secret bind,
empty source-environment mask and selected writable operation were verified.
The generated key resides outside the checkout in the Windows user profile,
with user-only access checked. Runtime settings and secret contents were absent
from the public state and service logs. The service runner builds both the
server and native environment CLI; its CLI help was checked.

After native encrypted migration and account/recovery verification, the old
environment folder was retired before the new container was created, so its
read-only source mount could not expose the old plaintext files. The approved
old container `1c7232e713cb2c7e8449a2b11bd7f5a15df4fc634f529cea8d8e7c15de5083a1`
remained stopped until the new service passed live readiness and restart
verification; it was then removed. The
[operating contract](../projects/post-profit-exchange/doc/OPERATING_ENVIRONMENT.md)
and [launch instructions](../projects/post-profit-exchange/doc/APPLICATION_SERVICE.md)
describe the current arrangement.

These checks establish this local migration and launch, not production
readiness. The launcher still requires the checkout, Docker Desktop and the
documented local image. Encryption covers the database and migration recovery,
not JSON inputs, explicit exports or browser state. Worker authentication, key
rotation and a complete backup/restoration workflow remain unimplemented.

## 2026-10-15: native Windows exchange preparation

The exchange-owned application now has an isolated MinGW x86_64 build,
Windows configuration/key/path handling, direct C++ server startup with browser
launch, graceful console shutdown, and an encrypted-only Windows storage adapter.
The Windows adapter calls the existing IdyDB C API; no IdyDB source was ported,
patched or replaced. There is no database substitute. AMPL subprocess portability
is excluded from this build.

At this preparation checkpoint, no Windows IdyDB package was integrated, so
the native server was not linked. The later compatibility review below records
the SDK now available. Its manifest must identify the exact
header, binary dependencies, licenses and library evidence. The library owns
decrypted working storage, encryption, atomic publication and Windows durability.
The exchange validates paths and permissions, serializes writers, compares
the exact stored envelope, checks return statuses and verifies acknowledged writes
by reopening. Errors after a possible publication remain uncertain outcomes.

Verification completed in the existing Debian development container:

- The existing Linux regression suite passed **25/25 CTest cases**, 34.85 seconds,
  including encrypted storage and all nine HTTP integration cases. Accounting,
  economic behavior and the browser interface retain their existing implementation.
- The real exchange-owned Windows server, environment CLI, accounting and storage
  sources cross-compiled. Without the supplied library, no native application
  executable is linked.
- The final Windows CTest run had **6 cases pass and 1 case skipped**. Its
  package/receipt validation case includes **18 checks**.
- Independent Windows executable tests ran under Wine: unchanged accounting
  (**112 checks**), configuration/key/report handling (**45 checks**), server
  randomness/Unicode handling, Ctrl+C handling and readiness-output separation.
- The Windows filesystem/security executable completed **32 checks**, with two
  explicitly unverified Wine cases: directory DELETE-sharing enforcement and a
  genuine Windows reparse fixture. The CTest case is marked skipped; a native
  acceptance receipt must reject any skipped protection. Wine is not NTFS
  durability evidence.
- Build-tool tests exercise dependency hashes, required library contracts and
  release acceptance gates using metadata-only fixtures. They do not execute
  a fake database or establish that a supplied library meets its contract.
- Real-library encrypted-storage and complete Windows HTTP/accounting acceptance
  tests were prepared. The storage test source cross-compiled, but these tests
  have not run against a Windows IdyDB library. The PowerShell acceptance script
  is preparation for a future approved native test environment, not a host
  execution performed during this phase.

The refreshed standalone HTML passed its embedded-WASM/native accounting and
source-file contract checks. The connected transaction interface contract tests
also passed. These are deterministic browser/DOM checks, not a new visual review.
The WebAssembly runtime was reused without changing its compiled bytes.

At the independent verification checkpoint, the IdyDB tree matched the
start-of-task SHA256 inventory: all **17 files were unchanged**. Later concurrent
edits changed only its README and REVIEW documents and added future IdyDB source
paths to the exchange archive manifest and its test. Those edits were not made
by this task and were preserved. At that checkpoint, the IdyDB implementation,
header and tests also matched the original inventory. The separate IdyDB session
continued editing after these checks; this phase's results describe the tested
checkpoint, not acceptance of that subsequent database update.

A final documentation-only archive refresh then failed because those newly
listed upstream platform/package/fixture files were not present, first reporting
`tools/idydb/upstream/cmake/idydbConfig.cmake.in`. The previously tested HTML was
preserved as a checkpoint snapshot. The provider subsequently supplied the
listed source files; the documentation/source-bundle cleanup below supersedes
that temporary export blockage. No missing library file or fixture was fabricated.

The running vendor container retained ID
`199ac623559525cebb5aadd7790d463a3f26bae3dfc86fc0235bec22c1a7f801`,
revision **0** and its original accounts. Its encrypted database SHA256 remained
`754e94ef7a328e5519363b170c322f6155062461c3cdc9c30d62631e7f666a1d`.
No transaction or migration was applied to it.

The existing Docker launcher remains separate and unchanged by native build
output. A future vendor package will run directly on Windows without Docker or
WSL, but this phase does not deliver that runnable package. Real native NTFS
acceptance, encrypted cross-platform compatibility and the supplied library's
failure/durability evidence remain outstanding. The exact handoff is documented
in [WINDOWS_NATIVE.md](../projects/post-profit-exchange/doc/WINDOWS_NATIVE.md).

## 2026-10-15: IdyDB Windows compatibility review

The review inspected `C:\Work\idydb` and its existing managed `idydb-dev`
container without modifying the database or performing a general database
quality audit. The provider has an x64 Windows backend, BIG static/shared SDKs,
the seven C functions used by exchange, and an unchanged text-cell capacity
large enough for the 60 KiB exchange envelope.

The installed static SDK was copied directly between the two managed containers
for a bounded compatibility check. No SDK or compiler was installed on Windows.
Exchange's existing `x86_64-w64-mingw32-g++-posix` (GCC 12 POSIX) successfully
compiled and linked a consumer referencing all seven API functions against the
provider's BIG static library and static OpenSSL Crypto. The provider uses
MinGW GCC 14; this result applies to these exact copied artifacts:

| Input | SHA-256 |
| --- | --- |
| `db.h` | `aa4af0e4c7ca27b61e1f51939df1b1e05e9d5edc3286051f2d7a9c6de30e0f77` |
| `libidydb.a` | `05c7da9ea89cc0b8a3e2949e3b8bd8b8eb02b0a5ad06f0bb8a9b4d54775b7069` |
| `libcrypto.a` | `a9c633abc2e5709a540d26edfc192c6d81d09dd840c2366ca364d68f1d94e879` |

The link used `IDYDB_SIZING_MODE=3`, C++17, static GCC/C++ runtimes and
`advapi32`, `ws2_32`, `crypt32` and `gdi32` system libraries. Object inspection
identified an x64 PE executable importing only `ADVAPI32.dll`, `CRYPT32.dll`,
`KERNEL32.dll`, `msvcrt.dll`, `USER32.dll` and `WS2_32.dll`. The executable was
**not run**. This is a successful API/link check, not a native exchange release,
encrypted runtime test or end-to-end accounting acceptance.

The provider's existing native logs cover four build configurations and
Linux-created compatibility fixture reads. The native symlink fixture explicitly
reported missing creation privilege. Source and SDK files were being updated
during review; earlier test logs must not be presented as testing later builds.
The exchange still needs an identified SDK/evidence package, its `gdi32` allowlist
entry, resolution of the exact `checked_windows_flush_and_close` contract, and
native security/storage/HTTP acceptance. No manifest assertion was changed
to pass a gate.

The [Windows handoff](../projects/post-profit-exchange/doc/WINDOWS_NATIVE.md)
records the available SDK paths and responsibility boundary. In particular,
IdyDB's private Windows decrypted working file can reach disk. Its documented
flush/replacement contract is not a RAM-only, secure-erasure or universal
power-loss promise.

## 2026-10-15: documentation and source-bundle cleanup

Current guides now distinguish available Windows IdyDB support and a successful
link check from the still-pending native application acceptance and release.
The historical checkpoints above retain their original test scope.

The temporary seven-file link-check directory (copied SDK/crypto inputs, source,
unexecuted executable and empty link logs) was removed after checking the input
hashes recorded above. Two generated Python bytecode cache files were removed
from `code/windows/__pycache__`. This is targeted scratch-file cleanup, not Docker
object, build-volume, database, key or live-environment cleanup.

The temporary standalone export passed the existing Node contract suite:
embedded WebAssembly/native history and ledger parity, replay, failure cases,
central configuration and packaged-source checks. ZIP CRC and byte-for-byte
source comparison passed for all 189 repository source entries. The final
export includes these updated notes; its archive is checked again before
publication. The earlier missing-file export blockage is resolved.

The compiled WebAssembly runtime remains
`5d736b58711ae6cc2cf3a029733a92fa6558812fb267d98f31388996420fb9a1`.
No compiler/runtime code, economic behavior, UI code or dependency-validation
rules changed during this cleanup. The IdyDB import and the live encrypted
database were checked against the start-of-cleanup hashes. These checks do not
claim new native Windows application acceptance.

## Evidence limits and handoff

These are synthetic implementation and consistency checks. EWMA, declared consumer
priors, sigma bands and reserve stress are conditional assumptions, not validated
worst cases, calibrated insurance probabilities or evidence of store viability.
There is no implicit TFT training, live publication, payment integration or actual
assurance provider. The checks do not establish dynamic stability, demand-response
accuracy, a benefit from removing owner returns, or entitlement for operational AMPL use.

Continue from the [handoff](../projects/post-profit-exchange/doc/HANDOFF.md),
[model definitions](../projects/post-profit-exchange/doc/MODELS.md),
[simulation contract](../projects/post-profit-exchange/doc/CONTRACT.md),
and separate [assurance contract](../projects/post-profit-assurance/CONTRACT.md).
