// Snake VERSUS 2 joueurs sur ESP32-S3 + écran OLED, tout en websocket.
// Chaque joueur se co au wifi "SNAKE-BRIGADE" et ouvre http://192.168.4.1 sur son tel.
// J1 = serpent plein, J2 = serpent en contour, pomme = petit point. Dernier en vie gagne.
// (lib à installer : WebSockets de Markus Sattler)

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>

#define USE_AP true
const char* AP_SSID  = "SNAKE-BRIGADE";
const char* AP_PASS  = "";
const char* STA_SSID = "TON_WIFI";
const char* STA_PASS = "TON_MOT_DE_PASSE";

#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64
#define OLED_ADDR 0x3C
#define OLED_SDA 8
#define OLED_SCL 9
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

WebServer server(80);
WebSocketsServer webSocket(81);

// cases de 4px, 8px réservés en haut pour le score -> plateau 32x14
#define CELL 4
#define TOPBAR 8
#define GRID_W (SCREEN_WIDTH / CELL)
#define GRID_H ((SCREEN_HEIGHT - TOPBAR) / CELL)
#define MAX_LEN (GRID_W * GRID_H)
#define MAXP 2

struct Player {
  uint8_t bx[MAX_LEN], by[MAX_LEN];
  int len;
  int dx, dy, pdx, pdy;   // direction courante + celle demandée
  int score;
  bool alive;             // en vie dans la manche en cours
  bool connected;         // un client websocket occupe ce slot
  int  wsNum;             // le client en question (-1 = personne)
};
Player P[MAXP];

int numRacers = 0;        // nb de serpents dans la manche
int soloIdx = -1;         // si un seul joueur, lequel
uint8_t foodX, foodY;
bool paused, dirty;
unsigned long lastMove = 0;
unsigned long moveInterval = 130;

enum GState { LOBBY, PLAYING, RESULT };
GState state = LOBBY;
String resultText = "";
String statusText = "";
String netName, ipStr;

const char INDEX_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html><html lang="fr"><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no">
<title>Snake 2P — Brigade Sud</title>
<style>
  :root{--accent:#39ff14}
  *{box-sizing:border-box;-webkit-user-select:none;user-select:none;-webkit-tap-highlight-color:transparent}
  body{margin:0;min-height:100vh;background:#0b0f0b;color:#e8ffe8;
       font-family:ui-monospace,Menlo,Consolas,monospace;display:flex;flex-direction:column;
       align-items:center;justify-content:center;gap:14px;padding:20px}
  h1{margin:0;font-size:1.6rem;letter-spacing:2px;color:var(--accent);text-shadow:0 0 10px #1a4d10}
  #me{font-size:1rem;font-weight:bold}
  #status{font-size:.8rem;opacity:.85}
  #status.ok{color:var(--accent)} #status.ko{color:#ff5a5a}
  #score{font-size:1.05rem;min-height:1.2em;text-align:center}
  .hint{font-size:.72rem;opacity:.55;text-align:center;line-height:1.6}
  .pad{display:grid;grid-template-columns:repeat(3,72px);grid-template-rows:repeat(3,72px);gap:8px}
  button{font:inherit;font-size:1.5rem;color:#e8ffe8;background:#14210f;border:1px solid #2c4a1c;
         border-radius:14px;cursor:pointer;touch-action:manipulation}
  button:active{background:var(--accent);color:#0b0f0b}
  .wide{width:232px;font-size:1rem}
  .empty{visibility:hidden}
</style></head><body>
  <h1>&#128013; SNAKE 2P</h1>
  <div id="me">&hellip;</div>
  <div id="status" class="ko">Connexion&hellip;</div>
  <div id="score">&nbsp;</div>
  <div class="pad">
    <div class="empty"></div><button data-k="U">&#9650;</button><div class="empty"></div>
    <button data-k="L">&#9664;</button><button data-k="P">&#9208;</button><button data-k="R">&#9654;</button>
    <div class="empty"></div><button data-k="D">&#9660;</button><div class="empty"></div>
  </div>
  <button class="wide" data-k="X">&#8635; Rejouer</button>
  <div class="hint">Fl&egrave;ches ou WASD &middot; P = pause &middot; Espace = rejouer</div>
<script>
let ws,connected=false,me=0;
const st=document.getElementById('status'),sc=document.getElementById('score'),meEl=document.getElementById('me');
function setStatus(ok){connected=ok;st.textContent=ok?'Connecté ✔':'Déconnecté ✖';st.className=ok?'ok':'ko';}
function showMe(){
  if(me===1) meEl.textContent='🟩 Joueur 1 (serpent plein)';
  else if(me===2) meEl.textContent='⬜ Joueur 2 (serpent contour)';
  else meEl.textContent='👀 Partie pleine — spectateur';
}
function onMsg(m){
  if(m.indexOf('you:')===0){ me=+m.slice(4); showMe(); }
  else if(m.indexOf('info:')===0) sc.textContent=m.slice(5);
}
function connect(){
  ws=new WebSocket('ws://'+location.hostname+':81/');
  ws.onopen=()=>setStatus(true);
  ws.onmessage=e=>onMsg(e.data);
  ws.onclose=()=>{setStatus(false);setTimeout(connect,800);};
  ws.onerror=()=>ws.close();
}
function send(k){if(connected&&ws.readyState===1)ws.send(k);}
connect();
const KM={ArrowUp:'U',ArrowDown:'D',ArrowLeft:'L',ArrowRight:'R',
          w:'U',s:'D',a:'L',d:'R',W:'U',S:'D',A:'L',D:'R',p:'P',P:'P',' ':'X'};
document.addEventListener('keydown',e=>{
  const k=KM[e.key];if(!k)return;e.preventDefault();if(e.repeat)return;send(k);
},{passive:false});
document.querySelectorAll('button[data-k]').forEach(b=>{
  b.addEventListener('pointerdown',e=>{e.preventDefault();send(b.dataset.k);});
});
</script></body></html>
)HTML";

void handleRoot() { server.send_P(200, "text/html", INDEX_HTML); }
void wsBroadcast(const String& s) { webSocket.broadcastTXT(s.c_str(), s.length()); }
void wsSend(uint8_t num, const String& s) { webSocket.sendTXT(num, s.c_str(), s.length()); }

int connectedCount() { int c = 0; for (int i = 0; i < MAXP; i++) if (P[i].connected) c++; return c; }

// construit le texte d'état et l'envoie à toutes les pages
void updateStatus() {
  String t;
  if (state == LOBBY)       t = "En attente (" + String(connectedCount()) + "/2) - appuie sur une fleche";
  else if (state == RESULT) {
    if (numRacers >= 2)     t = resultText + "   (P1 " + P[0].score + " / P2 " + P[1].score + ")";
    else                    t = resultText + "   (score " + P[soloIdx < 0 ? 0 : soloIdx].score + ")";
  } else {
    if (numRacers >= 2)     t = "P1  " + String(P[0].score) + "     P2  " + String(P[1].score);
    else                    t = "Score " + String(P[soloIdx < 0 ? 0 : soloIdx].score);
  }
  statusText = t;
  wsBroadcast("info:" + t);
}

void placeFood() {
  bool bad;
  do {
    bad = false;
    foodX = random(GRID_W);
    foodY = random(GRID_H);
    for (int i = 0; i < MAXP && !bad; i++)
      if (P[i].alive)
        for (int k = 0; k < P[i].len; k++)
          if (P[i].bx[k] == foodX && P[i].by[k] == foodY) { bad = true; break; }
  } while (bad);
}

void spawnPlayer(int i) {
  P[i].len = 3;
  if (i == 0) {                       // J1 démarre en haut à gauche, va à droite
    int hx = 5, hy = 3;
    for (int k = 0; k < 3; k++) { P[i].bx[k] = hx - k; P[i].by[k] = hy; }
    P[i].dx = 1; P[i].dy = 0;
  } else {                            // J2 démarre en bas à droite, va à gauche
    int hx = GRID_W - 6, hy = GRID_H - 4;
    for (int k = 0; k < 3; k++) { P[i].bx[k] = hx + k; P[i].by[k] = hy; }
    P[i].dx = -1; P[i].dy = 0;
  }
  P[i].pdx = P[i].dx; P[i].pdy = P[i].dy;
  P[i].score = 0;
  P[i].alive = true;
}

void startRound() {
  numRacers = 0; soloIdx = -1;
  for (int i = 0; i < MAXP; i++) {
    if (P[i].connected) { spawnPlayer(i); numRacers++; soloIdx = i; }
    else { P[i].alive = false; P[i].len = 0; }
  }
  if (numRacers == 0) return;         // personne de branché, on reste en lobby
  if (numRacers >= 2) soloIdx = -1;
  moveInterval = 130;
  paused = false;
  placeFood();
  state = PLAYING;
  lastMove = millis();
  dirty = true;
  updateStatus();
}

void setPlayerDir(int i, int nx, int ny) {
  if (P[i].len > 1 && nx == -P[i].dx && ny == -P[i].dy) return;  // pas de demi-tour
  P[i].pdx = nx; P[i].pdy = ny;
}

void applyCommand(int i, char c) {
  if (i < 0) return;                  // spectateur, il regarde et c'est tout
  if (c == 'P') { if (state == PLAYING) { paused = !paused; dirty = true; } return; }
  if (c == 'X' || c == ' ') { if (state != PLAYING) startRound(); return; }

  int nx = 0, ny = 0;
  switch (c) {
    case 'U': ny = -1; break;
    case 'D': ny =  1; break;
    case 'L': nx = -1; break;
    case 'R': nx =  1; break;
    default: return;
  }
  if (state != PLAYING) startRound();
  setPlayerDir(i, nx, ny);
  dirty = true;
}

// la tête du joueur i tape-t-elle un corps (le sien sauf sa tête, ou l'autre) ?
bool headCollides(int i, bool wallDead[]) {
  int hx = P[i].bx[0], hy = P[i].by[0];
  for (int j = 0; j < MAXP; j++) {
    if (!P[j].alive || wallDead[j]) continue;
    int start = (j == i) ? 1 : 0;
    for (int k = start; k < P[j].len; k++)
      if (P[j].bx[k] == hx && P[j].by[k] == hy) return true;
  }
  return false;
}

void endRound() {
  state = RESULT;
  paused = false;
  if (numRacers < 2) {
    resultText = "Game over";
  } else {
    int aliveCount = 0, last = -1;
    for (int i = 0; i < MAXP; i++) if (P[i].alive) { aliveCount++; last = i; }
    int win;
    if (aliveCount == 1) win = last;                    // survivant = vainqueur
    else win = (P[0].score > P[1].score) ? 0 : (P[1].score > P[0].score ? 1 : -1);
    resultText = (win < 0) ? "Egalite !" : ("Joueur " + String(win + 1) + " gagne !");
  }
  dirty = true;
  updateStatus();
}

void step() {
  bool wallDead[MAXP] = {false, false};
  bool overlap[MAXP]  = {false, false};
  bool grows[MAXP]    = {false, false};
  int nhx[MAXP], nhy[MAXP];

  // 1. intentions + morts contre les murs
  for (int i = 0; i < MAXP; i++) {
    if (!P[i].alive) continue;
    P[i].dx = P[i].pdx; P[i].dy = P[i].pdy;
    nhx[i] = P[i].bx[0] + P[i].dx;
    nhy[i] = P[i].by[0] + P[i].dy;
    if (nhx[i] < 0 || nhx[i] >= GRID_W || nhy[i] < 0 || nhy[i] >= GRID_H) wallDead[i] = true;
    else grows[i] = (nhx[i] == foodX && nhy[i] == foodY);
  }

  // 2. on avance les serpents qui n'ont pas mordu un mur
  for (int i = 0; i < MAXP; i++) {
    if (!P[i].alive || wallDead[i]) continue;
    int lim = (P[i].len < MAX_LEN - 1) ? P[i].len : MAX_LEN - 1;
    for (int k = lim; k > 0; k--) { P[i].bx[k] = P[i].bx[k-1]; P[i].by[k] = P[i].by[k-1]; }
    P[i].bx[0] = nhx[i]; P[i].by[0] = nhy[i];
    if (grows[i] && P[i].len < MAX_LEN) P[i].len++;
  }

  // 3. collisions entre corps (une fois tout le monde déplacé) -> gère le choc frontal
  for (int i = 0; i < MAXP; i++)
    if (P[i].alive && !wallDead[i]) overlap[i] = headCollides(i, wallDead);

  // 4. on applique les morts
  for (int i = 0; i < MAXP; i++) if (wallDead[i] || overlap[i]) P[i].alive = false;

  // 5. pomme + score pour les survivants qui ont mangé
  bool ate = false;
  for (int i = 0; i < MAXP; i++)
    if (P[i].alive && grows[i]) { P[i].score++; ate = true; if (moveInterval > 60) moveInterval -= 2; }
  if (ate) { placeFood(); updateStatus(); }

  dirty = true;

  // 6. fin de manche ? (à 2 : dès qu'il reste 1 vivant ; en solo : quand le seul meurt)
  int aliveCount = 0;
  for (int i = 0; i < MAXP; i++) if (P[i].alive) aliveCount++;
  bool over = (numRacers >= 2) ? (aliveCount <= 1) : (aliveCount == 0);
  if (over) endRound();
}

void drawSnake(int i) {
  for (int k = 0; k < P[i].len; k++) {
    int px = P[i].bx[k] * CELL, py = TOPBAR + P[i].by[k] * CELL;
    if (k == 0)      display.fillRect(px, py, CELL, CELL, SSD1306_WHITE);          // tête pleine
    else if (i == 0) display.fillRect(px, py, CELL - 1, CELL - 1, SSD1306_WHITE);  // J1 corps plein
    else             display.drawRect(px, py, CELL, CELL, SSD1306_WHITE);          // J2 corps en contour
  }
}

void draw() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  if (state == LOBBY) {
    display.setCursor(28, 2);  display.print(F("SNAKE 2P"));
    display.setCursor(2, 16);  display.print(F("WiFi: ")); display.print(netName);
    display.setCursor(2, 28);  display.print(ipStr);
    display.setCursor(2, 42);  display.print(F("Joueurs: ")); display.print(connectedCount()); display.print(F("/2"));
    display.setCursor(2, 54);  display.print(F("Fleche = jouer"));
    display.display();
    return;
  }

  if (state == RESULT) {
    display.setCursor(4, 8);   display.print(resultText);
    display.setCursor(4, 26);
    if (numRacers >= 2) { display.print(F("P1 ")); display.print(P[0].score); display.print(F("   P2 ")); display.print(P[1].score); }
    else                { display.print(F("Score ")); display.print(P[soloIdx < 0 ? 0 : soloIdx].score); }
    display.setCursor(13, 48); display.print(F("Fleche = rejouer"));
    display.display();
    return;
  }

  // barre de score
  display.setCursor(0, 0);
  if (numRacers >= 2) { display.print(F("P1:")); display.print(P[0].score); display.print(F(" P2:")); display.print(P[1].score); }
  else                { display.print(F("Score: ")); display.print(P[soloIdx < 0 ? 0 : soloIdx].score); }

  // pomme = petit point au centre de sa case
  display.fillRect(foodX * CELL + 1, TOPBAR + foodY * CELL + 1, CELL - 2, CELL - 2, SSD1306_WHITE);

  for (int i = 0; i < MAXP; i++) if (P[i].alive) drawSnake(i);

  if (paused) {
    display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
    display.setCursor(49, 32); display.print(F("PAUSE"));
    display.setTextColor(SSD1306_WHITE);
  }
  display.display();
}

int slotOf(int num)  { for (int i = 0; i < MAXP; i++) if (P[i].connected && P[i].wsNum == num) return i; return -1; }
int assignSlot(int num) {
  for (int i = 0; i < MAXP; i++)
    if (!P[i].connected) { P[i].connected = true; P[i].wsNum = num; P[i].alive = false; P[i].len = 0; return i; }
  return -1;   // partie pleine
}
void freeSlot(int num) { int i = slotOf(num); if (i >= 0) { P[i].connected = false; P[i].wsNum = -1; P[i].alive = false; } }

void onWsEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t len) {
  switch (type) {
    case WStype_CONNECTED: {
      int slot = assignSlot(num);
      wsSend(num, "you:" + String(slot < 0 ? 0 : slot + 1));
      updateStatus();                     // met à jour le compteur de joueurs partout
      dirty = true;
      break;
    }
    case WStype_DISCONNECTED:
      freeSlot(num);
      updateStatus();
      dirty = true;
      break;
    case WStype_TEXT:
      if (len > 0) applyCommand(slotOf(num), (char)payload[0]);
      break;
    default: break;
  }
}

void setup() {
  Serial.begin(115200);
  Wire.begin(OLED_SDA, OLED_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR, true, false)) {
    Serial.println("pas d'écran ?!");
    while (true) delay(1000);
  }

  for (int i = 0; i < MAXP; i++) { P[i].connected = false; P[i].wsNum = -1; P[i].alive = false; P[i].len = 0; }

  if (USE_AP) {
    WiFi.mode(WIFI_AP);
    if (strlen(AP_PASS) > 0) WiFi.softAP(AP_SSID, AP_PASS); else WiFi.softAP(AP_SSID);
    netName = AP_SSID; ipStr = WiFi.softAPIP().toString();
  } else {
    WiFi.mode(WIFI_STA);
    WiFi.begin(STA_SSID, STA_PASS);
    unsigned long t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) delay(250);
    netName = STA_SSID;
    ipStr = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : String("WiFi KO");
  }
  Serial.println("http://" + ipStr);

  server.on("/", handleRoot);
  server.begin();
  webSocket.begin();
  webSocket.onEvent(onWsEvent);
  randomSeed(esp_random());

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE); display.setTextSize(1);
  display.setCursor(20, 4);  display.print(F("SNAKE 2P VERSUS"));
  display.setCursor(2, 24);  display.print(F("WiFi: ")); display.print(netName);
  display.setCursor(2, 38);  display.print(F("http://")); display.print(ipStr);
  display.display();
  delay(2500);

  state = LOBBY;
  updateStatus();
  dirty = true;
}

void loop() {
  server.handleClient();
  webSocket.loop();

  if (state == PLAYING && !paused && millis() - lastMove >= moveInterval) {
    lastMove = millis();
    step();
  }
  if (dirty) { draw(); dirty = false; }
}
