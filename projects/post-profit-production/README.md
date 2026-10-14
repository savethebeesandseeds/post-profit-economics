<!-- SPDX-License-Identifier: MIT -->
# post-profit-production

This project is a **production** demonstration within Praesidium Humanitatis's
[post-profit economics program](../../docs/post-profit-economics.md), with
connected questions of **allocation** of labor, resources and productive
capacity. It researches arrangements for sustained production under worker
control, without passive-owner profit extraction.

**Stage:** initial synthetic C++17 research simulator and operating
specification, with a configuration-driven scheduling experiment runner and
an independent offline WebAssembly view.
Configuration defines resources, processes, labor pools,
reusable capacity, production jobs, scheduling and risk assumptions. No
particular industry or product is built into the engine. The implemented
model covers finite stocks, integer work and declared risk mechanisms; it
does not represent every productive process or establish economic viability.

Worker compensation, necessary costs, reserves, replacement of assets,
reinvestment and collectively governed surplus remain part of the research
model to be developed. This first simulator records fixed compensation
obligations independently of work completed or capacity interruptions. Its
`compensation_settlement: "not_modeled"` output explicitly leaves payments
unmodeled. Funding, worker consent, authenticated governance and decisions
about surplus remain future work. Resource endowments are declared; production
does not silently create money, outside support or a financed enterprise.

## Research question

Under which resource, labor, capacity and uncertainty assumptions can
worker-governed productive units sustain useful output, meet declared needs
and maintain agreed operating protections? How do scheduling, production
timelines and exposure to disruptions affect those outcomes?

The [production specification](PRODUCTION_SPECIFICATION.md) distinguishes
the intended economic model, implemented mechanisms and evidence required.
Tests of quantities and scheduling establish implementation behavior only.
Claims about worker benefit, reliable provision or the absence of
passive-owner extraction require separate operational and comparative evidence.

## Configuration-driven production

A productive unit is a configured instance of the model. A process declares
its stages and dependencies, resource inputs and outputs, work required,
labor and reusable-capacity requirements, maximum work rate and risk settings.
A job schedules one execution of a process. Shared stocks and capacity allow
different processes and products to compete for the same finite resources.

The timeline follows actual work. Scarce labor, bottlenecks, missing inputs,
stage dependencies and interruptions can stretch completion. Additional labor
helps only where other constraints allow it. Rate limits express a configured
saturation point; the prototype uses fixed input ratios rather than a fitted
production function. Pure time-dependent stages can omit labor and capacity.

Inputs are consumed once when a stage starts; work continues across ticks;
outputs become available after completion, for use from the next tick.
Independent stages may run in parallel. Every tick records capacity use,
progress and a reason for waiting. The model uses a declared, fixed time unit;
changing that resolution requires a separately reviewed configuration.

FIFO and earliest-deadline scheduling are implemented comparison policies.
Neither is an optimal schedule or a complete allocation or governance protocol.
Configuration selects these policies and supplies parameters; it does not
execute arbitrary code or install additional models.

## Timeline and risk

Configuration supports scheduled resource losses and capacity reductions,
stochastic labor or capacity interruptions, stage delays and reduced output.
Stage delay and output-loss probabilities can increase or decrease with elapsed
time since the stage started. This permits research into the interaction
between stretched timelines and declared exposure. Slower production is not
assumed universally safer or riskier; each configuration states its relation.

Risk settings are supplied assumptions, not learned probabilities, calibrated
insurance estimates or guarantees. Common scheduled shocks can affect multiple
pools; independent named availability risks do not themselves fit correlated
losses. Uncertainty about the assumptions must be explored across configurations
and seeds. Changing a time step without changing probabilities and rates changes
the modeled experiment.

The [simulation contract](simulation/CONTRACT.md) defines the exact timing,
probability opportunities, rounding, bounds, resource balances and replay
semantics. The [abstract research configuration](configs/research.cfg) uses
neutral resource and process identifiers and arbitrary synthetic values.
It supplies no observed production or worker data and no recommended policy.

## Interfaces and records

The MIT library target is `ph::production_simulation` in namespace
`ph::production`. The JSON command boundary supports `defaults` and `simulate`
under `production.sim.v1`; full configuration uses `production.config.v1`.
The native runner accepts one command on stdin or an explicit `.cfg` path and
writes the complete result to stdout. It creates no record directories and
performs no implicit access to neighboring files.

Results retain the input configuration, per-tick states, events, resource
balances, labor/capacity use, job completion and missed deadlines. Unfinished
work at the horizon is a modeled outcome. Completing the required work can
still yield no usable output under a declared loss event. Payment settlement,
financial solvency and human needs met are not inferred from job completion.

Identical configuration and seed reproduce the integer simulation and its
event sequence. Rendering is outside the model. There is no resumable checkpoint
or exchange adapter.

The [standalone web view](dist/post-profit-production.html) compiles the same
C++ simulation and experiment runner to WebAssembly. Open the downloaded HTML
in a browser; its runtime, examples, source archive and notices are embedded.
The view edits complete configuration or experiment-plan JSON, runs in a
cancellable worker, and inspects recorded stages, resources, labor, capacity,
events and quantity balances. Selecting a tick plays back an existing trajectory;
it does not change the run's horizon or compensation obligations. Export keeps
the complete result, including unfinished work and failed experiment checks.
See the [web guide](web/README.md) for usage and verification boundaries.

The [experiment runner](experiments/README.md) compares complete configurations
under declared policies and seed sets, checks predictions, and measures waiting,
progress, pauses and unfinished work from existing records. Its
[scheduling comparison plan](configs/scheduling-comparison.cfg) contains 22
deterministic policy runs and, when those checks pass, 80 exploratory runs.
JSON reports retain configurations, trajectories and events; a Markdown view
supports inspection. The runner changes no production semantics and ranks no
policy. These are synthetic observations and contract checks, not evidence of
funded operation or worker benefit.

## Repository boundaries and build

This project owns production processes, scheduling, risk assumptions, domain
state and records. The planned [tycoon world/environment twin](../post-profit-tycoon/README.md)
is a separate project; its environmental representation, rendering and
interaction are future work. Production and exchange remain independently
runnable, with separate operating rules. Any future connection must explicitly
reconcile units, clocks, resource transfers and settlement.

The simulator uses the repository's existing unmodified MIT `nlohmann/json`
header; it links no pricing, forecasting, AMPL, LibTorch or CUDA library.
Reusable capabilities can move into `tools/` once their independent contract
and reuse are established. Application code and documents are MIT licensed;
the [JSON dependency notices](../../tools/price-optimization/third_party/nlohmann/PROVENANCE.md)
remain in force.

Develop, build, test and run only in the documented
[existing Linux container](environment/README.md). No new container is required.
The native build uses existing GCC/CMake; the web build reuses the exchange
project's pinned Emscripten SDK. From that environment:

```sh
cmake -S projects/post-profit-production -B .build/post-profit-production/native -DCMAKE_BUILD_TYPE=Release
cmake --build .build/post-profit-production/native --parallel 2
ctest --test-dir .build/post-profit-production/native --output-on-failure --no-tests=error
.build/post-profit-production/native/production_simulation --config projects/post-profit-production/configs/research.cfg
.build/post-profit-production/native/production_experiment --plan projects/post-profit-production/configs/scheduling-comparison.cfg --format markdown
```

The root build can include this project with `-DPH_BUILD_PRODUCTION=ON`.
See the [verification record](../../docs/production-simulation-verification.md)
for checks actually completed and their limits.
