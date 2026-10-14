# Reusable tools

Tools provide technical capabilities that [research projects](../projects/README.md)
can use through explicit interfaces. Each tool owns its implementation, contract,
validation and dependency instructions. Economic operating policies and accounts
stay in the projects that use them.

| Tool | Purpose | License | Current stage |
| --- | --- | --- | --- |
| [IdyDB](idydb/README.md) | C/C++ cell/vector database and optional encrypted backing files | [MIT](idydb/upstream/LICENSE) | Experimental; shared corrections and regression tests included; operational validation remains incomplete |
| [Exponential smoothing](exponential-smoothing/README.md) | Dependency-free C++17 EWMA, uncertainty and past-only diagnostics | [MIT](exponential-smoothing/LICENSE) | Exchange baseline; empirical accuracy remains unvalidated |
| [Temporal Fusion Transformer](temporal-fusion-transformer/README.md) | C++17 / LibTorch / CUDA forecasting and model diagnostics | [MIT](temporal-fusion-transformer/LICENSE) | Implemented; real-data replication remains a gate |
| [Price optimization](price-optimization/README.md) | Public-price constraints, bounded enumeration and AMPL optimization | [Worker Protection License 1.0](price-optimization/LICENSE) | Synthetic prototype; operational AMPL rights and store integration required |

The root [CMake entry](../CMakeLists.txt) can select the implemented libraries
and project simulators. Component guides describe their independent builds.
Develop, build and run them in the documented [managed environment](../docs/environment.md).

IdyDB is disabled by default (`PH_BUILD_IDYDB=OFF`). The exchange operating
environment uses a project-owned JSON-envelope storage adapter. Its
[review](idydb/REVIEW.md) records adoption gates; its
[maintenance note](idydb/README.md#source-and-maintenance) asks that database
fixes and additions also update the original standalone project at `C:\Work\idydb`.

Each tool's license applies independently. An MIT client does not remove the
price optimizer's conditions or vendor dependency terms. See the
[licensing boundaries](../docs/licensing.md) and
[third-party notices](../docs/third-party-notices.md).
