# post-profit-exchange

License: MIT for this application; the [price optimization tool](../../tools/price-optimization/README.md)
has its own [Worker Protection License](../../tools/price-optimization/LICENSE).

Our aim is an open, inspectable tool that vendors can implement to organize and
operate an exchange without a passive owner. The people doing the work should
govern the operation and benefit from it. The intended product combines
day-to-day operating support with a separately funded continuity-assurance
arrangement, so an unsuccessful operation need not become a catastrophe for
the people involved.

The current delivery includes a headless C++ research model, an offline browser
simulator and a first [operating-environment checkpoint](doc/OPERATING_ENVIRONMENT.md),
reviewed on **2026-10-14**. The operational checkpoint saves opening accounts and
records paid deliveries, cash sales and new paid/unpaid expenses in IdyDB. A shared
connected workspace opens on Home, with focused Record, Stock and Activity
views; Advanced contains full accounts and read-only settings. It displays the
native account updates. A generated snapshot remains available for read-only inspection. The approved [application-service setup](doc/APPLICATION_SERVICE.md) gives each
operation its own container and loopback endpoint, while retaining the general
development container. Container creation/readiness is checked by the launcher.
Operational price recommendations, worker authorization and funded assurance
remain to be integrated. Protection against catastrophic failure is an aim to
establish through those arrangements, not a guarantee from the simulator.

## The idea

The exchange sells goods to sustain the people who work in it and the service it
provides. Prices must fund replacement goods, agreed worker pay, operating costs
and reserves. The proposed organization has no passive owner entitled to extract
profits; workers collectively govern its policy and the use of surplus.

When trading has earned more than those requirements, the exchange can pass some
of that advantage to customers through lower prices. When trading has fallen
short, prices may rise where the forecasts indicate that the increase will
preserve contribution toward costs. Sometimes the permitted answer is to hold
prices. Pricing cannot repair every shortage.

The signal is **earned funding after costs**, not just units sold or sales
revenue. More sales can still leave a shortfall if margins are small, goods are
wasted or costs rise. Conversely, existing cash is not evidence of earned surplus:
it may be opening capital or money obtained by selling stock already owned.

This project remains a store application within [post-profit economics](../../docs/post-profit-economics.md).
It models exchange through public prices, distribution through procurement and
stock, and allocation through budgets and inventory. The separate production,
distribution, allocation and assurance projects retain their own scope and
authority. No common operating core or universal economic state is required.

Worker control is an organizational requirement, not something a price solver
establishes. The simulator records fixed wage obligations; it does not authenticate
worker decisions, execute payments or establish an ownerless legal entity.

## How prices are calculated

There is one public price per product. The optimizer chooses a combination from
configured candidate prices; it does not assign personalized prices or add a
fixed profit markup to each item's purchase cost. Wages and shared operating
costs belong to the whole exchange, rather than an invented per-product charge.

FIFO means first-in, first-out: the oldest acquisition costs are expensed first.
The calculation uses three quantities:

| Quantity | Meaning |
| --- | --- |
| Accumulated funding balance `B` | Actual revenue minus FIFO cost of goods sold, spoilage, wages, operating costs and newly scheduled reserve funding, accumulated since the start. Opening resources are excluded. |
| Daily adjustment `b` | `B / feedback_recovery_days`, rounded to an integer minor currency unit, with nonzero sign preserved. The default horizon is three days. |
| Today's requirement `F` | Wages + operating costs + newly scheduled reserve funding. |

For each forecast scenario, the candidate prices imply a number of saleable units:

```text
forecast contribution = sum over products(
    (candidate price - current replacement cost) * forecast units sold
)
adjusted balance = b + forecast contribution - F
score = sum over scenarios(probability * abs(adjusted balance))
```

The optimizer selects the **permitted combination with the smallest score**.
The [specification](doc/STORE_SPECIFICATION.md#price-feedback-from-actual-operating-results)
gives the exact rules, rounding, cash coverage and solver boundaries.

| Earlier trading | Permitted price direction |
| --- | --- |
| Ahead of funding needs (`B > 0`) | Lower or hold. |
| Behind funding needs (`B < 0`) | Raise or hold; every increase must preserve that product's forecast contribution versus holding in every scenario. |
| Exactly balanced (`B = 0`) | Hold; this includes the first day with no earned history. |

Price ceilings, per-update change limits, available stock and scenario coverage
also apply. Coverage may use disclosed liquid cash, but that cash does not become
earned funding or change the price-direction signal. A higher candidate is
excluded if a locally permitted cheaper candidate provides at least as many
purchases and at least as much contribution in every scenario. Remaining choices
are still ranked by the balance score: there is no general lowest-price objective.

Candidate spacing and change limits explain steps and holds in the price charts.
A three-day recovery horizon is a response setting, not a guarantee that a
funding gap will disappear in three days. Forecast contribution uses current
replacement costs and has no explicit projected spoilage charge; actual accounts
use acquisition costs in FIFO order and do charge spoilage.

## A worked example from the current simulation

The unchanged example configuration, seed 42 and empty history produced the
following first two days in the 2026-10-14 native review. All amounts are cents
in the example's EUR label. These are synthetic research values, not a proposed
wage or operating budget.

| Balancing-feedback path | Day 1 | Day 2 |
| --- | ---: | ---: |
| Accumulated funding balance before pricing | 0 | 4,000 |
| Daily adjustment, using three days | 0 | 1,333 |
| Wages + operations + new reserve funding | 1,200 | 1,200 |
| Bread public price | 200 | 160 |
| Beans public price | 300 | 240 |
| Actual contribution after goods sold and waste | 5,200 | 3,710 |
| Actual funding result for the day | +4,000 | +2,510 |
| Accumulated funding balance after trading | 4,000 | 6,510 |

On day one, the policy holds starting prices. Actual revenue is 10,600; FIFO cost
of goods sold is 5,300 and waste costs 100. Contribution is therefore
`10,600 - 5,300 - 100 = 5,200`. After wages 800, operations 200 and reserve funding
200, the exchange is 4,000 ahead.

On day two, that earned history permits reductions. Bread and beans each fall
20%, matching the configured per-day change limit. Earlier earned funding is
being used to guide a lower-price decision. It does not force the accumulated
balance to zero immediately: the decision uses forecasts, permitted candidates
and a gradual adjustment, while the next balance comes from actual simulated
sales and costs.

The full 30-day results and fresh checks are in the
[verification record](../../docs/exchange-simulation-verification.md).
One successful synthetic trajectory does not establish practical self-sufficiency.

## What happens during a day

1. Observe current replacement costs; forecast from eligible past sales, estimate
   reserve needs and schedule any new reserve funding requirement.
2. Buy toward configured stock targets, subject to cash, procurement budget,
   existing reserve and protected obligations. Delivery is immediate in this model.
3. Choose prices from the permitted candidates using the forecasts and earned
   funding history.
4. Simulate customer purchases, consume inventory and record spoilage using FIFO.
5. Accrue wages and operating costs, pay obligations from available cash, update
   funding history, earmark reserve where possible and reconcile the accounts.

**Cash, economic result and funding balance answer different questions.** Buying
inventory exchanges cash for stock; it does not immediately expense the purchase.
A sale or spoilage recognizes inventory cost. Reserving some cash changes what is
earmarked, not total cash or profit. New reserve funding enters the funding
requirement separately. The [daily accounting rules](doc/STORE_SPECIFICATION.md#daily-operations-and-accounts)
make these distinctions explicit.

If forecast coverage is financially infeasible, the declared research
continuity rule retains existing prices and records an assurance request. Trading
continues with uncertified coverage while cash permits. A request does **not**
contact a provider or create funding. Zero cash after fixed obligations, or
unpaid obligations, ends that policy path as simulated insolvency. Technical
model failures end separately and roll back the unexecuted day's preparation.

[Post-Profit Continuity Assurance](../post-profit-assurance/README.md) remains
an external domain. Its real funding, authorization and settlement arrangements
must be defined before relying on it in an operation.

## Customers, forecasts and the comparison

Customers are synthetic. Potential visitors draw visit/skip, budgets, needs and
purchase/no-purchase choices. **Prices affect buying after arrival; they do not
affect whether someone visits this store.** Arrivals depend on the configured
visit probability and demand shock. There is no competitor/store-choice model,
persistent household or product substitution model. Earlier products in the
configured order get first access to each customer's budget.

Both pricing paths use the same seed and matched underlying draws. Actual sales
can differ because prices, remaining budgets and stock differ. Each path then
learns from its own sales. This makes a controlled comparison within the stated
simulation assumptions; it does not identify a real customer price response.

The current forecasting tool is [simple exponential smoothing](../../tools/exponential-smoothing/README.md)
(EWMA). Defaults contain no real sales history: demand levels and initial error
scales are declared assumptions. The model learns from prior uncensored sales,
scores predictions before learning, and excludes sellouts from updates.
The [model notes](doc/MODELS.md) explain the resulting selection bias and why
sigma stress is not guaranteed protection. The separate TFT tool is not used.

Imported real history would initialize the forecast; subsequent sales here
would still be generated by the simulator. The consumer generator and forecast
normalization also share price-response assumptions. Keep assumed, simulated and
observed quantities distinct.

The fixed-price comparator has the same no-owner-payout accounts. It compares
pricing policies, **not the effect of removing an owner's profit extraction**.
The two paths may end on different dates; totals must be read with those dates.

## What is ready, and what remains to establish

| Area | Implemented now | Still needed for practical use |
| --- | --- | --- |
| Pricing | Explicit policy, finite candidate search, shared C++ checks and explanations. | Real demand-response evidence, suitable candidate grids and measured operating limits. |
| Store accounts | Simulation accounts plus operational delivery, cash-sale and new-expense entry, FIFO stock, IdyDB replay and exact account updates. | Existing-debt settlement, returns/corrections, waste, supplier/payment timing, complete local costs and recovery procedures. |
| Forecasting | Past-only EWMA updates and inspectable error/stress assumptions. | Evaluation on held-out observations, missing/censored data handling and sensitivity to incorrect demand assumptions. |
| Continuity | Unfunded assurance events and explicit terminal outcomes. | A separate assurance agreement and verified funding/settlement, not assumed payouts. |
| Governance and publication | Configured policy and one public price per SKU. | Authenticated worker approval, correction/recall, current-state validation, expiry and reliable checkout/price publication. |
| Evidence | Deterministic cases and exact internal accounting checks. | Observed operating evidence; calibrated uncertainty and sustained affordability, worker benefit and continuity. |

The next practical research step is to collect traceable operating records,
evaluate the forecast and price-response assumptions, and run in **shadow mode**:
calculate recommendations without changing live prices. Define the observation
period, starting resources, obligations and success/failure measures beforehand.
Only then consider a bounded, worker-approved operating trial with a real
continuity arrangement. This is a sequence for gathering evidence, not a claim
that current defaults are suitable for a store.

The [vendor-readiness roadmap](doc/ROADMAP.md) defines the implementation order,
completion criteria and first shadow-operation checkpoint. The
[folder and dependency instructions](doc/STRUCTURE.md) specify where each concern
belongs as that work is implemented. Planned modules are not yet live features.

## Use the standalone simulator

Open [post-profit-exchange.html](web/dist/post-profit-exchange.html) in a modern
browser. Defaults run automatically. Settings appear first; run controls follow.
Use **Run all days**, or **Reset results** then **Step one day**. **Restore defaults
& run** reloads the original example; **Stop** preserves the last completed result.

The page needs no server, network connection or AMPL installation. Settings expose
every parameter. Chart groups cover prices/funding, customers/stock and
accounts/reserves; **All charts** shows the complete set. Choose a day and policy
in **Day detail** or select a chart point. Expand the ledger for all columns.

**Why this price?** shows shared funding needs, candidate forecasts and exclusion
reasons. Each candidate row changes one product while holding other published
prices fixed; it is not a replay of the full joint search. Continuity comparisons
are hypothetical, and the fixed-price path explains its configured price only.
JSON/CSV exports retain the full result regardless of the selected view.

Use **Open central .cfg** and **Open history records .json** to load saved inputs.
The .cfg is strict JSON. File selection is explicit; the browser does not read
neighboring files automatically. Reloading clears unexported work.

The [operating environment](doc/OPERATING_ENVIRONMENT.md) is an operation's
configuration, metadata, records and private database, consumed by the application.
The example supplies labeled synthetic inputs to the same commands and shared
operator interface that accept another operation's configuration. Connected entry
records deliveries, sales and expenses; the generated report remains read-only.
Both are separate from the research simulator and neither calculates new prices
yet. Use the walkthrough to improve the shared application, keeping actual
operation data distinct from test fixtures.

## Project layout and interfaces

```text
post-profit-exchange/
  README.md             purpose, explanation and starting points
  CMakeLists.txt        native build entry
  doc/                  policy, model, file/interface contracts and build guide
  code/                 headless C++ operations, accounts and consumer/forecast adapters
    operations/         opening validation and transaction/account transitions
    storage/            exchange-specific IdyDB adapter
    interfaces/         environment commands, snapshot rendering and HTTP transport
    environments/       per-operation config, metadata, records and private data
    configs/            central simulator .cfg defaults
    records/            empty history and explicitly synthetic examples
    tests/              native deterministic tests
  web/                  browser controls and charts, WASM build and packaging
    operations/         shared connected entry and read-only snapshot template
    tests/              native/WASM parity and packaged-interface checks
    dist/               ready-to-open standalone HTML
```

The native command line and browser share the same C++ operation model:

| Boundary | Existing interface | Responsibility |
| --- | --- | --- |
| Headless engine | `ph::exchange::run_json` in `code/simulation.hpp` | Strict JSON commands and complete simulated runs with accounts and explanations. |
| Operating environment | `exchange_environment initialize/inspect/transact/render` | Opening records, ordered transactions, IdyDB replay and current account reports; no price proposals yet. |
| Connected operator view | `code/interfaces/serve.py` and `web/operations/transactions.html` | Same-origin local entry forms calling native commands; configuration and data come from the selected environment. |
| Native records | `exchange_simulation --config`, `--example-config`, or JSON on stdin | Configuration/history input, new run directories and exact request replay. |
| Browser adapter | C ABI `exchange_run` through WebAssembly in a worker | Runs the same model; JavaScript renders results and handles explicit imports/exports. |
| Computing tools | Independent price optimizer and simple exponential smoothing under repository `tools/` | Reusable numerical work; exchange retains its operating policy, stock, accounts and consumer assumptions. |
| Assurance | Existing event records and the separate assurance contract | Reports a need for support; it does not approve or transfer funds. |

The operational checkpoint is separate from the simulation/file interfaces.
A vendor deployment still needs broader transaction handling, recovery,
operational price proposals, worker-approved publication and assurance settlement.
The bounded transaction checkpoint does not claim those integrations exist.
The other post-profit projects and computing tools retain their own folders and
authority; this cleanup introduces no shared economic engine.

## Components, records and development

The exchange application owns store operations and uses independent tools:
the restricted pricing core and MIT exponential smoothing. The browser calls
C++ bounded enumeration through WebAssembly; **AMPL does not run in the HTML**.
A future service adapter can use the tool's existing supervised AMPL JSON backend:
`ph.price.v3`, engine `0.5.0`, model `public-prices.v4`, objective
`operating_balance_tracking` version 2. Operational vendor rights remain necessary.

The simulator supports 1–12 products, 1–16 candidates per product and 1–365 days,
with one pricing update per day. Enumeration allows at most 200,000 combinations
**remaining after local policy and affordable-alternative filtering**. Exceeding
the configured bound is a model error, not an approximate solution. The bounded
enumerator retains original candidate order for remaining equal scores; different
solver backends are not promised to choose the same remaining tie.
This is a daily simulator, not a real-time publication service.

All products and operating/run settings come from the central
[exchange.cfg](code/configs/exchange.cfg); history and results are JSON records.
The current simulator does not use a database. Native file runs write new
directories and refuse existing output paths. A completion manifest is written
last; failed writes preserve partial records. The [file contract](doc/FILES.md)
describes exact replay.

The [operational roadmap](doc/ROADMAP.md#2-keep-reliable-operational-records-with-idydb)
now selects IdyDB in repository `tools/idydb/` for continuing vendor records,
through an exchange-specific `code/storage/` adapter. Database development is
maintained separately. Central `.cfg` configuration and JSON imports/exports
remain. The adapter persists opening records and ordered events, checks revisions
and identities, and atomically replaces verified database snapshots. Full recovery
procedures and backup integration remain roadmap work; the present limit is
100 transactions in a 60 KiB envelope.

Development and tests run in the existing managed `post-profit-economics-gpu`
container. The connected application runs in a separate container per operation,
using the shared source and that operation's private data. Follow the
[exchange environment guide](doc/ENVIRONMENT.md) for container-local builds and
[application service](doc/APPLICATION_SERVICE.md) for operation lifecycle. Keep
`code/setup.sh` limited to reproducible dependency/environment setup; it does not
launch containers, build the project or migrate records. Do not install or run
project toolchains on Windows.

| Document | Read it for |
| --- | --- |
| [Operating environment](doc/OPERATING_ENVIRONMENT.md) | Opening records, delivery/sale/expense entry, IdyDB replay, worked accounts and limits. |
| [Application service](doc/APPLICATION_SERVICE.md) | Approved per-environment containers, local endpoints and lifecycle commands. |
| [Roadmap](doc/ROADMAP.md) | Milestones and acceptance gates from simulation to vendor operation. |
| [Folder instructions](doc/STRUCTURE.md) | Ownership, dependencies and incremental separation of code and interfaces. |
| [Store specification](doc/STORE_SPECIFICATION.md) | Policy, exact price formulation, daily operation and authority boundaries. |
| [Simulation contract](doc/CONTRACT.md) | Every input, limit, output and accounting identity. |
| [Model notes](doc/MODELS.md) | Consumer choices, forecast equations, data provenance and uncertainty assumptions. |
| [File records](doc/FILES.md) | Configuration, history, retained results and replay. |
| [Verification](../../docs/exchange-simulation-verification.md) | Dated checks, observed synthetic outcomes and evidence limits. |
| [Development handoff](doc/HANDOFF.md) | Boundaries and decisions to preserve when extending the project. |

The independent [production project](../post-profit-production/README.md) has
its own resource, labor, capacity and risk model; integration is future work.

We acknowledge the potential disruption to store and factory owners' income and
control. We refer them to [Adversarial Cooperation](https://github.com/savethebeesandseeds/adversarial-cooperation)
and the [planned protocol research note](../../docs/adversarial-cooperation.md).
The protocol aims to address their interests alongside the potential advantage
of foregoing passive-owner extraction. That advantage remains a hypothesis;
worker pay, necessary costs, reserves and worker-controlled surplus remain part
of the model.
