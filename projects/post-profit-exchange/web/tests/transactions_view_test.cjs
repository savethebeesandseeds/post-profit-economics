// SPDX-License-Identifier: MIT
// Deterministic DOM/API contract checks. No browser, database or accounting emulation.
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");
const html = fs.readFileSync(process.argv[2] || path.join(__dirname, "../operations/transactions.html"), "utf8");
const app = html.match(/<script>([\s\S]*?)<\/script>/)?.[1];
assert(app, "inline operation script");
assert(!/<(?:script|link|img|iframe)[^>]+(?:src|href)\s*=/i.test(html), "no external assets");
assert(!/innerHTML|outerHTML|document\.write|eval\s*\(/.test(app), "record values never become HTML or code");
new vm.Script(app);
const ids = [...html.matchAll(/\bid="([^"]+)"/g)].map(match => match[1]);
assert.equal(ids.length, new Set(ids).size, "unique element IDs");
// Read the actual static markup so accessibility contracts are checked against the
// page, rather than supplied as convenient defaults by the DOM test double.
const staticTags = [...html.slice(0, html.indexOf("<script>")).matchAll(/<([a-z][a-z0-9-]*)(\s[^<>]*?)?>/gi)].map(match => ({
  tag: match[1].toLowerCase(),
  attributes: Object.fromEntries([...String(match[2] || "").matchAll(/([\w-]+)(?:="([^"]*)")?/g)]
    .map(attribute => [attribute[1], attribute[2] ?? ""]))
}));
const helpTags = staticTags.filter(tag => Object.hasOwn(tag.attributes, "data-help"));
assert(helpTags.length, "contextual field help is present");
for (const tag of helpTags) {
  const attrs = tag.attributes, target = staticTags.find(candidate => candidate.attributes.id === attrs["data-help"]);
  assert.equal(tag.tag, "button", "help is keyboard-operable through a native button");
  assert.equal(attrs.type, "button", "help must never submit its surrounding form");
  assert(/^About \S/.test(attrs["aria-label"] || ""), "help button has a meaningful accessible name");
  assert.equal(attrs["aria-controls"], "help-tooltip", "button controls the shared floating description");
  assert.equal(attrs["aria-describedby"], attrs["data-help"], "button keeps its own accessible description");
  assert.equal(attrs["aria-expanded"], "false", "help begins collapsed");
  assert(target, "help caption exists: " + attrs["data-help"]);
  assert(target.attributes.class?.split(/\s+/).includes("context-help"), "help uses the shared caption style");
  assert(Object.hasOwn(target.attributes, "hidden"), "caption is initially hidden in the delivered HTML");
}
const tooltipTag = staticTags.find(tag => tag.attributes.id === "help-tooltip");
assert(tooltipTag, "one shared floating help element exists");
assert.equal(tooltipTag.attributes.role, "tooltip");
assert(Object.hasOwn(tooltipTag.attributes, "hidden"), "floating help begins hidden");
for (const match of html.matchAll(/<button\b([^>]*\bdata-help="[^"]+"[^>]*)>([\s\S]*?)<\/button>/g)) {
  assert(/<svg\b[^>]*aria-hidden="true"/.test(match[2]), "help uses an accessible decorative SVG");
  assert(!match[2].includes("ⓘ"), "icon does not depend on font-specific information glyph alignment");
}
for (const tag of staticTags.filter(tag => tag.attributes["aria-describedby"])) {
  for (const reference of tag.attributes["aria-describedby"].split(/\s+/)) {
    assert(ids.includes(reference), "accessible description points to an existing element: " + reference);
  }
}
const editableFields = ["sale-sku", "sale-units", "unit-price", "delivery-sku", "delivery-units", "unit-cost",
  "lot-id", "expense-category", "settlement", "expense-description", "expense-amount", "date"];
for (const field of editableFields) {
  const tag = staticTags.find(candidate => candidate.attributes.id === field);
  assert(tag?.attributes["aria-describedby"]?.split(/\s+/).includes("help-" + field),
    "editable field has an accessible contextual description: " + field);
  assert(helpTags.some(candidate => candidate.attributes["data-help"] === "help-" + field),
    "editable field has a help trigger: " + field);
}

const dangerous = '</script><img src=x onerror="globalThis.injected=true"> & <b>bread</b>';
function fixture(revision = 0) {
  return {
    schema_version: "exchange.environment-view.v1",
    environment: {schema_version: "exchange.environment.v1", environment_id: "one-operation", currency: "EUR", minor_unit_digits: 2, storage: {database: "data/exchange.idydb"}},
    metadata: {schema_version: "exchange.environment-metadata.v1", display_name: "An operation", description: "Test fixtures.", provenance: "observed",
      products: [{sku: "BREAD", label: dangerous, unit: "piece"}, {sku: "OIL", label: "Oil", unit: "piece"}]},
    state: {schema_version: "exchange.opening.v1", environment_id: "one-operation", as_of: "2026-10-14", cash: 100000, reserve: 5000, earned_funding: -125,
      obligations: [{id: "wages-1", kind: "wages", amount: 800}],
      products: [{sku: "BREAD", published_price: 250, replacement_cost: 130, lots: [{id: "bread-1", units: 50, unit_cost: 100}]},
        {sku: "OIL", published_price: 400, replacement_cost: 215, lots: [{id: "oil-1", units: 10, unit_cost: 200}]}]},
    // Distinct authoritative values test display ownership, not financial calculations.
    summary: {inventory_value: 12345, inventory_units: 60, obligations: 800, cash_unreserved: 95000, cash_after_obligations_and_reserve: 94200, net_assets: 106200},
    capabilities: {opening_state: true, transactions: true, price_recommendations: false, price_publication: false},
    database: {backend: "idydb", status: "loaded"}, snapshot_at: "2026-10-14T12:00:00Z", revision, events: []
  };
}
const response = (body, status = 200) => ({ok: status >= 200 && status < 300, status, json: async () => body});
const tick = async () => { for (let i = 0; i < 12; i++) await new Promise(resolve => setImmediate(resolve)); };
function memoryStorage() {
  const values = new Map();
  return {get length() {return values.size;}, key: index => [...values.keys()][index] ?? null,
    getItem: key => values.get(key) ?? null, setItem: (key, value) => values.set(key, String(value)),
    removeItem: key => values.delete(key)};
}
function page(initial = fixture(), storage = memoryStorage(), sessionToken = "local-test-token", hash = "", viewport = null) {
  class Element {
    constructor(tag = "div") { this.tagName = tag; this.children = []; this.events = {}; this.attributes = {}; this.value = ""; this.disabled = false; this.hidden = false; this._text = ""; this.style = {}; this.rect = {left: 120, top: 100, right: 144, bottom: 124, width: 24, height: 24}; }
    get id() { return this.getAttribute("id") || ""; }
    set id(value) { this.setAttribute("id", value); }
    get type() { return this.getAttribute("type") || ""; }
    set type(value) { this.setAttribute("type", value); }
    get className() { return this.getAttribute("class") || ""; }
    set className(value) { this.setAttribute("class", value); }
    setAttribute(name, value) { this.attributes[name] = String(value); }
    getAttribute(name) { return this.attributes[name] ?? null; }
    removeAttribute(name) { delete this.attributes[name]; }
    focus() {
      if (document.activeElement === this) return;
      const previous = document.activeElement; document.activeElement = this;
      previous?.dispatch("blur", {relatedTarget: this});
      this.dispatch("focus", {relatedTarget: previous});
    }
    blur() { if (document.activeElement === this) {document.activeElement = null; this.dispatch("blur");} }
    contains(target) { return target === this || this.children.some(child => child.contains(target)); }
    getBoundingClientRect() { return {...this.rect}; }
    get offsetWidth() { return this.rect.width; }
    get offsetHeight() { return this.rect.height; }
    get textContent() { return this._text + this.children.map(child => child.textContent).join(""); }
    set textContent(value) { this._text = String(value); this.children = []; }
    appendChild(child) { child.parentElement = this; this.children.push(child); return child; }
    replaceChildren(...children) { this._text = ""; this.children = children; }
    addEventListener(name, callback) { this.events[name] = callback; }
    dispatch(name, properties = {}) { const event = {target: this, currentTarget: this, preventDefault() { this.prevented = true; }, ...properties}; this.events[name]?.(event); return event; }
    click() { if (!this.disabled) this.dispatch("click"); }
    set innerHTML(_) { throw new Error("HTML injection"); }
    set outerHTML(_) { throw new Error("HTML injection"); }
  }
  const staticElements = staticTags.map(tag => {
    const element = new Element(tag.tag);
    for (const [name, value] of Object.entries(tag.attributes)) element.setAttribute(name, value);
    element.hidden = Object.hasOwn(tag.attributes, "hidden");
    element.disabled = Object.hasOwn(tag.attributes, "disabled");
    return element;
  });
  const elements = new Map(staticElements.filter(element => element.getAttribute("id"))
    .map(element => [element.getAttribute("id"), element]));
  const helpButtons = staticElements.filter(element => element.getAttribute("data-help") !== null);
  for (const element of staticElements.filter(element => element.className.split(/\s+/).includes("context-help"))) {
    const start = html.indexOf('id="' + element.id + '"');
    const textStart = html.indexOf(">", start) + 1;
    element.textContent = html.slice(textStart, html.indexOf("<", textStart));
  }
  elements.get("help-tooltip").rect = {left: 0, top: 0, right: 288, bottom: 130, width: 288, height: 130};
  elements.get("kind").value = "sale"; elements.get("expense-category").value = "operating"; elements.get("settlement").value = "paid";
  const queue = [response({token: sessionToken}), response(initial)], requests = [], windowEvents = {}, documentEvents = {}, timers = new Map();
  let clock = 0, nextTimer = 0;
  const setTimer = (callback, delay = 0) => {const id = ++nextTimer; timers.set(id, {callback, due: clock + delay}); return id;};
  const advance = milliseconds => {clock += milliseconds; for (const [id, timer] of [...timers]) if (timer.due <= clock) {timers.delete(id); timer.callback();}};
  let nextId = 0;
  const document = {title: "", getElementById: id => { assert(elements.has(id), "existing ID " + id); return elements.get(id); }, createElement: tag => new Element(tag), createElementNS: (_, tag) => new Element(tag),
    documentElement: {clientWidth: 1024, clientHeight: 768},
    addEventListener: (name, callback) => {documentEvents[name] = callback;},
    querySelectorAll: selector => {
      assert.equal(selector, "[data-help]", "explicit supported DOM query");
      return helpButtons;
    }};
  const context = {document, setTimeout: setTimer, clearTimeout: id => timers.delete(id), localStorage: storage, crypto: {randomUUID: () => "uuid-" + (++nextId)}, window: {visualViewport: viewport, innerWidth: 1024, innerHeight: 768, setTimeout: setTimer, clearTimeout: id => timers.delete(id), location: {origin: "http://127.0.0.1:8766", hash}, addEventListener: (name, callback) => {windowEvents[name] = callback;}},
    fetch: async (url, options) => {
      requests.push({url, options});
      assert(queue.length, "planned API response for " + url);
      const item = queue.shift();
      if (item instanceof Error) throw item;
      return await item;
    }};
  vm.runInNewContext(app, context, {timeout: 1000});
  const set = (id, value) => { elements.get(id).value = value; };
  return {elements, helpButtons, queue, requests, context, document, windowEvents, documentEvents, storage, set, advance,
    text: id => elements.get(id).textContent,
    rows: id => elements.get(id).children.map(row => row.children.map(cell => cell.textContent)),
    kind: value => {set("kind", value); elements.get("kind").dispatch("change");},
    navigate: view => elements.get("nav-" + view).click(),
    submit: () => elements.get("transaction-form").dispatch("submit"),
    posts: () => requests.filter(request => request.url === "/api/transactions")};
}
const views = ["home", "record", "stock", "activity", "advanced"];
function assertView(run, expected) {
  for (const view of views) {
    assert.equal(run.elements.get("screen-" + view).hidden, view !== expected, "only selected screen visible: " + view);
    assert.equal(run.elements.get("nav-" + view).getAttribute("aria-current"), view === expected ? "page" : null,
      "selected navigation is conveyed accessibly: " + view);
  }
}
function committed(event, revision, commitStatus = "recorded") {
  const result = fixture(revision);
  result.commit_status = commitStatus;
  result.state.cash = 87654;
  result.state.earned_funding = 321;
  result.summary.inventory_value = 22222;
  result.events = [{event, effect: {cash_delta: -29, inventory_value_delta: 29, earned_funding_delta: 0, revenue: 0, cost_of_goods_sold: 0, expense: 0}}];
  return result;
}
function delivery(run) {
  run.kind("delivery"); run.set("delivery-sku", "BREAD"); run.set("lot-id", "new-lot"); run.set("delivery-units", "3"); run.set("unit-cost", "0.29");
}
(async () => {
  const run = page();
  assert(run.elements.get("save").disabled, "disabled until service state loads");
  await tick();
  assert.equal(run.text("display-name"), "An operation");
  assert.equal(run.text("provenance"), "Observed records");
  assert.equal(run.text("cash"), "1,000.00 EUR");
  assert.equal(run.text("earned-funding"), "−1.25 EUR");
  assert.equal(run.text("inventory-value"), "123.45 EUR", "native summary is displayed without recomputing");
  assert.equal(run.elements.get("save").disabled, false);
  assert.equal(run.elements.get("date").value, "2026-10-14");
  assert.equal(run.elements.get("unit-price").value, "2.50", "default actual price is current public price");
  assert.equal(run.rows("products")[0][0], dangerous, "hostile catalog text remains literal");
  assert.equal(run.context.injected, undefined);
  assert.equal(run.rows("lots")[0][3], "1.00 EUR", "acquisition cost distinct from public price");
  assert.deepEqual(run.rows("history"), [["No transactions recorded after opening."]]);
  assertView(run, "home");
  assert.equal(run.text("view-title"), "Home");
  assert.equal(run.text("home-cash"), "1,000.00 EUR");
  assert.equal(run.text("home-stock"), "123.45 EUR", "home inventory uses authoritative native summary");
  assert.equal(run.text("home-available"), "942.00 EUR", "home available cash uses authoritative native summary");
  assert.equal(run.text("home-obligations"), "8.00 EUR");
  assert.equal(run.rows("products")[0][1], "50", "stock screen exposes remaining units");
  assert.equal(run.rows("products")[0][2], "2.50 EUR", "stock distinguishes published price");
  assert.equal(run.rows("products")[0][3], "1.30 EUR", "stock distinguishes replacement cost");
  assert(run.text("setup-settings").includes("EUR"), "read-only configuration exposes the selected currency");
  assert(run.text("setup-settings").includes("data/exchange.idydb"), "configuration points to the selected environment store");
  const descendants = element => [element, ...element.children.flatMap(descendants)];
  const tooltip = run.elements.get("help-tooltip");
  const escapeHelp = () => run.documentEvents.keydown({key: "Escape", preventDefault() {this.prevented = true;}});
  const assertOpen = (button, caption) => {
    assert.equal(tooltip.hidden, false, "floating description is visible");
    assert.equal(tooltip.textContent, caption.textContent, "description is copied as literal text");
    assert.equal(caption.hidden, true, "source caption stays hidden: help never changes page layout");
    assert.equal(button.getAttribute("aria-expanded"), "true", "only active trigger is expanded");
  };
  const assertClosed = button => {
    assert.equal(tooltip.hidden, true, "floating description is dismissed");
    assert.equal(button.getAttribute("aria-expanded"), "false");
  };
  const settingNodes = descendants(run.elements.get("setup-settings"));
  const settingButtons = settingNodes.filter(element => element.getAttribute("data-help") !== null);
  assert.equal(settingButtons.length, run.elements.get("setup-settings").children.length,
    "each generated read-only setting uses the same contextual help pattern");
  assert(settingButtons.length > 0, "generated environment settings contain contextual help");
  for (const button of settingButtons) {
    const captionId = button.getAttribute("data-help"), caption = settingNodes.find(element => element.id === captionId);
    assert(caption, "generated help has an existing description");
    assert.equal(button.tagName, "button");
    assert.equal(button.type, "button");
    assert(/^About \S/.test(button.getAttribute("aria-label") || ""));
    assert.equal(button.getAttribute("aria-controls"), "help-tooltip");
    assert.equal(button.getAttribute("aria-describedby"), captionId);
    assert.equal(button.getAttribute("aria-expanded"), "false");
    assert.equal(caption.hidden, true);
    assert(caption.textContent.trim(), "generated caption has an actual explanation");
    assert(descendants(button).some(node => node.tagName === "svg" && node.getAttribute("aria-hidden") === "true"),
      "generated settings use the same decorative SVG icon");
    assert(settingNodes.some(element => element.getAttribute("aria-describedby")?.split(/\s+/).includes(captionId)),
      "generated setting value is linked to its description");
    button.focus();
    assertOpen(button, caption);
    button.click(); button.blur(); run.advance(1000);
    assertOpen(button, caption);
    escapeHelp(); assertClosed(button);
  }

  // Contextual help never changes form drafts, activity selection, or persisted records.
  run.elements.get("action-sale").click();
  run.set("sale-units", "7"); run.set("unit-price", "2.13"); run.set("date", "2026-10-17");
  for (const button of run.helpButtons) {
    const captionId = button.getAttribute("data-help"), caption = run.elements.get(captionId);
    assert.equal(caption.hidden, true, "description stays hidden: " + captionId);
    button.dispatch("pointerenter", {pointerType: "mouse"});
    assertOpen(button, caption);
    button.dispatch("pointerleave", {pointerType: "mouse"});
    run.advance(1000); assertClosed(button);
    button.focus();
    assertOpen(button, caption);
    assert.equal(run.document.activeElement, button, "floating help leaves focus at its trigger");
    button.click(); // Native focus occurs before click; this must pin, not immediately close.
    assertOpen(button, caption);
    button.blur(); run.advance(1000);
    assertOpen(button, caption);
    button.click(); assertClosed(button);
    button.focus();
    assertOpen(button, caption);
    run.documentEvents.keydown({key: "ArrowRight"});
    assertOpen(button, caption);
    escapeHelp(); assertClosed(button);
    assert.equal(run.document.activeElement, button, "Escape leaves keyboard focus at the trigger");
    button.blur();
    assert.equal(run.elements.get("sale-units").value, "7", "help preserves quantity");
    assert.equal(run.elements.get("unit-price").value, "2.13", "help preserves actual price");
    assert.equal(run.elements.get("date").value, "2026-10-17", "help preserves record date");
    assert.equal(run.elements.get("kind").value, "sale", "help preserves chosen activity");
    assertView(run, "record");
  }
  assert.equal(run.requests.length, 2, "help makes no service request");
  assert.equal(run.posts().length, 0, "help creates no transaction");
  assert.equal(run.storage.length, 0, "help creates no pending recovery request");

  const helpA = run.elements.get("info-sale-units"), helpB = run.elements.get("info-unit-price");
  const captionA = run.elements.get("help-sale-units"), captionB = run.elements.get("help-unit-price");
  helpA.dispatch("pointerenter", {pointerType: "mouse"});
  helpA.dispatch("pointerleave", {pointerType: "mouse"});
  run.advance(100);
  assertOpen(helpA, captionA); // A short crossing gap must not dismiss the popup.
  tooltip.dispatch("pointerenter", {pointerType: "mouse"});
  run.advance(1000); assertOpen(helpA, captionA);
  run.windowEvents.scroll({target: tooltip});
  assertOpen(helpA, captionA);
  tooltip.dispatch("pointerleave", {pointerType: "mouse"});
  run.advance(1000); assertClosed(helpA);
  helpA.focus();
  helpA.dispatch("pointerleave", {pointerType: "mouse"});
  run.advance(1000); assertOpen(helpA, captionA);
  helpA.blur(); run.advance(1000); assertClosed(helpA);

  helpA.dispatch("pointerenter", {pointerType: "touch"});
  assertClosed(helpA);
  helpA.focus(); helpA.click(); // Touch browsers can focus before synthesizing click.
  assertOpen(helpA, captionA);
  helpA.blur(); run.advance(1000); assertOpen(helpA, captionA);
  helpA.click(); assertClosed(helpA);

  helpA.dispatch("pointerenter", {pointerType: "mouse"});
  helpB.dispatch("pointerenter", {pointerType: "mouse"});
  assertOpen(helpB, captionB);
  assert.equal(helpA.getAttribute("aria-expanded"), "false", "opening another description collapses the old trigger");
  assert.equal(run.helpButtons.filter(button => button.getAttribute("aria-expanded") === "true").length, 1);
  helpA.dispatch("pointerleave", {pointerType: "mouse"});
  run.advance(1000); assertOpen(helpB, captionB);
  helpB.click();
  run.documentEvents.pointerdown({target: tooltip});
  assertOpen(helpB, captionB);
  run.documentEvents.pointerdown({target: helpB});
  assertOpen(helpB, captionB);
  run.documentEvents.pointerdown({target: run.elements.get("sale-units")});
  assertClosed(helpB);

  const originalCaption = captionA.textContent;
  captionA.textContent = dangerous;
  helpA.click(); assertOpen(helpA, captionA);
  assert.equal(tooltip.children.length, 0, "description markup is never parsed as HTML");
  assert.equal(run.context.injected, undefined);
  escapeHelp(); captionA.textContent = originalCaption;

  // Position checks are layout contracts using deterministic geometry, not rendered browser assertions.
  run.context.window.innerWidth = run.document.documentElement.clientWidth = 320;
  run.context.window.innerHeight = run.document.documentElement.clientHeight = 260;
  helpA.rect = {left: 292, right: 316, top: 226, bottom: 250, width: 24, height: 24};
  helpA.dispatch("pointerenter", {pointerType: "mouse"});
  const left = Number.parseFloat(tooltip.style.left), top = Number.parseFloat(tooltip.style.top);
  assert(left >= 0 && left + tooltip.rect.width <= 320, "right-edge popup stays inside viewport");
  assert(top >= 0 && top + tooltip.rect.height <= 260, "bottom-edge popup stays inside viewport");
  assert(top < helpA.rect.top, "popup flips above an icon when space below is insufficient");
  run.windowEvents.resize(); assertClosed(helpA);
  helpA.rect = {left: 0, right: 24, top: 0, bottom: 24, width: 24, height: 24};
  helpA.dispatch("pointerenter", {pointerType: "mouse"});
  assert(Number.parseFloat(tooltip.style.left) >= 0, "left-edge popup stays inside viewport");
  assert(Number.parseFloat(tooltip.style.top) >= 0, "top-edge popup stays inside viewport");
  run.windowEvents.scroll({target: run.document}); assertClosed(helpA);
  run.context.window.innerWidth = run.document.documentElement.clientWidth = 1024;
  run.context.window.innerHeight = run.document.documentElement.clientHeight = 768;

  helpA.click(); run.navigate("advanced"); assertClosed(helpA);
  run.navigate("record");
  helpA.click(); run.elements.get("choose-delivery").click(); assertClosed(helpA);
  run.elements.get("choose-sale").click();
  const viewportEvents = {}, viewport = {width: 320, height: 260, offsetLeft: 30, offsetTop: 40,
    addEventListener: (name, callback) => {viewportEvents[name] = callback;}};
  const zoomed = page(fixture(), memoryStorage(), "local-test-token", "", viewport); await tick();
  const zoomButton = zoomed.elements.get("info-home-cash"), zoomTip = zoomed.elements.get("help-tooltip");
  zoomButton.rect = {left: 326, right: 350, top: 270, bottom: 294, width: 24, height: 24};
  zoomButton.dispatch("pointerenter", {pointerType: "mouse"});
  assert(Number.parseFloat(zoomTip.style.left) >= viewport.offsetLeft, "zoomed viewport offset is respected");
  assert(Number.parseFloat(zoomTip.style.left) + zoomTip.rect.width <= viewport.offsetLeft + viewport.width);
  assert(Number.parseFloat(zoomTip.style.top) >= viewport.offsetTop);
  assert(Number.parseFloat(zoomTip.style.top) + zoomTip.rect.height <= viewport.offsetTop + viewport.height);
  viewportEvents.scroll(); assert.equal(zoomTip.hidden, true, "visual viewport panning dismisses stale help");
  zoomButton.click(); viewportEvents.resize();
  assert.equal(zoomTip.hidden, true, "visual viewport resizing dismisses stale help");

  run.elements.get("action-sale").click();
  assertView(run, "record");
  assert.equal(run.elements.get("kind").value, "sale", "home action selects the sale workflow");
  assert.equal(run.elements.get("sale-fields").hidden, false);
  run.set("sale-units", "7"); run.set("unit-price", "2.13"); run.set("date", "2026-10-17");
  for (const view of ["home", "stock", "activity", "advanced", "record"]) {
    run.navigate(view); assertView(run, view);
    assert.equal(run.elements.get("sale-units").value, "7", "navigation retains quantity draft");
    assert.equal(run.elements.get("unit-price").value, "2.13", "navigation retains actual-price override");
    assert.equal(run.elements.get("date").value, "2026-10-17", "navigation retains draft date");
    assert.equal(run.elements.get("kind").value, "sale", "navigation retains selected workflow");
  }
  run.elements.get("choose-delivery").click();
  assert.equal(run.elements.get("kind").value, "delivery");
  run.elements.get("choose-sale").click();
  assert.equal(run.elements.get("unit-price").value, "2.13", "workflow switching preserves actual-price override");
  run.navigate("home"); run.elements.get("action-sale").click();
  assert.equal(run.elements.get("unit-price").value, "2.13", "returning through the sale quick action preserves the price draft");
  run.elements.get("choose-expense").click();
  run.set("expense-description", "Maintenance draft"); run.set("expense-amount", "9.18"); run.set("settlement", "unpaid");
  for (const view of ["home", "advanced", "record"]) {
    run.navigate(view);
    assert.equal(run.elements.get("expense-description").value, "Maintenance draft", "navigation retains expense description");
    assert.equal(run.elements.get("expense-amount").value, "9.18", "navigation retains expense amount");
    assert.equal(run.elements.get("settlement").value, "unpaid", "navigation retains expense payment choice");
    assert.equal(run.elements.get("kind").value, "expense");
  }

  for (const kind of ["sale", "delivery", "expense"]) {
    run.navigate("home"); run.elements.get("action-" + kind).click();
    assertView(run, "record");
    assert.equal(run.elements.get("kind").value, kind, "home quick action selects " + kind);
    for (const candidate of ["sale", "delivery", "expense"]) {
      assert.equal(run.elements.get(candidate + "-fields").hidden, candidate !== kind);
      assert.equal(run.elements.get(candidate + "-fields").disabled, candidate !== kind);
    }
  }
  for (const view of ["record", "advanced"]) {
    run.navigate(view);
    const beforeSkipHash = run.context.window.location.hash;
    const skip = run.elements.get("skip-workspace").dispatch("click");
    assert.equal(skip.prevented, true, "skip link prevents fragment routing");
    assertView(run, view);
    assert.equal(run.context.window.location.hash, beforeSkipHash, "skip link preserves selected view URL");
    assert.equal(run.document.activeElement, run.elements.get("view-title"), "skip link moves keyboard focus into current workspace");
  }
  assert.equal(run.requests.length, 2, "navigation is local and never records or refreshes accounts");
  assert.equal(run.posts().length, 0);
  run.set("date", "2026-10-14"); run.set("sale-units", ""); run.set("unit-price", "2.50");


  delivery(run);
  let resolvePost;
  run.queue.push(new Promise(resolve => {resolvePost = resolve;}));
  run.submit(); run.submit();
  assert.equal(run.posts().length, 1, "duplicate submit while awaiting acknowledgement is ignored");
  assert(run.elements.get("save").disabled);
  const firstRequest = run.posts()[0], first = JSON.parse(firstRequest.options.body);
  assert.equal(run.storage.length, 1, "outbox stored before POST acknowledgement");
  for (const view of views) {
    run.navigate(view); assertView(run, view);
    assert.equal(run.elements.get("pending").hidden, false, "unconfirmed request remains visible on " + view);
    assert(run.elements.get("save").disabled, "new records remain blocked on " + view);
    for (const kind of ["sale", "delivery", "expense"]) {
      assert(run.elements.get("action-" + kind).disabled, "quick action blocked while saving");
      assert(run.elements.get("choose-" + kind).disabled, "workflow blocked while saving");
    }
  }

  assert.equal(JSON.parse(run.storage.getItem(run.storage.key(0))).body, firstRequest.options.body);
  assert(!run.storage.getItem(run.storage.key(0)).includes("local-test-token"), "session token is not stored");
  assert.equal(firstRequest.options.headers["X-Exchange-Token"], "local-test-token");
  assert.equal(firstRequest.options.headers["Content-Type"], "application/json");
  assert.equal(first.expected_revision, 0);
  assert.deepEqual(first.event, {schema_version: "exchange.transaction.v1", id: first.event.id, environment_id: "one-operation", date: "2026-10-14", kind: "delivery",
    details: {sku: "BREAD", lot_id: "new-lot", units: 3, unit_cost: 29}});
  const refreshingHelp = run.elements.get("info-home-cash");
  refreshingHelp.click();
  assertOpen(refreshingHelp, run.elements.get("help-home-cash"));
  resolvePost(response(committed(first.event, 1)));
  await tick();
  assertClosed(refreshingHelp); // Do not leave a stale description open across authoritative re-render.
  assert(run.text("status").startsWith("Recorded transaction: "));
  assert.equal(run.text("cash"), "876.54 EUR", "cash repaints from server");
  assert.equal(run.text("inventory-value"), "222.22 EUR", "inventory repaints from server");
  assert.equal(run.text("home-cash"), "876.54 EUR", "home cash repaints from saved response");
  assert.equal(run.text("home-stock"), "222.22 EUR", "home inventory repaints from saved response");
  assert(/delivery/i.test(run.text("home-history")), "home activity has a human-readable kind");
  assert(run.text("home-history").includes(dangerous), "home activity identifies the product as literal text");
  assert(run.text("home-history").includes("3"), "home activity describes actual recorded quantity");
  assert(run.text("history").includes(dangerous), "full activity also describes product without opening JSON");

  assert.equal(run.text("earned-funding"), "3.21 EUR");
  assert.equal(run.elements.get("pending").hidden, true);
  assert.equal(run.elements.get("save").disabled, false);
  assert.equal(run.storage.length, 0, "confirmed request removed from outbox");
  assert.equal(run.elements.get("delivery-units").value, "", "confirmed form quantities clear");
  assert(run.text("history").includes(first.event.id), "complete saved event and effect inspectable");
  assert(run.text("history").includes("cash_delta"), "original accounting effect remains inspectable");

  run.kind("sale"); run.set("sale-sku", "OIL"); run.elements.get("sale-sku").dispatch("change");
  assert.equal(run.elements.get("unit-price").value, "4.00");
  run.set("sale-units", "2"); run.set("unit-price", "3.19"); run.set("date", "2026-10-15");
  const saleResult = fixture(2); saleResult.commit_status = "recorded"; saleResult.state.as_of = "2026-10-15";
  run.queue.push(response(saleResult)); run.submit(); await tick();
  const sale = JSON.parse(run.posts().at(-1).options.body);
  assert.equal(sale.expected_revision, 1); assert.equal(sale.event.date, "2026-10-15");
  assert.deepEqual(sale.event.details, {sku: "OIL", units: 2, unit_price: 319});
  assert.equal(sale.event.kind, "sale"); assert.notEqual(sale.event.id, first.event.id);

  run.kind("expense"); run.set("expense-category", "wages"); run.set("expense-description", dangerous); run.set("expense-amount", "12.50"); run.set("settlement", "unpaid");
  const expenseResult = fixture(3); expenseResult.commit_status = "recorded";
  expenseResult.state.obligations.push({id: "expense-3", kind: "wages", amount: 1250});
  run.queue.push(response(expenseResult)); run.submit(); await tick();
  const expense = JSON.parse(run.posts().at(-1).options.body);
  assert.equal(expense.expected_revision, 2); assert.equal(expense.event.kind, "expense");
  assert.deepEqual(expense.event.details, {category: "wages", description: dangerous, amount: 1250, settlement: "unpaid"});
  assert.equal(run.rows("obligations").at(-1)[2], "12.50 EUR");
  run.set("expense-category", "operating"); run.set("expense-description", "Electricity"); run.set("expense-amount", "1"); run.set("settlement", "paid");
  run.queue.push(response({...fixture(4), commit_status: "recorded"})); run.submit(); await tick();
  assert.deepEqual(JSON.parse(run.posts().at(-1).options.body).event.details, {category: "operating", description: "Electricity", amount: 100, settlement: "paid"});

  delivery(run);
  const count = run.posts().length;
  run.elements.get("delivery-reference").open = false;
  run.set("lot-id", "invalid lot reference"); run.submit();
  assert.equal(run.posts().length, count, "invalid lot reference is rejected before transport");
  assert.equal(run.elements.get("delivery-reference").open, true, "invalid lot reference is revealed for correction");
  assert.equal(run.document.activeElement, run.elements.get("lot-id"), "invalid reference receives keyboard focus");
  run.elements.get("delivery-reference").open = false;
  run.elements.get("lot-id").dispatch("invalid");
  assert.equal(run.elements.get("delivery-reference").open, true, "native form validation also reveals the reference");
  run.set("lot-id", "new-lot");
  for (const invalid of ["0.001", "1e3", "1,00", "-1", "Infinity", "", "90000000000.01"]) {
    run.set("unit-cost", invalid); run.submit();
    assert.equal(run.posts().length, count, "invalid money rejected before transport: " + invalid);
    assert.equal(run.elements.get("error").hidden, false);
  }
  run.set("unit-cost", "1.25"); run.set("delivery-units", "1.5"); run.submit();
  assert.equal(run.posts().length, count, "fractional units rejected");
  run.set("delivery-units", "1000001"); run.submit(); assert.equal(run.posts().length, count);
  run.set("delivery-units", "1"); run.set("unit-cost", "90000000000.00");
  run.queue.push(response({status: "error", message: "Native account limit test."}, 400), response(fixture(4)));
  run.submit(); await tick();
  assert.equal(JSON.parse(run.posts().at(-1).options.body).event.details.unit_cost, 9000000000000, "maximum monetary input parsed exactly");
  assert(run.text("error").includes("was not recorded"));
  assert(!run.text("status").startsWith("Recorded"));

  run.kind("sale"); run.set("sale-units", "1"); run.set("unit-price", "0");
  const beforeZeroSale = run.posts().length; run.submit(); assert.equal(run.posts().length, beforeZeroSale, "zero sale price rejected");

  delivery(run); run.set("unit-cost", "0");
  run.queue.push(new Error("connection lost after commit")); run.submit(); await tick();
  const uncertain = run.posts().at(-1), uncertainEvent = JSON.parse(uncertain.options.body).event;
  assert.equal(uncertainEvent.details.unit_cost, 0, "zero acquisition cost accepted");
  assert.equal(run.elements.get("pending").hidden, false);
  assert(run.elements.get("save").disabled);
  assert.equal(run.elements.get("retry").disabled, false);
  assert(run.text("error").includes("uncertain"));
  assert(!run.text("status").startsWith("Recorded"));
  run.submit(); assert.equal(run.posts().at(-1), uncertain, "uncertain submission cannot be recreated");
  const unload = {preventDefault() {this.prevented = true;}};
  run.windowEvents.beforeunload(unload); assert.equal(unload.prevented, true, "warn before abandoning pending identity");
  run.queue.push(response(committed(uncertainEvent, 5)));
  run.elements.get("refresh").click(); await tick();
  assert.equal(run.elements.get("pending").hidden, false, "refresh alone does not discard unconfirmed request");
  assert(run.text("status").includes("still needs confirmation"));
  run.queue.push(response(committed(uncertainEvent, 5, "already_recorded")));
  run.elements.get("retry").click(); await tick();
  assert.equal(run.posts().at(-1).options.body, uncertain.options.body, "retry preserves exact body, original revision and ID");
  assert(run.text("status").startsWith("Previously recorded transaction confirmed: "));
  assert.equal(run.elements.get("pending").hidden, true);
  assert.equal(run.elements.get("save").disabled, false);

  delivery(run);
  run.queue.push(response({status: "error", message: dangerous}, 409), response(fixture(9)));
  run.submit(); await tick();
  assert(run.text("error").includes(dangerous), "server error displayed as literal text");
  assert.equal(run.elements.get("delivery-units").value, "3", "definitively rejected form retained");
  assert.equal(run.elements.get("pending").hidden, true);
  assert.equal(run.elements.get("save").disabled, false);
  assert.equal(run.text("database-status"), "IdyDB · saved revision 9", "stale revision refreshes accounts");
  run.queue.push(response({status: "error", message: "Storage confirmation unavailable."}, 500));
  run.submit(); await tick();
  const serverUncertain = run.posts().at(-1).options.body;
  assert.equal(JSON.parse(serverUncertain).expected_revision, 9);
  assert.equal(run.elements.get("pending").hidden, false, "server error may follow commit and keeps request identity");
  run.queue.push(response({...fixture(10), commit_status: "recorded"}));
  run.elements.get("retry").click(); await tick();
  assert.equal(run.posts().at(-1).options.body, serverUncertain);

  delivery(run);
  run.queue.push({ok: true, status: 200, json: async () => {throw new Error("truncated response");}});
  run.submit(); await tick();
  assert.equal(run.elements.get("pending").hidden, false, "unreadable acknowledgement keeps pending identity");
  assert(run.elements.get("save").disabled);

  // Service restart changes the anti-CSRF token; it must not destroy a lost-ACK identity.
  const retainedBody = run.posts().at(-1).options.body;
  run.queue.push(response({status: "error", message: "Session expired."}, 403), response({token: "renewed-token"}));
  run.elements.get("retry").click(); await tick();
  assert.equal(run.posts().at(-1).options.body, retainedBody);
  assert.equal(run.elements.get("pending").hidden, false);
  assert.equal(run.storage.length, 1);
  assert(run.text("status").includes("Session renewed"));
  assert(!run.text("error").includes("was not recorded"), "authentication failure makes no claim about earlier commit");
  run.queue.push(response({...fixture(11), commit_status: "already_recorded"}));
  run.elements.get("retry").click(); await tick();
  assert.equal(run.posts().at(-1).options.body, retainedBody);
  assert.equal(run.posts().at(-1).options.headers["X-Exchange-Token"], "renewed-token");
  assert.equal(run.storage.length, 0);

  // Reload restores the same outbox even when the fresh server state has advanced.
  const sharedStorage = memoryStorage();
  const beforeReload = page(fixture(), sharedStorage); await tick();
  delivery(beforeReload);
  beforeReload.queue.push(new Error("acknowledgement lost"));
  beforeReload.submit(); await tick();
  const beforeReloadBody = beforeReload.posts()[0].options.body;
  const afterReload = page(fixture(7), sharedStorage, "restart-token"); await tick();
  assert(afterReload.elements.get("save").disabled, "restored pending outbox locks fresh submissions");
  assert.equal(afterReload.elements.get("pending").hidden, false);
  for (const view of ["home", "stock", "activity", "advanced"]) {
    afterReload.navigate(view); assertView(afterReload, view);
    assert.equal(afterReload.elements.get("pending").hidden, false, "restored request remains globally visible");
    assert(afterReload.elements.get("save").disabled);
    assert.equal(afterReload.elements.get("delivery-units").value, "3", "navigation preserves restored pending input");
  }

  afterReload.queue.push(response({status: "error", message: "Temporary validation response."}, 400), response(fixture(7)));
  afterReload.elements.get("retry").click(); await tick();
  assert.equal(afterReload.posts()[0].options.body, beforeReloadBody);
  assert.equal(afterReload.storage.length, 1, "a rejection after uncertainty cannot erase the earlier identity");
  assert(!afterReload.text("error").includes("was not recorded"));
  afterReload.queue.push(response({...fixture(7), commit_status: "already_recorded"}));
  afterReload.elements.get("retry").click(); await tick();
  assert.equal(afterReload.posts().at(-1).options.body, beforeReloadBody);
  assert.equal(sharedStorage.length, 0);

  const staleStorage = memoryStorage();
  const staleBefore = page(fixture(), staleStorage); await tick();
  delivery(staleBefore); staleBefore.queue.push(new Error("lost request")); staleBefore.submit(); await tick();
  const staleBody = staleBefore.posts()[0].options.body;
  const staleAfter = page(fixture(1), staleStorage); await tick();
  assert.equal(staleAfter.elements.get("delivery-units").value, "3", "reload restores pending form inputs");
  assert.equal(staleAfter.elements.get("unit-cost").value, "0.29");
  staleAfter.queue.push(response({status: "error", error_type: "conflict", commit_status: "not_recorded",
    message: "Another client changed the revision; this event was not recorded."}, 409), response(fixture(1)));
  staleAfter.elements.get("retry").click(); await tick();
  assert.equal(staleAfter.posts()[0].options.body, staleBody);
  assert.equal(staleStorage.length, 0, "native verified non-commit resolves stale uncertain outbox");
  assert.equal(staleAfter.elements.get("save").disabled, false);
  assert.equal(staleAfter.elements.get("delivery-units").value, "3", "verified rejection retains entries for review");
  assert(staleAfter.text("error").includes("was not recorded"));
  staleAfter.queue.push(response({...fixture(2), commit_status: "recorded"}));
  staleAfter.submit(); await tick();
  const reviewed = JSON.parse(staleAfter.posts().at(-1).options.body);
  assert.equal(reviewed.expected_revision, 1, "reviewed submission uses refreshed revision");

  const otherEnvironment = fixture();
  otherEnvironment.environment.environment_id = "other-operation";
  otherEnvironment.state.environment_id = "other-operation";
  const originalPending = page(fixture(), sharedStorage); await tick();
  delivery(originalPending); originalPending.queue.push(new Error("lost")); originalPending.submit(); await tick();
  const otherPage = page(otherEnvironment, sharedStorage); await tick();
  assert.equal(otherPage.elements.get("save").disabled, false, "outboxes are scoped to environment identity");
  assert.equal(sharedStorage.length, 1, "other environment preserves original outbox");

  const blockedStorage = memoryStorage();
  blockedStorage.setItem = () => {throw new Error("Browser storage is full.");};
  const blocked = page(fixture(), blockedStorage); await tick();
  delivery(blocked); blocked.submit(); await tick();
  assert.equal(blocked.posts().length, 0, "no submission without a durable recovery request");
  assert(blocked.text("error").includes("storage is full"));

  // Finishing one workflow must not erase drafts entered in another workflow.
  const draftFields = {
    delivery: {"delivery-units": "3", "unit-cost": "0.29", "lot-id": "draft-lot"},
    sale: {"sale-units": "2", "unit-price": "3.11"},
    expense: {"expense-description": "Unfinished maintenance entry", "expense-amount": "7.25", "settlement": "unpaid"}
  };
  for (const recordedKind of ["sale", "delivery", "expense"]) {
    const drafts = page(); await tick();
    for (const fields of Object.values(draftFields)) {
      for (const [id, value] of Object.entries(fields)) drafts.set(id, value);
    }
    drafts.kind(recordedKind);
    drafts.queue.push(response({...fixture(1), commit_status: "recorded"}));
    drafts.submit(); await tick();
    assert.equal(drafts.posts().length, 1, "one " + recordedKind + " is submitted");
    assert(drafts.text("status").startsWith("Recorded transaction: "));
    for (const [kind, fields] of Object.entries(draftFields)) {
      if (kind === recordedKind) continue;
      for (const [id, value] of Object.entries(fields)) {
        assert.equal(drafts.elements.get(id).value, value, "recording " + recordedKind + " preserves " + kind + " draft " + id);
      }
    }
    const cleared = {sale: "sale-units", delivery: "delivery-units", expense: "expense-description"}[recordedKind];
    assert.equal(drafts.elements.get(cleared).value, "", "confirmed " + recordedKind + " form clears");
  }

  const activity = fixture(6);
  activity.state.as_of = "2026-10-19";
  activity.events = Array.from({length: 6}, (_, index) => ({
    event: {schema_version: "exchange.transaction.v1", id: "activity-" + index, environment_id: "one-operation",
      date: "2026-10-" + (14 + index), kind: "delivery",
      details: {sku: "OIL", lot_id: "lot-" + index, units: index + 1, unit_cost: 100}},
    effect: {cash_delta: -(index + 1) * 100, inventory_value_delta: (index + 1) * 100, earned_funding_delta: 0,
      revenue: 0, cost_of_goods_sold: 0, expense: 0}
  }));
  const activityPage = page(activity); await tick();
  assert.equal(activityPage.rows("home-history").length, 5, "home limits recent activity without dropping full history");
  assert.equal(activityPage.rows("history").length, 6, "full history remains available in Activity");
  assert.equal(activityPage.rows("home-history")[0][0], "2026-10-19", "recent activity is newest first");
  assert.equal(activityPage.rows("home-history").at(-1)[0], "2026-10-15");
  assert.equal(activityPage.rows("home-history")[0][2], "−6.00 EUR", "recent activity uses saved native cash effect");
  assert(activityPage.rows("home-history")[0][1].includes("Oil"), "recent activity identifies product");
  assert(activityPage.rows("home-history")[0][1].includes("6"), "recent activity identifies quantity");

  const shortfall = fixture();
  shortfall.summary.cash_after_obligations_and_reserve = -1234;
  const shortfallPage = page(shortfall); await tick();
  assert.equal(shortfallPage.text("home-available"), "−12.34 EUR", "home does not hide a negative native cash position");

  const linked = page(fixture(), memoryStorage(), "local-test-token", "#stock"); await tick();
  assertView(linked, "stock");
  linked.context.window.location.hash = "#advanced";
  linked.windowEvents.hashchange();
  assertView(linked, "advanced");
  linked.context.window.location.hash = "#not-a-screen";
  linked.windowEvents.hashchange();
  assertView(linked, "home");

  const invalid = fixture(); invalid.capabilities.transactions = false;
  const failed = page(invalid); await tick();
  assert(failed.elements.get("save").disabled);
  assert(failed.text("error").includes("Unsupported"));
  assert.equal(failed.posts().length, 0);

  const empty = fixture(); empty.metadata.products = []; empty.state.products = []; empty.state.obligations = [];
  const noStock = page(empty); await tick();
  assert.deepEqual(noStock.rows("products"), [["No products recorded."]]);
  assert.deepEqual(noStock.rows("lots"), [["No acquisition lots recorded."]]);
  assert.deepEqual(noStock.rows("obligations"), [["No outstanding obligations recorded."]]);
  assert.equal(noStock.elements.get("save").disabled, false, "expenses remain usable without products");

  console.log("Connected operation UI: accessible hover/focus/touch floating help, Home and workflow navigation, draft preservation, advanced opt-in, authoritative stock and activity, globally visible pending requests, exact minor-unit inputs, real transaction bodies, native account display, duplicate prevention, durable outbox, session renewal, uncertain-commit retry, stale-state refresh, errors and literal text checks pass. Visual browser testing is separate.");
})().catch(failure => {console.error(failure); process.exitCode = 1;});
