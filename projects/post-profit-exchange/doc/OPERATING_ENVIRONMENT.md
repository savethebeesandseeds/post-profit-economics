<!-- SPDX-License-Identifier: MIT -->
# Operating environment: records and transactions

An **environment** is one exchange operation's configuration, metadata and
persisted records. The exchange application opens this directory; the directory
contains no accounting, pricing or interface implementation. This differs from
the [Debian development environment](ENVIRONMENT.md), which builds and runs the
software.

The current checkpoint saves an opening position in IdyDB and records paid
deliveries, cash sales and new paid or unpaid expenses. Native C++ updates the
accounts; the shared interface displays the saved result. Price recommendations,
publication and assurance are not connected to this operational path. The
research simulator and its pricing policy remain unchanged.

## Files belonging to one operation

The checked-in [vendor operation](../environments/vendor_0x01/exchange.cfg)
is an explicitly synthetic example:

```text
environments/vendor_0x01/
  exchange.cfg             central configuration: accounts, runtime and key path
  metadata.json            displayed names, catalog and provenance
  records/opening.json     opening position imported once
  data/exchange.idydb       encrypted opening position and ordered transactions
  data/migration-original.idydb optional encrypted migration recovery
  reports/environment.html optional plaintext read-only export
```

Only the three example input files are distributed. Generated databases, lock
and pending files, reports and other local environments are ignored by Git and
excluded from packaged source. This prevents accidental inclusion in the normal
source workflow; it is not access control. Keep actual operating data private.

Another operation supplies its own directory and declared inputs, without changes
to C++ or HTML. Removing the example leaves the application buildable and usable.
Do not copy a populated example database and relabel it as observed data.

## Open the accounts

Use the [application launcher](APPLICATION_SERVICE.md) with the existing
`exchange.cfg` to open the connected interface. It starts the configured
container and opens the browser after readiness. The operation's data belongs
under `environments/`, separate from the application's `code/` and `web/`.

Native commands remain available inside the managed Linux container. Mount the
external secret read-only at `/run/secrets/exchange.key`, as the application
launcher does. For an already initialized encrypted environment:

```sh
environment_dir=/operation
/opt/post-profit-exchange/native/exchange_environment inspect "$environment_dir" --key-file /run/secrets/exchange.key
```

`initialize ENVIRONMENT_DIRECTORY --key-file KEY_PATH` imports an opening once
and refuses an existing database. It is an explicit preparation step, never a
side effect of launch. `transact` accepts the same key-file option and reads its
JSON request from stdin. Commands return JSON on stdout and diagnostics on
stderr; success exits with 0 and errors return JSON with exit 2.

For an encrypted operation, a report is an explicit plaintext export:

```sh
/opt/post-profit-exchange/native/exchange_environment render /operation projects/post-profit-exchange/web/operations/environment.template.html --key-file /run/secrets/exchange.key --allow-plaintext-export
```

Keep exported records in an appropriately private location. Ordinary service
startup does not generate a report. Use `--help` for the command syntax.

The generated `reports/environment.html` is a **read-only snapshot**. It shows
the accounts and saved prices at its generation time. Rerun `render` to refresh
it; opening or refreshing the file neither opens IdyDB nor records activity.
The main report shows balances, stock, saved selling prices and unpaid costs in
ordinary store language. Reserve remains identified as part of cash; example
data and the saved-report date remain visible. Additional balances and formulas,
purchase batches, and technical configuration/records are available in closed
details. The shared template owns this presentation; operation metadata and
accounting records are not rewritten to change the wording.

The **connected operator interface** is the application's shared
`web/operations/transactions.html`, served by the C++ `exchange_server`
executable in `code/interfaces/server_main.cpp`. Its cpp-httplib transport calls
the same environment inspection and transaction functions as the native command
line, with no Python runtime or subprocess bridge. It contains no accounting
model and serves only its declared application routes. Its setup is
documented in [APPLICATION_SERVICE.md](APPLICATION_SERVICE.md).
The approved launch model uses one container per configured operation, named
`post-profit-exchange-<environment_id>`. Shared repository source is mounted
read-only; only the selected environment is writable at `/operation`, with other
exchange environments masked. Each active operation uses its own loopback port
(default 8766). The general `post-profit-economics-gpu` container is retained.
The launcher verifies container identity and readiness; this contract alone does
not establish that a container is running. Opening the HTML as a local file is
not a substitute for the connected service.

Dependency/environment preparation belongs in `code/setup.sh`, run inside the
Linux container. Container lifecycle, build/start and record changes remain in
their respective launchers and application commands. Follow
[APPLICATION_SERVICE.md](APPLICATION_SERVICE.md) for the exact setup and launch.

## Daily workspace

The connected application opens on **Home**. It shows the latest saved position,
the operation's name and record provenance, useful attention messages and recent
activity. These are recorded balances and events, not a generated daily forecast.
The three main actions open a focused form: **record a sale**, **receive a
delivery**, or **add an expense**.

Use the persistent navigation to move between:

| View | Purpose |
| --- | --- |
| Home | Current position, common actions and a short recent-activity view. |
| Record | One sale, delivery or new-expense form at a time, with the relevant product and amount information. |
| Stock | Products, remaining quantities and public prices, with acquisition lots available for inspection. |
| Activity | Saved records described in ordinary language, with original inputs and accounting effects available in their details. |
| Advanced | Complete balances, obligations, read-only operation settings and technical records. |

Labels use direct names such as **Current balances**, **Record activity** and
**Notices**. Fields, account figures and settings that need explanation use the
same **SVG information button**, aligned with its label. Hovering over it or
focusing it with the keyboard opens a floating caption without moving the page
layout. Clicking or tapping keeps the caption open; activating it again,
pressing Escape or clicking elsewhere closes it. The pointer can move from the
icon onto the caption to read, scroll or select its text. Help never submits a
form or changes a draft.

This is the standard for new operator fields: use a short label, a separately
labelled non-submit help button, and an associated caption explaining the
meaning, units or accounting effect. Keep formulas and longer definitions in
captions. Keep errors, uncertain-save warnings, duplicate-expense warnings and
read-only status visible. The implementation uses accessible names,
`aria-expanded`, `aria-controls` and `aria-describedby`; help is not hover-only.
One shared floating caption sits outside scrolling tables and stays within the
visible window. Changing views, refreshing records, or scrolling/resizing the
page closes it so it cannot remain attached to an outdated field.

Draft entries remain in place when navigating between these views. An ordinary
unsent draft is not a saved business record. The durable browser outbox described
below applies to requests that have been submitted; an uncertain request remains
visible and blocks new submissions from every view until resolved.

Advanced settings show the business configuration actually loaded by the operation.
Host paths, the encryption key location and runtime settings are not exposed by
the accounts API.
They are explicitly **read-only**: the current application has no general
configuration migration or settings-write command. Moving settings out of the daily workflow
does not add editing support or change the accounting policy. Raw JSON remains
available for inspection under details rather than filling the landing page.

## Record activity through the shared interface

From Home, choose the activity, or open Record and select its type. Review the date and enter the actual amounts:

| Activity | Inputs | Account effect |
| --- | --- | --- |
| Delivery received and paid | SKU, unique lot identifier, whole units, actual acquisition cost per unit. | Adds a FIFO lot and pays its acquisition cost from cash. No immediate expense or earned funding change. Paid receipts protect reserve and existing obligations; free receipts remain possible during a shortfall. |
| Cash sale | SKU, whole units and actual sale price per unit. | Adds receipts to cash, removes units in FIFO order, and adds revenue minus FIFO cost to earned funding. |
| New expense, paid | Worker pay or operating category, description and amount. | Reduces cash and earned funding once; protects reserve. Existing obligations remain outstanding. |
| New expense, unpaid | The same fields, marked unpaid. | Adds an obligation identified by the transaction ID and reduces earned funding once; cash is unchanged. |

A sale defaults to the saved public price, but the operator records the price
actually charged. Neither sale entry nor a delivery changes public prices or
replacement quotes. Monetary controls accept decimal currency amounts and send
exact integer minor units. Dates cannot precede the current accounts date.
No activity is generated to fill missing records.

Save the record once and wait for confirmation. Accounts, stock
lots, obligations and transaction history refresh from the native response.
If the connection fails after submission, the transaction may already be saved:
use **Retry the same transaction**. Before submission, the interface retains
the exact request and identifier in a browser outbox scoped to this origin and
environment. Reopening restores pending requests and locks new submission until
their result is confirmed. The outbox contains recovery requests, not accounting
authority; do not clear browser storage while requests remain unresolved. A
service restart renews its session token without replacing the transaction ID.
A definitive initial validation rejection retains the form for review. After
uncertainty, a native conflict response explicitly confirming not_recorded
can also release the request; other ambiguous failures retain it. Refreshes do
not themselves record activity.

New expenses are **not payments of previously recorded debts**. Existing-debt
settlement, refunds/corrections, spoilage, credit purchases/sales, tax treatment,
automatic period accrual, price proposals and publication remain unsupported.
These forms record activity; they do not send money through a payment provider.
The local session token is request protection, not worker authentication.

## Worked example

All amounts below are integer cents in EUR. These are labeled synthetic fixtures,
not recommended capitalization, salaries or evidence of trading performance.

| Opening quantity | Amount |
| --- | ---: |
| Cash | 100,000 |
| Reserve within cash | 5,000 |
| Stock | 45 bread units at 100 cents and 35 bean units at 150 cents |
| Inventory acquisition value | 9,750 |
| Outstanding obligations | 1,000: wages 800, operating 200 |
| Earned funding history | 0 |
| Net assets | 108,750 |

Saved public prices are 200 cents for bread and 300 for beans. Opening resources
are not earnings. The reserve is part of cash, not an additional asset.

An isolated copy of these inputs supports this transaction walkthrough:

1. Receive 10 bread units at 120 cents each, paid now: cash decreases by 1,200;
   inventory value increases by 1,200.
2. Sell 50 bread units at 200 cents: receipts are 10,000; FIFO cost is
   `45 × 100 + 5 × 120 = 5,100`; earned funding increases by 4,900.
3. Record a new operating expense of 700, paid now.
4. Record new worker pay of 300, unpaid.

| Result after all four records | Amount |
| --- | ---: |
| Cash | 108,100 |
| Reserve within cash | 5,000 |
| Remaining stock | 5 bread units at 120 cents and 35 bean units at 150 cents |
| Inventory acquisition value | 5,850 |
| Outstanding obligations | 1,300 |
| Earned funding | 3,900 |
| Cash after obligations and reserve | 101,800 |
| Net assets | 112,650 |

Reopen the operation and compare these accounts and all four records. Retry an
accepted transaction with its original identity: accounts must remain unchanged.
Tests use isolated environments; they do not add these records to the existing
vendor example database. During an interactive walkthrough, record confusing
labels or missing steps as improvements to the shared application.

## Contracts and persistence

- `exchange.cfg` is strict JSON, schema `exchange.environment.v2`: environment
  identity, uppercase three-letter currency, two decimal places, relative
  database/metadata/opening paths, and `runtime` settings for the workspace,
  loopback bind/port and external key-file path. See
  [APPLICATION_SERVICE.md](APPLICATION_SERVICE.md) for its exact runtime fields.
  Legacy v1 configurations remain supported for explicit plaintext native
  fixtures. They are not the encrypted application launcher contract.
- The persisted and public business configuration remains
  `exchange.environment.v1`. The loader removes v2 `runtime` settings and uses
  that business projection for matching/replay. A port or key-path edit does
  not alter accounting; changing key contents still requires a supported
  re-encryption procedure, which is not implemented.
- `metadata.json` uses `exchange.environment-metadata.v1`: display metadata and
  the matching SKU catalog. Provenance is explicitly `synthetic` or `observed`;
  this is a declaration, not independent verification.
- `exchange.opening.v1` supplies date, cash, reserve, signed earned funding,
  obligations, saved public prices, replacement costs and FIFO acquisition lots.
- `exchange_environment transact ENVIRONMENT_DIRECTORY < request.json` accepts
  `{expected_revision, event}`. An event contains exactly `schema_version:
  "exchange.transaction.v1"`, `id`, `environment_id`, `date`, `kind` and
  `details`. The kind and detail fields are listed below.
- Inspection returns `exchange.environment-view.v1`: configuration, metadata,
  current state in the opening-shaped schema, native summary, revision, ordered
  `events` containing each original `event` and derived `effect`, timestamp
  and capability flags. Transaction success adds `commit_status` equal to
  `recorded` or `already_recorded`.

| Kind | Exact detail fields |
| --- | --- |
| `delivery` | `sku`, `lot_id`, `units`, `unit_cost` |
| `sale` | `sku`, `units`, `unit_price` |
| `expense` | `category` (`wages` or `operating`), `description`, `amount`, `settlement` (`paid` or `unpaid`) |

Amounts in the native contract are integer minor units, not decimal strings.
Event and lot identities use 1–64 letters, digits, dots, underscores or hyphens.
A reused event ID with identical content returns the latest accounts without
applying it again; conflicting content is rejected. A new event requires the
current revision. Lot identities cannot be reused after their stock is sold.

IdyDB holds a compact JSON envelope in a text cell, preserving exact bounded
integers. Existing `exchange.environment-store.v1` opening-only envelopes remain
readable without modification. The first accepted transaction stores v2 with the
original opening state and ordered raw events. Inspection replays those events
through the C++ operations; derived totals are not independent stored authority.

The exchange adapter serializes writers with a sibling lock, checks the expected
stored envelope, writes and verifies a unique pending database, synchronizes it,
then atomically replaces the current file and synchronizes the directory. Reads
see a complete old or new envelope. A failed pre-replacement write retains its
pending file for inspection. A failure after replacement can have an uncertain
result: reopen and check the original event ID; never blindly submit a new ID.
Do not delete lock or pending files as an automatic cleanup step. Backups and
operator-guided recovery remain roadmap work; the narrowly scoped migration
below is not a complete backup system.

The encrypted adapter uses IdyDB's built-in authenticated encryption. Required
encryption refuses a plaintext store or absent/wrong key; opening a database is
not permission to convert it. An explicit plaintext legacy fixture remains a
separate contract. No key bytes enter the stored business configuration.

Business configuration and metadata must match their initialized parsed values.
Currency, catalog, labels and provenance changes are rejected; formatting-only
changes are allowed. Runtime settings are excluded from this comparison. There
is no reset or general migration command. Database paths remain
relative beneath `data/`, end in `.idydb` and cannot traverse symlinks or parent
directories. Inputs stay outside `data/` and `reports/`.

## Explicit environment migration

The command below supports the approved rename/encryption migration from a
plaintext v1 environment into a prepared v2 encrypted environment:

```sh
exchange_environment migrate SOURCE_DIRECTORY DESTINATION_DIRECTORY --key-file KEY_PATH
```

Stop writers first and resolve any uncertain browser request with its original
ID before migrating. The destination must have its own configuration, metadata
and opening JSON with the new environment identity. The command validates the
source and destination, writes `data/migration-original.idydb` as an encrypted
copy of the original persisted envelope, creates the encrypted destination
database and verifies both. It changes environment identity where required,
preserving monetary amounts, event identities, ordered records and accounting
effects. It does not initialize new example trading or delete the source.

The original envelope is a recovery artifact, not a complete filesystem backup.
Protect the external key and retain the recovery database separately as
appropriate. Source-folder cleanup, handling earlier plaintext reports and
replacement of the old container are separate explicit steps after verification;
normal startup performs none of them. Encryption does not securely erase old
storage blocks or encrypt JSON imports, reports and browser outbox data.
Migration does not clear the browser's old environment-scoped pending requests.

## Bounds and evidence

This checkpoint runs in Linux with 1–12 products in whole `piece` units, at most
100 FIFO lots per product, 100 outstanding obligations and **100 transactions**.
Quantities are 1–1,000,000 units. Monetary inputs and supported totals are bounded
to 9,000,000,000,000 minor units. Acquisition cost may be zero; sale prices and
expense amounts must be positive. Reserve cannot exceed cash.

Each input and the compact stored envelope is bounded to **61,440 bytes**;
transaction requests are bounded to 16 KiB. Reaching a bound rejects the new
record and preserves the existing operation. These are implementation bounds,
not production capacity claims.

The [verification record](../../../docs/exchange-simulation-verification.md)
records executed checks, including the separate C++ server migration results.
The earlier transaction checkpoint passed **16/16** native tests, including
HTTP/native transactions, storage concurrency and pure accounting. Its connected
UI deterministic Node checks also passed. Those earlier results do not by
themselves verify the replacement HTTP server.
These fixtures exercise account transitions, reopen/replay, duplicate/conflicting IDs, stale
revisions and uncertain acknowledgements. This does not establish actual vendor
outcomes, complete crash recovery, backup restoration or worker authorization.
Milestones 1 and 2 remain partial; unsupported accounting cases and recovery
procedures still need implementation before the broader vendor pilot.
