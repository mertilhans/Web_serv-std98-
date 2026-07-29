#!/usr/bin/env python3
import os
import sys
import html
from urllib.parse import parse_qs

method = os.environ.get("REQUEST_METHOD", "GET")
query = os.environ.get("QUERY_STRING", "")

body = ""
if method == "POST":
    try:
        length = int(os.environ.get("CONTENT_LENGTH", "0") or "0")
    except ValueError:
        length = 0
    if length > 0:
        body = sys.stdin.read(length)

params = parse_qs(query if method == "GET" else body)
name = params.get("name", [""])[0]
name_safe = html.escape(name)

print("Content-Type: text/html; charset=utf-8")
print()
print("<html><body>")
print("<h1>webserv CGI testi (python3)</h1>")
print("<form method=\"POST\" action=\"/cgi-bin/hello.py\">")
print("  <label>Isim: <input type=\"text\" name=\"name\" value=\"%s\"></label>" % name_safe)
print("  <button type=\"submit\">Gonder</button>")
print("</form>")
if name_safe:
    print("<p>Merhaba, %s!</p>" % name_safe)
print("<hr>")
print("<p>METHOD=%s</p>" % html.escape(method))
print("<p>QUERY_STRING=%s</p>" % html.escape(query))
print("<p>SERVER_NAME=%s</p>" % html.escape(os.environ.get("SERVER_NAME", "")))
print("</body></html>")
