# Projects

Projects include economic demonstrations within Praesidium Humanitatis's
[post-profit economics research program](../docs/post-profit-economics.md)
and the planned tycoon world/environment twin. Economic demonstrations discover,
formalize and evaluate models of production, distribution, exchange and
allocation in concrete settings. Each states its research question and evidence
stage.

Application code and documents in this directory are MIT licensed, as specified
by the repository's root `LICENSE`. Dependencies retain their own terms.

| Project | Economic areas and purpose | Reusable tool | Current state |
| --- | --- | --- | --- |
| [post-profit-exchange](post-profit-exchange/README.md) | **Exchange**, with **distribution** and **allocation**: investigate a self-sufficient store under worker control, without passive-owner profit extraction | MIT [simple exponential smoothing baseline](../tools/exponential-smoothing/README.md); separately licensed price optimizer with AMPL and bounded offline backends | Standalone C++/WebAssembly simulation, plain charts and reconciled synthetic accounts; live operation remains future work |
| [post-profit-production](post-profit-production/README.md) | **Production**, with **allocation**: investigate configured resource transformations, labor, capacity, timelines and risk under worker control | Project-owned MIT C++/WebAssembly simulation and experiment runner; no pricing or forecasting dependency | Independent offline web view and initial operating specification; compensation obligations recorded, settlement and economic evaluation remain future work |
| [post-profit-assurance](post-profit-assurance/README.md) | **Allocation** and continuity: research collectively governed support for productive units facing resource shortfalls | Proposed `exchange.assurance.v1` event boundary | Specification placeholder and unfunded exchange events only; no receiver, funded pool or payouts |
| [post-profit-distribution](post-profit-distribution/) | **Distribution**: procurement, storage, logistics and provision | None selected | Preserved research placeholder |
| [post-profit-allocation](post-profit-allocation/) | **Allocation**: assigning scarce resources, capacity and surplus | None selected | Preserved research placeholder |
| [post-profit-tycoon](post-profit-tycoon/README.md) | **World/environment twin**: represent the surroundings in which the economic components operate | Specific component interfaces remain future work | Concept note only; no twin, renderer or adapters |

Every demonstration must identify its hypothesis, participants, governance,
resources and flows, objectives and constraints, comparison models, and
measures of benefit and harm. It must report what is specified, implemented
and supported by evidence. See the
[formalization requirements](../docs/post-profit-economics.md#discovery-formalization-and-demonstration).

Each application folder describes a complete domain of operation. For
`post-profit-exchange`, this includes stock, procurement, sales, accounts and
worker benefit. Its [store specification](post-profit-exchange/doc/STORE_SPECIFICATION.md)
defines policy, objectives, costs and evidence; the optimization tool returns
price decisions for the application to evaluate and apply.

Applications consume versioned tool interfaces. Store workflows and dashboard
code belong in the store project; reusable solving, validation and model code
belong in the tool. Tools must not depend on application code.

The [tycoon project](post-profit-tycoon/README.md) is a planned world/environment
twin with its own environmental representation, rendering and inspection.
Exchange and production retain their operating rules and accounts and run
independently. Future connections require specific interfaces; no shared
economic operating framework is proposed.

The production project defines a general, configuration-driven architecture;
productive units and their processes are configured instances. Distribution
and allocation still have separate placeholder folders. The portfolio can grow
across all four areas while each project's evidence stage remains explicit.

Using the optimizer through an MIT application does not remove the optimizer's
conditions. See [license boundaries](../docs/licensing.md).
