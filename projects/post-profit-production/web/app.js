/* SPDX-License-Identifier: MIT; Copyright (c) 2026 Waajacu */
(() => {
  "use strict";

  // View model helpers begin
  function simulationEnvelope(raw) {
    if (typeof raw !== "string") throw new Error("Configuration input must be text.");
    // Move an optional document BOM to the envelope start; JSON tokens stay exact.
    if (raw.charCodeAt(0) === 0xfeff) return String.fromCharCode(0xfeff) + '{"op":"simulate","config":' + raw.slice(1) + '}';
    return '{"op":"simulate","config":' + raw + '}';
  }
  function classifyStage(stage) {
    if (stage.work_this_tick > 0) return "work";
    if (stage.status === "completed") return "complete";
    if (stage.reason === "awaiting_release") return "unreleased";
    if (stage.start_tick !== null && stage.start_tick !== undefined) return "pause";
    return "wait";
  }
  function selectedRow(result, tick) {
    return result && Array.isArray(result.rows) ? result.rows.find((row) => row.tick === tick) || null : null;
  }
  function outcomeRows(report) {
    const rows = [];
    for (const summary of report.case_summaries || []) {
      for (const [job, counts] of Object.entries(summary.jobs || {})) {
        rows.push({case_id: summary.case_id, phase: summary.phase, policy: summary.policy, job,
          sample_count: summary.sample_count, completed_count: counts.completed_count,
          unfinished_count: counts.unfinished_count, missed_deadline_count: counts.missed_deadline_count,
          completion_tick_counts: counts.completion_tick_counts});
      }
    }
    return rows;
  }
  // View model helpers end

  // Worker handler begin
  const workerHandler = `
"use strict";
const productionModule = createProductionModule({noInitialRun:true,print:()=>{},printErr:()=>{}});
productionModule.then(() => self.postMessage({ready:true})).catch((error) => self.postMessage({ready:false,error:String(error.message || error)}));
self.onmessage = async (event) => {
  const request = event.data || {};
  try {
    if (typeof request.text !== "string") throw new Error("Input must be text.");
    if (request.kind !== "simulation" && request.kind !== "experiment") throw new Error("Unknown research operation.");
    if (request.text.includes(String.fromCharCode(0))) throw new Error("Input contains an actual null character.");
    for (let i=0;i<request.text.length;++i) {
      const code=request.text.charCodeAt(i);
      if (code>=0xd800 && code<=0xdbff) {
        const following=request.text.charCodeAt(++i);
        if (!(following>=0xdc00 && following<=0xdfff)) throw new Error("Input contains an unpaired Unicode surrogate.");
      } else if (code>=0xdc00 && code<=0xdfff) throw new Error("Input contains an unpaired Unicode surrogate.");
    }
    const hasBOM = request.text.charCodeAt(0) === 0xfeff;
    const command = request.kind === "simulation" ? (hasBOM ? String.fromCharCode(0xfeff) : "") + '{"op":"simulate","config":' + (hasBOM ? request.text.slice(1) : request.text) + '}' : request.text;
    if (new TextEncoder().encode(command).length>2*1024*1024) throw new Error("Input exceeds the engine's 2 MiB JSON limit, including the command envelope.");
    const module = await productionModule;
    const exported = request.kind === "simulation" ? "production_run" : "production_experiment_run";
    const output = module.ccall(exported,"string",["string"],[command]);
    const result = JSON.parse(output);
    if (result.status === "error") throw new Error(typeof result.error === "string" ? result.error : result.error && result.error.message || "The engine rejected this input.");
    if (request.kind === "simulation" && (result.schema_version !== "production.sim.v1" || result.status !== "ok")) throw new Error("The engine returned an unsupported production result.");
    if (request.kind === "experiment" && (result.schema_version !== "production.experiment.v1" || !["ok","verification_failed"].includes(result.status))) throw new Error("The engine returned an unsupported experiment report.");
    self.postMessage({id:request.id,ok:true,result});
  } catch (error) {
    self.postMessage({id:request.id,ok:false,error:String(error.message || error)});
  }
};
`;
  function workerSource(runtimeText) { return runtimeText + "\n" + workerHandler; }
  // Worker handler end

  const $ = (id) => document.getElementById(id);
  const numberFormat = new Intl.NumberFormat("en", {maximumFractionDigits: 0});
  const format = (value) => value === null || value === undefined ? "—" : typeof value === "number" ? numberFormat.format(value) : typeof value === "boolean" ? value ? "yes" : "no" : String(value);
  const readable = (value) => String(value).replace(/_/g, " ");
  const state = {mode:"simulation", inputs:{simulation:$("default-config").textContent.trim(), experiment:$("default-plan").textContent.trim()},
    inputLabels:{simulation:"Example configuration",experiment:"Example scheduling plan"}, worker:null, workerURL:null, sequence:0, pending:null,
    busy:false, result:null, report:null, recordedInput:null, recordedKind:null, tick:1, playTimer:null};

  function node(tag, text, className) {
    const element = document.createElement(tag);
    if (text !== null && text !== undefined) element.textContent = String(text);
    if (className) element.className = className;
    return element;
  }
  function svgNode(tag, attrs, text) {
    const element = document.createElementNS("http://www.w3.org/2000/svg", tag);
    for (const [key, value] of Object.entries(attrs || {})) element.setAttribute(key, String(value));
    if (text !== undefined) element.textContent = String(text);
    return element;
  }
  function cell(value, className) { return node("td", format(value), className); }
  function identityCell(id, detail) {
    const element = cell(id);
    if (detail) element.append(node("span", detail, "subline"));
    return element;
  }
  function bodyFor(id) { const body = $(id).querySelector("tbody"); body.replaceChildren(); return body; }
  function tableRow(body, cells) { const row = node("tr"); for (const item of cells) row.append(item); body.append(row); }
  function badge(value, className) { return node("span", readable(value), "condition " + (className || "")); }
  function setOptions(select, items, previous) {
    select.replaceChildren();
    for (const item of items) { const option = node("option", item.label); option.value = item.value; select.append(option); }
    if (items.some((item) => item.value === previous)) select.value = previous;
    select.disabled = !items.length;
  }
  function error(message) { $("run-error").textContent = message; $("run-error").hidden = false; }
  function clearError() { $("run-error").textContent = ""; $("run-error").hidden = true; }
  function updateEditorState() {
    const raw = $("config-editor").value;
    state.inputs[state.mode] = raw;
    $("editor-bytes").textContent = format(new TextEncoder().encode(raw).length) + " UTF-8 bytes";
    const current = state.recordedKind === state.mode && state.recordedInput === raw;
    $("editor-state").textContent = current ? "Matches the recorded input" : state.result ? "Input changed · recorded result retained" : state.inputLabels[state.mode];
  }
  function switchMode(mode) {
    state.inputs[state.mode] = $("config-editor").value;
    state.mode = mode;
    for (const candidate of ["simulation", "experiment"]) {
      const active = candidate === mode;
      $(candidate + "-tab").setAttribute("aria-selected", String(active));
      $(candidate + "-tab").tabIndex = active ? 0 : -1;
    }
    $("editor-panel").setAttribute("aria-labelledby", mode + "-tab");
    const experiment = mode === "experiment";
    $("editor-label").textContent = experiment ? "Scheduling comparison plan" : "Production configuration";
    $("schema-label").textContent = experiment ? "production.experiment.config.v1" : "production.config.v1";
    $("editor-help").textContent = experiment ? "Declare verification cases, expectations, policies and seed sets. Exploration runs only after verification passes. The engine validates the exact JSON text." : "Define resources, labor, capacity, process stages, jobs and risk in JSON. The engine validates the exact text.";
    $("run-button").textContent = experiment ? "Run comparison" : "Run production";
    $("load-input").textContent = experiment ? "Open plan" : "Open configuration";
    $("export-input").textContent = experiment ? "Export plan" : "Export configuration";
    $("config-editor").value = state.inputs[mode];
    clearError(); updateEditorState();
  }
  function setBusy(busy) {
    state.busy = busy;
    $("run-button").disabled = busy;
    $("cancel-button").disabled = !busy;
  }
  function disposeWorker() {
    if (state.worker) state.worker.terminate();
    if (state.workerURL) URL.revokeObjectURL(state.workerURL);
    state.worker = null; state.workerURL = null;
  }
  function finishError(message) {
    state.pending = null; setBusy(false); disposeWorker();
    $("run-status").textContent = "Run failed · last completed result retained";
    error(message);
  }
  function run() {
    if (state.busy) return;
    stopPlayback(); clearError();
    const raw = $("config-editor").value;
    state.inputs[state.mode] = raw;
    const request = {id:++state.sequence, kind:state.mode, text:raw};
    state.pending = request;
    setBusy(true);
    $("run-status").textContent = state.mode === "experiment" ? "Verifying and recording comparison runs…" : "Recording the complete horizon…";
    try {
      if (!state.worker) {
        state.workerURL = URL.createObjectURL(new Blob([workerSource($("production-runtime").textContent)], {type:"application/javascript"}));
        state.worker = new Worker(state.workerURL);
        const activeWorker = state.worker;
        state.worker.onerror = (event) => { event.preventDefault(); if (state.worker !== activeWorker) return; finishError(event.message || "The local engine could not start."); };
        state.worker.onmessage = (event) => {
          if (state.worker !== activeWorker) return;
          const response = event.data || {};
          if (response.ready === false) { finishError(response.error || "The local engine could not start."); return; }
          if (response.ready === true || !state.pending || response.id !== state.pending.id) return;
          const completed = state.pending;
          if (!response.ok) { finishError(response.error || "The engine rejected the input."); return; }
          state.pending = null; setBusy(false);
          state.recordedInput = completed.text; state.recordedKind = completed.kind;
          if (completed.kind === "experiment") {
            state.report = response.result; renderExperiment(state.report);
            const first = state.report.runs[0];
            if (first) showResult(first.result, first); else { state.result = null; $("results-section").hidden = true; }
            $("run-status").textContent = state.report.status === "ok" ? "Comparison recorded · checks passed" : "Verification failed · diagnostic records retained";
          } else {
            state.report = null; $("experiment-section").hidden = true;
            showResult(response.result, null);
            $("run-status").textContent = "Complete horizon recorded";
          }
          updateEditorState();
        };
      }
      state.worker.postMessage(request);
    } catch (exception) { finishError(String(exception.message || exception)); }
  }
  function cancel() {
    if (!state.busy) return;
    ++state.sequence; state.pending = null; disposeWorker(); setBusy(false);
    $("run-status").textContent = "Canceled · last completed result retained";
  }

  function metric(label, value, note, amber) {
    const element = node("div", null, "metric" + (amber ? " amber" : ""));
    element.append(node("span", label, "metric-label"), node("strong", format(value), "metric-value"), node("span", note, "metric-note"));
    return element;
  }
  function showResult(result, runRecord) {
    stopPlayback(); state.result = result; state.tick = 1;
    $("results-section").hidden = false;
    const config = result.config; const summary = result.summary;
    const context = (runRecord ? runRecord.case_id + " / " + runRecord.phase + " · " : "") + readable(config.scheduling_policy) + " · seed " + format(config.seed) + " · " + format(config.horizon_ticks) + " ticks · " + config.time_unit;
    $("result-context").textContent = context;
    $("result-provenance").textContent = "Engine validated and recorded: " + context + ". The summary covers the entire horizon.";
    $("summary-metrics").replaceChildren(metric("Completed jobs", summary.completed_jobs, "of " + format(config.jobs.length) + " configured jobs"), metric("Unfinished jobs", summary.unfinished_jobs, "retained in every denominator", summary.unfinished_jobs > 0), metric("Missed deadlines", summary.missed_deadlines, "as observed within this horizon", summary.missed_deadlines > 0), metric("Compensation due", summary.compensation_due, config.currency + " · declared minor units", false));
    const jobs = bodyFor("job-table");
    for (const job of summary.jobs) {
      const status = cell(""); status.append(badge(job.status, job.status === "completed" ? "work" : "wait"));
      tableRow(jobs, [identityCell(job.id, job.process),cell(job.release_tick),cell(job.deadline_tick),cell(job.completed_tick === null ? "unfinished" : job.completed_tick),status,cell(job.missed_deadline)]);
    }
    const firstRow = result.rows[0];
    const stageCount = firstRow ? firstRow.stages.length : 0;
    const jobOptions = config.jobs.map((job) => ({value:job.id,label:job.id + " / " + job.process}));
    jobOptions.unshift({value:"*",label:"All jobs"});
    setOptions($("timeline-job"), jobOptions, stageCount > 24 ? config.jobs[0].id : "*");
    const resources = config.resources.map((resource) => ({value:resource.id,label:resource.id + " · " + resource.unit}));
    setOptions($("resource-chart-select"), resources, $("resource-chart-select").value);
    const pools = [];
    for (const kind of ["labor", "capacity"]) for (const pool of config[kind]) pools.push({value:kind + ":" + pool.id,label:pool.id + " / " + kind + " · " + pool.unit});
    setOptions($("pool-chart-select"), pools, $("pool-chart-select").value);
    $("tick-slider").max = String(result.rows.length); $("tick-slider").value = "1";
    renderLedger(result); renderInventory(config); renderTimeline(); renderTick();
  }
  function visibleStages(row) {
    const selected = $("timeline-job").value;
    return row.stages.filter((stage) => selected === "*" || stage.job === selected);
  }
  function renderTimeline() {
    if (!state.result) return;
    const rows = state.result.rows;
    const stages = visibleStages(rows[0]);
    const visible = stages.slice(0,60);
    const width = Math.max(720, Math.min(2400, 210 + rows.length * 16));
    const labelWidth = 205; const plotWidth = width - labelWidth - 16;
    const cellWidth = plotWidth / rows.length; const height = 35 + visible.length * 27;
    const svg = svgNode("svg", {viewBox:"0 0 " + width + " " + height, width, height, class:"timeline-svg",role:"img","aria-label":"Recorded stage timeline. Use the recorded tick slider for keyboard selection."});
    svg.append(svgNode("title", {}, "Stage states across " + rows.length + " recorded ticks"));
    const step = Math.max(1, Math.ceil(rows.length / 15));
    for (let t=1;t<=rows.length;t+=step) {
      const x = labelWidth + (t - .5) * cellWidth;
      svg.append(svgNode("line", {x1:x,y1:23,x2:x,y2:height-5,class:"tick-grid"}), svgNode("text",{x,y:16,"text-anchor":"middle"},t));
    }
    const rowMaps = rows.map((row) => new Map(row.stages.map((stage) => [stage.job + "/" + stage.stage,stage])));
    for (let i=0;i<visible.length;++i) {
      const stage = visible[i]; const y = 29 + i*27;
      const fullLabel = stage.job + " / " + stage.stage;
      const label = svgNode("text",{x:10,y:y+13,class:"stage-name"},fullLabel.length>32?fullLabel.slice(0,31)+"…":fullLabel);
      label.append(svgNode("title",{},fullLabel)); svg.append(label);
      let beginning = 0; let current = classifyStage(rowMaps[0].get(stage.job + "/" + stage.stage));
      for (let t=1;t<=rows.length;++t) {
        const next = t < rows.length ? classifyStage(rowMaps[t].get(stage.job + "/" + stage.stage)) : null;
        if (next !== current) {
          const rectangle = svgNode("rect",{x:labelWidth+beginning*cellWidth,y,width:Math.max(.3,(t-beginning)*cellWidth-.8),height:18,rx:2,class:"timeline-cell " + current});
          rectangle.append(svgNode("title",{},stage.job + " / " + stage.stage + " · ticks " + (beginning+1) + "–" + t + " · " + readable(current)));
          svg.append(rectangle); beginning = t; current = next;
        }
      }
    }
    const cursor = svgNode("line",{x1:0,y1:23,x2:0,y2:height-5,class:"selected-tick",id:"timeline-cursor"}); svg.append(cursor);
    svg.addEventListener("click", (event) => {
      const rectangle = svg.getBoundingClientRect();
      const x = (event.clientX-rectangle.left)/rectangle.width*width;
      if (x < labelWidth) return;
      setTick(Math.max(1,Math.min(rows.length,Math.floor((x-labelWidth)/cellWidth)+1)));
    });
    $("timeline-container").replaceChildren(svg);
    $("timeline-note").textContent = stages.length > 60 ? "Showing 60 of " + stages.length + " stages. Choose a job to inspect its complete timeline. Select a tick or use the controls below." : "Showing " + stages.length + " stages. Select a tick on the timeline or use the recorded tick controls below.";
    svg.dataset.labelWidth = String(labelWidth); svg.dataset.cellWidth = String(cellWidth);
  }
  function setTick(tick) {
    if (!state.result) return;
    state.tick = Math.max(1,Math.min(state.result.rows.length,Number(tick)));
    renderTick();
  }
  function renderTick() {
    const result = state.result; if (!result) return;
    const row = selectedRow(result,state.tick); if (!row) return;
    $("tick-label").textContent = state.tick + " / " + result.rows.length;
    $("tick-slider").value = String(state.tick);
    $("tick-heading").textContent = "Tick " + state.tick + " · " + result.config.time_unit;
    $("tick-compensation").textContent = "Due this tick: " + format(row.compensation_due) + " " + result.config.currency + " minor units";
    $("tick-back").disabled = state.tick <= 1; $("tick-forward").disabled = state.tick >= result.rows.length;
    const svg = $("timeline-container").querySelector("svg");
    if (svg) { const x = Number(svg.dataset.labelWidth)+(state.tick-.5)*Number(svg.dataset.cellWidth); const cursor = $("timeline-cursor"); cursor.setAttribute("x1",String(x)); cursor.setAttribute("x2",String(x)); }
    const stages = bodyFor("stage-table");
    for (const stage of visibleStages(row)) {
      const condition = cell(""); condition.append(badge(stage.reason,classifyStage(stage)));
      tableRow(stages,[identityCell(stage.job,stage.stage),cell(stage.work_this_tick),cell(format(stage.work_completed)+" / "+format(stage.work_required)),cell(readable(stage.status)),condition,cell(format(stage.start_tick)+" / "+format(stage.completed_tick))]);
    }
    const stocks = bodyFor("stock-table");
    for (const resource of result.config.resources) tableRow(stocks,[cell(resource.id),cell(resource.unit),cell(row.resources[resource.id])]);
    const pools = bodyFor("pool-table");
    for (const kind of ["labor","capacity"]) for (const pool of result.config[kind]) {
      const observed = row[kind][pool.id];
      tableRow(pools,[identityCell(pool.id,kind),cell(pool.unit),cell(observed.available),cell(observed.used),cell(kind === "labor" ? observed.compensation_due + " " + result.config.currency : null)]);
    }
    renderResourceChart(); renderPoolChart(); renderEvents(row.tick);
  }
  function lineChart(container, series, unit, initial) {
    const result = state.result;
    const width=560, height=165, left=64, right=16, top=18, bottom=35;
    const start = initial === undefined ? 1 : 0;
    const horizon = result.rows.length;
    const x = (tick) => left+(tick-start)/Math.max(1,horizon-start)*(width-left-right);
    const maximum = Math.max(1,...series.flatMap((line) => line.values.map((entry) => entry.value)));
    const y = (quantity) => height-bottom-quantity/maximum*(height-top-bottom);
    const svg=svgNode("svg",{viewBox:"0 0 "+width+" "+height,class:"series-svg",role:"img","aria-label":series.map((line) => line.label).join(" and ")+" in "+unit+" across recorded ticks"});
    svg.append(svgNode("title",{},"Declared unit: "+unit));
    for (const fraction of maximum<=1?[0,1]:[0,.5,1]) {
      const quantity=Math.round(maximum*fraction); const position=y(quantity);
      svg.append(svgNode("line",{x1:left,y1:position,x2:width-right,y2:position,class:"chart-grid"}),svgNode("text",{x:left-7,y:position+4,"text-anchor":"end"},format(quantity)));
    }
    for (const tick of start===horizon?[horizon]:[start,horizon]) svg.append(svgNode("text",{x:x(tick),y:height-12,"text-anchor":"middle"},"tick "+tick));
    for (const line of series) {
      const points=line.values.map((entry)=>x(entry.tick)+","+y(entry.value)).join(" ");
      svg.append(svgNode("polyline",{points,class:"series-line "+(line.className||"")}));
      const selected=line.values.find((entry)=>entry.tick===state.tick);
      if (selected) { const point=svgNode("circle",{cx:x(selected.tick),cy:y(selected.value),r:3,class:"series-point"}); point.append(svgNode("title",{},line.label+": "+format(selected.value)+" "+unit)); svg.append(point); }
    }
    svg.append(svgNode("line",{x1:x(state.tick),y1:top,x2:x(state.tick),y2:height-bottom,class:"selected-tick"}));
    container.replaceChildren(svg);
  }
  function renderResourceChart() {
    const result=state.result; const id=$("resource-chart-select").value;
    const definition=result.config.resources.find((resource)=>resource.id===id);
    if (!definition) { $("resource-chart").replaceChildren(node("p","No resource is selected.","chart-empty")); return; }
    const values=[{tick:0,value:definition.initial_quantity},...result.rows.map((row)=>({tick:row.tick,value:row.resources[id]}))];
    lineChart($("resource-chart"),[{label:id+" closing stock",values}],definition.unit,definition.initial_quantity);
    $("resource-chart").append(node("p",id+" · "+definition.unit+" · tick 0 is the opening stock","series-key"));
  }
  function renderPoolChart() {
    const result=state.result; const selected=$("pool-chart-select").value;
    if (!selected) { $("pool-chart").replaceChildren(node("p","No labor or capacity pools are configured.","chart-empty")); return; }
    const delimiter=selected.indexOf(":"); const kind=selected.slice(0,delimiter); const id=selected.slice(delimiter+1);
    const definition=result.config[kind].find((pool)=>pool.id===id);
    const available=result.rows.map((row)=>({tick:row.tick,value:row[kind][id].available}));
    const used=result.rows.map((row)=>({tick:row.tick,value:row[kind][id].used}));
    lineChart($("pool-chart"),[{label:id+" available",values:available,className:"available"},{label:id+" used",values:used}],definition.unit);
    const key=node("p","Available · dashed", "series-key"); key.append(node("span","Used · solid · "+definition.unit,"key-used")); $("pool-chart").append(key);
  }
  function renderEvents(tick) {
    const events=state.result.events.filter((event)=>event.tick===tick);
    $("event-heading").textContent="Events at tick "+tick+" · "+events.length;
    const list=$("event-list"); list.replaceChildren();
    if (!events.length) { list.append(node("p","No events recorded at this tick.","muted")); return; }
    for (const event of events) {
      const entry=node("article",null,"event-entry"); entry.append(node("h4",readable(event.kind || event.type || "event")));
      const fields=node("dl",null,"event-fields");
      for (const [key,value] of Object.entries(event)) {
        if (key==="tick" || key==="kind" || key==="type") continue;
        const pair=node("div"); pair.append(node("dt",readable(key)+":"),node("dd",typeof value==="object" && value!==null ? JSON.stringify(value) : format(value))); fields.append(pair);
      }
      entry.append(fields); list.append(entry);
    }
  }
  function renderLedger(result) {
    const body=bodyFor("resource-ledger");
    for (const [id,resource] of Object.entries(result.summary.resources)) tableRow(body,[identityCell(id,resource.unit),cell(resource.opening),cell(resource.produced),cell(resource.consumed),cell(resource.lost),cell(resource.closing),cell(resource.unrealized_output)]);
  }
  function renderInventory(config) {
    const container=$("validated-inventory"); container.replaceChildren();
    const grid=node("div",null,"inventory-grid");
    for (const kind of ["resources","labor","capacity"]) {
      const section=node("div"); section.append(node("h4",readable(kind)+" · "+config[kind].length));
      const list=node("ul");
      for (const item of config[kind]) {
        const detail=kind==="resources"?"opening "+format(item.initial_quantity):format(item.units_per_tick)+" per tick";
        const obligation=kind==="labor"?" · due "+format(item.compensation_due_per_tick)+" "+config.currency+" minor units per tick":"";
        list.append(node("li",item.id+" · "+item.unit+" · "+detail+obligation));
      }
      if (!config[kind].length) list.append(node("li","None configured"));
      section.append(list); grid.append(section);
    }
    container.append(grid);
    for (const process of config.processes) {
      const section=node("div",null,"process-definition"); section.append(node("h4",process.id+" · "+process.stages.length+" stages"));
      section.append(node("pre",JSON.stringify(process,null,2))); container.append(section);
    }
    const risks=node("div",null,"process-definition"); risks.append(node("h4","Declared shocks and availability risks"),node("pre",JSON.stringify({shocks:config.shocks,availability_risks:config.availability_risks},null,2))); container.append(risks);
  }

  function renderExperiment(report) {
    $("experiment-section").hidden=false;
    $("experiment-status").textContent=report.status==="ok"?"Checks passed":"Verification failed";
    $("experiment-status").className="tag "+(report.status==="ok"?"passed":"failed");
    $("experiment-description").textContent=report.plan.id+" · "+report.plan.description;
    const passed=report.checks.filter((check)=>check.passed).length;
    $("experiment-check-summary").replaceChildren(node("span",format(report.runs.length)+" recorded runs"),node("span",format(passed)+" / "+format(report.checks.length)+" checks passed","check-pass"),node("span",format(report.checks.length-passed)+" checks failed",report.checks.length===passed?"":"check-fail"));
    $("skipped-cases").hidden=!report.skipped_cases.length;
    $("skipped-cases").textContent="Exploration skipped after verification failure: "+report.skipped_cases.join(", ")+". Verification records remain available.";
    const outcomes=bodyFor("outcome-table");
    for (const row of outcomeRows(report)) {
      const frequencies=Object.entries(row.completion_tick_counts).map(([tick,count])=>(tick==="unfinished"?"unfinished":"tick "+tick)+": "+count+" / "+row.sample_count).join("; ");
      tableRow(outcomes,[identityCell(row.case_id,row.phase),cell(readable(row.policy)),cell(row.job),cell(row.sample_count),cell(row.completed_count+" / "+row.sample_count),cell(row.unfinished_count+" / "+row.sample_count),cell(row.missed_deadline_count+" / "+row.sample_count),cell(frequencies)]);
    }
    const checks=bodyFor("checks-table");
    for (const check of report.checks) {
      const outcome=cell(""); outcome.append(node("span",check.passed?"passed":"failed",check.passed?"check-pass":"check-fail"));
      const identity=check.case_id || check.short_case+" → "+check.long_case;
      const policy=check.policy || (check.policies||[]).map(readable).join(" / ");
      const detail={};
      for (const [key,value] of Object.entries(check)) if (!["kind","case_id","short_case","long_case","policy","policies","seed","passed"].includes(key)) detail[key]=value;
      const description=cell(""); description.append(node("code",JSON.stringify(detail),"check-data"));
      tableRow(checks,[cell(readable(check.kind)),cell(identity),cell(readable(policy)+" · seed "+check.seed),outcome,description]);
    }
    $("checks-heading").textContent="Verification and consistency checks · "+passed+" / "+report.checks.length+" passed";
    setOptions($("experiment-run"),report.runs.map((run,index)=>({value:String(index),label:run.case_id+" / "+run.phase+" · "+readable(run.policy)+" · seed "+run.seed})),"0");
    const interpretation=$("experiment-interpretation"); interpretation.replaceChildren();
    for (const statement of report.interpretation || []) interpretation.append(node("li",statement));
    updateRunExpectation(report.runs[0]);
  }
  function updateRunExpectation(runRecord) {
    $("run-expectation").textContent=!runRecord?"No runs recorded":runRecord.expectation_passed===null?"Exploration · no predicted outcome assertion":runRecord.expectation_passed?"Declared expectations passed":"Declared expectations failed";
  }
  function stopPlayback() {
    if (state.playTimer) clearInterval(state.playTimer);
    state.playTimer=null; $("play-button").textContent="Play ticks";
  }
  function togglePlayback() {
    if (state.playTimer) { stopPlayback(); return; }
    if (!state.result) return;
    if (state.tick===state.result.rows.length) setTick(1);
    $("play-button").textContent="Pause ticks";
    state.playTimer=setInterval(()=>{ if (state.tick>=state.result.rows.length) { stopPlayback(); return; } setTick(state.tick+1); },700);
  }
  function download(text,filename,type) {
    const url=URL.createObjectURL(new Blob([text],{type:type||"application/json;charset=utf-8"}));
    const anchor=node("a"); anchor.href=url; anchor.download=filename; document.body.append(anchor); anchor.click(); anchor.remove();
    setTimeout(()=>URL.revokeObjectURL(url),1000);
  }

  for (const mode of ["simulation","experiment"]) {
    $(mode+"-tab").addEventListener("click",()=>switchMode(mode));
    $(mode+"-tab").addEventListener("keydown",(event)=>{ if (["ArrowLeft","ArrowRight","Home","End"].includes(event.key)) { event.preventDefault(); const target=event.key==="Home"?"simulation":event.key==="End"?"experiment":mode==="simulation"?"experiment":"simulation"; switchMode(target); $(target+"-tab").focus(); } });
  }
  $("config-editor").addEventListener("input",()=>{ state.inputLabels[state.mode]="Edited input"; updateEditorState(); });
  $("run-button").addEventListener("click",run);
  $("cancel-button").addEventListener("click",cancel);
  $("reset-input").addEventListener("click",()=>{ $("config-editor").value=$(state.mode==="simulation"?"default-config":"default-plan").textContent.trim(); state.inputLabels[state.mode]="Example "+(state.mode==="simulation"?"configuration":"scheduling plan"); updateEditorState(); clearError(); });
  $("load-input").addEventListener("click",()=>$("input-file").click());
  $("input-file").addEventListener("change",async()=>{
    const file=$("input-file").files[0]; if (!file) return;
    const mode=state.mode;
    try {
      if (file.size>2*1024*1024) throw new Error("Input exceeds the engine's 2 MiB JSON limit.");
      const raw=new TextDecoder("utf-8",{fatal:true,ignoreBOM:true}).decode(await file.arrayBuffer());
      state.inputs[mode]=raw; state.inputLabels[mode]=file.name;
      if (state.mode===mode) { $("config-editor").value=raw; updateEditorState(); clearError(); }
    } catch (exception) { error(String(exception.message || exception)); }
    $("input-file").value="";
  });
  $("export-input").addEventListener("click",()=>download($("config-editor").value,state.mode==="simulation"?"production-config.cfg":"production-experiment-plan.cfg"));
  $("export-result").addEventListener("click",()=>{ const recorded=state.report || state.result; if (recorded) download(JSON.stringify(recorded,null,2),state.report?"production-experiment-result.json":"production-result.json"); });
  $("export-run-input").addEventListener("click",()=>{ if (state.recordedInput!==null) download(state.recordedInput,state.recordedKind==="experiment"?"production-recorded-plan.cfg":"production-recorded-config.cfg"); });
  $("timeline-job").addEventListener("change",()=>{ renderTimeline(); renderTick(); });
  $("tick-slider").addEventListener("input",()=>{ stopPlayback(); setTick($("tick-slider").value); });
  $("tick-back").addEventListener("click",()=>{ stopPlayback(); setTick(state.tick-1); });
  $("tick-forward").addEventListener("click",()=>{ stopPlayback(); setTick(state.tick+1); });
  $("play-button").addEventListener("click",togglePlayback);
  $("resource-chart-select").addEventListener("change",renderResourceChart);
  $("pool-chart-select").addEventListener("change",renderPoolChart);
  $("experiment-run").addEventListener("change",()=>{ if (!state.report) return; const record=state.report.runs[Number($("experiment-run").value)]; if (record) { showResult(record.result,record); updateRunExpectation(record); } });
  document.addEventListener("visibilitychange",()=>{ if (document.hidden) stopPlayback(); });
  window.addEventListener("pagehide",()=>{ stopPlayback(); disposeWorker(); });
  $("config-editor").value=state.inputs.simulation; updateEditorState(); run();
})();
