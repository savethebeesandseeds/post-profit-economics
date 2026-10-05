// SPDX-License-Identifier: MIT
// Run inside the documented development container:
// node standalone_test.cjs <standalone HTML> <native simulation> <native experiment> [review HTML]
// Executes the delivered Worker and pure view helpers in Node. It does not
// establish browser rendering, keyboard interaction or visual correctness.
'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const vm = require('node:vm');
const {Worker} = require('node:worker_threads');
const {execFileSync} = require('node:child_process');
const {createHash} = require('node:crypto');
const {inflateRawSync} = require('node:zlib');

const BUFFER_LIMIT=512*1024*1024;
const MAX_INPUT=2*1024*1024;
const project=path.resolve(__dirname,'..');
const root=path.resolve(project,'../..');
const pagePath=path.resolve(process.argv[2] || '');
const nativeSimulation=path.resolve(process.argv[3] || '');
const nativeExperiment=path.resolve(process.argv[4] || '');
const reviewPath=path.resolve(process.argv[5] || path.join(root,'.build/post-profit-production/web-review/post-profit-production.html'));
function script(page,id) {
  const expression=new RegExp('<script\\b(?=[^>]*\\bid="'+id+'")[^>]*>([\\s\\S]*?)<\\/script\\s*>','g');
  const matches=[...page.matchAll(expression)];assert.equal(matches.length,1,'one embedded '+id);
  return matches[0][1];
}
function htmlAttribute(value) {
  return value.replace(/&quot;/g,'"').replace(/&#x27;|&#39;/g,"'").replace(/&lt;/g,'<')
    .replace(/&gt;/g,'>').replace(/&amp;/g,'&');
}
function csp(page) {
  const tags=[...page.matchAll(/<meta\b[^>]*http-equiv="Content-Security-Policy"[^>]*>/gi)];
  assert.equal(tags.length,1,'one CSP delivery location');
  const value=tags[0][0].match(/\bcontent="([^"]*)"/)[1];
  return new Map(htmlAttribute(value).split(';').map(part=>part.trim()).filter(Boolean)
    .map(part=>{const [name,...tokens]=part.split(/\s+/);return [name,tokens];}));
}
function digestToken(text) {return "'sha256-"+createHash('sha256').update(text,'utf8').digest('base64')+"'";}
function extractBlock(source,start,end) {
  const first=source.indexOf(start),last=source.indexOf(end,first+start.length);
  assert(first>=0 && last>first,'delivered block '+start);
  return source.slice(first+start.length,last);
}
function zipFiles(page) {
  const encoded=page.match(/download="post-profit-production-source\.zip" href="data:application\/zip;base64,([A-Za-z0-9+/=]+)"/);
  assert(encoded,'corresponding source ZIP is embedded');
  const archive=Buffer.from(encoded[1],'base64');
  let end=archive.length-22;
  while (end>=Math.max(0,archive.length-65557) && archive.readUInt32LE(end)!==0x06054b50) end--;
  assert(end>=0 && archive.readUInt32LE(end)===0x06054b50,'source ZIP end record');
  const files=new Map();let cursor=archive.readUInt32LE(end+16);
  for(let index=0;index<archive.readUInt16LE(end+10);index++) {
    assert.equal(archive.readUInt32LE(cursor),0x02014b50,'source ZIP central entry');
    const nameLength=archive.readUInt16LE(cursor+28),extraLength=archive.readUInt16LE(cursor+30),commentLength=archive.readUInt16LE(cursor+32);
    const name=archive.subarray(cursor+46,cursor+46+nameLength).toString('utf8');
    assert(!files.has(name),'unique ZIP path');assert(!name.startsWith('/') && !name.split('/').includes('..'),'relative bounded ZIP path');
    const method=archive.readUInt16LE(cursor+10),compressedSize=archive.readUInt32LE(cursor+20),size=archive.readUInt32LE(cursor+24),offset=archive.readUInt32LE(cursor+42);
    assert.equal(archive.readUInt32LE(offset),0x04034b50,'source ZIP local entry');
    const start=offset+30+archive.readUInt16LE(offset+26)+archive.readUInt16LE(offset+28);
    const compressed=archive.subarray(start,start+compressedSize);
    assert([0,8].includes(method),'supported source ZIP compression');
    const bytes=method===8?inflateRawSync(compressed):compressed;
    assert.equal(bytes.length,size,'source ZIP uncompressed size');files.set(name,bytes);
    cursor+=46+nameLength+extraLength+commentLength;
  }
  return files;
}
function execute(program,args,input) {
  try{return {code:0,stdout:execFileSync(program,args,{input,encoding:'utf8',maxBuffer:BUFFER_LIMIT})};}
  catch(error) {
    if(error.status===null || error.status===undefined)throw error;
    return {code:error.status,stdout:String(error.stdout || ''),stderr:String(error.stderr || '')};
  }
}
function nativeCommand(raw) {
  const response=execute(nativeSimulation,[],raw);assert(response.stdout.trim(),'native simulator emits its envelope');
  return JSON.parse(response.stdout);
}
function nativePlan(raw) {
  const temporary=fs.mkdtempSync(path.join(os.tmpdir(),'production-page-test-'));
  try {
    const file=path.join(temporary,'plan.cfg');fs.writeFileSync(file,raw);
    const response=execute(nativeExperiment,['--plan',file]);
    assert(response.stdout.trim(),'native experiment emits a report');
    return {code:response.code,result:JSON.parse(response.stdout)};
  } finally {fs.rmSync(temporary,{recursive:true,force:true});}
}
async function main() {
  assert(process.argv[2] && process.argv[3] && process.argv[4],
    'supply standalone HTML, native simulation and native experiment paths');
  const page=fs.readFileSync(pagePath,'utf8'),review=fs.readFileSync(reviewPath,'utf8');
  const runtime=script(page,'production-runtime'),app=script(page,'production-app');
  const advertisedRuntimeDigest=page.match(/compiled runtime SHA-256: <code>([a-f0-9]{64})<\/code>/)?.[1];
  assert(advertisedRuntimeDigest,'compiled runtime digest supplied');
  assert.equal(createHash('sha256').update(runtime.replace(/<\\\/script/g,'</script'),'utf8').digest('hex'),advertisedRuntimeDigest,
    'advertised compiled runtime digest matches embedded bytes before HTML escaping');
  const rawConfig=script(page,'default-config'),rawPlan=script(page,'default-plan');
  const style=page.match(/<style\b[^>]*>([\s\S]*?)<\/style\s*>/)[1];
  assert(page.includes('type="text/plain" id="production-runtime"'),'runtime is inert until the Worker loads it');
  for(const id of ['default-config','default-plan'])assert(page.includes('type="application/json" id="'+id+'"'),'inert '+id);
  const executable=[...page.matchAll(/<script\b([^>]*)>/g)].filter(match=>!match[1].includes('type="text/plain"') && !match[1].includes('type="application/json"'));
  assert.equal(executable.length,1,'one executable app script');
  assert(executable[0][1].includes('id="production-app"'));
  const releasePolicy=csp(page),reviewPolicy=csp(review);
  assert.deepEqual(releasePolicy.get('script-src'),[digestToken(app),"'wasm-unsafe-eval'"],'actual app bytes are hash-authorized');
  assert.deepEqual(releasePolicy.get('style-src'),[digestToken(style)],'actual CSS bytes are hash-authorized');
  for(const policy of [releasePolicy,reviewPolicy]) {
    assert.deepEqual(policy.get('default-src'),["'none'"]);
    assert.deepEqual(policy.get('script-src-attr'),["'none'"]);
    assert.deepEqual(policy.get('style-src-attr'),["'none'"]);
    assert.deepEqual(policy.get('worker-src'),['blob:']);assert.deepEqual(policy.get('connect-src'),["'none'"]);
    assert(!policy.get('script-src').includes("'unsafe-inline'"));assert(!policy.get('script-src').includes("'unsafe-eval'"));
  }
  assert(!releasePolicy.has('style-src-elem'),'release style policy remains hash restricted');
  assert.deepEqual(reviewPolicy.get('style-src-elem'),["'self'","'unsafe-inline'"],'local review allows selector style elements only');
  assert(review.includes('<meta name="robots" content="noindex, nofollow">'));
  assert(!page.includes('<meta name="robots" content="noindex, nofollow">'));
  for(const [name,tokens] of releasePolicy)assert.deepEqual(reviewPolicy.get(name),tokens,'review preserves '+name);
  for(const id of ['production-app','production-runtime','default-config','default-plan'])assert.equal(script(review,id),script(page,id),'review retains identical '+id);
  const markup=page.replace(/(<script\b[^>]*>)[\s\S]*?(<\/script\s*>)/g,'$1$2')
    .replace(/(<style\b[^>]*>)[\s\S]*?(<\/style\s*>)/g,'$1$2');
  assert(!/<(?:script|link|img|iframe|audio|video)\b[^>]+(?:src|href)\s*=\s*["'](?:https?:|\/\/)/i.test(markup),'no remotely loaded page assets');
  assert(!/\son[a-z]+\s*=/i.test(markup),'no inline event attributes');
  assert(!/\sstyle\s*=/i.test(markup),'no inline style attributes');
  assert(!/\b(?:innerHTML|outerHTML|insertAdjacentHTML)\b/.test(app),'configured text is not inserted as executable markup');
  assert(!/\beval\s*\(|\bnew\s+Function\s*\(/.test(app),'app uses no JS evaluation workaround');
  assert(!/url\(\s*["']?(?:https?:|\/\/)/i.test(style),'CSS loads no external assets');
  const files=zipFiles(page),prefix='projects/post-profit-production/';
  for(const name of ['CMakeLists.txt','README.md','PRODUCTION_SPECIFICATION.md','build-wasm.sh','package-html.py',
    'configs/research.cfg','configs/scheduling-comparison.cfg','environment/README.md',
    'simulation/simulation.hpp','simulation/simulation.cpp','simulation/main.cpp','simulation/wasm.cpp','simulation/embed-config.py',
    'experiments/experiment.hpp','experiments/experiment.cpp','experiments/main.cpp',
    'web/index.template.html','web/app.js','web/style.css','web/wasm_test.cjs','web/standalone_test.cjs']) {
    assert(files.has(prefix+name),'bundled corresponding source '+name);
    assert.deepEqual(files.get(prefix+name),fs.readFileSync(path.join(project,name)),'bundled source matches current '+name);
  }
  const allowedOutside=new Set(['LICENSE','tools/price-optimization/third_party/nlohmann/json.hpp',
    'tools/price-optimization/third_party/nlohmann/LICENSE.MIT','tools/price-optimization/third_party/nlohmann/PROVENANCE.md']);
  for(const name of files.keys()) {
    assert(name.startsWith(prefix) || name.startsWith('runtime-notices/') || allowedOutside.has(name),
      'only production source and required MIT JSON/runtime material bundled: '+name);
    assert(!name.split('/').some(part=>['dist','runs','records','__pycache__','.build'].includes(part)),
      'private/generated data excluded: '+name);
  }
  for(const name of allowedOutside)assert(files.has(name),'required notice/dependency source '+name);
  assert([...files.keys()].some(name=>name.startsWith('runtime-notices/')),'runtime notices supplied');
  assert.deepEqual(JSON.parse(rawConfig),JSON.parse(files.get(prefix+'configs/research.cfg').toString('utf8')));
  assert.deepEqual(JSON.parse(rawPlan),JSON.parse(files.get(prefix+'configs/scheduling-comparison.cfg').toString('utf8')));
  const helpersSource=extractBlock(app,'// View model helpers begin','// View model helpers end');
  const helpers=vm.runInNewContext(helpersSource+'\n({classifyStage,outcomeRows,selectedRow,simulationEnvelope});',{TextEncoder});
  assert.equal(typeof helpers.simulationEnvelope,'function');
  const command=helpers.simulationEnvelope(rawConfig);
  assert(command.includes(rawConfig),'the exact configuration text reaches C++ validation');
  const nativeDefault=nativeCommand(command);assert.equal(nativeDefault.status,'ok');
  const report=nativePlan(rawPlan);assert.equal(report.code,0);assert.equal(report.result.runs.length,102);
  const handlerSource=extractBlock(app,'// Worker handler begin','// Worker handler end');
  const workerFactory=vm.runInNewContext(handlerSource+'\nworkerSource;',{TextEncoder});
  assert.equal(typeof workerFactory,'function','the delivered Worker source factory is extracted unchanged');
  // Only the browser Worker transport is adapted. The source, C++ runtime and
  // request handler are the exact bytes delivered by the standalone page.
  const adapter="const {parentPort}=require('node:worker_threads');globalThis.self=globalThis;self.postMessage=data=>parentPort.postMessage(data);parentPort.on('message',data=>self.onmessage({data}));\n";
  const worker=new Worker(adapter+workerFactory(runtime),{eval:true});
  let nextId=0;const pending=new Map();
  let readyResolve,readyReject;
  const ready=new Promise((resolve,reject)=>{readyResolve=resolve;readyReject=reject;});
  const timer=setTimeout(()=>readyReject(new Error('delivered Worker startup timed out')),30000);
  worker.on('message',message=>{
    if(message.ready){clearTimeout(timer);readyResolve();return;}
    const request=pending.get(message.id);assert(request,'Worker returned a known request id');
    clearTimeout(request.timer);pending.delete(message.id);request.resolve(message);
  });
  worker.on('error',error=>{clearTimeout(timer);readyReject(error);for(const request of pending.values()){clearTimeout(request.timer);request.reject(error);}pending.clear();});
  const request=(kind,text)=>new Promise((resolve,reject)=>{
    const id=++nextId;
    const deadline=setTimeout(()=>{pending.delete(id);reject(new Error('delivered Worker request timed out'));},30000);
    pending.set(id,{resolve,reject,timer:deadline});worker.postMessage({id,kind,text});
  });
  let failedReport;
  try {
    await ready;
    const actual=await request('simulation',rawConfig);assert.equal(actual.ok,true);
    assert.deepEqual(actual.result,nativeDefault,'delivered Worker default native parity');
    const again=await request('simulation',rawConfig);assert.deepEqual(again.result,actual.result,'delivered Worker replay');
    const bomConfig='\ufeff'+rawConfig;
    assert(helpers.simulationEnvelope(bomConfig).startsWith('\ufeff'),'configuration BOM remains a document marker at the command start');
    const bomResult=await request('simulation',bomConfig);assert.equal(bomResult.ok,true);
    assert.deepEqual(bomResult.result,nativeDefault,'single leading UTF-8 BOM does not alter configuration lexemes or results');
    const actualReport=await request('experiment',rawPlan);assert.equal(actualReport.ok,true);
    assert.deepEqual(actualReport.result,report.result,'delivered Worker full 102-run/281-check native parity');
    const badPrediction=JSON.parse(rawPlan);badPrediction.verification_cases[0].expectations.fifo.result.summary.completed_jobs=999;
    const failed=await request('experiment',JSON.stringify(badPrediction));
    failedReport=failed.result;
    assert.equal(failed.ok,true,'failed verification remains an inspectable research report');
    assert.equal(failed.result.status,'verification_failed');
    assert.equal(failed.result.runs.filter(run=>run.phase==='exploration').length,0);
    for(const raw of [rawConfig.replace('"seed": 41','"seed": 41.0'),
      rawConfig.replace('"seed": 41','"seed": 41,"seed":42'),'{"schema_version":"production.config.v1","schema_version":"production.config.v1"}',
      '{}','null','{bad json','\ufeff\ufeff'+rawConfig,' \ufeff'+rawConfig,
      rawConfig+'\u0000ignored',rawConfig+'}\u0000ignored']) {
      const error=await request('simulation',raw);assert.equal(error.ok,false,'raw invalid configuration is rejected');
      assert.equal(typeof error.error,'string');
    }
    const nullPlan=await request('experiment',rawPlan+'\u0000ignored');
    assert.equal(nullPlan.ok,false,'C string transport cannot truncate an invalid raw plan into an accepted plan');
    // The exported C-string API excludes actual embedded NUL. The Worker must
    // reject it before ccall; the native stream parser independently rejects
    // the same raw command. An escaped JSON control reaches C++ validation.
    for(const raw of [rawConfig+'\u0000ignored',rawConfig+'}\u0000ignored'])
      assert.equal(nativeCommand(helpers.simulationEnvelope(raw)).status,'error','native raw-NUL rejection');
    const escapedControl=JSON.stringify({...nativeDefault.config,time_unit:'\u0000'});
    assert(!escapedControl.includes('\u0000'),'JSON encodes the control instead of embedding a raw NUL');
    const escapedError=await request('simulation',escapedControl);
    assert.equal(escapedError.ok,false);assert(escapedError.error.includes('time_unit'),'escaped control reaches native field validation');
    for(const [kind,raw] of [['simulation',rawConfig+'\ud800'],['experiment',rawPlan+'\udc00']]) {
      const badUnicode=await request(kind,raw);assert.equal(badUnicode.ok,false);
      assert(badUnicode.error.includes('surrogate'),'unpaired UTF-16 cannot be silently altered by the C string encoder');
    }
    const nearConfig=structuredClone(nativeDefault.config);nearConfig.time_unit='é';
    const dense=JSON.stringify(nearConfig);
    const overhead=Buffer.byteLength(helpers.simulationEnvelope(''),'utf8');
    const near=' '.repeat(MAX_INPUT-overhead-Buffer.byteLength(dense,'utf8'))+dense;
    assert.equal(Buffer.byteLength(helpers.simulationEnvelope(near),'utf8'),MAX_INPUT);
    const nearResult=await request('simulation',near);assert.equal(nearResult.ok,true,'near-limit raw UTF-8 Worker input is accepted');
    assert.deepEqual(nearResult.result,nativeCommand(helpers.simulationEnvelope(near)));
    const oversized=await request('simulation',' '.repeat(MAX_INPUT+1));assert.equal(oversized.ok,false);
    const badKind=await request('unknown',rawConfig);assert.equal(badKind.ok,false,'unknown operation is rejected');
  } finally {clearTimeout(timer);for(const request of pending.values())clearTimeout(request.timer);await worker.terminate();}
  viewModelRegressions(helpers,nativeDefault,report.result);
  lifecycleRegressions(app,nativeDefault,failedReport,rawConfig,rawPlan,workerFactory);
  console.log('Standalone ZIP, applicable source/licenses, release/review CSP hashes and unchanged delivered Worker pass. Full 102-run/281-check protocol, raw-input rejection, replay and 2 MiB UTF-8 boundary agree with native C++; view helpers and request cancellation/stale responses checked. No browser or visual verification is claimed.');
}
function viewModelRegressions(helpers,result,report) {
  assert.equal(helpers.selectedRow(result,1).tick,1);
  assert.equal(helpers.selectedRow(result,result.config.horizon_ticks).tick,result.config.horizon_ticks);
  assert.equal(helpers.selectedRow(result,0),null,'out of range selections have no invented tick');
  assert.equal(helpers.selectedRow(result,result.config.horizon_ticks+1),null,'past-horizon selections have no invented tick');
  const before=JSON.stringify(result);
  const stages=result.rows.flatMap(row=>row.stages);
  for(const stage of stages)assert.equal(typeof helpers.classifyStage(stage),'string');
  // The completion tick includes real work. Only subsequent zero-work rows
  // belong in the already-completed timeline category.
  const completeProgress=stages.find(stage=>stage.status==='completed' && stage.work_this_tick>0);
  const laterCompleted=stages.find(stage=>stage.status==='completed' && stage.work_this_tick===0);
  assert(completeProgress && laterCompleted,'default fixture exercises completion and subsequent completed rows');
  assert.notEqual(helpers.classifyStage(completeProgress),helpers.classifyStage(laterCompleted));
  const outcomes=helpers.outcomeRows(report);assert(Array.isArray(outcomes));assert(outcomes.length>0);
  const expected=report.case_summaries.flatMap(summary=>Object.entries(summary.jobs).map(([job,observed])=>({
    case_id:summary.case_id,phase:summary.phase,policy:summary.policy,job,sample_count:summary.sample_count,
    completed_count:observed.completed_count,unfinished_count:observed.unfinished_count,
    missed_deadline_count:observed.missed_deadline_count,completion_tick_counts:observed.completion_tick_counts})));
  assert.deepEqual(JSON.parse(JSON.stringify(outcomes)),expected,'rendered outcomes preserve every recorded sample and frequency');
  for(const row of outcomes) {
    assert.equal(row.completed_count+row.unfinished_count,row.sample_count,'unfinished observations remain in each denominator');
    assert.equal(Object.values(row.completion_tick_counts).reduce((sum,count)=>sum+count,0),row.sample_count);
    assert.equal(row.completion_tick_counts.unfinished,row.unfinished_count,'explicit unfinished frequency includes zero');
  }
  assert(outcomes.some(row=>row.unfinished_count>0),'verification fixtures exercise unfinished outcomes');
  assert(outcomes.some(row=>row.phase==='exploration' && row.sample_count===20),'twenty-seed samples are not collapsed into selected observations');
  assert.equal(helpers.classifyStage({status:'pending',reason:'awaiting_release',start_tick:null,work_this_tick:0}),'unreleased');
  assert.equal(helpers.classifyStage({status:'pending',reason:'awaiting_resources',start_tick:null,work_this_tick:0}),'wait');
  assert.equal(helpers.classifyStage({status:'active',reason:'risk_delay',start_tick:1,work_this_tick:0}),'pause');
  assert.equal(helpers.classifyStage(completeProgress),'work');
  assert.equal(helpers.classifyStage(laterCompleted),'complete');
  assert.equal(JSON.stringify(result),before,'view helpers do not mutate completed records');
}
function lifecycleRegressions(app,result,failedReport,rawConfig,rawPlan,workerSource) {
  const slice=(start,end)=>{
    const first=app.indexOf(start),last=app.indexOf(end,first+start.length);
    assert(first>=0 && last>first,'delivered lifecycle functions '+start);return app.slice(first,last);
  };
  // Execute the delivered state and request lifecycle. Only the DOM property
  // boundary, asynchronous transport and renderer hand-off are substituted;
  // full C++ Worker execution and view-model helpers are checked above.
  const elements=new Map();
  const $=id=>{if(!elements.has(id))elements.set(id,{textContent:'',value:'',disabled:false,hidden:false});return elements.get(id);};
  $('default-config').textContent=rawConfig;$('default-plan').textContent=rawPlan;
  $('production-runtime').textContent='test transport runtime';
  const requests=[],workers=[],revoked=[],rendered=[],reports=[];
  let actualState;
  class PendingWorker {
    constructor(url){this.url=url;workers.push(this);}
    postMessage(message){requests.push({worker:this,message:structuredClone(message)});}
    terminate(){this.terminated=true;}
  }
  const context=vm.createContext({document:{getElementById:$},TextEncoder,Worker:PendingWorker,
    Blob:class {constructor(parts,options){this.parts=parts;this.options=options;}},
    URL:{createObjectURL:()=> 'blob:test-'+workers.length,revokeObjectURL:url=>revoked.push(url)},
    workerSource,stopPlayback:()=>{},
    renderExperiment:report=>reports.push(report),
    showResult:(record,run)=>{actualState.result=record;actualState.tick=1;rendered.push({record,run});}});
  const actual=vm.runInContext(slice('  const $ =','  function node(')+
    slice('  function error(','  function switchMode(')+
    slice('  function setBusy(','  function metric(')+'\n({state,run,cancel,updateEditorState});',context);
  actualState=actual.state;
  $('config-editor').value=actualState.inputs.simulation;
  assert(/updateEditorState\(\);\s*run\(\);\s*\}\)\(\);\s*$/.test(app),'the delivered startup automatically requests its default production run');
  actual.run();
  assert.equal(requests.length,1);assert.equal(actualState.result,null,'startup fabricates no result while C++ is pending');
  assert(actualState.busy && $('run-button').disabled && !$('cancel-button').disabled);
  const respond=(request,response)=>request.worker.onmessage({data:{id:request.message.id,...response}});
  const initial=requests[0];
  assert.equal(initial.message.kind,'simulation');assert.equal(initial.message.text,rawConfig.trim());
  respond(initial,{ready:true});assert.equal(actualState.result,null);
  respond(initial,{id:initial.message.id+100,ok:true,result});assert.equal(actualState.result,null,'mismatched response id is ignored');
  respond(initial,{ok:true,result:structuredClone(result)});
  assert(!actualState.busy);assert.deepEqual(actualState.result,result);
  assert.equal(actualState.recordedInput,initial.message.text);assert.equal(actualState.recordedKind,'simulation');
  assert.equal($('editor-state').textContent,'Matches the recorded input');
  const prior=actualState.result;
  $('config-editor').value=rawConfig+'\n ';
  actual.run();const editing=requests.at(-1);
  actual.run();assert.equal(requests.length,2,'busy state prevents competing runs');
  assert.equal(actualState.result,prior,'pending work retains the completed result');
  $('config-editor').value='{future edit';actual.updateEditorState();
  assert($('editor-state').textContent.includes('recorded result retained'));
  respond(editing,{ok:true,result:structuredClone(result)});
  assert.equal(actualState.recordedInput,editing.message.text,'completion records the requested input rather than later editor changes');
  assert.equal($('config-editor').value,'{future edit');
  assert($('editor-state').textContent.includes('recorded result retained'));
  const preserved=actualState.result,preservedText=actualState.recordedInput;
  actual.run();const canceled=requests.at(-1);actual.cancel();
  assert(canceled.worker.terminated && !actualState.busy && actualState.pending===null);
  assert.equal(actualState.result,preserved);assert.equal(actualState.recordedInput,preservedText);
  $('config-editor').value=rawConfig.trim();actual.run();const next=requests.at(-1),pending=actualState.pending;
  canceled.worker.onmessage({data:{ready:false,error:'stale initialization failure'}});
  canceled.worker.onerror({preventDefault(){},message:'stale worker error'});
  canceled.worker.onmessage({data:{id:next.message.id,ok:true,result:structuredClone(result)}});
  assert.equal(actualState.pending,pending,'a canceled worker cannot finish or fail the replacement request');
  assert(actualState.busy && actualState.worker===next.worker);assert.equal(actualState.result,preserved);
  assert.equal($('run-error').textContent,'');
  respond(next,{ok:false,error:'deliberate invalid input'});
  assert(!actualState.busy && actualState.worker===null && actualState.pending===null);
  assert.equal(actualState.result,preserved);assert.equal(actualState.recordedInput,preservedText);
  assert($('run-error').textContent.includes('deliberate invalid input'));
  actualState.mode='experiment';$('config-editor').value=JSON.stringify(failedReport.plan);
  actual.run();const comparison=requests.at(-1);
  respond(comparison,{ok:true,result:structuredClone(failedReport)});
  assert.equal(actualState.report.status,'verification_failed','failed predictions remain an inspectable report');
  assert.equal(actualState.report.runs.filter(run=>run.phase==='exploration').length,0);
  assert.equal(actualState.recordedKind,'experiment');assert.equal(actualState.recordedInput,comparison.message.text);
  assert($('run-status').textContent.includes('Verification failed'));
  assert.equal(reports.length,1);assert.equal(rendered.at(-1).run.case_id,failedReport.runs[0].case_id);
  actualState.mode='simulation';$('config-editor').value=rawConfig.trim();
  actual.run();respond(requests.at(-1),{ok:true,result:structuredClone(result)});
  assert.equal(actualState.report,null,'a new production run replaces the comparison report for export');
  assert($('experiment-section').hidden);assert.equal(actualState.recordedKind,'simulation');
  assert(revoked.length>=2,'cancellation and failed transport release their blob URLs');
  actual.cancel();assert(!actualState.busy,'canceling an idle view leaves it idle');
}
main().catch(error=>{console.error(error);process.exitCode=1;});
