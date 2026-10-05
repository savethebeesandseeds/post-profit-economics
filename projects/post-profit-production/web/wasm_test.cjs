// SPDX-License-Identifier: MIT
// Run inside the documented development container:
// node wasm_test.cjs <runtime.js> <native simulation> <native experiment> [plan.cfg]
// This checks the actual compiled C++ boundary. It does not open a browser.
'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const {execFileSync} = require('node:child_process');

const MAX_INPUT = 2 * 1024 * 1024;
const BUFFER_LIMIT = 512 * 1024 * 1024;
const project = path.resolve(__dirname, '..');
const nativeSimulation = path.resolve(process.argv[3]);
const nativeExperiment = path.resolve(process.argv[4]);
const planPath = path.resolve(process.argv[5] || path.join(project, 'configs/scheduling-comparison.cfg'));
const riskZero = () => ({delay_probability_bps:0, delay_age_increment_bps:0, delay_ticks:1,
  output_loss_probability_bps:0, output_loss_age_increment_bps:0, output_loss_bps:0});
function basic() {
  return {schema_version:'production.config.v1', time_unit:'research tick', currency:'EUR',
    horizon_ticks:8, seed:41, scheduling_policy:'fifo',
    resources:[{id:'input',unit:'declared input unit',initial_quantity:100},
      {id:'output',unit:'declared output unit',initial_quantity:0}],
    labor:[{id:'labor',unit:'declared labour unit',units_per_tick:2,compensation_due_per_tick:1000}],
    capacity:[{id:'capacity',unit:'declared capacity unit',units_per_tick:8}],
    processes:[{id:'process',stages:[{id:'stage',depends_on:[],work_units:4,max_work_per_tick:2,
      inputs:{input:2},outputs:{output:1},labor:{labor:1},capacity:{capacity:1},risk:riskZero()}]}],
    jobs:[{id:'job-late',process:'process',release_tick:1,deadline_tick:8},
      {id:'job-early',process:'process',release_tick:1,deadline_tick:2}],shocks:[],availability_risks:[]};
}
function execute(program, args, input) {
  try {return {code:0, stdout:execFileSync(program,args,{input,encoding:'utf8',maxBuffer:BUFFER_LIMIT})};}
  catch (error) {
    if (error.status === null || error.status === undefined) throw error;
    return {code:error.status,stdout:String(error.stdout || ''),stderr:String(error.stderr || '')};
  }
}
function nativeCommand(raw) {
  const response=execute(nativeSimulation,[],raw);
  assert(response.stdout.trim(),'native command returns its JSON envelope');
  return JSON.parse(response.stdout);
}
function nativePlan(raw) {
  const temporary=fs.mkdtempSync(path.join(os.tmpdir(),'production-wasm-test-'));
  const file=path.join(temporary,'plan.cfg');
  try {
    fs.writeFileSync(file,raw);
    const response=execute(nativeExperiment,['--plan',file]);
    return {...response,result:response.stdout.trim() ? JSON.parse(response.stdout) : null};
  } finally {fs.rmSync(temporary,{recursive:true,force:true});}
}
function verifyPhysicalAccounts(result) {
  assert.equal(result.status,'ok'); assert.equal(result.schema_version,'production.sim.v1');
  const config=result.config;
  assert.equal(result.rows.length,config.horizon_ticks,'every configured tick is observed');
  const totalDue=config.horizon_ticks*config.labor.reduce((sum,pool)=>sum+pool.compensation_due_per_tick,0);
  assert.equal(result.summary.compensation_due,totalDue,'fixed obligations include idle or absent labour');
  assert.equal(result.summary.compensation_settlement,'not_modeled','obligations are not presented as paid');
  for (const [index,row] of result.rows.entries()) {
    assert.equal(row.tick,index+1);
    for (const pool of config.labor) {
      const state=row.labor[pool.id];
      assert(state.used>=0 && state.used<=state.available);
      assert.equal(state.compensation_due,pool.compensation_due_per_tick);
    }
    for (const pool of config.capacity) {
      const state=row.capacity[pool.id]; assert(state.used>=0 && state.used<=state.available);
    }
    for (const resource of config.resources) assert(row.resources[resource.id]>=0);
    for (const stage of row.stages) {
      assert(stage.work_this_tick>=0 && stage.work_completed>=0);
      assert(stage.work_completed<=stage.work_required);
      assert(['pending','active','completed'].includes(stage.status));
    }
  }
  for (const resource of config.resources) {
    const account=result.summary.resources[resource.id];
    assert.equal(account.opening+account.produced-account.consumed-account.lost,account.closing,
      'physical resource balance for '+resource.id);
    assert.equal(account.closing,result.rows.at(-1).resources[resource.id]);
    for (const key of ['opening','produced','consumed','lost','closing','unrealized_output'])
      assert(account[key]>=0,'nonnegative '+key);
  }
  assert.equal(result.summary.completed_jobs+result.summary.unfinished_jobs,config.jobs.length);
  assert.equal(result.summary.jobs.filter(job=>job.status==='unfinished').length,result.summary.unfinished_jobs);
}
async function main() {
  assert(process.argv[2] && process.argv[3] && process.argv[4],
    'supply runtime, native simulation and native experiment paths');
  const factory=require(path.resolve(process.argv[2]));
  const module=await factory();
  // Use the exact ccall string boundary used by the delivered Worker, including
  // the accepted near-limit input. A separate JS model or alternate parser is not used.
  const invoke=(name,raw)=>JSON.parse(module.ccall(name,'string',['string'],[raw]));
  const defaults=invoke('production_run','{"op":"defaults"}');
  assert.deepEqual(defaults,nativeCommand('{"op":"defaults"}'));
  assert.equal(defaults.config.schema_version,'production.config.v1');
  const fixtures=[];
  const add=(name,edit,verify=()=>{})=>{
    const config=basic(); edit(config);
    fixtures.push({name,raw:JSON.stringify({op:'simulate',config}),status:'ok',verify});
  };
  const reject=(name,edit)=>{add(name,edit);fixtures.at(-1).status='error';};
  fixtures.push({name:'bundled research configuration',raw:JSON.stringify({op:'simulate',config:defaults.config}),status:'ok',verify:()=>{}});
  add('FIFO labour contention',()=>{},result=>{
    assert.deepEqual(result.summary.jobs.map(job=>job.completed_tick),[2,4]);
    assert.equal(result.summary.missed_deadlines,1);
  });
  add('earliest deadline labour contention',config=>{config.scheduling_policy='earliest_deadline';},result=>{
    assert.deepEqual(result.summary.jobs.map(job=>job.completed_tick),[4,2]);
    assert.equal(result.summary.missed_deadlines,0);
  });
  add('capacity contention',config=>{config.labor[0].units_per_tick=8;config.capacity[0].units_per_tick=2;},result=>{
    assert(result.rows.flatMap(row=>row.stages).some(stage=>stage.reason==='awaiting_capacity'));
  });
  add('empty resource stock',config=>{config.resources[0].initial_quantity=0;},result=>{
    assert.equal(result.summary.completed_jobs,0);assert.equal(result.summary.resources.input.consumed,0);
    assert(result.rows.every(row=>row.stages.every(stage=>stage.reason==='awaiting_resources')));
  });
  add('partial bundle stock',config=>{config.resources[0].initial_quantity=2;},result=>{
    assert.equal(result.summary.completed_jobs,1);assert.equal(result.summary.resources.input.consumed,2);
    assert.equal(result.summary.unfinished_jobs,1);
  });
  add('dependency outputs available next tick',config=>{
    config.jobs.splice(1);config.labor[0].units_per_tick=8;
    const second=structuredClone(config.processes[0].stages[0]);
    Object.assign(second,{id:'second',depends_on:['stage'],work_units:1,max_work_per_tick:1,
      inputs:{output:1},outputs:{}});config.processes[0].stages.push(second);
  },result=>{
    const first=result.events.find(event=>event.type==='stage_completed' && event.stage==='stage');
    const second=result.events.find(event=>event.type==='stage_started' && event.stage==='second');
    assert.equal(second.tick,first.tick+1,'completion cannot supply another stage within the same tick');
  });
  add('full shock interruption',config=>{config.shocks=[{id:'pause',start_tick:2,end_tick:3,
    labor_factors:{labor:0},capacity_factors:{},resource_losses:{}}];},result=>{
    assert.deepEqual(result.summary.jobs.map(job=>job.completed_tick),[4,6]);
    assert.equal(result.rows[1].labor.labor.available,0);
  });
  add('clipped opening resource loss',config=>{
    config.resources[0].initial_quantity=10;config.shocks=[{id:'loss',start_tick:1,end_tick:1,
      labor_factors:{},capacity_factors:{},resource_losses:{input:20}}];
  },result=>{assert.equal(result.summary.resources.input.lost,10);assert.equal(result.summary.resources.input.closing,0);});
  add('multi-tick stage delay with retained input',config=>{
    config.jobs.splice(1);Object.assign(config.processes[0].stages[0].risk,
      {delay_probability_bps:10000,delay_age_increment_bps:-10000,delay_ticks:2});
  },result=>{
    assert.equal(result.summary.jobs[0].completed_tick,4);
    assert.equal(result.rows.filter(row=>row.stages[0].reason==='risk_delay').length,2);
    assert.equal(result.summary.resources.input.consumed,2);
    assert.equal(result.events.filter(event=>event.type==='stage_delay').length,1);
  });
  add('output loss leaves completed work without invented output',config=>{
    config.jobs.splice(1);Object.assign(config.processes[0].stages[0].risk,
      {output_loss_probability_bps:10000,output_loss_bps:10000});
  },result=>{
    assert.equal(result.summary.completed_jobs,1);assert.equal(result.summary.resources.output.produced,0);
    assert.equal(result.summary.resources.output.unrealized_output,1);
  });
  add('availability risk before stage start',config=>{
    config.availability_risks=[{id:'absence',target_kind:'labor',target_id:'labor',probability_bps:10000,
      duration_ticks:2,start_tick:1,end_tick:8}];
  },result=>{
    assert.equal(result.summary.completed_jobs,0);assert.equal(result.summary.resources.input.consumed,0);
    assert(result.rows.every(row=>row.labor.labor.available===0));
    assert.equal(result.events.filter(event=>event.type==='availability_interruption').length,8);
  });
  add('risk seed uint32 maximum and UTF-8 units',config=>{
    config.seed=4294967295;config.time_unit='é araştırma 🧶';config.resources[0].unit='élément';
    Object.assign(config.processes[0].stages[0].risk,{delay_probability_bps:2000,
      output_loss_probability_bps:2000,output_loss_age_increment_bps:250,output_loss_bps:5000});
  });
  add('horizon-censored active work',config=>{config.horizon_ticks=1;},result=>{
    assert.equal(result.summary.completed_jobs,0);assert.equal(result.summary.unfinished_jobs,2);
    assert.equal(result.summary.jobs[0].completed_tick,null);
    assert.equal(result.summary.resources.input.consumed,2,'started input remains consumed at the horizon');
  });
  add('unreleased jobs and fixed obligations',config=>{
    config.jobs.forEach(job=>{job.release_tick=20;job.deadline_tick=30;});
  },result=>{
    assert(result.rows.every(row=>row.stages.every(stage=>stage.reason==='awaiting_release')));
    assert.equal(result.summary.compensation_due,8000);
  });
  add('maximum horizon',config=>{config.horizon_ticks=1000;config.jobs.splice(1);});
  reject('unknown field',config=>{config.non_contract_field=1;});
  reject('fractional work value',config=>{config.processes[0].stages[0].work_units=4.5;});
  reject('duplicate job identifier',config=>{config.jobs[1].id=config.jobs[0].id;});
  reject('missing dependency',config=>{config.processes[0].stages[0].depends_on=['missing'];});
  reject('invalid resource reference',config=>{config.processes[0].stages[0].inputs={missing:1};});
  reject('boolean seed',config=>{config.seed=true;});
  const valid=JSON.stringify({op:'simulate',config:basic()});
  for (const [name,raw] of [
    ['duplicate command key','{"op":"defaults","op":"defaults"}'],
    ['escaped duplicate command key','{"op":"defaults","\\u006fp":"defaults"}'],
    ['fractional lexical integer',valid.replace('"horizon_ticks":8','"horizon_ticks":8.0')],
    ['exponent lexical integer',valid.replace('"seed":41','"seed":4.1e1')],
    ['nested duplicate key',valid.replace('"seed":41','"seed":41,"seed":42')],
    ['out of range numeric token',valid.replace('"seed":41','"seed":9007199254740993')],
    ['too deep','['.repeat(33)+'0'+']'.repeat(33)],['non-finite number','[1e10000]'],
    ['invalid null command','null'],['invalid JSON','{"op":"defaults",}']
  ]) fixtures.push({name,raw,status:'error',verify:()=>{}});
  const nearConfig=basic();nearConfig.time_unit='é';
  const nearBase=JSON.stringify({op:'simulate',config:nearConfig});
  const near=' '.repeat(MAX_INPUT-Buffer.byteLength(nearBase,'utf8'))+nearBase;
  assert.equal(Buffer.byteLength(near,'utf8'),MAX_INPUT);
  fixtures.push({name:'exact 2 MiB raw UTF-8 command',raw:near,status:'ok',verify:()=>{}});
  fixtures.push({name:'over 2 MiB command',raw:' '+near,status:'error',verify:()=>{}});
  for (const fixture of fixtures) {
    const actual=invoke('production_run',fixture.raw);
    assert.equal(actual.status,fixture.status,fixture.name+': '+JSON.stringify(actual.error));
    // Native CLI rejects oversize input before its model returns an envelope.
    if (Buffer.byteLength(fixture.raw,'utf8')<=MAX_INPUT)
      assert.deepEqual(actual,nativeCommand(fixture.raw),fixture.name+': exact native/WASM parity');
    assert.deepEqual(invoke('production_run',fixture.raw),actual,fixture.name+': deterministic replay');
    if (actual.status==='ok') verifyPhysicalAccounts(actual);
    fixture.verify(actual);
  }
  const rawPlan=fs.readFileSync(planPath,'utf8');
  const report=invoke('production_experiment_run',rawPlan);
  const nativeReport=nativePlan(rawPlan);
  assert.equal(nativeReport.code,0);assert.deepEqual(report,nativeReport.result,'complete protocol native/WASM parity');
  assert.deepEqual(invoke('production_experiment_run',rawPlan),report,'complete protocol deterministic replay');
  assert.equal(report.schema_version,'production.experiment.v1');assert.equal(report.status,'ok');
  assert.equal(report.runs.length,102);assert.equal(report.checks.length,281);
  assert(report.checks.every(check=>check.passed));assert.deepEqual(report.skipped_cases,[]);
  for (const run of report.runs) verifyPhysicalAccounts(run.result);
  const failedPlan=JSON.parse(rawPlan);
  failedPlan.verification_cases[0].expectations.fifo.result.summary.completed_jobs=999;
  const failedRaw=JSON.stringify(failedPlan);
  const failed=invoke('production_experiment_run',failedRaw), nativeFailure=nativePlan(failedRaw);
  assert.equal(nativeFailure.code,2);assert.deepEqual(failed,nativeFailure.result,'verification-failed report parity');
  assert.equal(failed.status,'verification_failed');
  assert.equal(failed.runs.filter(run=>run.phase==='exploration').length,0,'failed predictions gate stochastic exploration');
  assert.equal(failed.skipped_cases.length,failedPlan.exploration_cases.length);
  for (const raw of ['{"schema_version":"production.experiment.config.v1","schema_version":"production.experiment.config.v1"}',
    rawPlan.replace('"seed": 41','"seed": 41.0'), '{}','null',' '.repeat(MAX_INPUT+1)]) {
    const error=invoke('production_experiment_run',raw);assert.equal(error.status,'error','invalid plan returns an error envelope');
    assert.equal(typeof error.error.message,'string');
  }
  console.log(fixtures.length+' simulation fixtures agree with native C++; 102 recorded experiment runs and 281 checks agree exactly. Replay, physical accounts, failed-verification gating, strict raw tokens and 2 MiB UTF-8 input pass.');
}
main().catch(error=>{console.error(error);process.exitCode=1;});
