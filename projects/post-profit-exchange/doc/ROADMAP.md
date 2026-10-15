<!-- SPDX-License-Identifier: MIT -->
# Roadmap: from simulation to vendor operation

Status: implementation roadmap, 2026-10-15. The simulator exists, and the first
[operating-environment checkpoint](OPERATING_ENVIRONMENT.md) persists opening
records and delivery, cash-sale and new-expense transactions. The connected
interface is implemented; its approved [local service launch](APPLICATION_SERVICE.md)
uses one container per configured operation and retains the general development
container. Launch readiness is separately verified by the launcher. Milestones 1 and 2 remain
partial; no milestone below is complete. Price proposals/publication, broader
accounting workflows and funded assurance remain future work.

A separate [native Windows delivery](WINDOWS_NATIVE.md) is being prepared so
vendors can use the C++ application without Docker or WSL. IdyDB's Windows SDKs
now exist and a small MinGW consumer linked successfully; the full application
still needs a pinned dependency package and native acceptance. This delivery work
does not complete any accounting, governance or continuity milestone below.

Our aim is a practical, inspectable exchange that vendors can operate under
worker control without a passive owner, with separately funded protection
against catastrophic failure. The current policy and accounts are the starting
point. We will test their use in actual operations without replacing them with
a universal economic framework.

Read the [folder and dependency rules](STRUCTURE.md) before implementation.
The [store specification](STORE_SPECIFICATION.md) remains authoritative for the
current economic rules; [verification](../../../docs/exchange-simulation-verification.md)
records what has actually passed.

## Readiness and order of work

| Stage | Meaning | Current status |
| --- | --- | --- |
| Research simulator | Synthetic runs, accounting checks and inspectable price decisions. | Implemented and tested; defaults remain the baseline. |
| Shadow operation | Real records, continuing accounts and proposed prices; no automatic change to actual prices. | Opening state, delivery/cash-sale/new-expense entry and IdyDB replay implemented; tested with isolated synthetic inputs. Broader records/recovery and recommendations remain milestones 1–3. |
| Supervised vendor operation with continuity protection | Worker-approved publication, reliable operational recovery and an actual funded assurance arrangement. | Planned: milestones 4–6, after the earlier gates. |

The first useful checkpoint is: **load an opening position, import a real day,
reconcile the accounts, restart without losing or doubling anything, and explain
tomorrow's proposed prices.** Workers continue controlling actual prices.

For that first checkpoint, propose one exchange, one currency, a single writer,
whole-unit goods, a small catalog within current limits, and daily price proposals.
These are configurable pilot assumptions, not permanent project limits or a
decision about a particular shop. Identify the actual pilot's needs in milestone 1.
An unsupported activity must be reported explicitly rather than approximated
silently. More frequent pricing and larger catalogs need their own measured
acceptance cases.

## Build the application and exercise it in a vendor environment

Post-profit-exchange remains the reusable application. A one-vendor demonstration
environment is its first configured example and a way to examine the user
experience as features are implemented. Its setup, labeled sample inputs and
walkthrough are separate from operational code; follow the
[example boundaries](STRUCTURE.md#the-operating-environment-consumes-the-application).
It uses the same operator interface and application commands as a vendor
deployment, with its own configured IdyDB data directory.

The first environment lives at `environments/vendor_0x01/`, with
central `.cfg`, metadata, a synthetic opening import, its own generated IdyDB
database and a generated read-only report. The shared connected interface records
activity through the normal native commands; it contains no example-specific
rules. The application loads this directory and does not depend on the example.
Setup and the transaction walkthrough are in
[OPERATING_ENVIRONMENT.md](OPERATING_ENVIRONMENT.md).

For each milestone, deliver the reusable capability and exercise the available
part of it in the environment. Begin with opening an operation, receiving goods,
recording sales and expenses, inspecting accounts, restarting, and requesting
an explained price proposal. Add later workflow steps when their milestone is
implemented. Use explicit sample records until observed records are available;
neither the application nor the example fills gaps with invented activity.

During a walkthrough, record the operator's task, whether it could be completed,
confusing steps, and the proposed improvement to the shared interface. The
environment may reveal needed changes in workflow or terminology; it must not
introduce its own economic rules. Walkthroughs provide evidence about usability
of the exercised tasks, not economic results in actual operation.

**Separation check:** the same command/interface path must accept a separately
configured operation without editing C++ or depending on example files. Sample
products, amounts and names stay in inputs. The demonstration can be omitted
from a vendor release. Its existence does not mark live-operation or assurance
milestones complete.

## 1. Define one operation and its accounting cases

**Deliver:** a small, versioned operational input/state contract and exact
hand-calculated examples. Preserve the existing simulation contract and defaults.

Identify the chosen shop's opening cash, FIFO inventory lots, reserve earmark,
earned funding history, outstanding obligations and actual published prices.
Define its accounting periods and the supported events: receipts, sales,
returns/corrections, waste, obligations and actual payments. Specify how
replacement quotes differ from acquisition costs and cash settlement.
Record applicable fees, tax treatment, weighted goods, supplier credit and delivery
delays as either supported requirements or explicit exclusions for the pilot.

**Complete when:** fixtures reconcile cash, stock, inventory value, obligations
and earned funding across two periods. Paying an accrued expense does not charge
it again. Opening resources are not trading earnings. Refund/correction examples
reverse the appropriate original entries without erasing their history.

**Current increment:** opening validation and exact paid-delivery, FIFO cash-sale,
and new paid/unpaid-expense transitions are implemented. They preserve supplied
public prices and record actual sale prices. Refresh/replay never accrues a new
day. Existing-obligation settlement, returns/corrections, spoilage, credit and
period accrual remain unsupported, so the full completion gate is not met.
The [worked transaction sequence](OPERATING_ENVIRONMENT.md#worked-example)
reconciles cash, inventory, obligations and earned funding.

**Do not add:** a general accounting platform, live publication, new price
objectives or another post-profit project's state model.

## 2. Keep reliable operational records with IdyDB

**Deliver:** one central `.cfg` per operation and an exchange storage adapter
using the independently maintained IdyDB tool in repository `tools/idydb/`.
IdyDB is the selected database for actual events, approvals and reconstructible
operational state. This replaces the earlier file-only/no-database requirement.
Define versioned exchange records and keep JSON for documented imports, exports,
test fixtures and the existing simulator's run files.

Database implementation belongs to the separate IdyDB work. Exchange owns its
record schema and integration through `code/storage/`; do not fork the database,
duplicate its internals or make accounting depend on database types. The adapter
now stores opening records and ordered events as exact JSON text in IdyDB.
It replays accounts, rejects conflicting IDs and stale revisions, serializes
writers and atomically replaces a verified database. Identical event retries apply
once. The browser retains unconfirmed request identities across reloads; these
requests are not accounting authority. The current bound is 100 events and
60 KiB. Existing opening-only v1 data remains readable and becomes v2 on the first
accepted transaction. The v2 operating configuration selects an external key and
requires encrypted storage; its runtime fields are excluded from accounting
configuration comparisons. An explicit offline rename/encryption migration
retains an encrypted original envelope. General configuration/metadata changes,
migrations, reset, full recovery and backup procedures remain unsupported.
Use a configured private
data directory, separate from checked-in example records.

Record stable event identities, source references, ordering and state revisions.
Start with one writer. Define duplicate/conflicting imports, corrections,
interrupted writes, backup and restoration. Retain the lot costs and forecast
state or exact ordered observations needed to reproduce decisions; summary
totals alone are insufficient.

**Complete when:** importing the same event twice changes the accounts once;
conflicting content under the same identity is rejected; a concurrent writer
cannot silently overwrite state; an interrupted write is detected and recovered
according to the documented procedure. Restart and backup restoration reproduce
the accepted records and account balances exactly. Exercise these cases through
the exchange's IdyDB adapter with synthetic fixtures before importing pilot data.
These checks verify exchange integration; database internals remain the separate
tool's responsibility.

**First usable input:** manual records or a documented export from an existing
checkout. Building a new checkout is not a prerequisite.

## 3. Recommend prices from actual operation

**Deliver:** a headless shadow-operation path using the existing exchange rules,
optimizer and forecasting tool. Add a small operator view or report that shows
actual state, unmet funding needs, the proposal, and its explanation.

Use real time and revisioned observations. Learn from the prices actually charged,
not prices merely recommended. Preserve missing-period and stockout information;
do not generate customers, sales, receipts or payments in this path.
Keep assumptions and synthetic fixtures visibly distinct from observations.

Accrue wages, expenses and scheduled reserve requirements once per defined
period. Repeated forecasts, retries or price calculations must not accrue them
again. Test the forecast against subsequent observations without letting future
data enter an earlier decision. Keep TFT optional; adding it requires separate
evidence that it improves the relevant forecast.

**Complete when:** the first checkpoint above works from beginning to end.
Replaying recorded events reproduces the same accounts. With the recorded
configuration, observations, policy/build, decision-time context and candidate
order, the baseline bounded enumerator reproduces its recommendation. Do not
promise identical remaining ties across different backends; retain the original
decision and backend details for audit. Replay grants no new publication authority.
Repeated proposals leave accounts unchanged. Forecast error and data gaps are
visible. A shadow proposal has no checkout/publication side effect.

**Evidence limit:** observing sales at actual prices does not reveal what would
have sold at every untried price. Shadow results alone cannot validate the
counterfactual price-response model or establish economic superiority.

## 4. Put workers in control of price publication

**Deliver:** an authenticated, inspectable proposal → approval → publication
flow, including the actual effective price, acknowledgment, expiry and correction.

Record who had delegated authority under which approved policy. Check current
stock, costs, state revision and policy again before publishing. Keep shelf and
checkout prices consistent. Define what happens when publication partially
fails, a recommendation expires, or a worker withdraws an approval.
The interface must distinguish proposed, approved and actually effective prices.

The simulator's price-change cap is currently per update. Before a live scheduler
is enabled, specify its time window and reference price so repeated recalculations
cannot compound that cap unintentionally. Increasing solve frequency must not
change the approved economics by accident.

**Complete when:** an unauthorized or stale proposal cannot be applied; repeated
publication attempts do not create contradictory effective prices; a failed or
partial update is visible and recoverable. Workers can use an explicit manual
operating procedure when recommendations or publication are unavailable.

Worker authorization here is the minimum needed to operate the application.
It does not replace the broader governance arrangements of the collective.

## 5. Connect funded continuity assurance

**Deliver within exchange:** support requests with evidence, tracked responses,
and verified settlement records. The assurance domain defines funding, eligibility,
limits, decision authority, response timing and any recovery or exit arrangement.

A request or approval alone must not increase cash. Record confirmed receipts
once and keep external support separate from earned trading feedback.
Corrections and delayed, duplicate, refused or conflicting responses must have
explicit outcomes. Test-only funded responses remain labeled fixtures.

**Complete when:** an exercise with the actual assurance arrangement traces
request, authorized decision, payment confirmation and account reconciliation;
retries cannot duplicate support. Document how the people and operation are
supported if continued trading is not viable.

A funding arrangement may be developed in parallel by the separate assurance
project. Its absence does not prevent shadow research, but blocks a claim that
the exchange delivers funded continuity protection. Insurance adequacy and
successful operation are outcomes to establish, not consequences of an event log.

## 6. Release a bounded vendor pilot

**Deliver:** installation and update instructions for an ordinary vendor setup,
a supported catalog/accounting scope, access controls, tested backup restoration,
failure recovery and an identified support/maintenance responsibility.
The current container deployment is not a portable vendor installer. The
[native Windows delivery](WINDOWS_NATIVE.md) must pass its dependency, storage,
HTTP/accounting and release checks before it can satisfy this installation gate.

Agree the observation period, initial resources, measures and operating limits
before the trial. Track customer access/prices, worker obligations, stock/waste,
funding balance, cash availability, forecast errors and assurance use.
Define worker-led responses to failure and record adverse as well as favorable
outcomes. Any deployment backend must fit the measured workload and its applicable
license terms; a synthetic AMPL test is not an operational entitlement.

**Complete when:** the supported workflow can be installed, run, interrupted,
restored and explained by its intended operators; price publication and account
records reconcile; the required assurance arrangement is in place; and the
predefined trial measures can be evaluated from retained evidence.

Expand scope only for a concrete vendor need, with matching fixtures and evidence.
The wider economy, world twin, factories and laboratories need not share the
exchange's state or be finished before a bounded exchange pilot.

## Instructions for implementation sessions

1. Name the milestone and the smallest acceptance case being implemented.
   Deliver the capability in the application, then exercise its user workflow
   in the separate vendor example and record what the walkthrough reveals.
2. Follow [STRUCTURE.md](STRUCTURE.md). Create folders when their implementation
   arrives; avoid empty components or a wholesale rearrangement.
3. Extract exchange behavior only where its meaning is the same in simulation
   and operation. Keep one C++ implementation of those rules and preserve
   candidate ordering, tie behavior and accounting identities.
4. Keep the standalone simulation available as a separate research interface.
   An operational command must never silently fall back to synthetic activity.
5. Run the relevant deterministic, record-recovery and interface checks inside
   the managed Debian container. Repackage and check the standalone artifact
   when its source set changes.
6. Report implemented behavior, remaining assumptions, a worked example and
   check results. Update this roadmap's status only after the acceptance gate
   is met, and retain evidence in the verification record.

The next work is to exercise the approved per-environment local launch, then close
the remaining accounting and recovery cases in milestones 1–2. Keep existing-debt
settlement distinct from new expense entry. Add corrections, period boundaries
and restore procedures only with their matching deterministic fixtures before
advancing to shadow price proposals. The current transaction slice does not
complete these gates or calculate recommendations.
