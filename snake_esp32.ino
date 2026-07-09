// =====================================================================
//  SNAKE 2 JOUEURS  -  ESP32-S3 + petit écran OLED
// =====================================================================
// En gros : l'ESP32 fait tourner un Snake en versus à 2 joueurs.
// Chaque joueur ouvre une page web sur son tel et dirige son serpent
// avec les flèches, en temps réel grâce au websocket. Les deux serpents
// s'affichent sur le même écran OLED.
// J1 = serpent plein, J2 = serpent en contour, dernier en vie gagne.
//
// L'ESP32 essaie de rejoindre le wifi "decode-etudiants" ; s'il y arrive
// pas, il crée son propre wifi de secours pour qu'on puisse jouer quand même.
//
// Seule lib à installer : "WebSockets" de Markus Sattler.
// =====================================================================

// ---------- Les librairies dont on a besoin ----------
#include <Wire.h>               // pour parler à l'écran en I2C
#include <Adafruit_GFX.h>       // le moteur de dessin (formes, texte...)
#include <Adafruit_SSD1306.h>   // le pilote de notre écran OLED
#include <WiFi.h>               // le wifi de l'ESP32
#include <WebServer.h>          // pour héberger la page web
#include <WebSocketsServer.h>   // pour recevoir les touches en direct

// ---------- Réglages du wifi ----------
// USE_AP = false -> on rejoint un wifi qui existe déjà (mode STA).
// USE_AP = true  -> l'ESP32 crée son propre wifi (mode point d'accès).
#define USE_AP false
const char* AP_SSID  = "SNAKE-BRIGADE";     // nom du wifi qu'on crée (secours)
const char* AP_PASS  = "";                  // vide = pas de mot de passe
const char* STA_SSID = "decode-etudiants";  // le wifi qu'on veut rejoindre
const char* STA_PASS = "";                  // <-- ton mot de passe wifi ici

// ---------- Réglages de l'écran OLED ----------
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64
#define OLED_ADDR 0x3C          // l'adresse I2C de l'écran (0x3D si jamais ça marche pas)
#define OLED_SDA 8              // les broches I2C. On les force parce que la S3
#define OLED_SCL 9              // met pas l'I2C sur les mêmes broches qu'un ESP32 normal.
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// ---------- Les deux serveurs ----------
WebServer        server(80);     // sert la page web (port 80, le classique)
WebSocketsServer webSocket(81);  // reçoit les touches en temps réel (port 81)

// ---------- La grille de jeu ----------
// Une case fait 4 pixels. On garde 8px tout en haut pour afficher le score,
// donc le terrain fait 32 cases de large sur 14 de haut.
#define CELL 4
#define TOPBAR 8
#define GRID_W (SCREEN_WIDTH / CELL)          // 32
#define GRID_H ((SCREEN_HEIGHT - TOPBAR) / CELL)  // 14
#define MAX_LEN (GRID_W * GRID_H)             // taille max d'un serpent
#define MAXP 2                                // nombre de joueurs max

// ---------- Un joueur = un serpent ----------
// Ça c'est tout ce qui décrit un serpent : où sont ses cases, sa direction,
// son score, s'il est encore en vie, et à quel joueur il appartient.
struct Player {
  uint8_t bx[MAX_LEN], by[MAX_LEN];  // les coordonnées de chaque bout du corps
  int len;                           // sa longueur actuelle
  int dx, dy, pdx, pdy;              // direction actuelle + direction demandée
  int score;
  bool alive;                        // vivant dans la manche en cours ?
  bool connected;                    // un joueur est branché sur ce slot ?
  int  wsNum;                        // le numéro de sa connexion websocket (-1 = personne)
};
Player P[MAXP];   // nos deux joueurs

// ---------- Les variables globales du jeu ----------
int numRacers = 0;        // combien de serpents dans la manche
int soloIdx = -1;         // si on joue tout seul, c'est quel joueur
uint8_t foodX, foodY;     // position de la pomme
bool paused, dirty;       // en pause ? / faut-il redessiner l'écran ?
unsigned long lastMove = 0;
unsigned long moveInterval = 130;  // temps entre 2 déplacements (plus petit = plus rapide)

// Les 3 états possibles du jeu :
// LOBBY = on attend les joueurs, PLAYING = on joue, RESULT = fin de manche.
enum GState { LOBBY, PLAYING, RESULT };
GState state = LOBBY;
String resultText = "";   // le texte affiché en fin de manche
String statusText = "";   // le texte de score envoyé aux pages web
String netName, ipStr;    // le nom du wifi + l'IP à ouvrir (affichés à l'écran)

// ---------- La page web ----------
// En gros c'est toute la page (HTML + style + JavaScript) que l'ESP32 balance
// au navigateur quand on ouvre son IP. Le JS capte les flèches et les boutons
// tactiles, et envoie la touche à l'ESP32 par le websocket.
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

// ---------- Petites fonctions utilitaires ----------
// Quand le navigateur demande la page "/", on lui renvoie tout le HTML.
void handleRoot() { server.send_P(200, "text/html", INDEX_HTML); }
// Envoyer un message à toutes les pages / à une page précise.
void wsBroadcast(const String& s) { webSocket.broadcastTXT(s.c_str(), s.length()); }
void wsSend(uint8_t num, const String& s) { webSocket.sendTXT(num, s.c_str(), s.length()); }
// Combien de joueurs sont branchés en ce moment.
int connectedCount() { int c = 0; for (int i = 0; i < MAXP; i++) if (P[i].connected) c++; return c; }

// ---------- Le texte d'état envoyé aux pages ----------
// En gros ça fabrique la petite phrase de score/état ("P1 3  P2 5", "Égalité !"...)
// et l'envoie à toutes les pages web pour qu'elles l'affichent.
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

// ---------- Placer la pomme ----------
// On tire une case au hasard, et on recommence tant qu'elle tombe sur un serpent.
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

// ---------- Faire apparaître un serpent au début de la manche ----------
// J1 démarre en haut à gauche et va à droite, J2 en bas à droite et va à gauche.
// Comme ça ils partent chacun dans leur coin et se foncent un peu dessus.
void spawnPlayer(int i) {
  P[i].len = 3;
  if (i == 0) {
    int hx = 5, hy = 3;
    for (int k = 0; k < 3; k++) { P[i].bx[k] = hx - k; P[i].by[k] = hy; }
    P[i].dx = 1; P[i].dy = 0;
  } else {
    int hx = GRID_W - 6, hy = GRID_H - 4;
    for (int k = 0; k < 3; k++) { P[i].bx[k] = hx + k; P[i].by[k] = hy; }
    P[i].dx = -1; P[i].dy = 0;
  }
  P[i].pdx = P[i].dx; P[i].pdy = P[i].dy;
  P[i].score = 0;
  P[i].alive = true;
}

// ---------- Démarrer une manche ----------
// On fait apparaître un serpent pour chaque joueur branché, on pose la pomme,
// et on passe en mode "on joue".
void startRound() {
  numRacers = 0; soloIdx = -1;
  for (int i = 0; i < MAXP; i++) {
    if (P[i].connected) { spawnPlayer(i); numRacers++; soloIdx = i; }
    else { P[i].alive = false; P[i].len = 0; }
  }
  if (numRacers == 0) return;         // si personne n'est branché, on reste au lobby
  if (numRacers >= 2) soloIdx = -1;   // à 2 joueurs, pas de "solo"
  moveInterval = 130;
  paused = false;
  placeFood();
  state = PLAYING;
  lastMove = millis();
  dirty = true;
  updateStatus();
}

// ---------- Changer la direction d'un serpent ----------
// On refuse le demi-tour direct, sinon le serpent se rentre dedans tout seul.
void setPlayerDir(int i, int nx, int ny) {
  if (P[i].len > 1 && nx == -P[i].dx && ny == -P[i].dy) return;
  P[i].pdx = nx; P[i].pdy = ny;
}

// ---------- Traiter une touche reçue ----------
// i = quel joueur a appuyé, c = la touche. Ça c'est le "cerveau" des commandes.
void applyCommand(int i, char c) {
  if (i < 0) return;                  // un spectateur : il regarde, il joue pas
  if (c == 'P') { if (state == PLAYING) { paused = !paused; dirty = true; } return; }   // pause
  if (c == 'X' || c == ' ') { if (state != PLAYING) startRound(); return; }             // rejouer

  // les flèches -> une direction
  int nx = 0, ny = 0;
  switch (c) {
    case 'U': ny = -1; break;
    case 'D': ny =  1; break;
    case 'L': nx = -1; break;
    case 'R': nx =  1; break;
    default: return;                  // touche inconnue, on ignore
  }
  if (state != PLAYING) startRound();  // si on jouait pas, appuyer sur une flèche lance la partie
  setPlayerDir(i, nx, ny);
  dirty = true;
}

// ---------- Détecter si un serpent se cogne ----------
// La tête du joueur i tape-t-elle un corps (le sien, sauf sa propre tête, ou l'autre serpent) ?
bool headCollides(int i, bool wallDead[]) {
  int hx = P[i].bx[0], hy = P[i].by[0];
  for (int j = 0; j < MAXP; j++) {
    if (!P[j].alive || wallDead[j]) continue;   // on ignore les serpents déjà morts
    int start = (j == i) ? 1 : 0;               // pour soi-même, on saute sa propre tête
    for (int k = start; k < P[j].len; k++)
      if (P[j].bx[k] == hx && P[j].by[k] == hy) return true;
  }
  return false;
}

// ---------- Fin de manche : on désigne le gagnant ----------
void endRound() {
  state = RESULT;
  paused = false;
  if (numRacers < 2) {
    resultText = "Game over";                   // en solo, y'a pas de gagnant
  } else {
    // on compte qui est encore en vie
    int aliveCount = 0, last = -1;
    for (int i = 0; i < MAXP; i++) if (P[i].alive) { aliveCount++; last = i; }
    int win;
    if (aliveCount == 1) win = last;            // s'il reste un survivant, c'est lui le boss
    else win = (P[0].score > P[1].score) ? 0 : (P[1].score > P[0].score ? 1 : -1);  // sinon on départage au score
    resultText = (win < 0) ? "Egalite !" : ("Joueur " + String(win + 1) + " gagne !");
  }
  dirty = true;
  updateStatus();
}

// ---------- Le cœur du jeu : un tour de jeu ----------
// Appelé toutes les "moveInterval" ms. C'est là que tout se passe : on bouge
// les serpents, on gère les collisions, la bouffe, et on regarde si c'est fini.
void step() {
  bool wallDead[MAXP] = {false, false};   // qui est mort contre un mur
  bool overlap[MAXP]  = {false, false};   // qui s'est cogné dans un corps
  bool grows[MAXP]    = {false, false};   // qui va manger la pomme ce tour
  int nhx[MAXP], nhy[MAXP];               // la future position de chaque tête

  // 1) On calcule où va chaque tête, et on repère les morts contre les murs.
  for (int i = 0; i < MAXP; i++) {
    if (!P[i].alive) continue;
    P[i].dx = P[i].pdx; P[i].dy = P[i].pdy;   // on valide la direction demandée
    nhx[i] = P[i].bx[0] + P[i].dx;
    nhy[i] = P[i].by[0] + P[i].dy;
    if (nhx[i] < 0 || nhx[i] >= GRID_W || nhy[i] < 0 || nhy[i] >= GRID_H) wallDead[i] = true;
    else grows[i] = (nhx[i] == foodX && nhy[i] == foodY);
  }

  // 2) On fait avancer les serpents qui n'ont pas mordu un mur.
  //    (on décale tout le corps d'un cran, la tête prend la nouvelle case)
  for (int i = 0; i < MAXP; i++) {
    if (!P[i].alive || wallDead[i]) continue;
    int lim = (P[i].len < MAX_LEN - 1) ? P[i].len : MAX_LEN - 1;
    for (int k = lim; k > 0; k--) { P[i].bx[k] = P[i].bx[k-1]; P[i].by[k] = P[i].by[k-1]; }
    P[i].bx[0] = nhx[i]; P[i].by[0] = nhy[i];
    if (grows[i] && P[i].len < MAX_LEN) P[i].len++;   // s'il mange, il grandit (on garde la queue)
  }

  // 3) Maintenant que tout le monde a bougé, on check les collisions entre corps.
  //    Petit piège : faut le faire APRÈS le déplacement, sinon le choc frontal
  //    (les deux têtes sur la même case) serait pas détecté correctement.
  for (int i = 0; i < MAXP; i++)
    if (P[i].alive && !wallDead[i]) overlap[i] = headCollides(i, wallDead);

  // 4) On applique les morts (mur OU collision).
  for (int i = 0; i < MAXP; i++) if (wallDead[i] || overlap[i]) P[i].alive = false;

  // 5) La pomme : le survivant qui l'a mangée marque un point, et on accélère un peu.
  bool ate = false;
  for (int i = 0; i < MAXP; i++)
    if (P[i].alive && grows[i]) { P[i].score++; ate = true; if (moveInterval > 60) moveInterval -= 2; }
  if (ate) { placeFood(); updateStatus(); }

  dirty = true;

  // 6) C'est fini ? À 2 joueurs dès qu'il reste 1 vivant (l'autre a perdu),
  //    en solo quand le seul serpent meurt.
  int aliveCount = 0;
  for (int i = 0; i < MAXP; i++) if (P[i].alive) aliveCount++;
  bool over = (numRacers >= 2) ? (aliveCount <= 1) : (aliveCount == 0);
  if (over) endRound();
}

// ---------- Dessiner un serpent ----------
// La tête est toujours pleine. Pour le corps : J1 est plein, J2 est en contour,
// comme ça on distingue les deux même sur un écran noir et blanc.
void drawSnake(int i) {
  for (int k = 0; k < P[i].len; k++) {
    int px = P[i].bx[k] * CELL, py = TOPBAR + P[i].by[k] * CELL;
    if (k == 0)      display.fillRect(px, py, CELL, CELL, SSD1306_WHITE);
    else if (i == 0) display.fillRect(px, py, CELL - 1, CELL - 1, SSD1306_WHITE);
    else             display.drawRect(px, py, CELL, CELL, SSD1306_WHITE);
  }
}

// ---------- Tout l'affichage de l'écran ----------
// Selon l'état du jeu, on dessine soit le lobby, soit l'écran de fin, soit la partie.
void draw() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  // Écran d'attente : on montre le wifi, l'IP à ouvrir et le nombre de joueurs.
  if (state == LOBBY) {
    display.setCursor(28, 2);  display.print(F("SNAKE 2P"));
    display.setCursor(2, 16);  display.print(F("WiFi: ")); display.print(netName);
    display.setCursor(2, 28);  display.print(ipStr);
    display.setCursor(2, 42);  display.print(F("Joueurs: ")); display.print(connectedCount()); display.print(F("/2"));
    display.setCursor(2, 54);  display.print(F("Fleche = jouer"));
    display.display();
    return;
  }

  // Écran de fin : le gagnant + les scores.
  if (state == RESULT) {
    display.setCursor(4, 8);   display.print(resultText);
    display.setCursor(4, 26);
    if (numRacers >= 2) { display.print(F("P1 ")); display.print(P[0].score); display.print(F("   P2 ")); display.print(P[1].score); }
    else                { display.print(F("Score ")); display.print(P[soloIdx < 0 ? 0 : soloIdx].score); }
    display.setCursor(13, 48); display.print(F("Fleche = rejouer"));
    display.display();
    return;
  }

  // Sinon on est en pleine partie.
  // La barre de score en haut :
  display.setCursor(0, 0);
  if (numRacers >= 2) { display.print(F("P1:")); display.print(P[0].score); display.print(F(" P2:")); display.print(P[1].score); }
  else                { display.print(F("Score: ")); display.print(P[soloIdx < 0 ? 0 : soloIdx].score); }

  // La pomme : un petit point au centre de sa case (pour pas la confondre avec un serpent).
  display.fillRect(foodX * CELL + 1, TOPBAR + foodY * CELL + 1, CELL - 2, CELL - 2, SSD1306_WHITE);

  // Les serpents vivants.
  for (int i = 0; i < MAXP; i++) if (P[i].alive) drawSnake(i);

  // Le petit "PAUSE" par-dessus si on est en pause.
  if (paused) {
    display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
    display.setCursor(49, 32); display.print(F("PAUSE"));
    display.setTextColor(SSD1306_WHITE);
  }
  display.display();
}

// ---------- Gérer les places (slots) des joueurs ----------
// Chaque connexion websocket a un numéro. Là on fait le lien numéro <-> joueur.
int slotOf(int num)  { for (int i = 0; i < MAXP; i++) if (P[i].connected && P[i].wsNum == num) return i; return -1; }
// On donne la première place libre à un nouveau joueur (-1 si la partie est pleine).
int assignSlot(int num) {
  for (int i = 0; i < MAXP; i++)
    if (!P[i].connected) { P[i].connected = true; P[i].wsNum = num; P[i].alive = false; P[i].len = 0; return i; }
  return -1;
}
// Quand quelqu'un se déconnecte, on libère sa place.
void freeSlot(int num) { int i = slotOf(num); if (i >= 0) { P[i].connected = false; P[i].wsNum = -1; P[i].alive = false; } }

// ---------- Les événements du websocket ----------
// Appelé automatiquement quand un joueur se connecte, se déconnecte, ou envoie une touche.
void onWsEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t len) {
  switch (type) {
    case WStype_CONNECTED: {                       // un joueur arrive
      int slot = assignSlot(num);
      wsSend(num, "you:" + String(slot < 0 ? 0 : slot + 1));   // on lui dit s'il est J1, J2 ou spectateur
      updateStatus();                              // et on rafraîchit le compteur pour tout le monde
      dirty = true;
      break;
    }
    case WStype_DISCONNECTED:                       // un joueur part
      freeSlot(num);
      updateStatus();
      dirty = true;
      break;
    case WStype_TEXT:                               // un joueur appuie sur une touche
      if (len > 0) applyCommand(slotOf(num), (char)payload[0]);
      break;
    default: break;
  }
}

// ---------- Outils de diagnostic wifi ----------
// Donne un nom lisible au type de sécurité d'un réseau.
const char* authName(wifi_auth_mode_t a) {
  switch (a) {
    case WIFI_AUTH_OPEN:            return "OUVERT";
    case WIFI_AUTH_WEP:             return "WEP";
    case WIFI_AUTH_WPA_PSK:         return "WPA";
    case WIFI_AUTH_WPA2_PSK:        return "WPA2 (mot de passe)";
    case WIFI_AUTH_WPA_WPA2_PSK:    return "WPA/WPA2 (mot de passe)";
    case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2-ENTERPRISE (login+mdp)";
    case WIFI_AUTH_WPA3_PSK:        return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK:   return "WPA2/WPA3";
    default:                        return "?";
  }
}

// Liste dans le moniteur série tous les réseaux 2.4GHz que l'ESP32 voit.
// (Rappel : l'ESP32 capte QUE le 2.4GHz, jamais le 5GHz.)
void scanAndPrint() {
  int n = WiFi.scanNetworks();
  if (n <= 0) { Serial.println("  (aucun reseau 2.4GHz vu)"); return; }
  for (int i = 0; i < n; i++) {
    Serial.print("  "); Serial.print(WiFi.SSID(i));
    Serial.print("  RSSI="); Serial.print(WiFi.RSSI(i));
    Serial.print("  "); Serial.println(authName(WiFi.encryptionType(i)));
  }
}

// ---------- Le démarrage (une seule fois au boot) ----------
void setup() {
  Serial.begin(115200);
  Wire.begin(OLED_SDA, OLED_SCL);
  // On allume l'écran. periphBegin=false : sinon la lib relance l'I2C sur les
  // mauvaises broches et l'écran disparaît (le fameux piège de la S3).
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR, true, false)) {
    Serial.println("pas d'écran ?!");
    while (true) delay(1000);
  }

  // On remet les deux joueurs à zéro (personne de branché au départ).
  for (int i = 0; i < MAXP; i++) { P[i].connected = false; P[i].wsNum = -1; P[i].alive = false; P[i].len = 0; }

  // On affiche un écran TOUT DE SUITE, avant de tenter le wifi.
  // (parce que la connexion wifi peut bloquer plusieurs secondes, et pendant
  //  ce temps l'écran resterait noir sinon)
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE); display.setTextSize(1);
  display.setCursor(20, 4);  display.print(F("SNAKE 2P VERSUS"));
  display.setCursor(2, 30);  display.print(F("Connexion wifi..."));
  display.setCursor(2, 44);  display.print(USE_AP ? AP_SSID : STA_SSID);
  display.display();

  // On essaie de rejoindre le wifi de la fac. Si au bout de 12s c'est pas
  // connecté, on abandonne et on affiche les réseaux vus (pour comprendre pourquoi).
  bool staOk = false;
  if (!USE_AP) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(STA_SSID, STA_PASS);
    unsigned long t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 12000) { delay(250); Serial.print("."); }
    staOk = (WiFi.status() == WL_CONNECTED);
    if (!staOk) {
      Serial.println("\n[!] " + String(STA_SSID) + " injoignable. Reseaux 2.4GHz visibles :");
      scanAndPrint();
    }
  }

  // Si le wifi a marché on l'utilise, sinon on crée notre propre wifi de secours.
  if (staOk) {
    netName = STA_SSID; ipStr = WiFi.localIP().toString();
  } else {
    if (!USE_AP) Serial.println("[i] Bascule en point d'acces de secours (" + String(AP_SSID) + ").");
    WiFi.mode(WIFI_AP);
    if (strlen(AP_PASS) > 0) WiFi.softAP(AP_SSID, AP_PASS); else WiFi.softAP(AP_SSID);
    netName = AP_SSID; ipStr = WiFi.softAPIP().toString();
  }
  Serial.println("Reseau: " + netName + "  ->  http://" + ipStr);

  // On lance les deux serveurs (la page web + le websocket).
  server.on("/", handleRoot);
  server.begin();
  webSocket.begin();
  webSocket.onEvent(onWsEvent);
  randomSeed(esp_random());   // pour que la pomme tombe vraiment au hasard

  // Petit écran d'accueil avec le wifi et l'adresse à ouvrir.
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE); display.setTextSize(1);
  display.setCursor(20, 4);  display.print(F("SNAKE 2P VERSUS"));
  display.setCursor(2, 24);  display.print(F("WiFi: ")); display.print(netName);
  display.setCursor(2, 38);  display.print(F("http://")); display.print(ipStr);
  display.display();
  delay(2500);

  // Et on part sur l'écran d'attente.
  state = LOBBY;
  updateStatus();
  dirty = true;
}

// ---------- La boucle principale (tourne en continu) ----------
void loop() {
  server.handleClient();   // on répond aux pages web
  webSocket.loop();        // on écoute les touches

  // Si on joue et qu'il est temps, on avance le jeu d'un cran.
  if (state == PLAYING && !paused && millis() - lastMove >= moveInterval) {
    lastMove = millis();
    step();
  }

  // On redessine l'écran seulement si quelque chose a changé (sinon ça rame pour rien).
  if (dirty) { draw(); dirty = false; }
}
