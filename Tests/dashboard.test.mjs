import test from 'node:test';
import assert from 'node:assert/strict';
import vm from 'node:vm';
import {readFile} from 'node:fs/promises';
const source=await readFile(new URL('../web/app.js',import.meta.url),'utf8');
class Element {
 constructor(){this.textContent='';this.children=[];this.value='temperature';this.dataset={};this.classList={toggle(){}};}
 append(el){this.children.push(el);} replaceChildren(...els){this.children=els;} setAttribute(){} addEventListener(){} click(){}
}
const row=(patch={})=>({sample_id:'1234567890abcdef',sampled_at:new Date().toISOString(),received_at:new Date().toISOString(),o2_valid:true,co2_valid:true,sht_valid:true,oxygen:19.9,co2:850,temperature:23,humidity:80,...patch});
async function run(pages,configured=true){
 const els=new Map(),get=id=>{if(!els.has(id))els.set(id,new Element());return els.get(id);};
 const context=vm.createContext({window:{FRESHAGRO:configured?{supabaseUrl:'https://example.supabase.co',publishableKey:'public',deviceId:'freshagro-01',cycleMinutes:10}:{}},
 document:{getElementById:get,querySelectorAll:()=>[],createElement:()=>new Element(),createElementNS:(_,tag)=>{const el=new Element();el.tag=tag;return el;},addEventListener(){},hidden:false},
 URL,Blob,AbortSignal,Date,console,setInterval(){},setTimeout(){},fetch:async()=>{const data=pages.shift();if(data instanceof Error)throw data;return {ok:true,json:async()=>data??[]};}});
 vm.runInContext(source,context);for(let i=0;i<20;i++)await new Promise(resolve=>setImmediate(resolve));return {els,get,context};
}
test('empty configuration does not invent live readings',async()=>{const {get}=await run([],false);assert.match(get('notice').textContent,/No demonstration data/);});
test('failed sensor values are unavailable even if stale values are present',async()=>{const r=row({o2_valid:false,oxygen:20});const {get}=await run([[r],[r]]);assert.equal(get('oxygen').textContent,'—');assert.equal(get('co2').textContent,'850');assert.match(get('status').textContent,/Partial/);assert.equal(get('export').disabled,false);});
test('marks old samples stale and breaks chart segments at missing readings',async()=>{
 const start=Date.now()-3600000;
 const samples=[row({sampled_at:new Date(start).toISOString()}),row({sampled_at:new Date(start+600000).toISOString(),sht_valid:false,temperature:null,humidity:null}),row({sampled_at:new Date(start+1200000).toISOString()})];
 const {get}=await run([[samples[2]],samples]);assert.match(get('status').textContent,/stale/);
 const segments=get('chart').children[0].children.filter(el=>el.tag==='polyline');assert.equal(segments.length,2);
});
test('network failure is explicit',async()=>{const {get}=await run([new Error('Network unavailable')]);assert.match(get('notice').textContent,/Network unavailable/);});

test('offline backlog cannot replace newest captured reading in the same batch',async()=>{
 const received=new Date().toISOString();
 const current=row({sample_id:'0000000000000002',sampled_at:new Date(Date.now()-15000).toISOString(),received_at:received,temperature:24});
 const older=row({sample_id:'0000000000000001',sampled_at:null,received_at:received,temperature:21});
 const {get}=await run([[older,current],[older,current]]);
 assert.equal(get('temperature').textContent,'24');
 assert.doesNotMatch(get('events').children[0].textContent,/clock unknown/);
 assert.match(get('notice').textContent,/Some history/);
});
