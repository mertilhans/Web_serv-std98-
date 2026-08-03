#!/usr/bin/env python3
"""
Subject'in mandatory part'ta istedigi HER ozelligi configs/default.conf
uzerinden ucdan uca dogrulayan fonksiyonel test paketi. crash_test.py'nin
tamamlayicisi: o "cokmuyor mu" sorusuna cevap verir, bu ise "istenen
ozellik GERCEKTEN dogru calisiyor mu" sorusuna.

Kullanim:
    python3 tests/feature_test.py
"""
import http.client
import socket
import subprocess
import sys
import time
import os

BINARY = "./webserv"
CONFIG = "configs/default.conf"

MAIN_PORT = 8080
VHOST_PORT = 9090
ADMIN_PORT = 9091

passed = 0
failed = 0
proc = None


def start_server():
    global proc
    proc = subprocess.Popen([BINARY, CONFIG],
                             stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(1)
    if proc.poll() is not None:
        print("FATAL: sunucu baslatilamadi")
        sys.exit(1)


def stop_server():
    if proc is not None and proc.poll() is None:
        proc.terminate()
        try:
            proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            proc.kill()


def local_ip():
    """Makinenin loopback disi (gercek) IP'sini bulur -- wildcard-merge
    vhost testinde 'baska arayuzden gelen baglanti' senaryosunu kurmak icin."""
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("8.8.8.8", 80))
        ip = s.getsockname()[0]
    except OSError:
        ip = None
    finally:
        s.close()
    return ip


def report(name, ok, detail=""):
    global passed, failed
    if ok:
        passed += 1
        print("  [PASS] {}".format(name))
    else:
        failed += 1
        print("  [FAIL] {}  {}".format(name, detail))


def get(host, port, path, headers=None, timeout=5):
    conn = http.client.HTTPConnection(host, port, timeout=timeout)
    conn.request("GET", path, headers=headers or {})
    resp = conn.getresponse()
    body = resp.read()
    conn.close()
    return resp, body


def raw_request(data, host="127.0.0.1", port=MAIN_PORT, timeout=5, recv_bytes=65536):
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(timeout)
    s.connect((host, port))
    s.sendall(data)
    chunks = b""
    try:
        while True:
            part = s.recv(recv_bytes)
            if not part:
                break
            chunks += part
            if len(part) < recv_bytes:
                break
    except socket.timeout:
        pass
    s.close()
    return chunks


# ---------------------------------------------------------------------------
# 1. Statik site servisi
# ---------------------------------------------------------------------------

def test_static_file():
    resp, body = get("127.0.0.1", MAIN_PORT, "/index.html")
    report("statik dosya (GET /index.html)", resp.status == 200 and b"<html" in body.lower(),
           "status={}".format(resp.status))


def test_directory_index():
    resp, body = get("127.0.0.1", MAIN_PORT, "/")
    report("dizin -> index.html", resp.status == 200 and len(body) > 0,
           "status={}".format(resp.status))


def test_content_type():
    resp, _ = get("127.0.0.1", MAIN_PORT, "/style.css")
    ctype = resp.getheader("Content-Type", "")
    report("dogru Content-Type (css)", "css" in ctype, "Content-Type={}".format(ctype))


# ---------------------------------------------------------------------------
# 2. HTTP redirection
# ---------------------------------------------------------------------------

def test_redirect():
    conn = http.client.HTTPConnection("127.0.0.1", MAIN_PORT, timeout=5)
    conn.request("GET", "/old")
    resp = conn.getresponse()
    resp.read()
    conn.close()
    loc = resp.getheader("Location", "")
    report("HTTP redirection (/old -> 301)", resp.status == 301 and loc == "/index.html",
           "status={} Location={}".format(resp.status, loc))


# ---------------------------------------------------------------------------
# 3. Upload / Delete / Method restriction
# ---------------------------------------------------------------------------

def test_upload_create_overwrite_delete():
    conn = http.client.HTTPConnection("127.0.0.1", MAIN_PORT, timeout=5)
    conn.request("POST", "/uploads/feature_test.txt", body=b"hello")
    r1 = conn.getresponse()
    r1.read()
    conn.close()
    report("upload -- yeni dosya (201)", r1.status == 201, "status={}".format(r1.status))

    conn = http.client.HTTPConnection("127.0.0.1", MAIN_PORT, timeout=5)
    conn.request("POST", "/uploads/feature_test.txt", body=b"hello again")
    r2 = conn.getresponse()
    r2.read()
    conn.close()
    report("upload -- var olani ustune yazma (200)", r2.status == 200, "status={}".format(r2.status))

    conn = http.client.HTTPConnection("127.0.0.1", MAIN_PORT, timeout=5)
    conn.request("DELETE", "/uploads/feature_test.txt")
    r3 = conn.getresponse()
    r3.read()
    conn.close()
    report("delete -- var olan dosya (200)", r3.status == 200, "status={}".format(r3.status))

    conn = http.client.HTTPConnection("127.0.0.1", MAIN_PORT, timeout=5)
    conn.request("DELETE", "/uploads/feature_test.txt")
    r4 = conn.getresponse()
    r4.read()
    conn.close()
    report("delete -- artik yok (404)", r4.status == 404, "status={}".format(r4.status))


def test_method_not_allowed():
    conn = http.client.HTTPConnection("127.0.0.1", MAIN_PORT, timeout=5)
    conn.request("DELETE", "/index.html")
    resp = conn.getresponse()
    resp.read()
    conn.close()
    report("izin verilmeyen method (405)", resp.status == 405, "status={}".format(resp.status))


def test_client_max_body_size():
    # /uploads location'da limit 20M -- gercekte 20MB gondermek yerine,
    # ONCEDEN bildirilen dev bir Content-Length ile 413'un GOVDE
    # BEKLENMEDEN, sadece header'dan hemen dondugunu dogruluyoruz.
    huge = 21 * 1024 * 1024
    req = ("POST /uploads/toobig.txt HTTP/1.1\r\nHost: x\r\n"
           "Content-Length: {}\r\n\r\n").format(huge).encode()
    resp = raw_request(req, timeout=3)
    status_line = resp.split(b"\r\n", 1)[0] if resp else b""
    report("client_max_body_size asilinca 413", b"413" in status_line,
           "status_line={}".format(status_line))


def test_chunked_upload():
    body = (b"5\r\nhello\r\n6\r\n world\r\n0\r\n\r\n")
    req = (b"POST /uploads/chunked_test.txt HTTP/1.1\r\nHost: x\r\n"
           b"Transfer-Encoding: chunked\r\n\r\n") + body
    resp = raw_request(req, timeout=5)
    status_line = resp.split(b"\r\n", 1)[0] if resp else b""
    ok = b"201" in status_line or b"200" in status_line
    report("chunked transfer-encoding upload", ok, "status_line={}".format(status_line))
    if ok:
        conn = http.client.HTTPConnection("127.0.0.1", MAIN_PORT, timeout=5)
        conn.request("DELETE", "/uploads/chunked_test.txt")
        conn.getresponse().read()
        conn.close()


# ---------------------------------------------------------------------------
# 4. CGI
# ---------------------------------------------------------------------------

def test_cgi_get():
    resp, body = get("127.0.0.1", MAIN_PORT, "/cgi-bin/hello.py")
    report("CGI GET", resp.status == 200 and b"GET" in body, "status={}".format(resp.status))


def test_cgi_post():
    conn = http.client.HTTPConnection("127.0.0.1", MAIN_PORT, timeout=5)
    conn.request("POST", "/cgi-bin/hello.py", body=b"a=1&b=2",
                 headers={"Content-Type": "application/x-www-form-urlencoded"})
    resp = conn.getresponse()
    body = resp.read()
    conn.close()
    report("CGI POST (govde CGI'ye ulasiyor)", resp.status == 200 and b"POST" in body,
           "status={}".format(resp.status))


# ---------------------------------------------------------------------------
# 5. Ozel error page + autoindex (9091, admin_panel)
# ---------------------------------------------------------------------------

def test_custom_error_page():
    resp, body = get("127.0.0.1", ADMIN_PORT, "/does-not-exist")
    report("ozel error_page (404)", resp.status == 404 and len(body) > 0,
           "status={}".format(resp.status))


def test_autoindex():
    resp, body = get("127.0.0.1", ADMIN_PORT, "/")
    ok = resp.status == 200 and (b"<a href" in body or b"<html" in body.lower())
    report("autoindex acik -- dizin listeleme", ok, "status={}".format(resp.status))


def test_default_error_page_without_config():
    # 8080'de error_page tanimli DEGIL -- yerlesik varsayilan sayfa
    # uretilmeli, sunucu asla bos/crash cevap vermemeli.
    resp, body = get("127.0.0.1", MAIN_PORT, "/no-such-file-xyz")
    report("varsayilan (yerlesik) error page", resp.status == 404 and len(body) > 0,
           "status={}".format(resp.status))


# ---------------------------------------------------------------------------
# 6. Coklu port + wildcard/specific-IP vhost merge (9090)
# ---------------------------------------------------------------------------

def test_multiple_ports_listening():
    ok_all = True
    for port in (MAIN_PORT, VHOST_PORT, ADMIN_PORT):
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.settimeout(2)
            s.connect(("127.0.0.1", port))
            s.close()
        except OSError:
            ok_all = False
    report("coklu port dinleme (8080/9090/9091)", ok_all)


def test_vhost_specific_ip():
    resp, body = get("127.0.0.1", VHOST_PORT, "/", headers={"Host": "api_local"})
    report("specific-IP vhost (Host: api_local)", resp.status == 200 and b"api_local" in body,
           "status={}".format(resp.status))


def test_vhost_wildcard():
    ip = local_ip()
    if ip is None:
        report("wildcard vhost (Host: api_public)", False, "makine IP'si bulunamadi, atlandi")
        return
    resp, body = get(ip, VHOST_PORT, "/", headers={"Host": "api_public"})
    report("wildcard vhost (Host: api_public, gercek IP uzerinden)",
           resp.status == 200 and b"api_public" in body, "status={}".format(resp.status))


# ---------------------------------------------------------------------------
# 7. Keep-alive / pipelining (non-blocking + tek poll() dogal sonucu)
# ---------------------------------------------------------------------------

def _read_one_http_response(sock, buf):
    """buf'ta zaten okunmus baytlari kullanarak TEK bir HTTP yanitini
    (header + Content-Length kadar govde) okur, kalan (varsa pipelined
    bir sonraki yanita ait) baytlari geri doner. Yanit govdesinde
    tesadufen 'HTTP/1.1' gecebilecegi icin (orn. sayfa metninde) basit
    string-sayma yerine Content-Length'e gore KESIN sinir kullaniyoruz."""
    while b"\r\n\r\n" not in buf:
        chunk = sock.recv(65536)
        if not chunk:
            return None, buf
        buf += chunk

    header_end = buf.index(b"\r\n\r\n") + 4
    header_block = buf[:header_end]
    content_length = 0
    for line in header_block.split(b"\r\n"):
        if line.lower().startswith(b"content-length:"):
            content_length = int(line.split(b":", 1)[1].strip())
            break

    total_needed = header_end + content_length
    while len(buf) < total_needed:
        chunk = sock.recv(65536)
        if not chunk:
            break
        buf += chunk

    return buf[:total_needed], buf[total_needed:]


def test_keep_alive_pipelining():
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(5)
    s.connect(("127.0.0.1", MAIN_PORT))
    req = (b"GET /index.html HTTP/1.1\r\nHost: x\r\n\r\n"
           b"GET /style.css HTTP/1.1\r\nHost: x\r\n\r\n")
    s.sendall(req)
    buf = b""
    resp1, buf = _read_one_http_response(s, buf)
    resp2, buf = _read_one_http_response(s, buf)
    s.close()
    ok = (resp1 is not None and resp1.startswith(b"HTTP/1.1 200")
          and resp2 is not None and resp2.startswith(b"HTTP/1.1 200"))
    report("keep-alive + pipelining (tek baglantida 2 tam yanit)", ok,
           "resp1={} resp2={}".format(resp1[:15] if resp1 else None, resp2[:15] if resp2 else None))


def test_connection_close_header():
    conn = http.client.HTTPConnection("127.0.0.1", MAIN_PORT, timeout=5)
    conn.request("GET", "/index.html", headers={"Connection": "close"})
    resp = conn.getresponse()
    resp.read()
    conn_header = resp.getheader("Connection", "")
    conn.close()
    report("Connection: close saygi gorulmesi", conn_header.lower() == "close",
           "Connection={}".format(conn_header))


TESTS = [
    test_static_file,
    test_directory_index,
    test_content_type,
    test_redirect,
    test_upload_create_overwrite_delete,
    test_method_not_allowed,
    test_client_max_body_size,
    test_chunked_upload,
    test_cgi_get,
    test_cgi_post,
    test_custom_error_page,
    test_autoindex,
    test_default_error_page_without_config,
    test_multiple_ports_listening,
    test_vhost_specific_ip,
    test_vhost_wildcard,
    test_keep_alive_pipelining,
    test_connection_close_header,
]


def main():
    print("webserv feature-test paketi baslatiliyor (config: {})".format(CONFIG))
    start_server()
    try:
        for t in TESTS:
            try:
                t()
            except Exception as exc:
                global failed
                failed += 1
                print("  [EXC]  {}: {}".format(t.__name__, exc))
    finally:
        stop_server()

    print()
    print("Sonuc: {} basarili, {} basarisiz".format(passed, failed))
    sys.exit(1 if failed > 0 else 0)


if __name__ == "__main__":
    main()
