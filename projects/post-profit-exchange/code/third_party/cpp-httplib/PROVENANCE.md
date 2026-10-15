<!-- SPDX-License-Identifier: MIT -->
# cpp-httplib

Upstream: https://github.com/yhirose/cpp-httplib
Release: v0.61.0 (2026-10-14)
Commit: c419c904da4ed0d057f169c225c58cc795c07e4d
License: MIT; see LICENSE.

httplib.h and LICENSE are unmodified upstream files downloaded from that commit.
This dependency is used only by the native exchange HTTP server. It does not
enter the WebAssembly simulator. Builds use this local copy without network
access. TLS, compression, proxy support and static directory mounts are not
enabled by the exchange application.

SHA-256:
- httplib.h: bc77114769db3b3c97e4a6d753dab797c3cadaaa48beead3806298b09702df18
- LICENSE: 4b45cbe16d7b71b89ae6127e26e0d90a029198ca5e958ad8e3d0b8bbed364d8b
