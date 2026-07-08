// Snake sur ESP32-S3 + petit écran OLED, piloté depuis le navigateur en websocket.
// Tu te co au wifi "SNAKE-BRIGADE" et t'ouvres http://192.168.4.1
// (lib à installer : WebSockets de Markus Sattler)

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>

#define USE_AP true                       // false -> rejoint ton wifi au lieu de créer le sien
const char* AP_SSID  = "SNAKE-BRIGADE";
const char* AP_PASS  = "";                // vide = réseau ouvert
const char* STA_SSID = "TON_WIFI";
const char* STA_PASS = "TON_MOT_DE_PASSE";

#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64
#define OLED_ADDR 0x3C
#define OLED_SDA 8                         // faut forcer les pins, la S3 colle pas l'i2c sur 21/22
#define OLED_SCL 9
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

WebServer server(80);
WebSocketsServer webSocket(81);

// cases de 4px -> plateau de 32x16
#define CELL 4
#define GRID_W (SCREEN_WIDTH / CELL)
#define GRID_H (SCREEN_HEIGHT / CELL)
#define MAX_LEN (GRID_W * GRID_H)

uint8_t snakeX[MAX_LEN], snakeY[MAX_LEN];
int snakeLen;
int dirX, dirY, pendDirX, pendDirY;
uint8_t foodX, foodY;
int score;
bool gameOver, paused, waiting, dirty;
unsigned long lastMove = 0;
unsigned long moveInterval = 130;         // plus petit = plus rapide

String netName, ipStr;

// la page est en dur, plus simple que de trimballer un fichier
const char INDEX_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html><html lang="fr"><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no">
<title>Snake — Brigade Sud</title>
<style>
  :root{--accent:#39ff14}
  *{box-sizing:border-box;-webkit-user-select:none;user-select:none;-webkit-tap-highlight-color:transparent}
  body{margin:0;min-height:100vh;background:#0b0f0b;color:#e8ffe8;
       font-family:ui-monospace,Menlo,Consolas,monospace;display:flex;flex-direction:column;
       align-items:center;justify-content:center;gap:16px;padding:20px}
  h1{margin:0;font-size:1.7rem;letter-spacing:3px;color:var(--accent);text-shadow:0 0 10px #1a4d10}
  #status{font-size:.85rem;opacity:.85}
  #status.ok{color:var(--accent)} #status.ko{color:#ff5a5a}
  #score{font-size:1.05rem;min-height:1.2em}
  .hint{font-size:.75rem;opacity:.55;text-align:center;line-height:1.6}
  .pad{display:grid;grid-template-columns:repeat(3,72px);grid-template-rows:repeat(3,72px);gap:8px}
  button{font:inherit;font-size:1.5rem;color:#e8ffe8;background:#14210f;border:1px solid #2c4a1c;
         border-radius:14px;cursor:pointer;touch-action:manipulation}
  button:active{background:var(--accent);color:#0b0f0b}
  .wide{width:232px;font-size:1rem}
  .empty{visibility:hidden}
</style></head><body>
  <h1>&#128013; SNAKE</h1>
  <div id="status" class="ko">Connexion&hellip;</div>
  <div id="score">&nbsp;</div>
  <div class="pad">
    <div class="empty"></div><button data-k="U">&#9650;</button><div class="empty"></div>
    <button data-k="L">&#9664;</button><button data-k="P">&#9208;</button><button data-k="R">&#9654;</button>
    <div class="empty"></div><button data-k="D">&#9660;</button><div class="empty"></div>
  </div>
  <button class="wide" data-k="X">&#8635; Rejouer</button>
  <div class="hint">Clavier : fl&egrave;ches ou WASD &middot; P = pause &middot; Espace = rejouer</div>
<script>
let ws,connected=false;
const st=document.getElementById('status'),sc=document.getElementById('score');
function setStatus(ok){connected=ok;st.textContent=ok?'Connecté ✔':'Déconnecté ✖';st.className=ok?'ok':'ko';}
function onMsg(m){
  if(m.indexOf('s:')===0) sc.textContent='Score : '+m.slice(2);
  else if(m.indexOf('over:')===0) sc.textContent='\u{1F480} Game Over — Score '+m.slice(5);
  else if(m==='ready') sc.textContent='Prêt ! Appuie sur une flèche';
}
function connect(){
  ws=new WebSocket('ws://'+location.hostname+':81/');
  ws.onopen=()=>setStatus(true);
  ws.onmessage=e=>onMsg(e.data);
  ws.onclose=()=>{setStatus(false);setTimeout(connect,800);}; // ça se reconnecte tout seul
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

void placeFood() {
  // on tire une case au pif tant qu'elle tombe pas sur le serpent
  bool onSnake;
  do {
    onSnake = false;
    foodX = random(GRID_W);
    foodY = random(GRID_H);
    for (int i = 0; i < snakeLen; i++)
      if (snakeX[i] == foodX && snakeY[i] == foodY) { onSnake = true; break; }
  } while (onSnake);
}

void resetGame() {
  snakeLen = 3;
  int cx = GRID_W / 2, cy = GRID_H / 2;
  for (int i = 0; i < snakeLen; i++) { snakeX[i] = cx - i; snakeY[i] = cy; }
  dirX = 1; dirY = 0; pendDirX = 1; pendDirY = 0;
  score = 0;
  gameOver = false;
  paused = false;
  waiting = true;              // on attend la 1re flèche avant de démarrer
  moveInterval = 130;
  placeFood();
  lastMove = millis();
  dirty = true;
  wsBroadcast("ready");
}

void setDir(int nx, int ny) {
  if (snakeLen > 1 && nx == -dirX && ny == -dirY) return;  // pas de demi-tour, sinon suicide direct
  pendDirX = nx; pendDirY = ny;
}

// pour (re)partir dans un sens sans se faire jeter par le check du demi-tour
void startDir(int nx, int ny) {
  dirX = pendDirX = nx; dirY = pendDirY = ny; waiting = false;
}

void applyCommand(char c) {
  if (c == 'P') { if (!gameOver && !waiting) { paused = !paused; dirty = true; } return; }
  if (c == 'X' || c == ' ') { if (gameOver) resetGame(); return; }

  int nx = 0, ny = 0; bool isDir = true;
  switch (c) {
    case 'U': ny = -1; break;
    case 'D': ny =  1; break;
    case 'L': nx = -1; break;
    case 'R': nx =  1; break;
    default:  isDir = false; break;
  }
  if (!isDir) return;

  if (gameOver)     { resetGame(); startDir(nx, ny); }
  else if (waiting) { startDir(nx, ny); }
  else              { setDir(nx, ny); }
  dirty = true;
}

void step() {
  dirX = pendDirX; dirY = pendDirY;
  int newX = snakeX[0] + dirX;
  int newY = snakeY[0] + dirY;

  if (newX < 0 || newX >= GRID_W || newY < 0 || newY >= GRID_H) { gameOver = true; dirty = true; return; }

  bool willEat = (newX == foodX && newY == foodY);

  // si on mange pas, la queue va bouger, donc on l'ignore dans la collision
  int checkLen = willEat ? snakeLen : snakeLen - 1;
  for (int i = 0; i < checkLen; i++)
    if (snakeX[i] == newX && snakeY[i] == newY) { gameOver = true; dirty = true; return; }

  // on décale tout le corps d'un cran, la tête prend la nouvelle case
  int limit = (snakeLen < MAX_LEN - 1) ? snakeLen : MAX_LEN - 1;
  for (int i = limit; i > 0; i--) { snakeX[i] = snakeX[i-1]; snakeY[i] = snakeY[i-1]; }
  snakeX[0] = newX; snakeY[0] = newY;

  if (willEat) {
    score++;
    if (snakeLen < MAX_LEN) snakeLen++;
    if (snakeLen >= MAX_LEN) gameOver = true;               // plateau plein, t'as gagné
    else { placeFood(); if (moveInterval > 60) moveInterval -= 3; }  // et ça accélère un peu
    wsBroadcast(String("s:") + score);
  }
  dirty = true;
}

void draw() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  if (gameOver) {
    display.setCursor(34, 14); display.print(F("GAME OVER"));
    display.setCursor(37, 30); display.print(F("Score: ")); display.print(score);
    display.setCursor(13, 48); display.print(F("Fleche = rejouer"));
    display.display();
    return;
  }

  if (waiting) {
    display.setCursor(40, 2);  display.print(F("SNAKE"));
    display.setCursor(2, 18);  display.print(F("WiFi: ")); display.print(netName);
    display.setCursor(2, 30);  display.print(ipStr);
    display.setCursor(2, 52);  display.print(F("Fleche = jouer"));
    display.display();
    return;
  }

  display.fillRect(foodX * CELL, foodY * CELL, CELL, CELL, SSD1306_WHITE);
  for (int i = 0; i < snakeLen; i++) {
    // tête pleine, corps un peu plus petit pour voir les anneaux
    int s = (i == 0) ? CELL : CELL - 1;
    display.fillRect(snakeX[i] * CELL, snakeY[i] * CELL, s, s, SSD1306_WHITE);
  }
  if (paused) {
    display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
    display.setCursor(49, 28); display.print(F("PAUSE"));
    display.setTextColor(SSD1306_WHITE);
  }
  display.display();
}

void onWsEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t len) {
  if (type == WStype_CONNECTED) {
    // on renvoie l'état courant pour que la page affiche le bon texte
    if (gameOver)     wsSend(num, String("over:") + score);
    else if (waiting) wsSend(num, "ready");
    else              wsSend(num, String("s:") + score);
  }
  else if (type == WStype_TEXT && len > 0) {
    applyCommand((char)payload[0]);
  }
}

void setup() {
  Serial.begin(115200);
  Wire.begin(OLED_SDA, OLED_SCL);
  // periphBegin=false sinon la lib rappelle Wire.begin() et l'S3 repart sur ses pins par défaut
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR, true, false)) {
    Serial.println("pas d'écran ?!");
    while (true) delay(1000);
  }

  if (USE_AP) {
    WiFi.mode(WIFI_AP);
    if (strlen(AP_PASS) > 0) WiFi.softAP(AP_SSID, AP_PASS);
    else                     WiFi.softAP(AP_SSID);
    netName = AP_SSID;
    ipStr   = WiFi.softAPIP().toString();
  } else {
    WiFi.mode(WIFI_STA);
    WiFi.begin(STA_SSID, STA_PASS);
    unsigned long t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) delay(250);  // 15s max sinon tant pis
    netName = STA_SSID;
    ipStr   = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : String("WiFi KO");
  }
  Serial.println("http://" + ipStr);

  server.on("/", handleRoot);
  server.begin();
  webSocket.begin();
  webSocket.onEvent(onWsEvent);

  randomSeed(esp_random());

  // petit splash avec l'adresse à ouvrir
  display.clearDisplay();
  display.setCursor(24, 4);  display.print(F("SNAKE  WEBSOCKET"));
  display.setCursor(2, 24);  display.print(F("WiFi: ")); display.print(netName);
  display.setCursor(2, 38);  display.print(F("http://")); display.print(ipStr);
  display.display();
  delay(2500);

  resetGame();
}

void loop() {
  server.handleClient();
  webSocket.loop();

  // le jeu tourne sur une horloge millis(), on avance d'un cran toutes les moveInterval ms
  if (!gameOver && !waiting && !paused && millis() - lastMove >= moveInterval) {
    lastMove = millis();
    step();
    if (gameOver) wsBroadcast(String("over:") + score);
  }

  if (dirty) { draw(); dirty = false; }  // on redessine que si y'a eu un changement
}
