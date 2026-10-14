<!-- SPDX-License-Identifier: MIT -->
# Exchange folders and separation of concerns

Status: contributor instructions, 2026-10-14. The root separation
`doc/`, `code/`, `web/` exists. Operations, storage and interfaces now contain the
[opening and transaction checkpoint](OPERATING_ENVIRONMENT.md); their broader
responsibilities below remain incremental work. See [ROADMAP.md](ROADMAP.md)
for sequence and acceptance gates.

## Keep the boundaries simple

| Location | Owns | Must not own |
| --- | --- | --- |
| `doc/` | Purpose, economic policy, interface/file contracts, roadmap and evidence links. | Executable alternative versions of the economic rules. |
| `code/operations/` — transaction checkpoint implemented | Exchange state, FIFO stock, accounting periods/obligations, funding feedback, and adapters that prepare forecasts and price requests. | Synthetic customers, file writes, UI rendering, checkout/network calls or an assurance fund. |
| `code/simulation/` — planned | Synthetic consumers, declared shocks, scenario clocks, policy comparisons and research run orchestration. | A second implementation of shared exchange accounting or live transaction ingestion. |
| `code/storage/` — transaction checkpoint implemented | Exchange-specific IdyDB adapter, versioned record schemas, strict configuration/JSON serialization, and integration of ordering, recovery, replay and backups. | Database internals, choosing prices, inventing transactions or deciding worker/assurance authority. |
| `code/interfaces/` — transaction checkpoint implemented | Native commands, WASM entry points, actual-record imports, authorization and publication/assurance transport adapters. | Duplicated accounting, optimization objectives or synthetic fallback behavior. |
| `code/environments/` | Operation-specific central configuration, metadata, opening imports and private persisted transactions/data/reports; only the declared synthetic example is distributed. | Economic rules, database internals or a second UI implementation. |
| `code/configs/` | Declared example configuration and future versioned configuration examples. | Hidden runtime overrides or competing sources of policy. |
| `code/records/` | Empty default and explicitly synthetic, distributable record examples. | Real vendor records, credentials or automatically imported operating data. |
| `code/tests/` | Deterministic accounting, simulation, persistence and interface cases. | Fixtures represented as real-world evidence. |
| `web/` | Operator controls, charts, import/export presentation and packaging. | Authoritative financial calculations or direct mutation of business records. |

Native C++ remains the preferred home for exchange operations and numerical work.
The reusable optimizer, exponential smoothing, optional TFT and selected IdyDB
database stay in repository `tools/`, under their existing licenses. IdyDB's
implementation is maintained independently; exchange consumes it through its
storage adapter. Do not copy these tools into the exchange or create a universal
store/factory/laboratory framework.

Configuration remains one central `.cfg` for an operation. The first operational
opening state and ordered event records use IdyDB. JSON is
the import/export and fixture format, and the simulator retains its JSON run files. Choose a private database/data
directory through configuration and keep it out of source exports. The existing
simulator's default `code/runs/` output continues to be ignored. New storage
paths must be explicitly excluded from version control and release packaging. Never move or rewrite retained vendor
records as a side effect of a source-tree cleanup.

## The operating environment consumes the application

An environment is an operation's configuration, metadata, database and generated
reports, consumed by the exchange application. The first synthetic vendor
environment lets us examine the user experience through normal application
commands. The application supports opening accounts, IdyDB persistence and
paid-delivery/cash-sale/new-expense entry through a shared connected interface.
A separate generated snapshot remains read-only. Forecasts and price decisions
are not connected to this operational path yet. Keep one implementation of each feature as it arrives. Exchange modules must
not import the example, recognize its scenario name or require its files to build or run.

Keep the implemented environment material in these locations:

| Location | Responsibility |
| --- | --- |
| `code/environments/vendor-operation/` | One scenario `.cfg`, clearly labeled synthetic JSON inputs, and private generated database/report files. Normal application commands initialize and inspect it. No duplicate accounts, prices or storage implementation. |
| `doc/OPERATING_ENVIRONMENT.md` | Implemented contract, setup instructions, first walkthrough and remaining boundaries. |
| `web/operations/` | Shared operator workspace (Home, Record, Stock, Activity and Advanced) and a separate read-only snapshot template. Controls call normal application interfaces; sample setup stays outside ordinary workflows. |
| `code/interfaces/serve.py` | Local HTTP transport to native commands; no accounts, synthetic inputs or database internals. See [APPLICATION_SERVICE.md](APPLICATION_SERVICE.md) for the approved per-environment container launch. |

The example's name, catalog, balances and assumptions belong in configuration
and input records. Existing simulator defaults remain in `code/configs/` and
`code/records/`; a demonstration configuration is an explicit scenario, not a
second definition of the exchange policy.

Give the example its own configured private IdyDB data directory and visible
operation identity. Setup/import helpers use the normal validation path; they
must not write around the storage adapter. Starting the example preserves its
state; this checkpoint has no reset command and refuses a second initialization.
Any future reset must be explicit and scoped to the selected operation. Do not
add fixture-specific branches to accounting.

Each operational environment has its own application container and loopback
endpoint under the [service contract](APPLICATION_SERVICE.md). Shared code stays
read-only; its selected configuration/database is writable at `/operation`,
while other exchange environments are masked. This runtime boundary does not
put application code inside the environment. Retain the general development
container for common build/test work. `code/setup.sh` prepares dependencies and
environment only; launchers own lifecycle and application commands own records.

The dependency runs from example setup and walkthrough to normal application
interfaces, then to operations and storage, then to independent tools.
The shared interfaces render results for any configured operation; interactive
controls call the same native commands. Removing the
example must leave the application buildable and usable with another configuration.

Use each implemented workflow in the environment as it becomes available.
Optional tools and external integrations need not all exist first; display
unavailable steps explicitly. Any simulated external response belongs to a
clearly identified fixture adapter, never a real payment or approval. Improve
shared interfaces from walkthrough findings rather than creating a separate
demonstration UI. This environment does not replace the research simulator.

## Dependency direction

- Operations accept explicit state, events, policy and time values. They apply
  exchange rules and use the independent numerical tools. They do not read a
  global clock, perform I/O or depend on the synthetic consumer generator.
- Simulation constructs declared synthetic inputs and invokes exchange operations.
  The operational path accepts externally reported activity and cannot invoke
  simulation to fill gaps.
- Interfaces validate their transport/input boundary, obtain and record the
  necessary authority, load state through storage, and invoke operations.
  Domain validity and current-state checks still belong to operations.
- Storage uses IdyDB through an exchange-specific adapter to read/write versioned
  values and preserve their ordering and identity. Operations and browser code
  must not call IdyDB directly or use its types. Storage does not execute business
  decisions during deserialization or reimplement the database's internals.
  The standalone simulator does not acquire a database dependency from this plan.
- Browser interfaces call a documented entry point and render its results.
  Changing a chart, replay speed or display selection must not change accounts.
  A browser outbox may retain unconfirmed request bodies/IDs for safe retry, but
  cannot become an authoritative ledger or invent a saved outcome. Keep that
  outbox scoped to origin and operation, separate from IdyDB's actual records.
- Price publication and assurance receipt are explicit external events. A proposal
  is not a published price; a support request is not a payment.

Use small named modules and ordinary functions first. These boundaries do not
require services, a message broker, dependency-injection infrastructure or a
generic event framework. Add an interface only where an actual caller needs it.

## How the current files should evolve

Do not move working files merely to match the target diagram. Extract one
responsibility when an acceptance case in the roadmap needs it:

| Current file(s) | Eventual responsibility and destination |
| --- | --- |
| `code/simulation.cpp` | Move the genuinely reusable exchange transitions into `operations/`; retain the synthetic day/comparison driver in `simulation/`; place JSON command dispatch in `interfaces/`. |
| `code/models.cpp/.hpp` | Keep the EWMA/price-forecast adapters with operations; keep synthetic consumer generation with simulation. EWMA itself stays in the independent tool. |
| `code/files.cpp/.hpp`, `code/main.cpp` | Keep simulator file I/O and native command handling distinct from the new operational IdyDB adapter in `storage/`. Preserve the existing simulator CLI and output format; reuse serialization only where its meaning matches. |
| `code/tests/*_test.cpp` | Keep current fixtures as regression evidence; add tests beside the responsibility being introduced, grouping by concern when useful. |
| `web/app.js`, template and stylesheet | Retain the existing simulator while `web/operations/` holds the separate environment view. Move simulator presentation only when that move serves a concrete need. Share only proven common presentation helpers. |

The present simulation JSON contract and future operational contract must be
distinguishable. Adding real data to a simulator configuration must not silently
select a live mode. Opening and bounded transaction schemas/commands are
documented in [OPERATING_ENVIRONMENT.md](OPERATING_ENVIRONMENT.md); the wider
accounting/recovery scope remains milestones 1–2 work. Do not document
unimplemented APIs as available.

Both entry points should eventually exercise the same exchange transitions
where the meaning is identical. Preserve the simulation's current order,
rounding and tie behavior through extraction; do not force real payment timing,
refunds or deliveries into a synthetic shortcut. Any required economic-policy
change must be explicit and separately evidenced.

## Interfaces and simulations in the browser

Keep simulation controls and real-operation controls visibly separate. A fake
consumer or shock setting belongs only to the research interface. Real operators
need actual records, current balances, proposed/effective prices, approvals and
support status. Neither interface may infer a payment or approval from a chart.

`web/tests/` verifies UI behavior, WASM/native equivalence and the packaged
artifact. `web/dist/` holds intentional deliverables, never build caches or
private record exports. Preserve `web/dist/post-profit-exchange.html` as the
research entry until a separately named operational delivery is actually ready.

## External domains and packaging

Exchange prepares price requests; the optimizer selects and validates candidates.
Exchange records actual outcomes; numerical recommendations do not create sales.
Worker decision processes establish authority; a policy-name string alone is
not authentication. Assurance governs its own funds and decisions; exchange
requests help and reconciles confirmed receipts.

Production, distribution, allocation and the world twin remain separate projects.
They may integrate through explicit contracts when needed; they do not own
exchange's ledger or impose a common state representation.

Whenever files are added or moved, update CMake, build/packaging paths, tests,
local links and the explicit release list in `web/package-html.py`.
Keep application/tool/vendor license boundaries intact. Keep SDKs and build
intermediates inside the managed Debian container; see [ENVIRONMENT.md](ENVIRONMENT.md).
Do not add empty future folders, placeholder classes or unused dependencies.
