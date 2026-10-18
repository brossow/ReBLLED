Test certificates for `test_cert` (made with OpenSSL, nothing real; the serials are invented):

- `test_ca.pem`: stands in for Bambu Lab's "BBL CA".
- `leaf.pem`: issued to `TESTSERIAL0001`, signed by `test_ca.pem`, like a printer's certificate.
- `leaf_expired.pem`: the same, valid 2020-2021 (the controller has no clock, so dates are ignored).
- `other_ca.pem` / `leaf_other_ca.pem`: issued to `TESTSERIAL0001` but by a different authority.
- `self_signed.pem`: issued to `TESTSERIAL0001` by itself.

Regenerate with `sh make.sh`.
