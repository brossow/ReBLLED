#!/bin/sh
# Regenerates the test certificates (see README.md). Needs OpenSSL 3.4 or later (-not_before).
set -e
cd "$(dirname "$0")"
k() { openssl genrsa -out "$1" 2048 2>/dev/null; }
k ca.key; k other.key; k leaf.key
openssl req -x509 -new -key ca.key -days 3650 -subj "/C=XX/O=Test/CN=Test CA" -out test_ca.pem
openssl req -x509 -new -key other.key -days 3650 -subj "/C=XX/O=Other/CN=Other CA" -out other_ca.pem
openssl req -new -key leaf.key -subj "/CN=TESTSERIAL0001" -out leaf.csr
openssl x509 -req -in leaf.csr -CA test_ca.pem -CAkey ca.key -CAcreateserial -days 3650 -out leaf.pem 2>/dev/null
openssl x509 -req -in leaf.csr -CA test_ca.pem -CAkey ca.key -CAcreateserial -not_before 20200101000000Z -not_after 20210101000000Z -out leaf_expired.pem 2>/dev/null
openssl x509 -req -in leaf.csr -CA other_ca.pem -CAkey other.key -CAcreateserial -days 3650 -out leaf_other_ca.pem 2>/dev/null
openssl req -x509 -new -key leaf.key -days 3650 -subj "/CN=TESTSERIAL0001" -out self_signed.pem
rm -f *.key *.csr *.srl
