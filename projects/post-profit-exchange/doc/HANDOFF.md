<!-- SPDX-License-Identifier: MIT -->
# Development handoff: post-profit-exchange

Reviewed against code on 2026-10-14. Start with this file, the [project README](../README.md),
[store specification](STORE_SPECIFICATION.md), [simulation contract](CONTRACT.md),
[models](MODELS.md), [file records](FILES.md) and current
[verification record](../../../docs/exchange-simulation-verification.md).
The verification record, not this handoff, is authoritative for checks performed.
Start with the README's plain-language explanation and recorded two-day example.
The exchange now uses a project-local `doc/`, `code/`, `web/` layout. The headless
C++ model and configuration contents are preserved; build, packaging and source
archive paths follow the new layout. Preserve the distinction between a tested research model and
an operation validated with observed data.

The selected-day **Why this price?** view now shows shared funding needs and
per-candidate `price_comparison` evidence. It compares one price change against
the other published prices, preserves whole-exchange accounting, and distinguishes
optimizer recommendations from continuity and configured fixed prices. It adds
inspection output without changing the pricing policy or consumer model.

The standalone page loads and runs the defaults automatically on opening;
**Restore defaults & run** repeats that process. **Reset results** clears the output
for manual stepping. **Settings** appears first and is open initially, followed
by the run controls and compact overview. All parameters remain in named groups
and individual product sections. Contiguous solid earth-tone background bands,
sharp edges and system fonts divide the page without floating cards or external
assets. Charts are grouped as **Prices & funding**,
**Customers & stock**, and **Accounts & reserves**; **All charts** shows everything.
The day inspector selects one policy or **Compare both**. Chart points and ledger
day buttons select the corresponding policy and day. The ledger starts with key
columns and offers **Show every ledger column**. Candidate **Why / checks**,
scenario values, full accounts, product flows and raw records remain expandable.
Exports retain all data regardless of the visible sections. This is a display
reorganization; the simulator, pricing rule, consumer model and accounts are unchanged.

## Instructions for the next implementation

Follow [ROADMAP.md](ROADMAP.md) for milestone order and acceptance gates, and
[STRUCTURE.md](STRUCTURE.md) for folder ownership and dependency direction.
The [operating-environment checkpoint](OPERATING_ENVIRONMENT.md) now records
paid deliveries, FIFO cash sales and new paid/unpaid expenses on persisted opening
accounts. Native C++ owns the transitions; IdyDB stores opening records and ordered
events, and inspection replays them. The shared connected interface calls the
native commands through a small local HTTP transport. The approved
[application-service setup](APPLICATION_SERVICE.md) gives each operation a
container named from its environment ID and a configurable loopback port.
Retain the general development container; verify each launch independently.
Continue with the remaining accounting/recovery cases before shadow pricing.
IdyDB in repository `tools/idydb/` is the selected database; its implementation
is maintained separately. Exchange owns only its integration in `code/storage/`.
This supersedes the earlier no-database direction. The current flat C++ files
remain in place until a tested extraction needs a planned subfolder.
Milestones 1–2 remain partial: existing-debt settlement, returns/corrections,
spoilage, credit, period accrual, backup restoration, general migration and reset
are not implemented. The operational path does not calculate new prices.
Preserve one central `.cfg`, JSON imports/exports and simulation records, and the
existing economic rules. Current storage is bounded to 100 transactions/60 KiB.
Do not implement a universal economic framework or a synthetic fallback in a
live-operation path.

The vendor demonstration is a consumer of this application. Keep its configuration,
sample records in `code/environments/vendor-operation/`, with the implemented
contract and walkthrough in `doc/OPERATING_ENVIRONMENT.md`.
It uses the shared `web/operations/` interfaces and normal commands with its own
IdyDB data directory. Build each reusable workflow first, exercise it through
that environment, and improve the shared interface from the findings. Do not add
another accounting implementation, a demonstration-only UI or scenario-specific
branches to application code. An environment means data/configuration/metadata;
the Debian development container is a separate concern. Generated database and
report files remain private and excluded from release sources. Configuration
and metadata cannot change after initialization. Existing v1 opening-only stores
remain readable without mutation; the first transaction writes v2 with opening
records and ordered raw events. Inspection replays those events, not a modified
seed file. Same-ID identical retries apply once; conflicts/stale revisions fail.
Keep the browser's durable unconfirmed-request outbox until native confirmation.
It is recovery metadata, not another ledger. Session renewal must preserve the
original transaction ID and body after a lost acknowledgement. An uncertain
request clears only on native saved confirmation or an explicit conflict
commit_status of not_recorded; an expired session alone is never proof of absence.

## Purpose and decisions to preserve

The project is **post-profit-exchange**. The store is the exchange demonstration,
not the name or scope of the whole economics program. It studies public prices,
distribution through stock/procurement, and allocation through budgets and
inventory. Workers govern the productive unit and its surplus; passive-owner
profit extraction is outside the intended model. Incumbent owners' interests and
the proposed adversarial-cooperation research remain acknowledged in the README.

Prices respond to actual funding results. Earned positive funding permits lower
or held prices; a realized gap permits higher or held prices only when an increase
preserves forecast contribution in every supplied scenario. Zero history holds
the starting prices. Salaries are fixed inputs. Do not replace this with expected
profit maximization, a static cheapest-basket objective or the abandoned staged
surplus-target formulation.

Keep this operating model in the exchange project; production and distribution
need their own models. Do not replace the existing pricing feedback with a
cash-receipts target or introduce a shared operating framework.

The pricing guard excludes a candidate when a strictly cheaper, otherwise locally
legal candidate for the same SKU forecasts at least as many purchases and at
least as much contribution in every supplied scenario. All prior price-direction,
inventory, affordability, change-cap and coverage protections remain. The balance
objective ranks the remaining choices, so its score may be higher than before;
extra surplus alone must not reject this cheaper offer. This is a forecast-based
protection, not a general cheapest-feasible-basket objective. Remaining incomparable
ties retain existing solver behavior.

Verified 2026-09-24: all 11 native CTest cases, all 8 optimizer CTest cases
including AMPL/HiGHS, 26 native/WASM fixtures and the standalone HTML/worker
checks passed. Six hand-calculated affordability fixtures include both candidate
orders and a one-cent contribution difference near the numeric bound. The search
limit fixture now uses nonzero forecasts so redundant zero-sales prices cannot
collapse its candidate grid; the original failure/transaction-rollback assertions
remain. The consumer model and default configuration are unchanged. This checks
the declared rule and accounting, not empirical demand or long-run performance.

Keep these quantities separate:

- Raw earned funding history: realized FIFO economic result less newly scheduled
  reserve funding; opening resources are not earnings.
- Amortized history adjustment: raw history divided by `feedback_recovery_days`,
  with nonzero sign preserved, used by the price objective.
- Earned cash credit and other operating liquidity: disjoint coverage resources
  that do not change the objective's signed history or price direction.
- Reserve target, cumulative scheduled requirement and actual cash earmark:
  releasing or rebuilding an earmark must not charge an operating loss twice.
- Forecast replacement-cost contribution and realized FIFO economic result:
  their difference can reflect cost basis as well as forecast error.

Financial forecast infeasibility alone does not close the exchange. The explicit
application continuity rule retains valid existing public prices and requests
assurance, without claiming a solver recommendation. Technical model failure is
different: roll back that day's uncommitted purchases/scheduled requirement and
terminate with a model-error record. Zero cash after obligations or unpaid fixed
obligations produces simulated insolvency and stops that policy path. The fixed
comparator can end on a different date; never hide that difference in totals.

## Components and boundaries

| Location | Responsibility |
| --- | --- |
| `code/operations/opening.cpp`, `transactions.cpp` | State validation, acquisition-basis totals and delivery/sale/new-expense transitions; no I/O or synthetic behavior. |
| `code/storage/environment_store.cpp` | Exact JSON envelopes in IdyDB, writer locking, expected-state checks and verified atomic replacement; database internals stay in the independent tool. |
| `code/interfaces/environment.cpp`, `environment_main.cpp` | Config/metadata loading, ordered-event replay, initialize/inspect/transact/render commands and snapshot generation. |
| `code/interfaces/serve.py` | Local HTTP/session transport calling the C++ command; no economic calculations. |
| `code/environments/vendor-operation/` | Synthetic example inputs; generated database/report paths are ignored. |
| `web/operations/` | Shared connected transaction form and separate read-only snapshot; display native accounts, preserve retry identities, never publish prices. |
| `code/configs/exchange.cfg` | Central strict-JSON research configuration, including all product/settings values. Its simulation section supplies build-time embedded defaults. |
| `code/records/empty-history.json` | Default empty observations; no fabricated operating history is loaded. |
| `code/records/sample-history.json` | Opt-in synthetic negative-day history: 14 rows, seven per SKU; never describe it as customer observations. |
| `code/models.hpp/.cpp` | Exchange adapters for forecast configuration, JSON, stockouts and declared price response, plus the seeded binary-logit consumer generator. |
| `code/simulation.cpp` | Store operations, FIFO accounts, feedback, reserve stress, continuity and terminal/event records. |
| `code/files.hpp/.cpp` | Native `.cfg`/history loading and new JSON run directories; no database or network service. |
| `code/main.cpp` | Native CLI entry point. |
| `web/` | Plain standalone controls, SVG charts, inspectable records and explicit file import/export; no JavaScript replacement simulator. |
| `web/build-wasm.sh`, `web/package-html.py` | Pinned compiler build and single-file HTML/source/license packaging. |
| `tools/exponential-smoothing/` | Reusable MIT simple exponential smoothing (EWMA) core, pure C++17 target `ph::exponential_smoothing`, header `ph/exponential_smoothing/ewma.hpp`; no JSON or exchange dependency. |
| `tools/temporal-fusion-transformer/` | Separate MIT candidate forecasting tool; no exchange adapter, training or fitted exchange checkpoint. |
| `tools/price-optimization/` | Separately licensed pricing core, finite enumeration and AMPL backend. |

The application schema is `exchange.sim.v3`. Native run manifests use
`exchange.run.v1`, input history uses `exchange.history.v1`, events use
`exchange.assurance.v1`, and output manifest/log schemas are documented in
[FILES.md](FILES.md). The pricing API is `ph.price.v3`, model `public-prices.v4`, engine
`0.5.0`, objective `operating_balance_tracking` version 2. Its JSON shape is
unchanged, but objective version 1 requests are rejected. Its required feedback
has `funding_balance`, `coverage_credit` and `liquidity_buffer`. Expected absolute
balance is the score; expected worker surplus is a diagnostic.

The browser runs C++/WebAssembly with an explicit bounded exact enumeration
backend. It does not run AMPL. The native AMPL interface remains separate with
its existing supervision and vendor terms. Do not claim that a browser worker
test is an AMPL test or that native/WASM parity verifies visual interactions.

The application permits up to 12 SKUs, 16 candidates per SKU, 365 days and at most
200,000 combinations remaining after local policy and affordable-alternative
filtering. Reject an excessive remaining grid rather than silently searching
only part. A strict input contract does not silently
fill missing fields or convert older schemas. Operating configuration comes
from the supplied `.cfg`; browser example defaults are a build snapshot, not
automatic access to local files.

## Model assumptions

Forecasts update from prior uncensored sales only, normalized by declared price
response. Score the observation before updating. A stockout, including zero stock,
does not train level or errors. Imported history may warm up forecasts but never
adds cash, stock or earned funding. There is no implicit TFT training or connection.
The EWMA baseline does not model trend, seasonality or censored latent demand.

Default history is empty. Initial bread/beans levels are declared `base_demand`
priors of 20/14 units, with an assumed 3-unit initial error and continuing floor.
Eligible prior sales update the level/error with configured weight 0.25. During
a simulation those observations come from synthetic consumers. The seven-point
warmup flag is only a diagnostic state, not an accuracy or data-sufficiency gate.
The 14-row synthetic history file remains available only for explicit examples
and tests. Never mix it with real records without preserving that distinction.

Consumer arrivals, budgets, needs and choices are synthetic. Integer draws keyed
by seed/day/customer/SKU/event pair policies without shifting after different
choices. Purchase probability uses a declared binary-logit response; SKU order
allocates budgets and is a documented limitation. No persistent households,
cross-product substitution or real-customer coefficients are present.

Consumer generation and price normalization use the same declared response
priors. Evaluation against that simulator checks a controlled model, not observed
customer behavior or causal price response. Future TFT work needs a documented
data/feature adapter and past-only comparisons against simpler forecasts on real
observations. No universal minimum count or few-shot reliability is claimed; see
the [model notes and primary references](MODELS.md#future-tft-integration).

Sigma is smoothed RMS one-step error with a configured prior/floor. Report cold
start, warmup, MAE, RMSE, eligible coverage and censoring counts. Sigma bands and
the reserve's common-adverse-error rule are conditional stress assumptions, not
absolute worst cases, 99.7% promises or calibrated insurance prices. Review raw
forecasts and physical/saleable caps together.

**Post-Profit Continuity Assurance** is currently a provider contract name and
an unfunded event record, documented in the separate
[post-profit-assurance placeholder](../../post-profit-assurance/README.md).
A request neither contacts anyone nor transfers money.
No actual insurer, premium, pooled reserve, payout engine or legal coverage is
implemented. A later assurance project must define those responsibilities and
their evidence without retroactively inventing funding in exchange runs.
Preserve canonical event identity `(run_id, policy, event.id)` when exporting
or aggregating logs. The local event ID is only policy/day/type; run IDs are
caller-assigned and not globally enforced. No idempotent payout mechanism exists.

## Build, run and preserve records

Use the existing shared `post-profit-economics-gpu` container through the
optimizer launcher, as documented in the [environment guide](ENVIRONMENT.md).
The Windows repository is `C:\Work\post-profit-economics`; the container bind
remains `/workspace/praesidium-humanitatis`. Preserve its mounts and do not install
host toolchains. The approved [application service](APPLICATION_SERVICE.md)
uses a separate container per operation: repository source read-only, selected
environment writable at `/operation`, other exchange environments masked, and
a configurable loopback port. Preserve the general development container.
`code/setup.sh` only prepares reproducible dependencies and the environment;
keep lifecycle, builds, startup and data changes in their dedicated commands.
Native build products belong inside
the container filesystem. From the repository root inside that container:

```sh
cmake -S projects/post-profit-exchange -B /tmp/post-profit-exchange/native -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/post-profit-exchange/native --parallel 2
ctest --test-dir /tmp/post-profit-exchange/native -R '^(exponential_smoothing_ewma|exchange_(models|simulation|files))$' --output-on-failure
/tmp/post-profit-exchange/native/exchange_simulation --config projects/post-profit-exchange/code/configs/exchange.cfg
```

The runner refuses any existing output path. Select a new path in the `.cfg` for
each saved run; never erase an old run to make a test pass. A manifest is written
last, and a failed write preserves incomplete output for inspection. Replay a
saved `request.json` through stdin without overwriting records. Browser import
must explicitly select `.cfg` and history files; browser paths are not arbitrary
filesystem authority.

The standalone HTML lives in `web/dist/post-profit-exchange.html`. Rebuild it
and its corresponding-source archive through `web/build-wasm.sh` when changing
runtime, configuration or packaged source. The SDK, caches and intermediate
outputs now use container-local storage; see the environment guide.
The explicit release file list in `web/package-html.py` must include any new
required source or support file; update the packaged-source checks with it.
Keep operational run directories and imported history out of release source
archives; only the empty default and declared synthetic sample are intentional
record examples. Include the reusable exponential-smoothing tool's source and MIT license
in the corresponding-source archive. Embedded
application MIT notices do not relicense the restricted engine or third parties.

## Next work requiring evidence

Check current verification results before treating a build as complete. Preserve
past-only forecast tests, paired draws, exact cash/stock/FIFO identities, bounded
enumeration, continuity versus technical failure, insolvency stopping, record
preservation and native/WASM consistency when extending the code.

Economic claims need observed data, honest stockout metadata, held-out forecast
evaluation and demand-response identification; synthetic output is insufficient.
Further work includes richer cost/tax/payment timing, replenishment delays,
forecast misspecification, worker governance and approvals, publication/checkout
interfaces, and the separate assurance operating model. No task here establishes
self-sufficiency, owner-removal benefit or authorization for a live deployment.
