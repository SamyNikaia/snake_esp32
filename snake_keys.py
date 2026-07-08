#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
snake_keys.py — Pont clavier -> ESP32 (port serie)
==================================================
Capte les fleches (ou WASD) de ton clavier et les envoie a l'ESP32
qui fait tourner le Snake sur l'ecran OLED.

Cross-platform : marche sur macOS/Linux (termios) ET Windows (msvcrt).

Usage :
    python3 snake_keys.py                       # auto-detecte le port
    python3 snake_keys.py /dev/cu.usbmodem101   # macOS/Linux : port precis
    python  snake_keys.py COM5                   # Windows : port precis

Touches :
    Fleches / WASD  -> diriger le serpent
    P               -> pause
    Espace          -> rejouer (apres game over)
    Q  ou  Ctrl-C   -> quitter le pont
"""

import sys
import time

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    print("Le module 'pyserial' manque.")
    print("Installe-le :  python3 -m pip install --user pyserial")
    sys.exit(1)

BAUD = 115200


# ------------------------------------------------------------------
def find_port():
    """Cherche un port qui ressemble a un ESP32, sinon prend le premier."""
    ports = list(list_ports.comports())
    keywords = ("usbmodem", "usbserial", "slab", "wch", "ch34",
                "cp210", "silicon", "esp")
    for p in ports:
        blob = ((p.device or "") + " " + (p.description or "")).lower()
        if any(k in blob for k in keywords):
            return p.device
    return ports[0].device if ports else None


def drain_serial(ser):
    """Affiche ce que l'ESP32 renvoie (score, game over...)."""
    try:
        n = ser.in_waiting
    except Exception:
        return
    if n:
        try:
            txt = ser.read(n).decode("utf-8", "ignore")
            sys.stdout.write(txt.replace("\n", "\r\n"))
            sys.stdout.flush()
        except Exception:
            pass


def send_char(ser, ch):
    """Traduit une touche simple en commande serie. Renvoie False si on veut quitter."""
    low = ch.lower()
    if low == "q":
        return False
    mapping = {"w": "U", "s": "D", "a": "L", "d": "R", "p": "P", " ": "X"}
    if low in mapping:
        ser.write(mapping[low].encode())
    return True


# ------------------------------------------------------------------
def run_unix(ser):
    import termios, tty, select, os

    fd = sys.stdin.fileno()
    old = termios.tcgetattr(fd)
    try:
        tty.setcbreak(fd)              # touche par touche, sans Entree, garde Ctrl-C
        os.set_blocking(fd, False)
        running = True
        while running:
            drain_serial(ser)
            r, _, _ = select.select([sys.stdin], [], [], 0.02)
            if r:
                data = sys.stdin.buffer.read()
                if data:
                    running = process_bytes(ser, data.decode("utf-8", "ignore"))
    finally:
        termios.tcsetattr(fd, termios.TCSADRAIN, old)


def process_bytes(ser, s):
    """Analyse un paquet de touches (gere les sequences fleche ESC[A...)."""
    i = 0
    while i < len(s):
        c = s[i]
        if c == "\x1b" and s[i + 1:i + 2] == "[" and s[i + 2:i + 3] in ("A", "B", "C", "D"):
            cmd = {"A": "U", "B": "D", "C": "R", "D": "L"}[s[i + 2]]
            ser.write(cmd.encode())
            i += 3
            continue
        if c == "\x1b":                # Esc seul : on ignore
            i += 1
            continue
        if not send_char(ser, c):
            return False
        i += 1
    return True


def run_windows(ser):
    import msvcrt

    running = True
    while running:
        drain_serial(ser)
        if msvcrt.kbhit():
            ch = msvcrt.getwch()
            if ch in ("\x00", "\xe0"):          # prefixe touche speciale (fleches)
                ch2 = msvcrt.getwch()
                cmd = {"H": "U", "P": "D", "K": "L", "M": "R"}.get(ch2)
                if cmd:
                    ser.write(cmd.encode())
            else:
                running = send_char(ser, ch)
        time.sleep(0.005)


# ------------------------------------------------------------------
def main():
    port = sys.argv[1] if len(sys.argv) > 1 else find_port()
    if not port:
        print("Aucun port serie trouve. Branche l'ESP32, ou passe le port en argument.")
        print("  macOS/Linux : python3 snake_keys.py /dev/cu.usbmodem101")
        print("  Windows     : python  snake_keys.py COM5")
        sys.exit(1)

    print("Connexion a {} @ {} bauds...".format(port, BAUD))
    try:
        ser = serial.Serial(port, BAUD, timeout=0)
    except Exception as e:
        print("Impossible d'ouvrir {} : {}".format(port, e))
        sys.exit(1)

    time.sleep(2.0)            # laisse l'ESP32 rebooter (auto-reset au DTR)
    ser.reset_input_buffer()

    print("Pont actif ! Fleches/WASD pour jouer. P=pause, Espace=rejouer, Q=quitter.\n")
    try:
        if sys.platform.startswith("win"):
            run_windows(ser)
        else:
            run_unix(ser)
    except KeyboardInterrupt:
        pass
    finally:
        ser.close()
        print("\nPont ferme. A plus ! (snake)")


if __name__ == "__main__":
    main()
