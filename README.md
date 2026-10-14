# Post-profit economics

Praesidium Humanitatis is a research institution dedicated to the discovery
and formalization of **post-profit-economic models of production,
distribution, exchange and allocation**. Its research program is
**post-profit economics** (`post-profit-economics`).

Repository: [savethebeesandseeds/post-profit-economics](https://github.com/savethebeesandseeds/post-profit-economics).
The Windows checkout is `C:\Work\post-profit-economics`; the old folder and
junction have been removed. Development uses the shared NVIDIA GPU container
`post-profit-economics-gpu`. See the [environment guide](docs/environment.md).

The repository is organized into three directories:

| Directory | Contents |
| --- | --- |
| [`projects/`](projects/README.md) | Economic demonstrations and the planned world/environment twin |
| [`docs/`](docs/README.md) | Shared research, development, licensing and verification records |
| [`tools/`](tools/README.md) | Reusable forecasting, optimization and experimental storage capabilities |

We investigate how economic activity can meet human needs, reduce suffering
and sustain the people and resources on which it depends. In this program,
post-profit models exclude passive-owner profit extraction. Worker
compensation, necessary costs, reserves, reinvestment and collectively governed
surplus remain part of economic life.

## Four research areas

| Area | Central question |
| --- | --- |
| **Production** | How are goods and services created, and how are productive work, assets and decisions governed? |
| **Distribution** | How do goods and services reach people reliably and accessibly through procurement, storage, logistics and provision? |
| **Exchange** | On what terms do people and organizations transfer goods, services and resources, including prices, reciprocity and settlement? |
| **Allocation** | How are scarce resources, capacity and surplus assigned among competing needs and uses, and who decides? |

These areas interact. Each project is a research demonstration of one or more
post-profit-economic models: it states a hypothesis, formalizes an operating
model and produces evidence about its behavior and limits. A demonstration
may be a specification, simulation or evaluated operation; its stage must be
explicit. An implemented tool alone does not establish economic viability or
human benefit.

The [research framework](docs/post-profit-economics.md) defines the program and
what a demonstration must establish. The [research plan](docs/research-plan.md)
connects that purpose to current work.

Shared repository material and application code are MIT licensed. Each tool has
an explicit license. AMPL and other dependencies retain their own terms.

We acknowledge the potentially disruptive implications of operating stores
and factories without passive-owner profit extraction. Any resulting advantage
in affordability, worker benefit or resilience remains a research hypothesis
to test. Existing factory and store owners, including those who exercise power
within current arrangements, are directed to our
[Adversarial Cooperation work](https://github.com/savethebeesandseeds/adversarial-cooperation).
We aim to construct a protocol addressing their interests in relation to this
potential advantage. The [research note](docs/adversarial-cooperation.md)
records that intention and the distinction between foregoing owner profit and
funding worker pay, necessary costs and worker-controlled surplus.

## Tools

The [tools index](tools/README.md) describes component boundaries and development.

| Tool | Purpose | License | Status |
| --- | --- | --- | --- |
| [IdyDB](tools/idydb/README.md) | C/C++ cell and vector storage with optional encrypted backing files | [MIT](tools/idydb/upstream/LICENSE) | Optional experimental source; shared corrections and regression tests included; operational validation remains incomplete |
| [Exponential smoothing (EWMA)](tools/exponential-smoothing/README.md) | Dependency-free C++17 simple exponential smoothing, uncertainty and past-only diagnostics | [MIT](tools/exponential-smoothing/LICENSE) | Nonseasonal single-level baseline used by exchange; empirical accuracy remains unvalidated |
| [Temporal Fusion Transformer](tools/temporal-fusion-transformer/README.md) | C++17 / LibTorch / CUDA forecasting with model diagnostics | [MIT](tools/temporal-fusion-transformer/LICENSE) | Implemented; real-data replication remains a gate |
| [Price optimization](tools/price-optimization/README.md) | C++17 pricing constraints and AMPL mixed-integer optimization | [Worker Protection License 1.0](tools/price-optimization/LICENSE) | Initial synthetic prototype; operational AMPL rights and store integration required |

The optimizer is source-available with use restrictions, not OSI open source.
Its license prohibits exploitation and coercion and requires democratic worker
control and collective benefit in productive deployments. It is a legally
unreviewed draft; enforceability depends on applicable law. Neither licensing nor
technical checks can guarantee prevention of misuse. See the precise
[license boundaries and review status](docs/licensing.md).

## Research demonstrations

[post-profit-exchange](projects/post-profit-exchange/README.md) is a store
research demonstration centered on **exchange** through public pricing, with
**distribution** through stock and procurement and **allocation** through
inventory, protected budgets and worker-governed surplus. Its scope includes
stock, procurement, sales, accounts and worker benefit. A
[standalone C++/WebAssembly simulator](projects/post-profit-exchange/web/dist/post-profit-exchange.html)
provides plain controls, charts and reconciled accounts. Live integration and
evaluation with observed operating data remain future work.

[post-profit-production](projects/post-profit-production/README.md) is an initial
**production** demonstration with a configuration-driven C++ simulator and its
own [offline web view](projects/post-profit-production/dist/post-profit-production.html). Resources,
processes, labor, reusable capacity, production timelines and risk assumptions
are configured independently of any particular industry. It records quantity
balances, work, interruptions and fixed compensation obligations; funded payments,
governance, financing and economic viability remain research work. The initial
model is a bounded synthetic prototype, not a universal production model.
Demonstrations belong in `projects/`; reusable research capabilities belong in `tools/`.

The separate pricing tool used by the store project selects one public price
per product, enforces affordability and price-change bounds, and requires
projected worker pay,
operating costs, and reserves to be covered in each supplied demand scenario.
It returns a recommendation for review. A live autonomous store, demand-response
estimator, payment system, and hard real-time service are not implemented.

A [supervised JSON backend](tools/price-optimization/docs/backend.md) now exposes
the existing optimizer for the synthetic dashboard example, with versioned
inputs, process deadlines/cancellation and independently checked explanations.

## Tycoon world/environment twin

[`post-profit-tycoon`](projects/post-profit-tycoon/README.md) is a planned
world/environment twin representing people, workplaces, stores, resources and
supply routes. It is a separate project, currently a concept note only.
Exchange and production keep their own operating models; future connections
require specific interfaces. No twin, renderer or adapters are implemented.

## Build and environments

The tools have independent builds. Simple exponential smoothing is a dependency-free C++17
library (`ph::exponential_smoothing`); the optimizer and TFT have separate documented
Debian environments. The smoothing baseline and optimizer do not require
LibTorch or CUDA. Container dependency installation is separate from lifecycle
and build tasks.

In an existing Linux environment with CMake and a C++17 compiler, build and test
the exponential-smoothing baseline and optimizer's dependency-free core:

```bash
cmake -S . -B .build/core -DCMAKE_BUILD_TYPE=Release
cmake --build .build/core --parallel 2
ctest --test-dir .build/core --output-on-failure --no-tests=error
```

This does not run AMPL. Follow the
[optimizer guide](tools/price-optimization/README.md) and its
[container procedure](tools/price-optimization/environment/README.md) for the
optional C++ API, licensed runtime, solver, and integration checks.

For the existing TFT environment, from PowerShell:

```powershell
.\tools\temporal-fusion-transformer\environment\container.ps1 up
.\tools\temporal-fusion-transformer\environment\container.ps1 -Action exec -Command @('bash', 'tools/temporal-fusion-transformer/tasks.sh', 'test')
```

See the [TFT environment](docs/environment.md) for exact dependencies. Use
`tools/temporal-fusion-transformer/tasks.sh` and
`tools/temporal-fusion-transformer/environment/` for TFT tasks and setup.
Existing build artifacts are preserved; canonical TFT builds use
`.build/temporal-fusion-transformer`.

## Layout

```text
projects/
  post-profit-exchange/         store model, doc/, code/ and standalone web/ simulator
  post-profit-production/       production simulator, experiments and standalone web view
  post-profit-distribution/     research placeholder
  post-profit-allocation/       research placeholder
  post-profit-assurance/        continuity-support contract placeholder; no funded payouts
  post-profit-tycoon/           world/environment twin; concept note only
docs/                          shared research, development, licensing and evidence
tools/
  idydb/                      MIT experimental cell/vector database; optional build
  exponential-smoothing/       MIT simple exponential smoothing and tests
  temporal-fusion-transformer/ MIT forecasting library, tests and environment
  price-optimization/          restricted pricing engine, tests and environment
README.md                      repository overview and navigation
LICENSE                        MIT default, with explicit tool exceptions
CMakeLists.txt                 optional aggregate build entry
.gitignore                     local artifacts, generated runs and entitlement files
.gitattributes                 text and line-ending rules
```

Ignored local directories `.build/`, `.temp/` and `.codex/` sit outside this
source layout. Existing dependencies, outputs and historical evidence are
preserved; generated exchange runs are also ignored. Standalone HTML releases
and research fixtures remain versioned.

The TFT's existing [verification record](docs/verification.md),
[uncertainty study](docs/uncertainty-validation.md),
[replication plan](docs/tft-replication.md), and
[pending validation](docs/PENDING-TFT-VALIDATION.md) remain part of the project.
The institutional framing does not claim new forecasting or economic evidence.

[Licensing](docs/licensing.md) · [Third-party notices](docs/third-party-notices.md)
· [Reorganization](docs/reorganization.md)
