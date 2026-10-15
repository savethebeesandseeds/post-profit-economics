# Native Windows build

This directory builds the exchange-owned Windows application. It does not build,
patch, download or replace IdyDB. The existing `code/launcher` executable remains
the separate Docker launcher.

From the existing managed Debian development container:

```sh
bash projects/post-profit-exchange/code/windows/setup.sh
bash projects/post-profit-exchange/code/windows/build.sh
```

Setup only installs/verifies pinned dependencies. Build products, compiler caches
and Wine state remain under `/opt/post-profit-exchange/windows`. The build uses
MinGW x86_64 and the same accounting and browser interface as Linux.

Without a supplied Windows IdyDB package, the default target compiles the real
exchange-owned server, CLI, storage adapter and common code into Windows objects.
It also links and runs the independent accounting and platform test executables
under Wine. It deliberately does **not** link a runnable application.

The separately maintained IdyDB project now provides Windows static/shared SDKs.
A consumer of the seven exchange-used C functions successfully linked with our
existing MinGW compiler and its BIG static library. The executable was not run;
the actual exchange application still needs a pinned dependency package and native
acceptance. See [the compatibility review](../../doc/WINDOWS_NATIVE.md#available-idydb-sdk-and-compatibility-review).

After preparing the identified SDK as an exchange dependency package:

```sh
bash projects/post-profit-exchange/code/windows/build.sh \
  --idydb-package /opt/post-profit-exchange/dependencies/idydb-windows
```

The package must contain `exchange-idydb-windows.json` following
[idydb-package.example.json](idydb-package.example.json). The example has
unconfirmed contracts and placeholder hashes; it cannot pass validation.
A valid manifest creates the `post_profit_exchange` and `exchange_environment`
targets. DLLs listed in the manifest are copied beside the executables. System
libraries come from a narrow allowlist. Its pending `gdi32` addition and the
unresolved `checked_windows_flush_and_close` wording are listed in the
[handoff steps](../../doc/WINDOWS_NATIVE.md#idydb-package-handoff); this cleanup
does not change either validation rule. No database substitutes or stubs exist.

`integration_test.ps1` is a prepared **future** native NTFS acceptance test.
It runs only on isolated synthetic accounts, verifies the actual supplied library,
and writes an artifact-bound receipt after success. It has not been executed
against IdyDB Windows yet. Running Windows application tests on the host is not
authorized by the current container-only development policy; use an approved
Windows test environment after the SDK is integrated. Wine component results alone
cannot produce a release receipt.

`package.py` refuses to package without the native application, a hash-verified
IdyDB dependency, and the acceptance receipt. It creates an explicit allowlisted
ZIP in `/opt/post-profit-exchange/windows/packages`, with the existing interface,
licenses, synthetic uninitialized sample inputs and a per-file SHA256 manifest.
It never reads a live environment or key.

See [WINDOWS_NATIVE.md](../../doc/WINDOWS_NATIVE.md) for the runtime and library
contract, limits and remaining acceptance work.

For a future validated package, run inside the managed Debian container after
bringing back the receipt from the approved native acceptance environment:

```sh
python3 projects/post-profit-exchange/code/windows/package.py \
  --build /opt/post-profit-exchange/windows/build \
  --idydb-package /opt/post-profit-exchange/dependencies/idydb-windows \
  --integration-receipt /opt/post-profit-exchange/windows/windows-integration.json
```

The tested DLLs and interface must have exactly the same hashes as the files
packaged. A missing library, untested executable, skipped native security check
or mismatched receipt stops packaging.
