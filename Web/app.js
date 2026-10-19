'use strict';
const cfg = window.FRESHAGRO || {}, $ = id => document.getElementById(id);
const fields = ['temperature','humidity','oxygen','co2'];
const labels = {temperature:'Temperature · °C',humidity:'Humidity · %RH',oxygen:'Oxygen · % vol',co2:'Carbon dioxide · ppm'};
let days = 1, history = [], latest = null, busy = false, loadError = '', lastFetch = 0;
const validity = (s,k) => s[k === 'oxygen' ? 'o2_valid' : k === 'co2' ? 'co2_valid' : 'sht_valid'];
const stamp = s => Date.parse(s.sampled_at || s.received_at);
// Batch receipt times tie; monotonically increasing device IDs preserve capture order.
const newestFirst = (a,b) => Date.parse(b.received_at)-Date.parse(a.received_at) || b.sample_id.localeCompare(a.sample_id);
const health = s => Number(s.o2_valid)+Number(s.co2_valid)+Number(s.sht_valid);
const date = t => new Date(t).toLocaleString([], {month:'short',day:'numeric',hour:'2-digit',minute:'2-digit'});
function notice(message, error = false) { $('notice').textContent = message; $('notice').hidden = !message; $('notice').classList.toggle('error',error); }
function renderStatus() {
  if (!latest) return;
  const age = Math.max(0,Date.now()-stamp(latest)), stale = age > (cfg.cycleMinutes || 10)*60000*2;
  const n = health(latest), status = $('status');
  status.textContent = loadError ? '○ Connection issue' : stale ? '○ Data is stale' : n === 3 ? '● All sensors reporting' : n ? '● Partial readings' : '● No valid sensors';
  status.className = 'badge ' + (loadError || stale || n < 3 ? n === 0 ? 'bad' : 'warn' : 'good');
  $('synced').textContent = `Latest sample: ${Math.floor(age/60000)}m ago`;
  $('health').textContent = `${n} of 3 sensors reporting`;
  $('timestamp').textContent = `Latest: ${date(stamp(latest))}${latest.sampled_at ? '' : ' · receipt time; device clock unknown'}`;
}
function render() {
  renderStatus();
  for (const k of fields) {
    const valid = latest && validity(latest,k) && Number.isFinite(latest[k]);
    $(k).textContent = valid ? Number(latest[k]).toLocaleString(undefined,{maximumFractionDigits:k==='co2'?0:2}) : '—';
    $(k+'-state').textContent = !latest ? 'Awaiting readings' : valid ? 'Valid sensor response' : 'Unavailable this cycle';
  }
  const events = $('events'); events.replaceChildren();
  for (const s of [...history].sort(newestFirst).slice(0,8)) {
    const li = document.createElement('li'), n = health(s); li.className = n === 3 ? '' : n ? 'warn' : 'bad';
    li.textContent = `${n}/3 sensors valid${s.sampled_at ? '' : ' · clock unknown'}`;
    const time = document.createElement('time'); time.textContent = date(stamp(s)); li.append(time); events.append(li);
  }
  if (!events.children.length) { const li=document.createElement('li'); li.className='empty'; li.textContent='No cycles in this period.'; events.append(li); }
  $('export').disabled = !history.length;
  chart();
}
function chart() {
  const key = $('metric').value, host = $('chart'), end = Date.now(), start=end-days*86400000;
  const rows = history.filter(s=>stamp(s)>=start && stamp(s)<=end).sort((a,b)=>stamp(a)-stamp(b));
  const valid = rows.filter(s=>validity(s,key) && Number.isFinite(s[key]));
  $('chart-label').textContent=labels[key]; $('count').textContent=`${rows.length.toLocaleString()} cycles · ${valid.length.toLocaleString()} valid`;
  if (!valid.length) { host.replaceChildren(); const el=document.createElement('div');el.className='empty';el.textContent='No valid readings in this period.';host.append(el);return; }
  const values=valid.map(s=>s[key]); let lo=Math.min(...values), hi=Math.max(...values), pad=(hi-lo)*.15 || Math.max(1,Math.abs(hi)*.02);lo-=pad;hi+=pad;
  const x=t=>52+(t-start)/(end-start)*620, y=v=>212-(v-lo)/(hi-lo)*180;
  const ns='http://www.w3.org/2000/svg', svg=document.createElementNS(ns,'svg');svg.setAttribute('viewBox','0 0 700 250');svg.setAttribute('role','img');svg.setAttribute('aria-label',`${labels[key]}, ${valid.length} valid readings. Export CSV for exact values.`);
  const add=(tag,attrs,text)=>{const el=document.createElementNS(ns,tag);for(const [k,v]of Object.entries(attrs))el.setAttribute(k,v);if(text!==undefined)el.textContent=text;svg.append(el);return el;};
  for(let i=0;i<5;i++){const v=lo+(hi-lo)*i/4;add('line',{x1:52,x2:672,y1:y(v),y2:y(v),stroke:'#e5ece0'});add('text',{x:43,y:y(v)+3,'text-anchor':'end',fill:'#91a087','font-size':10},Number(v.toFixed(key==='co2'?0:1)).toLocaleString());}
  for(let i=0;i<5;i++){const t=start+(end-start)*i/4;add('text',{x:x(t),y:239,'text-anchor':'middle',fill:'#91a087','font-size':10},new Date(t).toLocaleString([],days===1?{hour:'2-digit',minute:'2-digit'}:{month:'short',day:'numeric'}));}
  let segment=[]; const flush=()=>{if(!segment.length)return;add('polyline',{points:segment.map(s=>`${x(stamp(s))},${y(s[key])}`).join(' '),fill:'none',stroke:'#269d74','stroke-width':2,'stroke-linejoin':'round'});segment=[];}; let previous=0;
  for(const s of rows){const t=stamp(s);if(!validity(s,key)||!Number.isFinite(s[key])){flush();previous=0;continue;}if(previous&&t-previous>(cfg.cycleMinutes||10)*90000)flush();segment.push(s);previous=t;}
  flush();
  // Keep dots bounded for month views; CSV retains every row.
  const step=Math.max(1,Math.floor(valid.length/150));valid.filter((_,i)=>i%step===0||i===valid.length-1).forEach(s=>{const dot=add('circle',{cx:x(stamp(s)),cy:y(s[key]),r:2.8,fill:'#269d74'});const title=document.createElementNS(ns,'title');title.textContent=`${date(stamp(s))}: ${s[key]}`;dot.append(title);});host.replaceChildren(svg);
}
async function query(params) {
  const url=new URL('/rest/v1/readings',cfg.supabaseUrl);for(const [k,v]of Object.entries(params))url.searchParams.set(k,v);
  const r=await fetch(url,{headers:{apikey:cfg.publishableKey},signal:AbortSignal.timeout(12000)});if(!r.ok)throw Error(`Data service returned ${r.status}`);const data=await r.json();if(!Array.isArray(data))throw Error('Unexpected data response');return data;
}
async function load() {
  if(busy)return;
  if(!cfg.supabaseUrl||!cfg.publishableKey){notice('Connect your Supabase project to see live readings. No demonstration data is shown.');return;}
  busy=true;$('refresh').disabled=true;notice('Refreshing readings…');
  try {
    const base={select:'sample_id,sampled_at,received_at,o2_valid,co2_valid,sht_valid,oxygen,co2,temperature,humidity',device_id:`eq.${cfg.deviceId}`,order:'received_at.desc,sample_id.desc'};
    const newest=await query({...base,limit:'72'});latest=newest.sort(newestFirst)[0]||null;
    let all=[], offset=0;const cutoff=new Date(Date.now()-days*86400000).toISOString();
    // Stable order and pagination; 30 days at ten-minute intervals fits below 6,000.
    for(;offset<6000;offset+=1000){const page=await query({...base,received_at:`gte.${cutoff}`,limit:'1000',offset:String(offset)});all.push(...page);if(page.length<1000)break;}
    history=all;lastFetch=Date.now();loadError='';render();
    notice(offset>=6000?'Showing the latest 6,000 cycles; choose a shorter range.':!latest?'Connected. Waiting for the device’s first upload.':history.some(s=>s.sampled_at===null)?'Device clock was unavailable. Some history uses upload receipt time.':'');
  } catch(e){loadError=e.message;notice(`${e.message}. ${lastFetch?'Previously loaded readings remain visible.':'Check public configuration and database policies.'}`,true);renderStatus();}
  finally{busy=false;$('refresh').disabled=false;}
}
$('device').textContent=cfg.deviceId||'freshagro-01';$('schedule').textContent=`Every ${cfg.cycleMinutes||10} minutes`;
$('refresh').addEventListener('click',load);$('metric').addEventListener('change',chart);
document.querySelectorAll('[data-days]').forEach(b=>b.addEventListener('click',()=>{if(busy)return;days=Number(b.dataset.days);document.querySelectorAll('[data-days]').forEach(el=>{el.classList.toggle('active',el===b);el.setAttribute('aria-pressed',String(el===b));});load();}));
$('export').addEventListener('click',()=>{const cols=['sampled_at','received_at','o2_valid','co2_valid','sht_valid',...fields];const quote=v=>`"${String(v??'').replaceAll('"','""')}"`;const csv=[cols.join(','),...[...history].sort((a,b)=>stamp(a)-stamp(b)).map(r=>cols.map(k=>quote(r[k])).join(','))].join('\r\n');const url=URL.createObjectURL(new Blob([csv],{type:'text/csv;charset=utf-8'}));const a=document.createElement('a');a.href=url;a.download=`freshagro-${days}d.csv`;a.click();setTimeout(()=>URL.revokeObjectURL(url),1000);});
load();setInterval(()=>{renderStatus();if(!document.hidden)load();},60000);document.addEventListener('visibilitychange',()=>{if(!document.hidden&&Date.now()-lastFetch>60000)load();});
