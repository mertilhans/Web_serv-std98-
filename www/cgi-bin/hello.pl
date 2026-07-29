#!/usr/bin/env perl
use strict;
use warnings;

sub html_escape {
    my ($s) = @_;
    $s =~ s/&/&amp;/g;
    $s =~ s/</&lt;/g;
    $s =~ s/>/&gt;/g;
    $s =~ s/"/&quot;/g;
    return $s;
}

sub parse_form {
    my ($raw) = @_;
    my %params;
    for my $pair (split /&/, $raw) {
        my ($k, $v) = split /=/, $pair, 2;
        next unless defined $k;
        $v = "" unless defined $v;
        $k =~ tr/+/ /;
        $v =~ tr/+/ /;
        $k =~ s/%([0-9A-Fa-f]{2})/chr(hex($1))/eg;
        $v =~ s/%([0-9A-Fa-f]{2})/chr(hex($1))/eg;
        $params{$k} = $v;
    }
    return %params;
}

my $method = $ENV{REQUEST_METHOD} || "GET";
my $query  = $ENV{QUERY_STRING} || "";

my $body = "";
if ($method eq "POST") {
    my $length = $ENV{CONTENT_LENGTH} || 0;
    read(STDIN, $body, $length) if $length > 0;
}

my %params = parse_form($method eq "POST" ? $body : $query);
my $name = $params{name} || "";
my $name_safe = html_escape($name);

print "Content-Type: text/html; charset=utf-8\r\n\r\n";
print "<html><body>\n";
print "<h1>webserv CGI testi (perl)</h1>\n";
print "<form method=\"POST\" action=\"/cgi-bin-pl/hello.pl\">\n";
print "  <label>Isim: <input type=\"text\" name=\"name\" value=\"$name_safe\"></label>\n";
print "  <button type=\"submit\">Gonder</button>\n";
print "</form>\n";
print "<p>Merhaba, $name_safe!</p>\n" if length($name_safe);
print "<hr>\n";
print "<p>METHOD=" . html_escape($method) . "</p>\n";
print "<p>QUERY_STRING=" . html_escape($query) . "</p>\n";
print "<p>SERVER_NAME=" . html_escape($ENV{SERVER_NAME} || "") . "</p>\n";
print "</body></html>\n";
