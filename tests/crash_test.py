#!/usr/bin/env python3
"""
Kendi crash-test paketimiz. Subject: "Your program must not crash under any
circumstances... your peers can add their own tests too" -- bu script tam
olarak bunu yapiyor: webserv'i baslatir, bir dizi kotu-niyetli/bozuk/uc-durum
istegi gonderir ve HER senaryodan SONRA sunucu process'inin hala ayakta olup
olmadigini kontrol eder. Tek bir crash bile "FAIL" sayilir (subject'e gore
grade 0 anlamina gelen durum).

Kullanim:
    python3 tests/crash_test.py [config_dosyasi]
"""
import socket
import subprocess
import sys
import time
import os

HOST = "127.0.0.1"
PORT = 8080
BINARY = "./webserv"
DEFAULT_CONFIG = "configs/default.conf"

passed = 0
failed = 0
proc = None


def start_server(config_path):
    global proc
    proc = subprocess.Popen([BINARY, config_path],
                             stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(1)
    if proc.poll() is not None:
        print("FATAL: sunucu hemen basladiktan sonra cikti, testler calistirilamiyor")
        sys.exit(1)


def alive():
    return proc.poll() is None


def raw(data, host=HOST, port=PORT, read_timeout=2.0, recv_bytes=4096):
    """Ham bayt gonderir, ne cevap gelirse (varsa) doner. Baglanti hatasi/
    timeout olursa None doner (crash degil, sadece o testin cevabi yok
    sayilir -- crash kontrolu ayrica alive() ile yapiliyor)."""
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(read_timeout)
        s.connect((host, port))
        if data is not None:
            s.sendall(data)
        try:
            resp = s.recv(recv_bytes)
        except socket.timeout:
            resp = b""
        s.close()
        return resp
    except (ConnectionRefusedError, ConnectionResetError, BrokenPipeError):
        return None
    except socket.timeout:
        return b""


def check(name, condition_fn):
    global passed, failed
    try:
        condition_fn()
    except Exception as exc:
        print("  [EXC] {}: {}".format(name, exc))
    ok = alive()
    if ok:
        passed += 1
        print("  [OK]   {} (sunucu ayakta)".format(name))
    else:
        failed += 1
        print("  [CRASH] {} <-- SUNUCU COKTU!".format(name))


# ---------------------------------------------------------------------------
# Test senaryolari
# ---------------------------------------------------------------------------

def t_empty_request():
    raw(b"")


def t_garbage_binary():
    raw(bytes([0, 1, 2, 3, 255, 254, 253, 10, 13, 0, 0, 0]) * 50)


def t_only_crlf():
    raw(b"\r\n\r\n\r\n\r\n")


def t_no_method():
    raw(b"/index.html HTTP/1.1\r\nHost: x\r\n\r\n")


def t_missing_version():
    raw(b"GET /index.html\r\nHost: x\r\n\r\n")


def t_huge_method():
    raw(b"A" * 100000 + b" / HTTP/1.1\r\nHost: x\r\n\r\n")


def t_huge_uri():
    raw(b"GET /" + b"a" * 200000 + b" HTTP/1.1\r\nHost: x\r\n\r\n")


def t_huge_header_count():
    headers = b"".join([b"X-Test-%d: value\r\n" % i for i in range(20000)])
    raw(b"GET / HTTP/1.1\r\nHost: x\r\n" + headers + b"\r\n")


def t_huge_single_header_value():
    raw(b"GET / HTTP/1.1\r\nHost: x\r\nX-Big: " + b"z" * 500000 + b"\r\n\r\n")


def t_null_bytes_in_path():
    raw(b"GET /index.html\x00.txt HTTP/1.1\r\nHost: x\r\n\r\n")


def t_null_bytes_percent_encoded():
    raw(b"GET /index.html%00.txt HTTP/1.1\r\nHost: x\r\n\r\n")


def t_path_traversal_deep():
    raw(b"GET /" + b"../" * 5000 + b"etc/passwd HTTP/1.1\r\nHost: x\r\n\r\n")


def t_malformed_percent_encoding():
    raw(b"GET /%ZZ%GG%%% HTTP/1.1\r\nHost: x\r\n\r\n")


def t_negative_content_length():
    raw(b"POST /uploads/x.txt HTTP/1.1\r\nHost: x\r\nContent-Length: -1\r\n\r\nbody")


def t_huge_content_length_no_body():
    # Content-Length dev gibi ama govde hic gelmiyor -- sunucu sonsuza kadar
    # beklememeli (timeout ile kapatmali), crash etmemeli.
    raw(b"POST /uploads/x.txt HTTP/1.1\r\nHost: x\r\nContent-Length: 999999999\r\n\r\n",
        read_timeout=2.0)


def t_chunked_huge_size_field():
    raw(b"POST /uploads/x.txt HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: chunked\r\n\r\n"
        b"FFFFFFFFFFFFFFFF\r\nabc\r\n0\r\n\r\n")


def t_chunked_garbage_size():
    raw(b"POST /uploads/x.txt HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: chunked\r\n\r\n"
        b"ZZZZ\r\nabc\r\n0\r\n\r\n")


def t_chunked_negative_looking_size():
    raw(b"POST /uploads/x.txt HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: chunked\r\n\r\n"
        b"-5\r\nabc\r\n0\r\n\r\n")


def t_both_cl_and_te():
    raw(b"POST /uploads/x.txt HTTP/1.1\r\nHost: x\r\nContent-Length: 3\r\n"
        b"Transfer-Encoding: chunked\r\n\r\n3\r\nabc\r\n0\r\n\r\n")


def t_double_headers():
    raw(b"GET / HTTP/1.1\r\nHost: a\r\nHost: b\r\nContent-Length: 5\r\nContent-Length: 10\r\n\r\n")


def t_header_no_colon():
    raw(b"GET / HTTP/1.1\r\nHost: x\r\nGarbageHeaderNoColon\r\n\r\n")


def t_bogus_method():
    raw(b"FOOBAR / HTTP/1.1\r\nHost: x\r\n\r\n")


def t_lowercase_method():
    raw(b"get / http/1.1\r\nHost: x\r\n\r\n")


def t_multiple_slashes():
    raw(b"GET ////index.html HTTP/1.1\r\nHost: x\r\n\r\n")


def t_unicode_path():
    raw("GET /café/ünicode.html HTTP/1.1\r\nHost: x\r\n\r\n".encode("utf-8"))


def t_abrupt_disconnect_mid_headers():
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(1.0)
        s.connect((HOST, PORT))
        s.sendall(b"GET / HTTP/1.1\r\nHost: x\r\nX-Partial:")
        s.close()  # header bitmeden abrupt kapatma
    except OSError:
        pass


def t_connect_and_immediately_close():
    for _ in range(20):
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.settimeout(1.0)
            s.connect((HOST, PORT))
            s.close()
        except OSError:
            pass


def t_one_byte_at_a_time():
    # Slowloris tarzi: cok kucuk parcalarla YAVAS gonderim. Sunucu bunu
    # sonsuza kadar beklememeli (header timeout), ama bu tek istekte
    # crash etmemeli. Kisa tutuyoruz (gercek 30sn header timeout'unu
    # burada beklemiyoruz, sadece "yavas gonderim sirasinda cokme var mi"
    # kontrolu).
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(2.0)
        s.connect((HOST, PORT))
        payload = b"GET / HTTP/1.1\r\nHost: x\r\n\r\n"
        for byte in payload:
            s.sendall(bytes([byte]))
            time.sleep(0.02)
        s.close()
    except OSError:
        pass


def t_many_concurrent_connections():
    socks = []
    try:
        for _ in range(150):
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.settimeout(2.0)
            s.connect((HOST, PORT))
            s.sendall(b"GET / HTTP/1.1\r\nHost: x\r\nConnection: keep-alive\r\n\r\n")
            socks.append(s)
        time.sleep(0.5)
    finally:
        for s in socks:
            try:
                s.close()
            except OSError:
                pass


def t_expect_100_continue_body_never_sent():
    raw(b"POST /uploads/x.txt HTTP/1.1\r\nHost: x\r\nContent-Length: 100\r\n"
        b"Expect: 100-continue\r\n\r\n", read_timeout=2.0)


def t_pipelined_garbage_after_valid():
    raw(b"GET / HTTP/1.1\r\nHost: x\r\n\r\n" + b"\x00\xff\xfe garbage not http \r\n\r\n")


def t_extremely_long_line_no_crlf():
    # \r\n hicbir zaman gelmeyen sinirsiz veri -- 431 ile kesilmeli, sonsuza
    # kadar biriktirmemeli.
    raw(b"GET / HTTP/1.1\r\nHost: x\r\nX-NoEnd: " + b"a" * 200000)


def t_cgi_bogus_query():
    raw(b"GET /cgi-bin/hello.py?" + b"a=" * 5000 + b"&b=%00%00 HTTP/1.1\r\nHost: x\r\n\r\n")


def t_delete_nonexistent():
    raw(b"DELETE /uploads/this_does_not_exist_at_all.txt HTTP/1.1\r\nHost: x\r\n\r\n")


def t_post_no_body_no_length():
    raw(b"POST /uploads/x.txt HTTP/1.1\r\nHost: x\r\n\r\n")


TESTS = [
    ("bos istek", t_empty_request),
    ("rastgele binary veri", t_garbage_binary),
    ("sadece CRLF'ler", t_only_crlf),
    ("method eksik", t_no_method),
    ("HTTP version eksik", t_missing_version),
    ("cok uzun method", t_huge_method),
    ("cok uzun URI (414 beklenir)", t_huge_uri),
    ("cok fazla header satiri (431 beklenir)", t_huge_header_count),
    ("tek header'da devasa deger", t_huge_single_header_value),
    ("path'te ham NULL bayt", t_null_bytes_in_path),
    ("path'te %00 (encoded null)", t_null_bytes_percent_encoded),
    ("derin path traversal denemesi", t_path_traversal_deep),
    ("bozuk percent-encoding", t_malformed_percent_encoding),
    ("negatif Content-Length", t_negative_content_length),
    ("devasa Content-Length, govde yok", t_huge_content_length_no_body),
    ("chunked: devasa chunk-size alani", t_chunked_huge_size_field),
    ("chunked: gecersiz (hex olmayan) size", t_chunked_garbage_size),
    ("chunked: negatif gorunumlu size", t_chunked_negative_looking_size),
    ("hem Content-Length hem chunked", t_both_cl_and_te),
    ("tekrarli Host/Content-Length header", t_double_headers),
    ("iki noktasiz (bozuk) header satiri", t_header_no_colon),
    ("bilinmeyen HTTP metodu", t_bogus_method),
    ("kucuk harfli method/version", t_lowercase_method),
    ("coklu ardisik slash", t_multiple_slashes),
    ("unicode path", t_unicode_path),
    ("header ortasinda ani baglanti kopmasi", t_abrupt_disconnect_mid_headers),
    ("20x hizli connect+close", t_connect_and_immediately_close),
    ("1 bayt bayt gonderim (slowloris-tarzi)", t_one_byte_at_a_time),
    ("150 es zamanli baglanti", t_many_concurrent_connections),
    ("Expect:100-continue, govde hic gelmiyor", t_expect_100_continue_body_never_sent),
    ("gecerli istekten sonra pipelined garbage", t_pipelined_garbage_after_valid),
    ("CRLF'siz sonsuz uzun satir (431 beklenir)", t_extremely_long_line_no_crlf),
    ("CGI'ye bozuk/devasa query string", t_cgi_bogus_query),
    ("var olmayan dosyayi DELETE", t_delete_nonexistent),
    ("POST, govde yok, Content-Length yok", t_post_no_body_no_length),
]


def main():
    global proc
    config = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_CONFIG
    print("webserv crash-test paketi baslatiliyor (config: {})".format(config))
    start_server(config)

    for name, fn in TESTS:
        check(name, fn)
        if not alive():
            break

    if proc is not None and proc.poll() is None:
        proc.terminate()
        try:
            proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            proc.kill()

    print()
    print("Sonuc: {} basarili, {} COKME".format(passed, failed))
    if failed > 0:
        print("SUBJECT KURALI IHLAL EDILDI: sunucu en az bir senaryoda cokmus/kapanmis.")
        sys.exit(1)
    print("Tum senaryolarda sunucu ayakta kaldi.")
    sys.exit(0)


if __name__ == "__main__":
    main()
