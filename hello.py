#!/usr/bin/env python3

import os
import sys


def read_body():
    try:
        length = int(os.environ.get("CONTENT_LENGTH", "0") or "0")
    except ValueError:
        length = 0
    if length <= 0:
        return ""
    data = sys.stdin.buffer.read(length)
    return data.decode("utf-8", errors="replace")


def escape(text):
    return (text.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;"))


def main():
    method = os.environ.get("REQUEST_METHOD", "")
    query = os.environ.get("QUERY_STRING", "")
    remote_addr = os.environ.get("REMOTE_ADDR", "?")
    server_name = os.environ.get("SERVER_NAME", "?")
    server_port = os.environ.get("SERVER_PORT", "?")
    script_name = os.environ.get("SCRIPT_NAME", "?")
    path_info = os.environ.get("PATH_INFO", "?")
    user_agent = os.environ.get("HTTP_USER_AGENT", "(none)")
    host_header = os.environ.get("HTTP_HOST", "(none)")

    body = read_body() if method == "POST" else ""

    http_vars = sorted(k for k in os.environ if k.startswith("HTTP_"))

    print("Content-Type: text/html")
    print()
    print("<!DOCTYPE html><html><head><title>webserv CGI</title>"
          "<link rel=\"stylesheet\" href=\"/style.css\"></head><body>")
    print("<h1>Hello from CGI</h1>")
    print("<section><h2>Request</h2><ul>")
    print("<li>REQUEST_METHOD: <code>%s</code></li>" % escape(method))
    print("<li>SCRIPT_NAME: <code>%s</code></li>" % escape(script_name))
    print("<li>PATH_INFO: <code>%s</code></li>" % escape(path_info))
    print("<li>QUERY_STRING: <code>%s</code></li>" % escape(query))
    print("<li>REMOTE_ADDR: <code>%s</code></li>" % escape(remote_addr))
    print("<li>SERVER_NAME:SERVER_PORT: <code>%s:%s</code></li>" % (escape(server_name), escape(server_port)))
    print("<li>Host header: <code>%s</code></li>" % escape(host_header))
    print("<li>User-Agent: <code>%s</code></li>" % escape(user_agent))
    print("</ul></section>")

    if method == "POST":
        print("<section><h2>Body received (%d bytes)</h2><pre>%s</pre></section>"
              % (len(body), escape(body)))

    print("<section><h2>All HTTP_* env vars seen by the CGI</h2><ul>")
    for key in http_vars:
        print("<li><code>%s</code> = <code>%s</code></li>" % (escape(key), escape(os.environ[key])))
    print("</ul></section>")
    print("<p><a href=\"/\">&larr; back</a></p>")
    print("</body></html>")


if __name__ == "__main__":
    main()
