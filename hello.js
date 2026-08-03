#!/usr/bin/env node
// hello.py'nin JavaScript (node) karsiligi -- webserv'in birden fazla CGI
// dilini ayni anda destekledigini gostermek icin. Ayni CGI/1.1 sozlesmesi:
// baglam env'den, POST body stdin'den, cikti "header'lar + bos satir + body".
"use strict";

function escapeHtml(s) {
    return String(s).replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
}

function readBody(cb) {
    var length = parseInt(process.env.CONTENT_LENGTH || "0", 10) || 0;
    if (length <= 0 || process.env.REQUEST_METHOD !== "POST")
        return cb("");
    var chunks = [];
    var got = 0;
    process.stdin.on("data", function (chunk) {
        chunks.push(chunk);
        got += chunk.length;
        if (got >= length)
            process.stdin.pause();
    });
    process.stdin.on("end", function () {});
    process.stdin.on("pause", flush);
    process.stdin.on("close", flush);
    var done = false;
    function flush() {
        if (done) return;
        done = true;
        cb(Buffer.concat(chunks).toString("utf8"));
    }
}

readBody(function (body) {
    var env = process.env;
    var out = [];
    out.push("Content-Type: text/html");
    out.push("");
    out.push("<!DOCTYPE html><html><head><title>webserv CGI (js)</title>" +
             '<link rel="stylesheet" href="/style.css"></head><body>');
    out.push("<h1>Hello from CGI (JavaScript)</h1>");
    out.push("<section><h2>Request</h2><ul>");
    out.push("<li>REQUEST_METHOD: <code>" + escapeHtml(env.REQUEST_METHOD || "") + "</code></li>");
    out.push("<li>SCRIPT_NAME: <code>" + escapeHtml(env.SCRIPT_NAME || "?") + "</code></li>");
    out.push("<li>QUERY_STRING: <code>" + escapeHtml(env.QUERY_STRING || "") + "</code></li>");
    out.push("<li>SERVER_NAME:SERVER_PORT: <code>" +
             escapeHtml(env.SERVER_NAME || "?") + ":" + escapeHtml(env.SERVER_PORT || "?") + "</code></li>");
    out.push("<li>User-Agent: <code>" + escapeHtml(env.HTTP_USER_AGENT || "(none)") + "</code></li>");
    out.push("</ul></section>");
    if (env.REQUEST_METHOD === "POST")
        out.push("<section><h2>Body received (" + body.length + " bytes)</h2><pre>" +
                 escapeHtml(body) + "</pre></section>");
    out.push('<p><a href="/">&larr; back</a></p>');
    out.push("</body></html>");
    process.stdout.write(out.join("\n") + "\n");
});
