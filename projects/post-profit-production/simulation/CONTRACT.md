<!-- SPDX-License-Identifier: MIT -->
# Production research simulator contract

`production.sim.v1` is the initial MIT C++17, configuration-driven production
simulator. Full inputs use `production.config.v1`. It models integer stocks,
staged work, finite labor/capacity, transparent scheduling and supplied risk
assumptions. It has no fitted production function, cash settlement, financial
solvency decision, live governance or integrated supply network.

## Commands and execution

`ph::production::run_json(text)` accepts one strict JSON command:

```json
{"op":"defaults"}
```

This returns `{schema_version:"production.sim.v1",status:"ok",config:...}`.
Defaults are embedded from [research.cfg](../configs/research.cfg) at build time.
Its identifiers and values are an abstract synthetic fixture, not observations
or approved operating policy.

The other command shape is:

```text
{"op":"simulate","config":<complete production.config.v1 object>}
```

The placeholder describes the shape, not literal JSON. Every configuration
field is required; empty arrays/maps and explicit null deadlines express unused
mechanisms. Unknown/missing fields, duplicate JSON keys, unknown references,
cyclic stage dependencies and unsupported policies are rejected. Integer fields
must be integer JSON tokens; strings, booleans and floating-point tokens do not
substitute for integers. Input is limited to 2 MiB and parser depth 32. Actual
NUL bytes are rejected before parsing; they cannot terminate the document and
hide trailing data. JSON escapes such as `\u0000` retain their normal JSON
meaning and are subsequently subject to field validation.

Invalid commands return `{schema_version,status:"error",error:{code:
"invalid_production",message:...}}` with no trajectory. Typed `simulate(config)`
and `validate_config(config)` instead throw on invalid configuration. All run
state is local. No file, network, wall clock or renderer influences simulation.

The native CLI reads a command from stdin, runs an explicit `--config PATH`,
prints `--example-config`, or displays `--help`. It writes results to stdout
without automatic record files. Command/config errors exit nonzero. A valid
run with unfinished jobs or missed deadlines exits zero and retains those
outcomes. File/argument errors go to stderr without a partial run.

## Configuration and units

Time is one declared fixed tick. Resource, labor and capacity values are whole
base units; `unit` labels describe those chosen units without performing a
conversion. Each stage's per-work ratios must be defined consistently with
them. Choose finer base units when fractional work or quantities matter.
Currency is a three-letter uppercase label; compensation amounts are integer
minor units. The engine does not infer a currency's minor-unit exponent.

| Top-level field | Contract |
| --- | --- |
| `schema_version` | Exactly `production.config.v1`. |
| `time_unit` | Nonempty UTF-8 label, at most 32 bytes, without controls. |
| `currency` | Three uppercase ASCII letters. |
| `horizon_ticks` | 1–1,000; the run records every tick even if jobs finish early. |
| `seed` | Unsigned 32-bit value, 0–4,294,967,295. |
| `scheduling_policy` | `fifo` or `earliest_deadline`. |
| `resources` | 1–128 resource definitions. |
| `labor`, `capacity` | 0–32 definitions each. |
| `processes` | 1–32 processes, each with 1–16 stages. |
| `jobs` | 1–128 executions of configured processes. |
| `shocks`, `availability_risks` | 0–128 definitions each. |

The sum of stage counts for all configured jobs times `horizon_ticks` must not
exceed **200,000**. This bounds the trajectory before simulation; there is no
silent truncation. It is not a measured real-time or scalability guarantee.

Every `id` is 1–64 ASCII characters drawn from letters, digits, dash, underscore
and dot. IDs are unique within their collection; stage IDs are unique within
a process. Unit labels are nonempty UTF-8 of at most 32 bytes without controls.

| Definition | Required fields |
| --- | --- |
| Resource | `id`, `unit`, `initial_quantity` (0–1,000,000,000,000). |
| Labor pool | `id`, `unit`, `units_per_tick` (0–1,000,000,000), `compensation_due_per_tick` (0–1,000,000,000). |
| Capacity pool | `id`, `unit`, `units_per_tick` (0–1,000,000,000). |
| Process | `id`, `stages`. |
| Job | `id`, `process`, `release_tick` (1–1,000,000), `deadline_tick` (null or release through 1,000,000). |

Jobs, shocks and risks may start after the horizon. They remain explicit but
do not cause future events or alter earlier trajectory rows. Zero pool capacity
is valid and can leave work unfinished. Labor pools describe aggregate capacity,
not identified people or observed skill estimates.

## Stages and rate

Every stage has exactly:

| Field | Contract |
| --- | --- |
| `id` | Unique process-local stage identifier. |
| `depends_on` | 0–15 distinct known stage IDs in the same process; graph must be acyclic. |
| `work_units` | Total integer work, 1–1,000,000. |
| `max_work_per_tick` | Process rate limit, 1–1,000,000. |
| `inputs`, `outputs` | Maps from resource IDs to quantities 1–1,000,000,000; empty maps are allowed. |
| `labor`, `capacity` | Maps from pool IDs to units needed per work unit, 1–1,000,000; empty maps are allowed. |
| `risk` | The complete stage-risk object below. |

A job executes one configured process once. Repeat executions are distinct jobs.
Process outputs can be intermediate inputs for other stages/jobs. Dependencies
do not by themselves transfer or reserve resources: shared inventory supplies
the declared input bundle under the scheduling order.

For each eligible stage, work this tick is the minimum of remaining work,
`max_work_per_tick`, and each available pool divided by its per-work requirement,
rounded down. Requirements are simultaneous: extra labor cannot substitute for
a missing required pool. Empty labor/capacity maps permit a timed stage to use
only its rate limit. Fixed ratios plus saturation are the implemented production
function; substitution, fitted efficiency curves and arbitrary plugins are absent.

## Tick order and scheduling

1. Apply each starting shock's stock loss once, capped by stock actually present.
   Apply scheduled pool reductions while start/end ticks are inclusive.
2. Evaluate named availability risks in their active window; interrupt selected
   pools through their inclusive `blocked_until` tick. Derive available capacity.
3. Collect released, unfinished stages whose predecessors completed **before**
   this tick. Sort them under the selected policy.
4. In that order, check delay state, input availability and positive feasible
   work. A new stage consumes its entire input bundle atomically at start. Then
   evaluate its delay risk; if triggered, preserve inputs/progress but allocate
   no labor/capacity. Otherwise reserve its work requirements for this tick.
5. Apply all planned work and produce completed stages' realized output. No
   current-tick output is usable in the allocation phase, even by independent
   jobs. Mark a job complete when all its stages complete.
6. Record stocks, pool use, obligations and stage states for the tick.

FIFO orders by job release, job configuration index, then stage configuration
index. Earliest deadline adds deadline as the first key, with null deadlines
last. Stable configuration order resolves ties. Both greedily assign as much
feasible work as the rate permits and can interrupt allocations between ticks.
Neither performs global optimization. A missing input for one candidate does
not prevent other feasible candidates from running.

Inputs consumed at stage start are not restored after delay or at the horizon.
The prototype assumes work can pause without losing earlier progress or paying
a restart cost. That assumption must be evaluated for any configured process.
Stages remain pending/active when insufficient capacity or inputs prevent
completion. Row reasons describe the first blocking check in this order, not
an exhaustive explanation of all possible shortages.

## Scheduled shocks and availability risks

A shock has `id`, `start_tick`, `end_tick`, `labor_factors`, `capacity_factors`
and `resource_losses`. Ticks are 1–1,000,000 with end at least start. Factor maps
reference known pools with values 0–10,000 basis points. Active overlapping
factors use their minimum, then capacity is rounded down. Factors cannot expand
declared workload limits. Resource-loss maps reference stocks with positive
quantities up to 1,000,000,000; loss happens only on the start tick. Overlapping
loss requests are applied in configuration order with individual actual losses
recorded, and cannot remove stock already consumed by a stage.

An availability risk has exactly `id`, `target_kind` (`labor` or `capacity`),
`target_id`, `probability_bps` (0–10,000), `duration_ticks` (1–1,000), `start_tick`
and `end_tick` (the same window bounds). One draw occurs per active tick,
including ticks when the target is unused or already interrupted. A trigger at
tick `t` prevents use through `t + duration_ticks - 1`; overlapping interruptions
extend to their maximum end, rather than summing durations. Subsequent triggers
can extend downtime beyond the configured risk window.

## Stage risk and elapsed exposure

Every stage risk has the following fields:

| Field | Bounds and meaning |
| --- | --- |
| `delay_probability_bps` | 0–10,000; base chance at stage start. |
| `delay_age_increment_bps` | −10,000 to 10,000; change per elapsed tick since start. |
| `delay_ticks` | 1–1,000 inclusive ticks per interruption, even when chance is zero. |
| `output_loss_probability_bps` | 0–10,000; base chance on completion. |
| `output_loss_age_increment_bps` | −10,000 to 10,000; change per elapsed tick since start. |
| `output_loss_bps` | 0–10,000; reduction of every declared output if the event occurs. |

At tick `t`, age is `t - start_tick`. Effective chance is base plus age times
increment, clamped to 0–10,000. Age includes intervening waits and interruptions
after start. It excludes time a job waited before its stage could start.

A delay draw occurs only when a stage is not already delayed and would otherwise
have positive feasible work. It occurs after initial input consumption and
before allocating work. No new delay draw occurs during an existing delay or
a tick without feasible labor/capacity. Risk controls can therefore stretch
timelines, and changed timelines can change subsequent risk exposure.

Output loss is drawn once on stage completion at its actual age. The same
trigger and factor apply to all outputs of that stage. Each realized output is
`floor(declared_output * (10000 - loss_bps) / 10000)` when triggered, otherwise
the full quantity. Unrealized output is recorded separately and was never added
to stock. A completed job can produce zero usable output. No rework, quality
grade, input recovery, indemnity or assistance is invented.

Signed age increments permit configured increasing, constant or decreasing
exposure. They are bounded linear model assumptions, not fitted hazards or
claims about the universal effect of speeding/slowing work.

## Randomness, replay and records

Named availability draws depend on seed, risk ID and tick. Stage draws depend
on seed, job ID, stage ID, event kind and tick. Identities are length-delimited
and hashed with fixed-width unsigned arithmetic; draws do not consume a shared
advancing stream. Scheduling one job does not shift another's random stream.
Modulo 10,000 mapping has finite discretization; it is neither cryptographic
randomness nor evidence of empirical event frequency or calibration.

Same configuration and seed reproduce rows and events. Changing release times,
rates, time resolution, identifiers or risk definitions can change an experiment.
Common external availability draws support policy comparisons; timeline-dependent
stage events can differ across those policies. The engine has no checkpoint or
incremental world-command interface. Playback must use recorded ticks, and
changing inputs requires a new run.

The result retains `config`, `rows`, `events` and `summary` with `status:"ok"`.
Rows include closing resources, each pool's available/used units, compensation
due, and each stage's progress, state, start/completion ticks and waiting reason.
Events record starts, resource losses, availability interruptions, stage delays,
stage output and job completion. IDs are local configuration identities, not
authenticated or globally unique event/settlement identifiers.

For each resource, the summary verifies:

```text
closing = opening + actual_produced - consumed_at_stage_starts - stock_lost
unrealized_output = declared_completed_stage_output - actual_produced
```

Unit-specific identities do not sum unlike resources or establish physical
mass/energy conservation for a user-defined recipe. Pool use is bounded by
available units each tick. Total compensation due is the sum of each labor
pool's fixed per-tick commitment over the entire horizon, including idle or
absent ticks; `compensation_settlement` is always `not_modeled`. No paid amount,
cash balance, funded reserve or solvency status is supplied.

Jobs are `completed` or `unfinished` at the horizon. Stage states are `pending`,
`active` or `completed`. A deadline is missed if completion is later than it, or
the horizon has reached it and the job is unfinished. Null/future deadlines
do not imply a miss. These records describe modeled work and commitments;
they do not establish delivered services, customer needs met or worker benefit.
