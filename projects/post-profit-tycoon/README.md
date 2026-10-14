<!-- SPDX-License-Identifier: MIT -->
# post-profit-tycoon

**Stage:** concept note only. No world/environment twin, renderer or component
adapters are implemented.

The tycoon is a planned world/environment twin: a representation of the people,
workplaces, stores, resources, supply routes and surrounding conditions in which
post-profit components operate. It should let us inspect their behavior in a
concrete setting, including resource movement, shortages, work and disruptions.

This is a separate project. Its responsibility is the environment and its
representation, interaction and inspection. Each economic component retains its
own operating rules and accounts.

## Component boundaries

| Component | Responsibility |
| --- | --- |
| Tycoon | Environmental state, spatial representation, interaction and inspection |
| [Exchange](../post-profit-exchange/README.md) | Store pricing, procurement, inventory, sales and accounts |
| [Production](../post-profit-production/README.md) | Resources, processes, labor, capacity and scheduling |
| [Assurance](../post-profit-assurance/README.md) | Its own future funding, authorization and settlement arrangements |

The exchange and production simulators remain independently runnable. Any future
connection to the twin must specify the observations and commands it exchanges,
including units, time, resource transfers and accounting responsibility. This
note proposes no shared economic operating core or universal economic state.

## Existing material

The [exchange contract](../post-profit-exchange/doc/CONTRACT.md) supplies daily
records that could support an initial inspection or playback view. Those records
contain aggregate flows; they do not provide individual routes or transaction
times. The [production contract](../post-profit-production/simulation/CONTRACT.md)
uses its own model and timing. A working environmental representation and
connections to either component remain future work.

Synthetic scenarios must identify their assumptions. Replaying a simulator's
records supplies no evidence that the represented environment matches an
observed operation. Rendering and playback must preserve the source quantities,
units and recorded events.

Project documents and independent application code use the repository's
[MIT license](../../LICENSE). Dependencies retain their own terms; see the
[licensing boundaries](../../docs/licensing.md).
