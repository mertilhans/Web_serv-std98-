#!/usr/bin/env python3
"""Cookie + session management demosu.

Akis:
  - Tarayicinin gonderdigi Cookie header'i webserv tarafindan HTTP_COOKIE
    env degiskeni olarak CGI'a gecirilir (buildCgiEnv).
  - Script cookie'de sid yoksa yeni bir session id uretir ve "Set-Cookie"
    header'i yazar; webserv bunu HTTP yanitina tasir (parseCgiOutput +
    insertSetCookie).
  - Session verisi (ziyaret sayisi) script'in dizinindeki sessions.txt'de
    tutulur -- CGI process'i istek basina yasadigi icin kalici durum dosyada.
"""
import os
import binascii

SESS_FILE = "sessions.txt"


def load_sessions():
    sessions = {}
    if os.path.exists(SESS_FILE):
        with open(SESS_FILE) as f:
            for line in f:
                if ":" in line:
                    sid, count = line.strip().split(":", 1)
                    sessions[sid] = int(count)
    return sessions


def save_sessions(sessions):
    with open(SESS_FILE, "w") as f:
        for sid, count in sessions.items():
            f.write("%s:%d\n" % (sid, count))


def get_sid_from_cookie():
    cookie = os.environ.get("HTTP_COOKIE", "")
    for part in cookie.split(";"):
        part = part.strip()
        if part.startswith("sid="):
            return part[4:]
    return ""


def main():
    sessions = load_sessions()
    sid = get_sid_from_cookie()
    is_new = sid == "" or sid not in sessions

    if is_new:
        sid = binascii.hexlify(os.urandom(8)).decode()
        sessions[sid] = 1
    else:
        sessions[sid] += 1
    save_sessions(sessions)

    if is_new:
        print("Set-Cookie: sid=%s" % sid)
    print("Content-Type: text/plain")
    print()
    if is_new:
        print("Merhaba! Yeni session olusturuldu.")
    else:
        print("Tekrar hosgeldin!")
    print("session id : %s" % sid)
    print("ziyaret    : %d" % sessions[sid])


if __name__ == "__main__":
    main()
