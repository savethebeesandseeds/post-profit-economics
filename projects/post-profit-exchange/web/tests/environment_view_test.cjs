// SPDX-License-Identifier: MIT
// DOM behavior check in Node; no browser screenshot or database execution.
// Run: node environment_view_test.cjs [template.html] [rendered-environment.html]
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

const template = fs.readFileSync(process.argv[2] || path.join(__dirname, "../operations/environment.template.html"), "utf8");
assert.equal(template.split("__ENVIRONMENT_JSON__").length, 2, "exactly one snapshot insertion marker");
assert(!/<(?:script|link|img|iframe)[^>]+(?:src|href)\s*=/i.test(template), "no external asset dependencies");
const app = template.match(/<script>([\s\S]*?)<\/script>/)?.[1];
assert(app, "one inline application script");
assert(!/innerHTML|outerHTML|document\.write|fetch\s*\(|XMLHttpRequest|WebSocket|sendBeacon/.test(app), "data stays text and no network requests");
new vm.Script(app);
const ids = [...template.matchAll(/\bid="([^"]+)"/g)].map(match => match[1]);
assert.equal(new Set(ids).size, ids.length, "unique IDs");
for (const id of ["environment-data", "display-name", "cash", "products", "lots", "obligations", "configuration", "raw-record"]) {
  assert(ids.includes(id), "required field " + id);
}

const dangerousLabel = '</script><img src=x onerror="globalThis.injected=true"> & <b>bread</b>';
const fixture = {
  schema_version: "exchange.environment-view.v1",
  environment: {
    schema_version: "exchange.environment.v1", environment_id: "example-one", currency: "EUR", minor_unit_digits: 2,
    storage: {database: "data/exchange.idydb"}, metadata_file: "metadata.json", opening_file: "records/opening.json"
  },
  metadata: {
    schema_version: "exchange.environment-metadata.v1", display_name: "Example exchange", description: "Test records only.",
    provenance: "synthetic", products: [{sku: "BREAD", label: dangerousLabel, unit: "piece"}, {sku: "OIL", label: "Oil", unit: "piece"}]
  },
  state: {
    schema_version: "exchange.opening.v1", environment_id: "example-one", as_of: "2026-10-14",
    cash: 100000, reserve: 5000, earned_funding: -125,
    obligations: [{id: "pay-01", kind: "wages", amount: 800}, {id: "rent-01", kind: "operating", amount: 200}],
    products: [
      {sku: "BREAD", published_price: 250, replacement_cost: 130, lots: [{id: "bread-1", units: 50, unit_cost: 100}, {id: "bread-2", units: 25, unit_cost: 110}]},
      {sku: "OIL", published_price: 400, replacement_cost: 215, lots: [{id: "oil-1", units: 10, unit_cost: 200}]}
    ]
  },
  summary: {inventory_value: 9750, inventory_units: 85, obligations: 1000, cash_unreserved: 95000, cash_after_obligations_and_reserve: 94000, net_assets: 108750},
  capabilities: {opening_state: true, transactions: false, price_recommendations: false, price_publication: false},
  database: {backend: "idydb", status: "loaded"},
  snapshot_at: "2026-10-14T12:00:00Z"
};
const escapeForScript = value => JSON.stringify(value).replace(/</g, "\\u003c").replace(/>/g, "\\u003e").replace(/&/g, "\\u0026");
const rendered = template.replace("__ENVIRONMENT_JSON__", escapeForScript(fixture));
assert.equal([...rendered.matchAll(/<script(?:\s[^>]*)?>/g)].length, 2, "record data cannot create a script element");
assert(!rendered.includes("<img src=x"), "record data cannot create HTML markup");
assert(rendered.includes("\\u003c/script\\u003e"), "closing script delimiter escaped");
assert(rendered.includes("\\u0026"), "ampersand escaped");
assert.deepEqual(JSON.parse(rendered.match(/<script id="environment-data" type="application\/json">([\s\S]*?)<\/script>/)[1]), fixture);

function runPage(page) {
  class Element {
    constructor(tagName = "div") {
      this.tagName = tagName; this.children = []; this.events = {};
      this.textContent = ""; this.hidden = false; this.disabled = false;
    }
    appendChild(child) { child.parent = this; this.children.push(child); return child; }
    addEventListener(name, callback) { this.events[name] = callback; }
    click() { if (!this.disabled) this.events.click?.(); if (this.tagName === "a") downloads.push(this); }
    remove() { if (this.parent) this.parent.children = this.parent.children.filter(item => item !== this); }
    set innerHTML(_) { throw new Error("Untrusted HTML insertion"); }
    set outerHTML(_) { throw new Error("Untrusted HTML insertion"); }
  }
  const elements = new Map(ids.map(id => [id, new Element()]));
  elements.get("environment-data").textContent = page.match(/<script id="environment-data" type="application\/json">([\s\S]*?)<\/script>/)[1];
  elements.get("content").hidden = true;
  elements.get("error").hidden = true;
  elements.get("print").disabled = true;
  elements.get("download").disabled = true;
  const downloads = [], blobs = [], revoked = [];
  let printed = 0;
  const document = {title: "", body: new Element("body"), getElementById: id => {
    assert(elements.has(id), "element exists: " + id); return elements.get(id);
  }, createElement: tag => new Element(tag)};
  const context = {document, window: {print: () => printed++}, Blob,
    URL: {createObjectURL: blob => {blobs.push(blob); return "blob:local-" + blobs.length;}, revokeObjectURL: url => revoked.push(url)},
    setTimeout: callback => callback()};
  vm.runInNewContext(app, context, {timeout: 1000});
  return {elements, downloads, blobs, revoked, document, context,
    printed: () => printed, text: id => elements.get(id).textContent,
    rows: id => elements.get(id).children.map(row => row.children.map(cell => cell.textContent))};
}
const run = runPage(rendered);
assert.equal(run.elements.get("content").hidden, false);
assert.equal(run.elements.get("error").hidden, true);
assert.equal(run.context.injected, undefined, "malicious product text never executes");
assert.equal(run.text("display-name"), "Example exchange");
assert.equal(run.document.title, "Example exchange · Exchange environment snapshot");
assert.equal(run.text("description"), "Test records only.");
assert.equal(run.text("provenance"), "Synthetic example records");
assert.equal(run.text("environment-id"), "Environment: example-one");
assert(run.text("as-of").includes("2026-10-14"));
assert(run.text("snapshot-at").includes(fixture.snapshot_at));
for (const [id, expected] of [
  ["cash", "1,000.00 EUR"], ["reserve", "50.00 EUR"], ["earned-funding", "−1.25 EUR"],
  ["inventory-value", "97.50 EUR"], ["obligations-total", "10.00 EUR"], ["cash-unreserved", "950.00 EUR"],
  ["cash-after-obligations", "940.00 EUR"], ["net-assets", "1,087.50 EUR"]
]) assert.equal(run.text(id), expected, id);
assert.equal(run.text("inventory-units"), "85 units in saved stock.");
assert.deepEqual(run.rows("products"), [
  [dangerousLabel, "BREAD", "piece", "2.50 EUR", "1.30 EUR"],
  ["Oil", "OIL", "piece", "4.00 EUR", "2.15 EUR"]
]);
assert.deepEqual(run.rows("lots"), [
  ["BREAD", "bread-1", "50", "1.00 EUR"], ["BREAD", "bread-2", "25", "1.10 EUR"], ["OIL", "oil-1", "10", "2.00 EUR"]
]);
assert.deepEqual(run.rows("obligations"), [["pay-01", "Worker pay", "8.00 EUR"], ["rent-01", "Operating expense", "2.00 EUR"]]);
assert.deepEqual(run.rows("capabilities").map(row => row[1]), ["Available in application", "Not implemented", "Not implemented", "Not implemented"]);
assert.deepEqual(JSON.parse(run.text("configuration")), fixture.environment);
assert.deepEqual(JSON.parse(run.text("metadata")), fixture.metadata);
assert.deepEqual(JSON.parse(run.text("raw-record")), fixture);
run.elements.get("print").click();
assert.equal(run.printed(), 1);
run.elements.get("download").click();
assert.equal(run.downloads.length, 1);
assert.equal(run.downloads[0].download, "exchange-environment-snapshot.json");
assert.deepEqual(run.revoked, ["blob:local-1"]);

(async () => {
  assert.deepEqual(JSON.parse(await run.blobs[0].text()), fixture, "download preserves snapshot records");
  const observed = structuredClone(fixture);
  observed.metadata.provenance = "observed";
  observed.summary.cash_after_obligations_and_reserve = -9000000000000;
  // Intentionally distinct provided totals: the interface must not recompute financial accounts.
  observed.summary.inventory_value = 12345;
  const observedRun = runPage(template.replace("__ENVIRONMENT_JSON__", escapeForScript(observed)));
  assert.equal(observedRun.text("provenance"), "Observed records");
  assert.equal(observedRun.text("cash-after-obligations"), "−90,000,000,000.00 EUR");
  assert.equal(observedRun.text("inventory-value"), "123.45 EUR", "uses native summary, not JavaScript accounting");
  const empty = structuredClone(fixture);
  empty.state.products = []; empty.metadata.products = []; empty.state.obligations = [];
  const emptyRun = runPage(template.replace("__ENVIRONMENT_JSON__", escapeForScript(empty)));
  assert.deepEqual(emptyRun.rows("products"), [["No products recorded."]]);
  assert.deepEqual(emptyRun.rows("lots"), [["No acquisition lots recorded."]]);
  assert.deepEqual(emptyRun.rows("obligations"), [["No outstanding obligations recorded."]]);
  for (const mutate of [
    report => {report.schema_version = "unsupported";},
    report => {report.state.environment_id = "different-environment";},
    report => {report.state.cash = 1.5;},
    report => {report.metadata.products = [];},
    report => {report.database.status = "missing";}
  ]) {
    const invalid = structuredClone(fixture); mutate(invalid);
    const failed = runPage(template.replace("__ENVIRONMENT_JSON__", escapeForScript(invalid)));
    assert.equal(failed.elements.get("content").hidden, true, "invalid report is not displayed as usable");
    assert.equal(failed.elements.get("error").hidden, false);
    assert(failed.text("error").startsWith("Cannot display this snapshot: "));
    assert.equal(failed.elements.get("download").disabled, true);
  }
  if (process.argv[3]) {
    const actual = fs.readFileSync(process.argv[3], "utf8");
    assert(!actual.includes("__ENVIRONMENT_JSON__"), "native renderer replaces the marker");
    assert(!/<(?:script|link|img|iframe)[^>]+(?:src|href)\s*=/i.test(actual), "native snapshot has no external assets");
    const actualRun = runPage(actual);
    assert.equal(actualRun.elements.get("content").hidden, false, actualRun.text("error"));
    assert.equal(actualRun.elements.get("error").hidden, true);
    const actualRecord = JSON.parse(actualRun.text("raw-record"));
    assert.equal(actualRecord.schema_version, "exchange.environment-view.v1");
    assert(actualRun.text("display-name").length > 0);
  }
  console.log("Environment inspector: record display, exact money formatting, native totals, literal text, download, empty records and invalid snapshots pass. Visual browser testing is separate.");
})().catch(error => {console.error(error); process.exitCode = 1;});
