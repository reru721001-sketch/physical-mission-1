#pragma once
// 보호자용 웹 대시보드 (아이폰 Safari / 모든 브라우저). http://192.168.4.1/
// 페이지 자체는 공개, 데이터는 토큰(X-Auth-Token) 있어야 조회 가능.
//
// ■ 구조
//   - HTML·CSS·JavaScript 전체를 하나의 문자열(WEB_PAGE)로 저장
//   - PROGMEM: RAM이 아닌 플래시 메모리에 저장해서 ESP32 RAM 절약
//   - R"HTML( ... )HTML" : C++ 원시 문자열 → 따옴표·줄바꿈을 이스케이프 없이 그대로 넣을 수 있음
//   - 외부 파일·인터넷 없이 이 페이지 하나로 동작
//
// ■ 화면 흐름
//   토큰 입력(login) → 대시보드(app): 방 카드 / 돌발 상황 목록 / 건강 기록 그래프
//   3초마다 상태·이벤트 갱신, 10초마다 그래프 갱신
const char WEB_PAGE[] PROGMEM = R"HTML(<!doctype html>
<html lang="ko"><head><meta charset="utf-8">
<!-- 휴대폰 화면 폭에 맞춤 + 아이폰 노치 영역 대응 -->
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<!-- 아이폰 '홈 화면에 추가' 시 앱처럼 전체 화면으로 열림 -->
<meta name="apple-mobile-web-app-capable" content="yes">
<meta name="theme-color" content="#2F6FED">
<title>홈케어 모니터</title>
<style>
/* 색상 변수: 안정(safe)=초록, 주의(warn)=주황, 위험(danger)=빨강, 연결 끊김(off)=회색 */
:root{--bg:#F3F5F9;--card:#fff;--line:#E2E6EE;--text:#1B2230;--sub:#6B7385;--pri:#2F6FED;--safe:#2E9E6A;--warn:#E39B1B;--danger:#D93838;--off:#8A93A6}
/* 휴대폰이 다크모드면 자동으로 어두운 색 */
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
<!-- ① 로그인 화면: 토큰이 없을 때만 보임 -->
<section id="login" class="card">
  <div class="title">보호자 인증</div>
  <div class="sub">보호자 토큰을 입력하세요. 이 기기에만 저장됩니다.</div>
  <input id="tok" type="password" placeholder="인증 토큰" autocomplete="off">
  <button onclick="saveTok()">시작하기</button>
  <div id="loginErr" class="muted" style="color:var(--danger);margin-top:8px"></div>
</section>
<!-- ② 대시보드: 토큰 입력 후 보임 -->
<section id="app" style="display:none">
  <!-- 확인 안 한 알림이 있으면 빨간 배너 -->
  <div id="banner" class="banner" onclick="location.hash='#events'"></div>
  <div class="row" style="margin-bottom:8px"><span class="muted" id="upd">연결 중…</span>
    <button class="ghost" id="sound" onclick="enableSound()">🔔 경고음 켜기</button></div>
  <!-- 방 카드 (JavaScript가 채움) -->
  <div id="rooms"></div>
  <!-- 돌발 상황 목록 -->
  <h2 id="events">돌발 상황</h2>
  <div class="card"><div class="row" style="margin-bottom:6px"><span class="muted">최근 기록</span>
    <button onclick="ackAll()">모두 확인</button></div><div id="evlist" class="muted">없음</div></div>
  <!-- 건강 기록 그래프 3개 (canvas에 직접 그림) -->
  <h2>건강 기록 (최근 10분 · 작은방 1)</h2>
  <div class="card"><canvas id="cT"></canvas><div class="lab">온도 ℃</div>
    <canvas id="cH" style="margin-top:10px"></canvas><div class="lab">습도 %</div>
    <canvas id="cM" style="margin-top:10px"></canvas><div class="lab">움직임 (10초당 초)</div></div>
  <p class="muted" style="text-align:center">브라우저를 열어 둔 동안만 경고음이 울립니다.<br>
    <a href="#" onclick="logout()" style="color:var(--sub)">토큰 다시 입력</a></p>
</section>
</main>
<script>
// ---------- 방 목록 ----------
// host '' = 지금 접속한 1번 보드, 'http://192.168.4.12' = 2번 보드 (없으면 '연결 끊김')
const ROOMS=[{id:1,name:'작은방 1 · 첫째 (7세)',note:'자폐 스펙트럼 · 감각 과민',host:''},
             {id:2,name:'작은방 2 · 둘째 (3세)',note:'영유아 · 낙상 주의',host:'http://192.168.4.12'}];
// ---------- 토큰 ----------
// 브라우저 localStorage에 저장 → 다음에 들어와도 다시 입력할 필요 없음
let T='';try{T=localStorage.getItem('hc_tok')||''}catch(e){}
let audio=null,seen=new Set(),firstLoad=true;   // seen = 이미 본 알림 (새 알림만 소리 내려고)
const $=id=>document.getElementById(id);
// 토큰 유무에 따라 로그인 화면 / 대시보드 전환
function show(){ $('login').style.display=T?'none':'block'; $('app').style.display=T?'block':'none'; }
function saveTok(){T=$('tok').value.trim();try{localStorage.setItem('hc_tok',T)}catch(e){};show();tick();}
function logout(){try{localStorage.removeItem('hc_tok')}catch(e){};T='';show();return false;}
// ---------- API 호출 공통 함수 ----------
// 모든 요청에 X-Auth-Token 헤더를 붙임. 2.5초 안에 응답 없으면 취소(=연결 끊김 처리)
async function api(host,path,opt={}){
  const ctl=new AbortController();setTimeout(()=>ctl.abort(),2500);
  const r=await fetch(host+path,{...opt,signal:ctl.signal,headers:{'X-Auth-Token':T,...(opt.headers||{})}});
  if(r.status===401||r.status===403)throw 'auth';   // 토큰 틀림 또는 차단됨
  if(!r.ok)throw r.status;return r.json();
}
// ---------- 경고음 ----------
// 아이폰은 사용자가 버튼을 눌러야 소리를 허용 → '경고음 켜기' 버튼으로 오디오 활성화
function enableSound(){audio=new (window.AudioContext||window.webkitAudioContext)();beep(1);$('sound').textContent='🔔 경고음 켜짐';$('sound').disabled=true;}
// 880Hz '삐' 소리를 n번 (음원 파일 없이 브라우저에서 직접 생성)
function beep(n){if(!audio)return;for(let i=0;i<n;i++){const o=audio.createOscillator(),g=audio.createGain();
  o.frequency.value=880;o.connect(g);g.connect(audio.destination);const t=audio.currentTime+i*0.35;
  g.gain.setValueAtTime(.25,t);g.gain.setValueAtTime(0,t+.2);o.start(t);o.stop(t+.22);}}
const fmt=v=>v==null?'--':v;
// ---------- 방 카드 HTML 만들기 ----------
// 상태: 응답 없음 → 회색 '연결 끊김' / 미확인 알림 있음 → 빨강 '위험 N건' / 그 외 → 초록 '안정'
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
// ---------- 메인 갱신 (3초마다) ----------
// 1) 모든 방의 /api/status, /api/events 를 동시에 요청
// 2) 방 카드 다시 그리기  3) 새 알림이면 경고음+진동  4) 알림 목록 갱신
async function tick(){
  if(!T)return;
  const res=await Promise.all(ROOMS.map(async r=>{
    try{const s=await api(r.host,'/api/status');const ev=await api(r.host,'/api/events');return {r,s,ev};}
    // 1번 보드에서 토큰이 틀리면 로그인 화면으로 돌아감. 그 외 실패 = 연결 끊김
    catch(e){if(e==='auth'&&!r.host){logout();$('loginErr').textContent='토큰이 맞지 않아요';}return {r,s:null,ev:[]};}
  }));
  let html='',total=0,fresh=0;allEvents=[];
  for(const x of res){const un=x.ev.filter(e=>!e.parent_checked).length;total+=un;html+=roomCard(x.r,x.s,un);
    // 처음 본 미확인 알림만 '새 알림'으로 셈 (페이지 처음 열 때는 소리 안 냄)
    for(const e of x.ev){const k=x.r.id+':'+e.event_id;allEvents.push({...e,room:x.r});
      if(!seen.has(k)){seen.add(k);if(!firstLoad&&!e.parent_checked)fresh++;}}}
  $('rooms').innerHTML=html;
  $('banner').style.display=total?'block':'none';$('banner').textContent='🚨 확인하지 않은 알림 '+total+'건';
  if(fresh){beep(3);if(navigator.vibrate)navigator.vibrate([400,200,400]);}   // 새 알림: 삐 3번 + 진동
  firstLoad=false;
  // 알림 목록: 최신순 20개, 미확인 알림에는 '확인' 버튼
  allEvents.sort((a,b)=>b.occurred_at_s-a.occurred_at_s);
  $('evlist').innerHTML=allEvents.length?allEvents.slice(0,20).map(e=>`<div class="ev"><div style="flex:1">
    <div class="t">${e.parent_checked?'✅':'🚨'} ${esc(e.event_type)}</div>
    <div class="m">${e.room.name.split(' · ')[0]} · ${e.occurred_at_s}초 · ${esc(e.auto_action_taken)}</div></div>
    ${e.parent_checked?'':`<button onclick="ack(${e.room.id},${e.event_id})">확인</button>`}</div>`).join(''):'없음';
  $('upd').textContent='업데이트 '+new Date().toLocaleTimeString('ko-KR');
}
// HTML 특수문자 변환 (XSS 방지: 데이터에 태그가 들어와도 실행되지 않게)
function esc(s){return String(s).replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]))}
// ---------- 알림 확인 ----------
// 해당 보드에 POST /api/ack → 보드의 부저가 멈춤
async function ack(room,id){const r=ROOMS.find(x=>x.id===room);
  try{await api(r.host,'/api/ack',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'event_id='+id});}catch(e){}tick();}
// event_id=0 → 그 보드의 알림 전체 확인
async function ackAll(){for(const r of ROOMS){try{await api(r.host,'/api/ack',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'event_id=0'});}catch(e){}}tick();}
// ---------- 그래프 ----------
// 라이브러리 없이 canvas에 꺾은선 그래프 직접 그림 (min~max 범위로 맞춤, 값 없는 구간은 끊어서 그림)
function chart(id,data,min,max,color){const c=$(id),d=devicePixelRatio||1;c.width=c.clientWidth*d;c.height=110*d;
  const g=c.getContext('2d');g.scale(d,d);const w=c.clientWidth,h=110;g.clearRect(0,0,w,h);
  g.strokeStyle=getComputedStyle(document.body).getPropertyValue('--line');g.lineWidth=1;
  for(let i=0;i<3;i++){g.beginPath();g.moveTo(0,5+i*50);g.lineTo(w,5+i*50);g.stroke();}   // 가로 눈금선
  const pts=data.filter(v=>v!=null);if(pts.length<2){g.fillStyle='#8A93A6';g.fillText('데이터 수집 중…',w/2-40,58);return;}
  g.strokeStyle=color;g.lineWidth=2.5;g.beginPath();let pen=false;
  data.forEach((v,i)=>{if(v==null){pen=false;return;}const x=w*i/(data.length-1),y=105-100*(Math.max(min,Math.min(max,v))-min)/(max-min);
    pen?g.lineTo(x,y):g.moveTo(x,y);pen=true;});g.stroke();}
// 1번 보드의 /api/logs 로 온도(빨강)·습도(파랑)·움직임(주황) 그래프
async function logs(){if(!T)return;try{const l=await api('','/api/logs');
  chart('cT',l.map(x=>x.temperature),15,32,'#D93838');chart('cH',l.map(x=>x.humidity),20,90,'#2F6FED');
  chart('cM',l.map(x=>x.motion_sec),0,10,'#E39B1B');}catch(e){}}
// ---------- 시작 ----------
// 화면 표시 → 즉시 1회 갱신 → 상태는 3초마다, 그래프는 10초마다 자동 갱신
show();tick();logs();setInterval(tick,3000);setInterval(logs,10000);
</script></body></html>)HTML";
