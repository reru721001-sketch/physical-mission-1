#pragma once
// 보호자용 웹 대시보드 (아이폰 Safari / 모든 브라우저). http://192.168.4.1/
// 페이지 자체는 공개, 데이터는 토큰(X-Auth-Token) 있어야 조회 가능.
const char WEB_PAGE[] PROGMEM = R"HTML(<!doctype html>
<html lang="ko"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<meta name="apple-mobile-web-app-capable" content="yes">
<meta name="theme-color" content="#2F6FED">
<title>홈케어 모니터</title>
<style>
:root{--bg:#F3F5F9;--card:#fff;--line:#E2E6EE;--text:#1B2230;--sub:#6B7385;--pri:#2F6FED;--safe:#2E9E6A;--warn:#E39B1B;--danger:#D93838;--off:#8A93A6}
@media (prefers-color-scheme:dark){:root{--bg:#11151C;--card:#1A2029;--line:#2A3240;--text:#E8ECF3;--sub:#98A2B5}}
*{box-sizing:border-box}body{margin:0;font-family:-apple-system,system-ui,sans-serif;background:var(--bg);color:var(--text)}
header{background:var(--pri);color:#fff;padding:calc(14px + env(safe-area-inset-top)) 16px 14px;font-size:18px;font-weight:600}
main{padding:16px;max-width:640px;margin:auto}
h2{font-size:15px;margin:22px 0 8px;color:var(--sub);font-weight:600}
.card{background:var(--card);border:2px solid var(--line);border-radius:16px;padding:16px;margin-bottom:12px}
.card.alert{border-color:var(--danger)}
.row{display:flex;align-items:center;justify-content:space-between;gap:8px}
.title{font-weight:700;font-size:16px}.sub{color:var(--sub);font-size:13px;margin-top:2px}
.pill{color:#fff;border-radius:20px;padding:4px 12px;font-size:13px;font-weight:700;white-space:nowrap}
.vals{display:flex;margin-top:14px}.vals div{flex:1;text-align:center}
.big{font-size:24px;font-weight:700}.lab{font-size:12px;color:var(--sub)}
.banner{background:var(--danger);color:#fff;border-radius:12px;padding:14px;font-weight:700;margin-bottom:12px;display:none}
.ev{display:flex;gap:10px;align-items:center;border-bottom:1px solid var(--line);padding:10px 0}
.ev:last-child{border:0}.ev .t{font-weight:600}.ev .m{font-size:12px;color:var(--sub)}
button{font:inherit;border:0;border-radius:10px;padding:10px 14px;background:var(--pri);color:#fff;font-weight:600}
button.ghost{background:transparent;color:var(--pri);border:1px solid var(--pri)}
button:disabled{opacity:.4}
input{font:inherit;width:100%;padding:12px;border-radius:10px;border:1px solid var(--line);background:var(--card);color:var(--text);margin:8px 0}
canvas{width:100%;height:110px;display:block}
.muted{color:var(--sub);font-size:13px}
#login{display:none}
</style></head><body>
<header>🏠 홈케어 모니터</header>
<main>
<section id="login" class="card">
  <div class="title">보호자 인증</div>
  <div class="sub">보호자 토큰을 입력하세요. 이 기기에만 저장됩니다.</div>
  <input id="tok" type="password" placeholder="인증 토큰" autocomplete="off">
  <button onclick="saveTok()">시작하기</button>
  <div id="loginErr" class="muted" style="color:var(--danger);margin-top:8px"></div>
</section>
<section id="app" style="display:none">
  <div id="banner" class="banner" onclick="location.hash='#events'"></div>
  <div class="row" style="margin-bottom:8px"><span class="muted" id="upd">연결 중…</span>
    <button class="ghost" id="sound" onclick="enableSound()">🔔 경고음 켜기</button></div>
  <div id="rooms"></div>
  <h2 id="events">돌발 상황</h2>
  <div class="card"><div class="row" style="margin-bottom:6px"><span class="muted">최근 기록</span>
    <button onclick="ackAll()">모두 확인</button></div><div id="evlist" class="muted">없음</div></div>
  <h2>건강 기록 (최근 10분 · 작은방 1)</h2>
  <div class="card"><canvas id="cT"></canvas><div class="lab">온도 ℃</div>
    <canvas id="cH" style="margin-top:10px"></canvas><div class="lab">습도 %</div>
    <canvas id="cM" style="margin-top:10px"></canvas><div class="lab">움직임 (10초당 초)</div></div>
  <p class="muted" style="text-align:center">브라우저를 열어 둔 동안만 경고음이 울립니다.<br>
    <a href="#" onclick="logout()" style="color:var(--sub)">토큰 다시 입력</a></p>
</section>
</main>
<script>
const ROOMS=[{id:1,name:'작은방 1 · 첫째 (7세)',note:'자폐 스펙트럼 · 감각 과민',host:''},
             {id:2,name:'작은방 2 · 둘째 (3세)',note:'영유아 · 낙상 주의',host:'http://192.168.4.12'}];
let T='';try{T=localStorage.getItem('hc_tok')||''}catch(e){}
let audio=null,seen=new Set(),firstLoad=true;
const $=id=>document.getElementById(id);
function show(){ $('login').style.display=T?'none':'block'; $('app').style.display=T?'block':'none'; }
function saveTok(){T=$('tok').value.trim();try{localStorage.setItem('hc_tok',T)}catch(e){};show();tick();}
function logout(){try{localStorage.removeItem('hc_tok')}catch(e){};T='';show();return false;}
async function api(host,path,opt={}){
  const ctl=new AbortController();setTimeout(()=>ctl.abort(),2500);
  const r=await fetch(host+path,{...opt,signal:ctl.signal,headers:{'X-Auth-Token':T,...(opt.headers||{})}});
  if(r.status===401||r.status===403)throw 'auth';
  if(!r.ok)throw r.status;return r.json();
}
function enableSound(){audio=new (window.AudioContext||window.webkitAudioContext)();beep(1);$('sound').textContent='🔔 경고음 켜짐';$('sound').disabled=true;}
function beep(n){if(!audio)return;for(let i=0;i<n;i++){const o=audio.createOscillator(),g=audio.createGain();
  o.frequency.value=880;o.connect(g);g.connect(audio.destination);const t=audio.currentTime+i*0.35;
  g.gain.setValueAtTime(.25,t);g.gain.setValueAtTime(0,t+.2);o.start(t);o.stop(t+.22);}}
const fmt=v=>v==null?'--':v;
function roomCard(r,s,unchecked){
  const on=!!s, color=!on?'var(--off)':unchecked?'var(--danger)':'var(--safe)';
  const label=!on?'연결 끊김':unchecked?'위험 '+unchecked+'건':'안정';
  const n=on?s.now:{};
  return `<div class="card ${unchecked?'alert':''}"><div class="row"><div><div class="title">${r.name}</div>
  <div class="sub">${r.note}</div></div><span class="pill" style="background:${color}">${label}</span></div>
  <div class="vals"><div><div class="big">${n.temperature!=null?n.temperature+'℃':'--'}</div><div class="lab">온도</div></div>
  <div><div class="big">${n.humidity!=null?Math.round(n.humidity)+'%':'--'}</div><div class="lab">습도</div></div>
  <div><div class="big" style="color:${n.motion?'var(--warn)':'inherit'}">${on?(n.motion?'있음':'없음'):'--'}</div><div class="lab">움직임</div></div></div>
  <div class="sub" style="margin-top:10px">${on?'수면등 '+(n.nightlight?'켜짐':'꺼짐')+' · 가동 '+Math.floor(n.uptime_s/60)+'분':'기기 응답 없음'}</div></div>`;
}
let allEvents=[];
async function tick(){
  if(!T)return;
  const res=await Promise.all(ROOMS.map(async r=>{
    try{const s=await api(r.host,'/api/status');const ev=await api(r.host,'/api/events');return {r,s,ev};}
    catch(e){if(e==='auth'&&!r.host){logout();$('loginErr').textContent='토큰이 맞지 않아요';}return {r,s:null,ev:[]};}
  }));
  let html='',total=0,fresh=0;allEvents=[];
  for(const x of res){const un=x.ev.filter(e=>!e.parent_checked).length;total+=un;html+=roomCard(x.r,x.s,un);
    for(const e of x.ev){const k=x.r.id+':'+e.event_id;allEvents.push({...e,room:x.r});
      if(!seen.has(k)){seen.add(k);if(!firstLoad&&!e.parent_checked)fresh++;}}}
  $('rooms').innerHTML=html;
  $('banner').style.display=total?'block':'none';$('banner').textContent='🚨 확인하지 않은 알림 '+total+'건';
  if(fresh){beep(3);if(navigator.vibrate)navigator.vibrate([400,200,400]);}
  firstLoad=false;
  allEvents.sort((a,b)=>b.occurred_at_s-a.occurred_at_s);
  $('evlist').innerHTML=allEvents.length?allEvents.slice(0,20).map(e=>`<div class="ev"><div style="flex:1">
    <div class="t">${e.parent_checked?'✅':'🚨'} ${esc(e.event_type)}</div>
    <div class="m">${e.room.name.split(' · ')[0]} · ${e.occurred_at_s}초 · ${esc(e.auto_action_taken)}</div></div>
    ${e.parent_checked?'':`<button onclick="ack(${e.room.id},${e.event_id})">확인</button>`}</div>`).join(''):'없음';
  $('upd').textContent='업데이트 '+new Date().toLocaleTimeString('ko-KR');
}
function esc(s){return String(s).replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]))}
async function ack(room,id){const r=ROOMS.find(x=>x.id===room);
  try{await api(r.host,'/api/ack',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'event_id='+id});}catch(e){}tick();}
async function ackAll(){for(const r of ROOMS){try{await api(r.host,'/api/ack',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'event_id=0'});}catch(e){}}tick();}
function chart(id,data,min,max,color){const c=$(id),d=devicePixelRatio||1;c.width=c.clientWidth*d;c.height=110*d;
  const g=c.getContext('2d');g.scale(d,d);const w=c.clientWidth,h=110;g.clearRect(0,0,w,h);
  g.strokeStyle=getComputedStyle(document.body).getPropertyValue('--line');g.lineWidth=1;
  for(let i=0;i<3;i++){g.beginPath();g.moveTo(0,5+i*50);g.lineTo(w,5+i*50);g.stroke();}
  const pts=data.filter(v=>v!=null);if(pts.length<2){g.fillStyle='#8A93A6';g.fillText('데이터 수집 중…',w/2-40,58);return;}
  g.strokeStyle=color;g.lineWidth=2.5;g.beginPath();let pen=false;
  data.forEach((v,i)=>{if(v==null){pen=false;return;}const x=w*i/(data.length-1),y=105-100*(Math.max(min,Math.min(max,v))-min)/(max-min);
    pen?g.lineTo(x,y):g.moveTo(x,y);pen=true;});g.stroke();}
async function logs(){if(!T)return;try{const l=await api('','/api/logs');
  chart('cT',l.map(x=>x.temperature),15,32,'#D93838');chart('cH',l.map(x=>x.humidity),20,90,'#2F6FED');
  chart('cM',l.map(x=>x.motion_sec),0,10,'#E39B1B');}catch(e){}}
show();tick();logs();setInterval(tick,3000);setInterval(logs,10000);
</script></body></html>)HTML";
