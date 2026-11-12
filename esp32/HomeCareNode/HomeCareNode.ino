/*
  HomeCareNode - 스마트 홈 케어 엣지 노드 (ESP32 DOIT DevKit V1, Arduino-ESP32 3.3.12)
  안드로이드 앱 "홈케어 모니터"(CareApi.java)와 같은 API 규격.

  ■ 네트워크 (인터넷·클라우드 없음)
    ROOM_ID 1 보드 = Wi-Fi 공유기 역할(AP) 192.168.4.1  ← 보호자 폰이 여기에 연결
    ROOM_ID 2 보드 = 1번 보드에 접속(STA)    192.168.4.12  (.2~ 는 폰 DHCP용)
    보드가 1개뿐이면 ROOM_ID 1 로만 올리면 됨 (앱에서 작은방 2는 '연결 끊김' 표시)

  ■ 배선 (모든 GND 공통, GPIO는 3.3V 신호만)
    ※ DOIT 보드가 넓어 브레드보드에서 한쪽 줄만 쓸 수 있으므로
      모든 부품을 윗줄(VIN·GND·D13·D12·D14·D27·D26·D25·D33·D32 …)에만 연결
    DHT11 VCC         -> GPIO33 (코드가 HIGH로 켜서 3.3V 전원으로 사용, DHT11 소비전류 2.5mA 이하)
    DHT11 DATA        -> GPIO27
    PIR VCC / OUT     -> VIN(5V) / GPIO32, OUT 3.3V 확인
    수면등 LED        -> GPIO25 -> 330Ω -> LED(+) -> GND   (+ 보드 내장 LED GPIO2 동시 점등)
    능동 부저         -> GPIO12 (+) / GND (-)  ※ 부팅 시 LOW 필요한 핀, 부저가 GND로 당겨 문제없음
    현장 확인 버튼    -> GPIO14 <-> GND (내부 풀업)
    GND               -> 윗줄 GND 핀 (VIN 옆)

  ■ API (헤더 X-Auth-Token 필수)
    GET  /           보호자 웹 대시보드 (토큰은 화면에서 입력)
    GET  /api/status   GET /api/events   GET /api/logs   GET /api/security
    POST /api/ack      body: event_id=N  (N 생략 시 전체 확인)
*/
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DHT.h>
#include "web_page.h"

// ================= 보드마다 바꾸는 값 =================
constexpr int ROOM_ID = 1;                         // 1 또는 2
const char *ROOM_LOCATION[] = {"", "작은방 1", "작은방 2"};

// ================= 모든 보드 + 앱에서 같은 값 =================
const char *AP_SSID    = "HomeCare-Net";
const char *AP_PASS    = "care-lab-2026!";         // 8자 이상, 발표 전 변경
const char *AUTH_TOKEN = "parent-token-1234";      // 앱 설정의 '인증 토큰'과 같게

// ================= 위험 판단 기준 (헬스케어 기준) =================
constexpr uint32_t MOTION_SUSTAIN_MS = 5000;       // PIR 연속 5초 = 침대 이탈 의심
constexpr uint32_t PIR_WARMUP_MS     = 60000;   // HC-SR501 자체 준비 약 1분
constexpr float    TEMP_LOW = 18.0, TEMP_HIGH = 28.0;
constexpr float    HUM_LOW  = 30.0, HUM_HIGH  = 70.0;
constexpr uint32_t NIGHTLIGHT_MS     = 60000;      // 자동 수면등 유지
constexpr uint32_t LOG_INTERVAL_MS   = 10000;

// ================= 보안 정책 =================
constexpr uint8_t  MAX_FAILS = 5;                  // 연속 인증 실패 허용
constexpr uint32_t BLOCK_MS  = 5UL * 60 * 1000;    // 5분 차단
constexpr uint8_t  MAX_CLIENTS = 4;                // AP 동시 접속 (폰 + 2번 보드 + 여유)

// ================= 핀 =================
constexpr uint8_t DHT_PIN = 27, PIR_PIN = 32;
constexpr uint8_t LIGHT_PIN = 25, BUILTIN_PIN = 2, BUZZER_PIN = 12, BUTTON_PIN = 14;
constexpr uint8_t DHT_POWER_PIN = 33;   // DHT11 전원 (3V3 핀이 가려져서 GPIO로 공급)

DHT dht(DHT_PIN, DHT11);
WebServer server(80);
String deviceId;

struct SensorLog { uint32_t id, t; float temp, hum; uint32_t motionSec; };
struct EventRec  { uint32_t id, t; String type, action; bool checked; };
struct AccessRec { uint32_t id, t; String ip, path; bool ok; };
struct IpState   { IPAddress ip; uint8_t fails; uint32_t blockedUntil; bool used; };

constexpr int N_LOG = 60, N_EVT = 30, N_ACC = 40, N_IP = 8;
SensorLog sensorLogs[N_LOG]; uint32_t logCount = 0;
EventRec  events[N_EVT];     uint32_t evtCount = 0;
AccessRec accLog[N_ACC];     uint32_t accCount = 0;
IpState   ipTable[N_IP];

float tempC = NAN, hum = NAN;
uint32_t motionMs = 0, lastSample = 0;   // 로그 구간 동안 움직인 시간
bool motionNow = false, motionOpen = false, envBad = false;
uint32_t motionSince = 0, lastDht = 0, lastLog = 0, lightUntil = 0, lastWifiTry = 0;

bool elapsed(uint32_t now, uint32_t since, uint32_t span) { return now - since >= span; }

// ===================== 엣지 제어 =====================
void addEvent(const String &type, const String &action) {
  events[evtCount % N_EVT] = {evtCount + 1, millis(), type, action, false};
  evtCount++;
  Serial.printf("[EVENT #%lu] %s -> %s\n", (unsigned long)evtCount, type.c_str(), action.c_str());
}

bool anyUnchecked() {
  for (uint32_t i = 0; i < min(evtCount, (uint32_t)N_EVT); i++) if (!events[i].checked) return true;
  return false;
}

void ack(long id) {     // id <= 0 이면 전체
  for (uint32_t i = 0; i < min(evtCount, (uint32_t)N_EVT); i++)
    if (id <= 0 || events[i].id == (uint32_t)id) events[i].checked = true;
}

void edgeControl(uint32_t now) {
  // 1) 움직임 지속 = 침대 이탈 의심
  motionNow = now > PIR_WARMUP_MS && digitalRead(PIR_PIN) == HIGH;
  if (motionNow) {
    if (!motionSince) motionSince = now;
    if (!motionOpen && elapsed(now, motionSince, MOTION_SUSTAIN_MS)) {
      motionOpen = true;
      lightUntil = now + NIGHTLIGHT_MS;
      addEvent("침대 이탈 의심", "수면등 자동 점등 (낙상 예방)");
    }
  } else { motionSince = 0; motionOpen = false; }
  if (motionNow) motionMs += now - lastSample;
  lastSample = now;

  // 2) 수면 환경
  if (elapsed(now, lastDht, 2500)) {
    lastDht = now;
    float t = dht.readTemperature(), h = dht.readHumidity();
    if (isfinite(t) && isfinite(h)) { tempC = t; hum = h; }
    bool bad = isfinite(tempC) && (tempC < TEMP_LOW || tempC > TEMP_HIGH || hum < HUM_LOW || hum > HUM_HIGH);
    if (bad && !envBad) addEvent("수면 환경 이탈", String(tempC, 1) + "℃ / " + String(hum, 0) + "% - 환기·냉난방 확인");
    envBad = bad;
  }

  // 1차 개입 출력
  bool light = (int32_t)(lightUntil - now) > 0;
  digitalWrite(LIGHT_PIN, light); digitalWrite(BUILTIN_PIN, light);
  // 부저: 새 알림 즉시 울리고, 확인 전까지 15초마다 반복 (능동·수동 부저 모두 소리 나도록 tone 사용)
  static uint32_t seenEvt = 0, beepFrom = 0; static bool buzzing = false;
  if (evtCount != seenEvt) { seenEvt = evtCount; beepFrom = now; }
  bool beep = anyUnchecked() && ((now - beepFrom) % 15000) < 600 && (now / 150) % 2;
  if (beep != buzzing) { buzzing = beep; if (beep) tone(BUZZER_PIN, 2000); else { noTone(BUZZER_PIN); digitalWrite(BUZZER_PIN, LOW); } }

  // 현장 확인 버튼
  static bool prev = HIGH; static uint32_t chg = 0;
  bool b = digitalRead(BUTTON_PIN);
  if (b != prev && elapsed(now, chg, 30)) { chg = now; prev = b; if (b == LOW) ack(0); }

  // 센서 기록
  if (elapsed(now, lastLog, LOG_INTERVAL_MS)) {
    lastLog = now;
    sensorLogs[logCount % N_LOG] = {logCount + 1, now, tempC, hum, motionMs / 1000};
    logCount++;
    motionMs = 0;
  }
}

// ===================== 접근 통제 =====================
IpState *ipSlot(IPAddress ip) {
  IpState *freeSlot = nullptr, *oldest = &ipTable[0];
  for (auto &s : ipTable) {
    if (s.used && s.ip == ip) return &s;
    if (!s.used && !freeSlot) freeSlot = &s;
    if (s.blockedUntil < oldest->blockedUntil) oldest = &s;
  }
  IpState *s = freeSlot ? freeSlot : oldest;
  *s = {ip, 0, 0, true};
  return s;
}

void logAccess(const String &ip, bool ok) {
  accLog[accCount % N_ACC] = {accCount + 1, millis(), ip, server.uri(), ok};
  accCount++;
  if (!ok) Serial.printf("[SECURITY] denied %s %s\n", ip.c_str(), server.uri().c_str());
}

// 작은방 1 웹 화면이 작은방 2 보드 데이터를 불러올 수 있게 허용 (토큰 검사는 그대로)
void cors() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Headers", "X-Auth-Token, Content-Type");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
}

bool authorize() {
  cors();
  IPAddress ip = server.client().remoteIP();
  IpState *s = ipSlot(ip);
  uint32_t now = millis();
  if (s->blockedUntil && (int32_t)(s->blockedUntil - now) > 0) {
    logAccess(ip.toString(), false);
    server.send(403, "application/json", "{\"error\":\"blocked\"}");
    return false;
  }
  String tok = server.header("X-Auth-Token");
  bool ok = tok.length() == strlen(AUTH_TOKEN);
  uint8_t diff = 0;
  for (size_t i = 0; ok && i < tok.length(); i++) diff |= tok[i] ^ AUTH_TOKEN[i];
  ok = ok && diff == 0;
  logAccess(ip.toString(), ok);
  if (ok) { s->fails = 0; s->blockedUntil = 0; return true; }
  if (++s->fails >= MAX_FAILS) {
    s->blockedUntil = now + BLOCK_MS;
    s->fails = 0;
    addEvent("비인가 접근 차단", ip.toString() + " 5분 차단");
  }
  server.send(401, "application/json", "{\"error\":\"unauthorized\"}");
  return false;
}

// ===================== JSON API =====================
String jnum(float v) { return isfinite(v) ? String(v, 1) : "null"; }
String jstr(const String &s) {
  String o = "\"";
  for (char c : s) { if (c == '"' || c == '\\') o += '\\'; o += c; }
  return o + "\"";
}

void apiStatus() {
  if (!authorize()) return;
  String j = "{\"device\":{\"device_id\":" + jstr(deviceId) + ",\"target_id\":" + ROOM_ID +
             ",\"device_type\":\"ESP32_PIR_DHT\",\"local_ip\":" + jstr(ROOM_ID == 1 ? WiFi.softAPIP().toString() : WiFi.localIP().toString()) +
             ",\"is_active\":true,\"room_location\":" + jstr(ROOM_LOCATION[ROOM_ID]) + "},";
  j += "\"now\":{\"temperature\":" + jnum(tempC) + ",\"humidity\":" + jnum(hum) +
       ",\"motion\":" + (motionNow ? "true" : "false") +
       ",\"nightlight\":" + (digitalRead(LIGHT_PIN) ? "true" : "false") + ",\"uptime_s\":" + millis() / 1000 + "}}";
  server.send(200, "application/json", j);
}

void apiEvents() {
  if (!authorize()) return;
  String j = "[";
  uint32_t n = min(evtCount, (uint32_t)N_EVT);
  for (uint32_t k = 0; k < n; k++) {
    EventRec &e = events[(evtCount - n + k) % N_EVT];
    if (k) j += ",";
    j += "{\"event_id\":" + String(e.id) + ",\"occurred_at_s\":" + e.t / 1000 + ",\"event_type\":" + jstr(e.type) +
         ",\"auto_action_taken\":" + jstr(e.action) + ",\"parent_checked\":" + (e.checked ? "true" : "false") + "}";
  }
  server.send(200, "application/json", j + "]");
}

void apiLogs() {
  if (!authorize()) return;
  String j = "[";
  uint32_t n = min(logCount, (uint32_t)N_LOG);
  for (uint32_t k = 0; k < n; k++) {
    SensorLog &l = sensorLogs[(logCount - n + k) % N_LOG];
    if (k) j += ",";
    j += "{\"log_id\":" + String(l.id) + ",\"recorded_at_s\":" + l.t / 1000 + ",\"temperature\":" + jnum(l.temp) +
         ",\"humidity\":" + jnum(l.hum) + ",\"motion_sec\":" + l.motionSec + "}";
  }
  server.send(200, "application/json", j + "]");
}

void apiSecurity() {
  if (!authorize()) return;
  String j = "[";
  uint32_t n = min(accCount, (uint32_t)N_ACC);
  for (uint32_t k = 0; k < n; k++) {
    AccessRec &a = accLog[(accCount - n + k) % N_ACC];
    if (k) j += ",";
    j += "{\"access_id\":" + String(a.id) + ",\"accessed_at_s\":" + a.t / 1000 + ",\"ip_address\":" + jstr(a.ip) +
         ",\"path\":" + jstr(a.path) + ",\"is_authorized\":" + (a.ok ? "true" : "false") + "}";
  }
  server.send(200, "application/json", j + "]");
}

void apiAck() {
  if (!authorize()) return;
  ack(server.hasArg("event_id") ? server.arg("event_id").toInt() : 0);
  server.send(200, "application/json", "{\"ok\":true}");
}

// ===================== 네트워크 =====================
void startNetwork() {
  WiFi.persistent(false);
  if (ROOM_ID == 1) {
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(IPAddress(192,168,4,1), IPAddress(192,168,4,1), IPAddress(255,255,255,0));
    bool ok = WiFi.softAP(AP_SSID, AP_PASS, 6, 0, MAX_CLIENTS);
    deviceId = WiFi.softAPmacAddress();
    Serial.printf("AP %s %s  IP 192.168.4.1\n", AP_SSID, ok ? "OK" : "FAIL");
  } else {
    WiFi.mode(WIFI_STA);
    WiFi.config(IPAddress(192,168,4,10 + ROOM_ID), IPAddress(192,168,4,1), IPAddress(255,255,255,0));
    WiFi.begin(AP_SSID, AP_PASS);
    deviceId = WiFi.macAddress();
    lastWifiTry = millis();
    Serial.printf("STA -> %s  IP 192.168.4.%d\n", AP_SSID, 10 + ROOM_ID);
  }
}

void keepWifi(uint32_t now) {
  if (ROOM_ID == 1 || WiFi.status() == WL_CONNECTED || !elapsed(now, lastWifiTry, 15000)) return;
  lastWifiTry = now;
  WiFi.disconnect();
  WiFi.begin(AP_SSID, AP_PASS);
}

void setup() {
  Serial.begin(115200);
  pinMode(LIGHT_PIN, OUTPUT); pinMode(BUILTIN_PIN, OUTPUT); pinMode(BUZZER_PIN, OUTPUT);
  pinMode(PIR_PIN, INPUT); pinMode(BUTTON_PIN, INPUT_PULLUP);
  digitalWrite(LIGHT_PIN, LOW); digitalWrite(BUILTIN_PIN, LOW); digitalWrite(BUZZER_PIN, LOW);
  pinMode(DHT_POWER_PIN, OUTPUT); digitalWrite(DHT_POWER_PIN, HIGH);
  delay(1500);                         // DHT11 전원 안정화
  dht.begin();
  startNetwork();

  const char *hdrs[] = {"X-Auth-Token"};
  server.collectHeaders(hdrs, 1);
  server.on("/api/status", HTTP_GET, apiStatus);
  server.on("/api/events", HTTP_GET, apiEvents);
  server.on("/api/logs", HTTP_GET, apiLogs);
  server.on("/api/security", HTTP_GET, apiSecurity);
  server.on("/api/ack", HTTP_POST, apiAck);
  server.on("/", HTTP_GET, [] {                // 보호자 웹 대시보드 (아이폰 Safari 등)
    server.sendHeader("Cache-Control", "no-store");
    server.send_P(200, "text/html; charset=utf-8", WEB_PAGE);
  });
  server.onNotFound([] {                       // 알 수 없는 경로 = 스캐닝 의심
    if (server.method() == HTTP_OPTIONS) { cors(); server.send(204); return; }  // CORS 사전 요청
    logAccess(server.client().remoteIP().toString(), false);
    server.send(404, "text/plain", "not found");
  });
  server.begin();
  Serial.printf("HomeCareNode room %d (%s) ready, id %s\n", ROOM_ID, ROOM_LOCATION[ROOM_ID], deviceId.c_str());
}

void loop() {
  uint32_t now = millis();
  edgeControl(now);
  keepWifi(now);
  server.handleClient();

  static uint32_t lastPrint = 0;
  if (elapsed(now, lastPrint, 2000)) {
    lastPrint = now;
    Serial.printf("T=%s H=%s motion=%d light=%d events=%lu\n", jnum(tempC).c_str(), jnum(hum).c_str(),
                  motionNow, digitalRead(LIGHT_PIN), (unsigned long)evtCount);
  }
}
