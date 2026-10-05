<!-- SPDX-License-Identifier: MIT -->
# Production experiments

The experiment runner compares configured production policies without changing
the [production simulation contract](../simulation/CONTRACT.md). It runs complete
`production.config.v1` configurations, derives measurements from their existing
rows and events, and retains those records for inspection.

This is a synthetic research instrument. A passing expectation establishes
agreement with a declared prediction; it does not establish empirical fit,
worker benefit, funded operation or economic viability.

## Runner and plan

The MIT library target is `ph::production_experiments`. Its C++17 API in
`experiment.hpp` is in namespace `ph::production::research`:
`validate_plan`, `run_experiment`, `measure_run` and `render_markdown`.
The command-line runner reads an explicitly supplied plan and writes a report
to stdout:

```sh
.build/post-profit-production/native/production_experiment --plan projects/post-profit-production/configs/scheduling-comparison.cfg
.build/post-profit-production/native/production_experiment --plan projects/post-profit-production/configs/scheduling-comparison.cfg --format markdown
```

JSON is the default format. The runner creates no record directories and does
not resolve implicit input files. Redirect stdout explicitly when retaining
a local report. Use the [documented development container](../environment/README.md)
for builds, tests and experiments.

Exit status `0` means the plan completed and its verification and consistency
checks passed.
Status `2` means a report was produced with a failed verification or consistency
check. A verification-phase failure skips exploration. A later exploratory
consistency failure retains the exploratory observations in the report.
Status `1` means an argument, input or validation error.
A successfully simulated unfinished job is an outcome, not an execution error.

The versioned plan schema is `production.experiment.config.v1`. It contains:

| Field | Meaning |
| --- | --- |
| `schema_version` | The supported experiment-plan version. |
| `id`, `description` | Identity and stated purpose of the experiment. |
| `policies` | The supported scheduling policies to compare. |
| `verification_cases` | Cases with declared expectations checked before exploration. |
| `exploration_cases` | Cases run only after verification and prefix checks pass. |
| `prefix_checks` | Declared comparisons of shorter trajectories with longer ones. |

Each case declares `id`, `label`, a complete `config`, `seeds`, and
`expectations`. The runner changes only `scheduling_policy` and `seed` when
expanding that case. It does not patch a configuration from neighboring cases.
Within a policy pair, the seed and every other configuration field are held
fixed, including identifiers and array order.

Expectations are keyed by policy and may inspect `result` and `measurements`.
Expected objects are recursively matched as subsets of the observed object;
expected arrays must match exactly. A missing expected field or a changed
expected value fails the check. An empty expectation supplies no prediction;
it must not be reported as corroboration of a hypothesis. Verification requires
a nonempty expectation object for each declared policy containing at least one
concrete scalar or exact-array prediction; subsets containing only nested empty
objects are rejected. An exact empty array is a concrete prediction, such as
asserting that no events occurred. Exploration requires an empty expectations
object.

Each prefix entry declares `short_case` and `long_case`, referencing two
verification cases with identical seeds and configurations except for an
increasing horizon. All verification cases run before the gate is evaluated,
so a failure retains the other verification observations for diagnosis.
Prefix checks compare both tick rows and events through the shorter horizon.

The plan is deliberately bounded: one or two policies, one to 64 verification
cases, zero to 64 exploration cases, and one to 64 distinct seeds per case.
The total expansion is at most 256 policy runs and at most 200,000 configured
stage-tick records across all case, policy and seed combinations. A separate
conservative bound permits at most 200,000 observation units: for each expanded
run, multiply its horizon by its instantiated stage count plus resource, labor
pool and capacity pool counts, then sum across runs. This accounts for additional
per-tick row payload; neither bound establishes real-time scalability. Simulation
configuration limits remain in force. JSON input uses the existing strict
parser, including its 2 MiB input and depth-32 limits. Identifiers are at most
64 bytes with the same restricted ASCII characters as the simulation;
control-free UTF-8 labels and description are limited to 256 and 2,048 bytes.
At most 64 prefix pairs are accepted. Unsupported keys and
malformed configurations are rejected rather than silently repaired.

## Scheduling comparison

The [scheduling comparison plan](../configs/scheduling-comparison.cfg) asks:

How do `fifo` and `earliest_deadline` distribute waiting before start, progress,
pauses after start and unfinished work under contested labor or capacity? How
do opening stocks, the observation horizon and declared interruptions affect
those observations?

The plan uses neutral identifiers and arbitrary synthetic values. It is
separate from [research.cfg](../configs/research.cfg), which mixes several
mechanisms and is not an isolated scheduling baseline.

The shared baseline has two single-stage jobs in the order `job-late`,
`job-early`, both released at tick 1. Their deadlines are 8 and 2 respectively.
Each stage needs four work units, can do at most two per tick, consumes two
units of `input-a` once at start and produces one unit of `output-a` at
completion. Each work unit requires one labor unit and one capacity unit.
Compensation due for `labor-a` is 1,000 integer minor units per tick; settlement
remains `not_modeled`. The baseline horizon is eight ticks.

All deterministic arms use seed 41, zero risk probabilities, and empty shock
and availability-risk lists unless the arm explicitly declares a shock.

| Arm | Controlled change | Declared observation or check |
| --- | --- | --- |
| S0 | Labor 8, capacity 8, opening input 100. | Both jobs start on tick 1 and complete on tick 2 under both policies. No waiting or deadline miss. |
| S1 | Labor 2, capacity 8, opening input 100. | FIFO starts the late job first; earliest-deadline starts the early job first. Completion ticks are 2 and 4. The deferred stage has two pre-start `awaiting_labor` rows. FIFO misses the early deadline; earliest-deadline meets both. |
| S2 | Labor 8, capacity 2, opening input 100. | S1's completion pattern, with pre-start `awaiting_capacity` replacing `awaiting_labor`. |
| S3 | S1 pools; separate processes require six work units for the late job and four for the early job. The early job is released at tick 2. | FIFO completes late/early at 3/5. Earliest-deadline completes early/late at 3/5, pausing the already-started late stage on ticks 2–3. The early job misses deadline 2 under both policies. |
| S4 | S1 with opening input 0, 2 or 4. | Zero input leaves both jobs unfinished. One input bundle lets only the first-priority job complete. Two bundles permit S1's labor-contested trajectory. |
| S5 | S1 at horizons 2, 4 and 8. | Shorter rows match the corresponding longer-run prefix for each policy. Horizon 2 leaves the deferred job unfinished; horizon 4 observes its completion. Deadline flags reflect whether a deadline has been reached. |
| S6 | S1 with labor availability reduced to zero on ticks 2–3 by one scheduled shock. | The started stage pauses; the other stage remains waiting before start. The calendar shock matches, while which job is exposed differs by policy. |
| S7 | Independently derived from S1, with no shocks; one labor availability risk, probability 2,000 basis points, duration one tick, window 1–8. | Seeds 0–19 explore matched external interruption draws and differing job exposure. Stage-risk probabilities stay zero. |
| S8 | Independently derived from S1, with no shocks or availability risks; stage-delay probability 2,000 basis points, duration one tick, age increment zero. | Seeds 0–19 explore stage-delay opportunities and events. Output-loss probability stays zero. Equal seeds do not equalize stage exposure. |

S3 needs two explicit process definitions because `work_units` belongs to a
process stage, not to a job. The jobs cannot override the process requirement.
S7 and S8 start independently from S1; neither inherits S6's shock or the
other exploratory arm's risk.

S0–S6 expand to 11 verification cases and 22 policy runs. S7 and S8 expand
to 80 exploratory policy runs. The complete plan therefore has 102 runs.
Each retained run is simulated again to check deterministic replay, giving
204 simulation executions when the full plan completes. Replay does not
add a second sample or a second trajectory to the report.
Four prefix checks compare S5's horizon-2 trajectory with horizon 4 and its
horizon-4 trajectory with horizon 8 across both policies. They reuse the retained runs.
Exploration is gated on all verification-phase checks, including the
deterministic expectations and those prefix comparisons.

Array order is held fixed. Swapping the two jobs changes FIFO when release
times tie. With distinct deadlines it does not change earliest-deadline
priority. Earliest-deadline consults array order only after deadline and
release-time keys tie. An order experiment would be a separately declared
comparison.

## Measurements

Measurements derive from the engine's existing records and preserve each
job and stage identity. They do not infer a new blocker set or alter the
production trajectory.

- **Before start:** rows with a null stage start tick, classified by their
  recorded reason. Unreleased rows remain explicitly identifiable; they are
  not evidence of a contested pool. Dependency, input, labor and capacity
  waiting remain distinct recorded categories.
- **Progress:** rows whose reason is `working`, including the completion tick,
  and the sum of `work_this_tick`.
- **Constrained progress:** working rows below the feasible unconstrained
  rate `min(max_work_per_tick, remaining_work_before_tick)`. A final partial
  work tick is therefore not labeled constrained merely because less than
  `max_work_per_tick` remains.
- **Pause after start:** rows with a start tick, zero work, and a reason other
  than `completed`, classified by the recorded reason. A delay triggered at
  start can produce such a row before any work has been allocated.
- **Completed rows:** later rows whose reason is `completed`; excluded from
  both waiting and pause counts.
- **Stage span:** inclusive elapsed time from start through completion,
  reported only when completion is observed.
- **Remaining work:** required work minus completed work at the horizon,
  including stages that never started.
- **Delay opportunities:** working stage rows plus newly triggered
  `stage_delay` events. Already-blocked `risk_delay` rows do not create new
  opportunities. Counting them would overcount multi-tick delays.

Job status, completion tick, release, deadline and missed-deadline flags are
retained separately. Summing parallel stage counts does not produce a job
duration. A multi-stage configuration remains inspectable by stage without
pretending that concurrent stage ticks are sequential elapsed time.

The report also retains pool availability and use, stock records, resource
balances, compensation obligations and the event stream. The first reported
reason is the engine's first failing check. For example, a labor failure is
reported before capacity is consulted. It does not establish that labor was
the only constraint or the unique cause of a delay.

Availability interruptions can appear as `awaiting_labor` or
`awaiting_capacity`; only the stage-delay mechanism produces `risk_delay`.
The availability event stream distinguishes the configured external events.

## Reports and interpretation

The JSON report uses `production.experiment.v1`. The independent
[offline web view](../web/README.md) runs this same C++ experiment runner and
inspects its checks, case/policy samples and retained trajectories. It preserves
verification failures and skipped exploration. Tick playback does not alter
the experiment's horizons, seeds or compensation obligations.

The report retains the entire plan,
full expanded configurations, trajectories, events, measurements, expectation
results and paired checks. Case/policy samples and histograms keep unfinished
outcomes visible in the denominator, including an explicit zero-count
`unfinished` category when all samples complete. The Markdown format is an
inspection view derived from the same report: it summarizes job completion,
stage-count and reason-specific count frequencies. The reason distributions
separate unreleased and dependency waiting from the recorded pool and input
conditions, and preserve post-start reasons. They do not establish a complete
blocker set or unique causal attribution. Detailed per-seed records, pool use,
stocks and compensation remain in JSON. The pure report contains
no wall-clock date or timing; an external receipt can record when it was run.

The runner supplies no policy ranking and no completion average restricted
to successful jobs. Examine who waited, who paused, how much work remained,
which deadlines were missed, and what horizon was observed.

At a fixed horizon, unchanged compensation due across policies is a
programmed invariant. Checking it verifies behavior; it does not measure
actual labor cost or payment. Across horizons, due changes with the number
of observed ticks and must not be interpreted as policy efficiency.

Similarly, missing input cannot be replenished merely by waiting longer:
these arms contain no replenishment process. That is a contract check, not
an empirical discovery about production. S5 can distinguish a completion
outside a short window from permanent input absence in these specific arms;
it cannot diagnose every unfinished trajectory.

The research observations concern the distribution of progress, waiting,
pauses, resource consumption and unfinished work under declared assumptions.
Synthetic risk probabilities are uncalibrated. Equal availability-risk keys
support matched external events; equal stage seeds do not guarantee matching
event opportunities because scheduling changes their timing.

No recorded outcome establishes safe staffing, worker consent, settlement,
useful demand served, physical recipe validity or institutional legitimacy.
Simultaneous-blocker diagnostics and funded operation remain subsequent
research and need separately defined semantics and checks.
