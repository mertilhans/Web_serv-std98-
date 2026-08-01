#!/bin/bash
# Bu oturum boyunca elle calistirdigimiz TUM curl (+ birkac nc) testlerinin
# kalici, tekrar calistirilabilir hali. feature_test.py/crash_test.py'nin
# Python alternatifi degil, tamamlayicisi -- hizli, gozle goruculur bir
# "duman testi" (smoke test) olarak dusunulebilir.
#
# Kullanim:
#   ./tests/curl_tests.sh
#
# Not: script kendi webserv instance'ini baslatir/durdurur, ayrica
# calisan bir instance olmasina gerek yok.

set -u
cd "$(dirname "$0")/.."

PASS=0
FAIL=0

check() {
	local name="$1" expected="$2" actual="$3"
	if [ "$expected" = "$actual" ]; then
		PASS=$((PASS + 1))
		echo "  [PASS] $name (beklenen=$expected, gelen=$actual)"
	else
		FAIL=$((FAIL + 1))
		echo "  [FAIL] $name (beklenen=$expected, gelen=$actual)"
	fi
}

check_contains() {
	local name="$1" needle="$2" haystack="$3"
	if echo "$haystack" | grep -qF "$needle"; then
		PASS=$((PASS + 1))
		echo "  [PASS] $name"
	else
		FAIL=$((FAIL + 1))
		echo "  [FAIL] $name -- '$needle' bulunamadi"
	fi
}

echo "webserv baslatiliyor..."
./webserv configs/default.conf > /tmp/curl_tests_webserv.log 2>&1 &
SERVER_PID=$!
sleep 1

if ! kill -0 "$SERVER_PID" 2>/dev/null; then
	echo "FATAL: sunucu baslatilamadi"
	cat /tmp/curl_tests_webserv.log
	exit 1
fi

cleanup() {
	kill "$SERVER_PID" 2>/dev/null
	wait "$SERVER_PID" 2>/dev/null
	rm -f /tmp/curl_tests_webserv.log
}
trap cleanup EXIT

echo
echo "=== Statik site ==="
check "GET /index.html"      "200" "$(curl -s -o /dev/null -w '%{http_code}' http://127.0.0.1:8080/index.html)"
check "GET / (dizin->index)" "200" "$(curl -s -o /dev/null -w '%{http_code}' http://127.0.0.1:8080/)"
check "GET /style.css"       "200" "$(curl -s -o /dev/null -w '%{http_code}' http://127.0.0.1:8080/style.css)"
check "GET /yok (404)"       "404" "$(curl -s -o /dev/null -w '%{http_code}' http://127.0.0.1:8080/yok-boyle-bir-sey)"

echo
echo "=== HTTP redirection ==="
check "GET /old -> 301"      "301" "$(curl -s -o /dev/null -w '%{http_code}' http://127.0.0.1:8080/old)"
LOC=$(curl -s -D - -o /dev/null http://127.0.0.1:8080/old | grep -i '^Location:' | tr -d '\r' | awk '{print $2}')
check "Location: /index.html" "/index.html" "$LOC"

echo
echo "=== Upload / Delete / Method restriction ==="
check "POST upload (201 yeni)"     "201" "$(curl -s -o /dev/null -w '%{http_code}' -X POST --data hi http://127.0.0.1:8080/uploads/curltest.txt)"
check "POST upload (200 ustune yaz)" "200" "$(curl -s -o /dev/null -w '%{http_code}' -X POST --data hi2 http://127.0.0.1:8080/uploads/curltest.txt)"
check "DELETE (200)"                "200" "$(curl -s -o /dev/null -w '%{http_code}' -X DELETE http://127.0.0.1:8080/uploads/curltest.txt)"
check "DELETE tekrar (404)"         "404" "$(curl -s -o /dev/null -w '%{http_code}' -X DELETE http://127.0.0.1:8080/uploads/curltest.txt)"
check "DELETE /index.html (405)"    "405" "$(curl -s -o /dev/null -w '%{http_code}' -X DELETE http://127.0.0.1:8080/index.html)"
check "GET /uploads (405, GET izinli degil)" "405" "$(curl -s -o /dev/null -w '%{http_code}' http://127.0.0.1:8080/uploads/)"

echo
echo "=== CGI ==="
check "CGI GET"  "200" "$(curl -s -o /dev/null -w '%{http_code}' http://127.0.0.1:8080/cgi-bin/hello.py)"
check "CGI POST" "200" "$(curl -s -o /dev/null -w '%{http_code}' -X POST --data 'a=1&b=2' http://127.0.0.1:8080/cgi-bin/hello.py)"
CGI_BODY=$(curl -s -X POST --data 'a=1&b=2' http://127.0.0.1:8080/cgi-bin/hello.py)
check_contains "CGI POST govdesi script'e ulasiyor" "POST" "$CGI_BODY"

echo
echo "=== Ozel error page + autoindex (9091, admin_panel) ==="
check "ozel error_page (404)" "404" "$(curl -s -o /dev/null -w '%{http_code}' http://127.0.0.1:9091/yok-boyle-bir-sey)"
check "autoindex acik"       "200" "$(curl -s -o /dev/null -w '%{http_code}' http://127.0.0.1:9091/)"

echo
echo "=== Wildcard/specific-IP vhost merge (9090) ==="
API_LOCAL=$(curl -s -H 'Host: api_local' http://127.0.0.1:9090/)
check_contains "Host: api_local -> api_local icerigi" "api_local" "$API_LOCAL"
MYIP=$(hostname -I | cut -d' ' -f1)
if [ -n "$MYIP" ]; then
	API_PUBLIC=$(curl -s -H 'Host: api_public' "http://$MYIP:9090/")
	check_contains "Host: api_public (gercek IP uzerinden) -> api_public icerigi" "api_public" "$API_PUBLIC"
fi

echo
echo "=== Coklu port dinleme ==="
check "8080 acik"  "200" "$(curl -s -o /dev/null -w '%{http_code}' http://127.0.0.1:8080/index.html)"
check "9090 acik"  "200" "$(curl -s -o /dev/null -w '%{http_code}' -H 'Host: api_local' http://127.0.0.1:9090/)"
check "9091 acik"  "200" "$(curl -s -o /dev/null -w '%{http_code}' http://127.0.0.1:9091/)"

echo
echo "=== Keep-alive / pipelining ==="
REUSE_COUNT=$(curl -s -v http://127.0.0.1:8080/index.html http://127.0.0.1:8080/style.css 2>&1 | grep -c 'Re-using existing connection')
check "keep-alive (Re-using existing connection)" "1" "$REUSE_COUNT"

echo
echo "=== Guvenlik: path traversal / null-byte / XSS ==="
check "path traversal engeli (../ )" "404" "$(curl -s -o /dev/null -w '%{http_code}' 'http://127.0.0.1:8080/../../etc/passwd')"
check "encoded null-byte reddi (%00)" "400" "$(curl -s -o /dev/null -w '%{http_code}' 'http://127.0.0.1:8080/index.html%00.txt')"

echo
echo "=== Raw-socket testleri (curl'un yapamadigi protokol-seviyesi durumlar) ==="
MISSING_HOST=$(printf 'GET / HTTP/1.1\r\n\r\n' | timeout 2 nc 127.0.0.1 8080 | head -1 | tr -d '\r')
check_contains "Host header eksik -> 400" "400" "$MISSING_HOST"

BOGUS_VERSION=$(printf 'GET / HTTP/9.9\r\nHost: x\r\n\r\n' | timeout 2 nc 127.0.0.1 8080 | head -1 | tr -d '\r')
check_contains "desteklenmeyen HTTP versiyonu -> 505" "505" "$BOGUS_VERSION"

MALFORMED_CL=$(printf 'POST / HTTP/1.1\r\nHost: x\r\nContent-Length: abc\r\n\r\n' | timeout 2 nc 127.0.0.1 8080 | head -1 | tr -d '\r')
check_contains "bozuk Content-Length -> 400" "400" "$MALFORMED_CL"

HUGE_CL=$(printf 'POST /uploads/x.txt HTTP/1.1\r\nHost: x\r\nContent-Length: 999999999\r\n\r\n' | timeout 2 nc 127.0.0.1 8080 | head -1 | tr -d '\r')
check_contains "client_max_body_size asilinca -> 413" "413" "$HUGE_CL"

EXPECT_CONTINUE=$(printf 'POST /uploads/expecttest.txt HTTP/1.1\r\nHost: x\r\nContent-Length: 5\r\nExpect: 100-continue\r\n\r\nhello' | timeout 2 nc 127.0.0.1 8080)
check_contains "Expect: 100-continue -> ara yanit gonderiliyor" "100 Continue" "$EXPECT_CONTINUE"
curl -s -o /dev/null -X DELETE http://127.0.0.1:8080/uploads/expecttest.txt

echo
echo "Sonuc: $PASS basarili, $FAIL basarisiz"
[ "$FAIL" -eq 0 ]
