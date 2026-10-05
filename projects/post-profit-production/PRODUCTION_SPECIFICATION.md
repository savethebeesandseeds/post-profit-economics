<!-- SPDX-License-Identifier: MIT -->
# post-profit-production: resources, labor, timelines and evidence

Status: initial operating specification and synthetic simulator, 2026-10-05.
The implemented subset is defined by the [simulation contract](simulation/CONTRACT.md).
Requirements for governance, funding and empirical evaluation below remain
research work. This document does not certify productive or financial viability.

## Purpose and boundary

This demonstration investigates post-profit-economic models of **production**:
how useful goods, services and productive capacity are created and sustained
under worker control. **Allocation** enters through competing demands on
finite stocks, labor and reusable capacity. Distribution and exchange require
separate interfaces and their own rules.

The architecture describes a productive unit through configuration. Product
identities, resource requirements, stage dependencies, labor, available capacity,
timeline assumptions and risks are supplied data. The first engine supports
a bounded family of integer, staged production models. General configuration
does not imply that its model fits all industries, services or technologies.

An economic demonstration must account for startup resources, financing,
necessary costs, maintenance, reserves and collectively governed surplus.
Passive-owner profit extraction is excluded from the intended arrangement.
The current resource/schedule simulator has no financing, cash account,
ownership or surplus-transfer mechanism; that omission supplies no evidence
about the effects or feasibility of removing owner extraction.

## Participants and authority

Workers govern the productive unit, approve operating commitments and retain
authority to change or suspend them. Participants supplying resources or
depending on output must have their obligations and decision rights stated.
Priorities, shortage rules, appeals and use of surplus require an accountable
decision process. These are institutional requirements, not properties created
by a configuration label or a scheduling algorithm.

The implemented model uses aggregate labor pools. It has no worker identity,
individual surveillance, performance ranking or authority to change pay.
Per-tick capacity bounds represent declared workload limits. They do not verify
consent, safe staffing, rest, qualification or the quality of employment.
Fixed compensation commitments remain due throughout the configured horizon,
including idle or interrupted ticks. Recording an obligation does not fund it.

## State, resources and production

Resources have declared base units and opening stocks. Stage inputs consume
these stocks; successful output quantities add to them. Reusable capacities
are per-tick constraints and are not consumed as inventory. Labor is a separate
finite capacity with fixed compensation commitments. Requirements per work
unit and maximum work rates are configured assumptions.

Processes contain acyclic stage dependencies. Jobs specify a process, release
time and optional deadline. Independent stages may run concurrently; the
scheduling policy assigns shared capacity. Stages consume their complete input
bundle atomically when they can start, retain work already performed across
interruptions, and produce their realized output on completion. Inputs already
consumed are not refunded at the horizon or after an interruption.

Resource records reconcile opening stock, actual output, consumption, losses
and closing stock for each identifier. These identities establish quantity
bookkeeping. Cross-resource physical conservation, process validity and the
usefulness of output depend on the declared transformation and appropriate
domain evidence. Currency labels and compensation totals do not form a
financial or inventory-valuation account.

## Decisions, timelines and disruptions

The first decision policies are FIFO and earliest deadline, with documented
tie order and allocation semantics. They are transparent comparators, not
claims of fairness or optimality. No universal profit, throughput or surplus
objective is imposed. Other priorities and production functions require an
explicit model extension and independent checks.

Completion time is derived from work performed, dependencies, inputs and
capacity actually available. A stage's rate limit can prevent additional labor
from shortening its timeline. Scarcity and interruptions can stretch work
without reducing the configured compensation commitment. The initial kernel
permits interruption between ticks without restart cost; processes that require
continuous operation or costly restart need a different declared mechanism.

Risk is represented through supplied scenarios and seeded events. Scheduled
shocks can reduce multiple pools together or remove unconsumed stock. Availability
risks interrupt a labor or capacity pool. Stage risks can delay work or reduce
output, with probability modulated by elapsed time. Those linear, bounded
relations are synthetic model choices; they are not empirical causal laws.

The prototype has no fitted fatigue model, continuous quality distribution,
maintenance optimizer, stochastic replenishment, general covariance model or
risk-calibration procedure. These can be investigated through explicit future
extensions. A configuration must identify whether parameter values come from
assumptions, observations or estimates, and retain the source of that evidence.
The provided example contains assumptions only.

## Outcomes and failure behavior

The simulator reports stage starts and completion, waiting reasons, labor and
capacity used, realized output, losses, unfinished jobs and missed deadlines.
Insufficient resources or capacity can leave work unfinished. An output loss
can eliminate useful output even when all required work completes. A deadline
miss is recorded without inventing delivery, assistance or restored inputs.

Invalid configuration is rejected before a run. Technical input errors return
an error without a trajectory. A valid run with shortages, delays or unfinished
jobs still returns its complete modeled trajectory. Financial insolvency and
outside support are not modeled here and must not be inferred from these states.

## Comparison and evidence

Predeclare horizons, outcome measures, seeds and comparison policies. Compare
schedules with equivalent opening stocks, resource/labor/capacity assumptions,
compensation commitments and external shocks. Availability draws are keyed by
named risk and tick, independent of scheduling order. Stage exposure depends
on the actual timeline; different schedules can therefore encounter different
stage events even with the same seed. Equal seeds do not imply equal outcomes.

Initial implementation checks should include independently calculated
timelines and balances, capacity contention, parallel/dependent stages,
interruptions, elapsed-time exposure, replay and negative cases. Sensitivity
work should vary labor, capacity, rates, input scarcity, horizon, scheduling
and risk assumptions across multiple seeds, including systematic/shared shocks.
Higher output alone does not establish human benefit or justify greater workload.

Economic evaluation subsequently needs funded compensation and operating
accounts, equipment replacement, maintenance, financing and outside support.
Comparisons involving owner returns must hold starting resources and obligations
explicitly and account for financing and transition costs. Simply removing a
cash outflow without representing its associated contributions cannot establish
an advantage of the proposed arrangement.

Empirical evaluation needs appropriate production observations, worker
participation, parameter estimation, held-out assessment and model discrepancy
checks. Measures should include access to useful output, continuity, resources
consumed or wasted, workload, compensation actually paid, unmet commitments
and dependence on outside support. Report failed cases and conditions where
the proposed model offers no advantage. Technical checks, simulated outcomes
and evaluated operations remain distinct evidence stages.

## Next research gates

1. Evaluate the bounded scheduling and risk mechanisms against independent
   calculations and varied synthetic configurations, preserving failure cases.
2. Define funded operating accounts and worker-governed decision rules without
   turning compensation into a score the scheduler can reduce.
3. Establish time, unit and transfer contracts before connecting production to
   distribution, exchange, assurance or the visual world engine.
4. Obtain suitable observations and assess which configured mechanisms are
   supported, which are inaccurate and which need replacement.

Passing a gate establishes its stated scope. It does not establish universal
production accuracy, calibrated risk or benefit from the institutional model.
