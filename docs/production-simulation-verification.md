<!-- SPDX-License-Identifier: MIT -->
# Production simulation verification

This record covers the initial configuration-driven C++17 production research
simulator, `production.sim.v1` with `production.config.v1`, on **2026-10-05**.
It records implementation and command-line checks, not empirical production
accuracy, calibrated risk, funded compensation or economic viability.

## Environment and completed checks

Checks ran in the existing CPU Debian container
`praesidium-humanitatis-price-optimization`, immutable ID
`5738ae552c575e91e0d4308b8aa2c66aa170e8dd817282fd7abd5817907d3ff9`,
with GCC 12.2.0 and the documented CMake environment. The existing configuration
was reused; no container, dependency, mount, volume, port or device was added.
No project build or simulation ran on the Windows host.

| Check | Observed result |
| --- | --- |
| Standalone production build | Compiled the library, CLI and fixture executable with configured warnings; no source warnings were reported. |
| Standalone CTest | **2/2 passed**, no skips, 0.19 seconds: production model and CLI suites. |
| Model suite | **6,926 assertions passed**; this includes repeated per-tick invariant checks, not 6,926 independent experiments. |
| Root integration with `PH_BUILD_PRODUCTION=ON` | Build succeeded; **10/10 CTest entries passed**, no skips, 2.46 seconds, including pricing/smoothing and both production suites. |
| Abstract `.cfg` run | Completed with strict input configuration retained and reconciled resource records. Result saved locally for inspection. |
| Source review | Tracked diff whitespace check passed; new production source/documents had no trailing whitespace. |

The root integration used the dependency-free pricing build with AMPL disabled.
These checks did not rerun AMPL integration, the exchange native/WebAssembly
suites, or the separate TFT CPU/CUDA suites. Production links none of those
engines and has no browser or WebAssembly build.

## What the fixtures establish

Hand-calculated fixtures check work rate under labor/capacity bottlenecks,
saturation, atomic input consumption, delayed output, shared pool allocation,
parallel/dependent stages and dependency order independent of stage array order.
An independent producer's output also cannot become another job's input within
the same tick.

Scheduling fixtures compare FIFO and earliest deadline, release times, actual
deadline misses and future unreleased work. Risk fixtures check inclusive
interruption durations, fixed compensation obligations during absence,
stock-bounded one-time loss, overlapping capacity factors, interrupted work,
elapsed-time output loss, rounding, and completed work with zero usable output.

Twenty-four seeded stochastic scenarios check replay and resource/capacity
invariants. Prefix and future-shock checks verify earlier trajectory rows remain
unchanged. Large bounded integer inputs preserve compensation totals. Validation
checks reject missing/unknown fields, duplicate IDs/JSON keys, unknown references,
cycles, incorrect types, bounds, unsupported policies, invalid UTF-8, excessive
nesting and the aggregate trajectory budget. CLI checks compare the complete
embedded configuration with the source file, replay a configured run and reject
invalid arguments without producing a run.

These fixtures establish the stated mechanisms for the checked configurations.
They do not fit risk parameters, validate productive recipes, measure human
outcomes or prove correctness for every permitted configuration.

## Observed abstract run

The provided configuration uses neutral resource/process IDs and arbitrary
synthetic values. At seed 41 and an eight-tick horizon, both jobs completed on
tick 4, with no deadline misses. Eight declared labor units and eight declared
capacity units were used. Resource A opened at 20 and closed at 14 after six
units were consumed. Resource B had two produced and two consumed units, ending
at zero. Each output resource had one realized unit and no unrealized output.

Fixed compensation obligations totaled 8,000 integer minor currency units over
the horizon. `compensation_settlement` remained **`not_modeled`**. This is no
claim that funds existed, compensation was paid, a productive unit was viable,
or anyone's needs were met. The named availability and output-loss risks did
not realize a loss in this particular fixture; other seeded/forced-event checks
exercise their implemented behavior.

## Local evidence and rerunning

Ignored artifacts are retained under
`.build/post-profit-production/verification/`:

- `standalone-tests.log`: preserved native CTest details.
- `root-tests.log`: preserved combined core CTest details.
- `research-result.json`: the complete abstract configured trajectory.
- `manifest.json`: source SHA-256 records, container identity and test receipts.

The manifest is a local receipt, not an authenticated or tamper-evident record.
The [environment guide](../projects/post-profit-production/environment/README.md)
documents the container and exact standalone commands. For the combined root
check inside that environment:

```sh
cmake -S . -B .build/core -DCMAKE_BUILD_TYPE=Release -DPH_BUILD_PRODUCTION=ON
cmake --build .build/core --parallel 2
ctest --test-dir .build/core --output-on-failure --no-tests=error
```

The [production specification](../projects/post-profit-production/PRODUCTION_SPECIFICATION.md)
sets out the intended economic model and next evidence requirements. Broad
sensitivity work, fitted mechanisms, funded accounts, worker-governed decisions,
clock/transfer adapters and empirical evaluation remain future research.

## Configured scheduling experiment, 2026-10-05

The [experiment runner](../projects/post-profit-production/experiments/README.md)
now implements the reviewed scheduling comparison as a bounded, configurable
research plan. Cases, policies, seeds and expected observations are data in
[scheduling-comparison.cfg](../projects/post-profit-production/configs/scheduling-comparison.cfg),
not product-specific code in the production kernel. The simulation source,
public simulation header, simulation CLI and original `research.cfg` retain
their earlier source hashes; production semantics were not changed.

The runner checks deterministic predictions, replay, quantity/progress/capacity
invariants, equivalent paired inputs, matched external availability and horizon
prefixes before interpreting observations. A failed verification phase retains
its diagnostic runs and skips exploration. Reports preserve unfinished jobs
and every declared seed; no completion-only average or policy ranking is added.

| Completed check | Observed result |
| --- | --- |
| Standalone production build | Library, simulation CLI, experiment library/CLI and both model suites compiled in the existing managed container; final build reported no source warnings. |
| Standalone CTest | **4/4 passed**, no skips, 0.49 seconds, including the new experiment model and CLI suites. |
| Experiment model suite | **633 assertions passed**; these include repeated checks of retained protocol observations, not 633 independent experiments. |
| Root integration | **12/12 passed**, no skips, 3.10 seconds, with production enabled and AMPL, exchange and TFT disabled. |
| Complete scheduling plan | **102 recorded policy runs**, all **281 verification/consistency checks passed**, no skipped cases. Each run also checked replay, giving 204 simulation executions per complete report. |
| CLI failure behavior | A changed deterministic expectation returned exit 2, retained 22 verification runs and skipped both exploratory cases. Invalid input/arguments returned exit 1 without a report. |

Checks ran with GCC **12.2.0** in the same inspected CPU Debian container and
immutable ID recorded above. No new dependency or container configuration was
introduced, and no project build or simulation ran on Windows. The managed
container was restored to its initially stopped state after verification.
The root checks do not establish AMPL, exchange or TFT verification for this change.

Focused tests cover completion ticks, final partial work, delay opportunities
with multi-tick interruptions, unreleased and dependent work, parallel stage
counts versus job duration, process-local stage identifiers, unfinished samples,
all-seed denominators, policy pairing, prefix checks and verification gating.
Strict plan checks cover duplicate/invalid keys and identities, unsupported
policies, seed bounds, exact array expectations, vacuous object predictions,
references and aggregate run/stage/resource/pool observation bounds.

### Observed synthetic risk arms

S7 and S8 each used the declared seeds **0–19** and an eight-tick horizon.
Both were independently configured from the labor-contested baseline. S7 added
only an uncalibrated labor-availability risk; S8 added only an uncalibrated
stage-delay risk. Neither inherited the scheduled shock or the other arm's risk.

| Arm | Policy | Early-job deadline misses / samples | Unfinished jobs across the 40 job observations |
| --- | --- | --- | --- |
| S7 availability | FIFO | 20/20 | 0 |
| S7 availability | Earliest deadline | 7/20 | 0 |
| S8 stage delay | FIFO | 20/20 | 0 |
| S8 stage delay | Earliest deadline | 5/20 | 0 |

These are frequencies in a declared synthetic seed set. They are not measured
operating rates, calibrated risk, or a general recommendation of one scheduler.
The early deadline and contention were deliberately chosen to expose priority
effects. Giving that job earlier priority also moves waiting and completion
time to the other job. The complete reports retain that distribution.
Unfinished outcomes remain explicitly represented even though these exploratory
samples all completed; the deterministic shortage/window cases and independent
measurement fixtures exercise unfinished observations.

Fixed compensation obligations equal 8,000 minor currency units in each
eight-tick run. Their equality is a programmed invariant, not an economic
finding. Settlement remains `not_modeled`, and the experiment supplies no
evidence of available funding, worker benefit or productive viability.

### Retained experiment records

Ignored local artifacts are under `.build/post-profit-production/experiments/`:

- `scheduling-comparison.json`: complete plan, 102 expanded configurations,
  trajectories, events, stage/job measurements, paired checks and seed samples.
- `scheduling-comparison.md`: presentation of those observations, including
  reason-specific waiting frequencies and explicit unfinished categories.
- `standalone-tests.log` and `root-tests.log`: completed CTest receipts.
- `manifest.json`: source/artifact hashes, environment and check receipts.

The manifest is a local integrity record, not authenticated evidence. The
original initial-simulator receipts remain separate under `verification/`.
Build and run instructions are in the project environment and experiment guides.

## Independent production web view, 2026-10-05

The [standalone production HTML](../projects/post-profit-production/dist/post-profit-production.html)
now embeds the same C++ simulation and experiment runner through WebAssembly,
with raw configuration/plan editors, recorded tick playback, SVG stage timelines,
resource/pool inspection, events, quantity accounting, experiment diagnostics and
complete result/input export. No industry-specific process or JavaScript model
was added. The default configuration and scheduling plan remain the source data.

The build reused the same inspected managed Debian container and immutable ID
recorded above, with the existing Emscripten **4.0.15**, Node **22.16.0** and Python
**3.11.2**. No dependency installation, new container, listener, mount, published
port or Windows project execution was introduced. The container was restored to
its initially stopped state after the checks.

| Final completed check | Observed result |
| --- | --- |
| Standalone native CTest after input fix | **4/4 passed**, no skips, 0.50 seconds. |
| Root integration after input fix | **12/12 passed**, no skips, 2.77 seconds; production, EWMA and pricing enabled; AMPL, exchange and TFT disabled. |
| Native / WASM runtime parity | **34 simulation fixtures passed** with exact JSON parity, replay and physical accounts; complete **102-run, 281-check** experiment report matched native C++ exactly. |
| Delivered standalone worker | Default production, full comparison, replay, failed-verification retention/gating and invalid raw input passed using the packaged worker source and embedded runtime. |
| Input boundaries | Duplicate keys, integer token types, document-marker handling, actual null bytes, Unicode surrogates and the **2 MiB UTF-8 command boundary** checked. File imports reject invalid UTF-8. |
| UI data and request lifecycle | Actual view helpers preserve completion-tick progress, unfinished frequencies and full sample denominators. Targeted UI lifecycle tests cover input snapshots, cancellation, previous-result retention and stale worker responses using mocked transport/DOM properties and renderer hand-offs. |
| Source and policy packaging | Embedded source matches current files; only production, required MIT JSON and applicable runtime notices included. Release/review app and style hashes match actual bytes; review-only style-element exception and noindex separation checked. |

The standalone test is a Node execution of the delivered worker with its browser
transport adapted. The lifecycle tests do not emulate layout or prove browser
interaction quality. Browser visual review was attempted, but Codex's URL policy
rejected the local `file:` protocol and prohibited alternate-surface workarounds.
No workaround was attempted. Thus layout, keyboard usability and actual browser
CSP enforcement remain **unverified**; the passing policy checks establish
packaged directives and hashes, not browser execution of those directives.

### Validation correction found during packaging

The first standalone check exposed acceptance of a valid JSON prefix followed
by an actual NUL byte and trailing text. The shared JSON lexer treated that byte
as end-of-input. `parse_json` now rejects actual NUL bytes before parsing, with ten
focused native assertions. Escaped `\u0000` remains valid JSON syntax and is
subject to existing field validation. The browser worker also rejects actual
null characters before its C string call, whose transport cannot represent them.
The failing native assertion was retained and passed after the fix. Scheduling,
allocation, quantity and risk calculations were not changed.

The release uses hashed scripts/styles, blob workers and WASM compilation, with
network fetches, JavaScript eval and inline script/style attributes prohibited.
A separate ignored `noindex` review copy permits inline style elements for the
Codex selector. It is excluded from release exports and the source ZIP. The
compiled WASM has an 8 MiB stack and a 512 MiB memory ceiling; browser memory
availability and handling of every maximum-size configuration are not guaranteed.

### Retained web artifacts and scope

The distributable is `projects/post-profit-production/dist/post-profit-production.html`.
It includes corresponding source and applicable notices without the pricing
engine. Ignored runtime and review copies remain under
`.build/post-profit-production/wasm/` and `web-review/`. Final test logs and a
source/artifact hash manifest are under `web-verification/`. The manifest is an
unauthenticated local receipt. See the [web guide](../projects/post-profit-production/web/README.md)
and [container procedure](../projects/post-profit-production/environment/README.md).

These checks establish the stated software behavior. They do not establish
calibrated risk, funded compensation, worker governance, productive viability or
human benefit. Tick playback observes a full completed trajectory; it does not
change the horizon or recompute compensation obligations.
