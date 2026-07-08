# 🐍 Snake ESP32 — Brigade Sud edition

Snake qui tourne sur l'**ESP32**, s'affiche sur un **écran OLED SSD1306 128x64 (I2C)**,
et se pilote aux **flèches du clavier** de ton PC (Mac ou Windows) via un pont série USB.

## Contenu
- `snake_esp32.ino` — le jeu, à uploader sur l'ESP32 (Arduino IDE)
- `snake_keys.py`   — le pont clavier → ESP32 (Python, Mac + Windows)

## Câblage OLED (I2C)
| OLED | ESP32   |
|------|---------|
| VCC  | 3V3     |
| GND  | GND     |
| SDA  | GPIO 21 |
| SCL  | GPIO 22 |

## 1) Uploader le jeu
1. Ouvre `snake_esp32.ino` dans **Arduino IDE**.
2. Librairies requises (déjà installées chez toi) : *Adafruit SSD1306*, *Adafruit GFX*, *Adafruit BusIO*.
3. Sélectionne ta carte ESP32 + le port (`/dev/cu.usbmodem101` sur ce Mac).
4. Clique **Upload**. Tu dois voir le splash "SNAKE ESP32" puis le serpent apparaître.

## 2) Lancer le contrôle clavier
> ⚠️ Ferme le **moniteur série** de l'Arduino IDE avant : un seul programme peut ouvrir le port à la fois.

**Mac / Linux :**
```bash
python3 snake_keys.py
# ou en forçant le port :
python3 snake_keys.py /dev/cu.usbmodem101
```

**Windows :**
```bat
python snake_keys.py
:: ou en forçant le port (regarde dans le Gestionnaire de périphériques) :
python snake_keys.py COM5
```

Puis joue :
- **Flèches** ou **WASD** → diriger le serpent
- **P** → pause
- **Espace** → rejouer après un game over
- **Q** ou **Ctrl-C** → quitter le pont

## Dépannage
- **Écran noir / rien ne s'affiche** → l'adresse I2C est peut-être `0x3D`. Dans le `.ino`,
  change `#define OLED_ADDR 0x3C` en `0x3D`. (La LED bleue de l'ESP32 clignote si l'écran n'est pas détecté.)
- **Écran câblé sur d'autres broches** → dans `setup()`, remplace `Wire.begin();`
  par `Wire.begin(TA_SDA, TA_SCL);` avec tes numéros de GPIO.
- **`ModuleNotFoundError: serial`** → `python3 -m pip install --user pyserial`
- **Port occupé / Access denied** → le moniteur série de l'IDE est encore ouvert, ferme-le.
- **Rien ne bouge quand j'appuie** → clique dans la fenêtre du terminal qui exécute le script
  (c'est lui qui capte le clavier), pas ailleurs.

## Portable Windows
Le script `snake_keys.py` détecte l'OS tout seul (termios sur Mac/Linux, msvcrt sur Windows) :
tu copies le dossier sur ton PC boulot, tu installes `pyserial`, et ça roule.
