# FTP implementation evidence and remaining differences

Reference: pinned server.js SHA256
`405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f`,
M1308 (routes/metadata), M1309 (FTP client).

Implemented: FTP URL decoding, anonymous or URL-supplied login, default binary/
UTF-8 settings, MDTM/SIZE/FEAT metadata, REST detection, EPSV with PASV fallback,
explicit keys and compressed-query keys, pending-key redirect, HEAD, stream
headers and legacy range quirks. Non-550 MDTM errors trigger MLSD/LIST -a/LIST
fallback. M1309's Unix/DOS parsers expose rawModifiedAt but not modifiedAt:
M1308 consequently rejects those entries as not found; native matches this.
REST support requires a nonempty feature value, matching M1308 truthiness.

Contiguous bounded reads reuse one passive transfer, with a 64 KiB socket read
buffer and no eager body cache. Routes request at most 64 KiB; the source API
caps each request at 1 MiB. Noncontiguous reads reconnect (and discard a bounded
prefix when REST is unavailable). Cancel/destruction suppress callbacks and
close both sockets. Idle sequential transfers are owned by their source until
reuse/destruction. PASV always uses the control peer rather than trusting a
different advertised host. Credentials are not logged. Fixtures use anonymous
loopback login only. Directory metadata is limited to 1 MiB.

Verification:
- `run_ftp_oracle.cjs`: 14 actual M1308 HEAD/range observations with a controlled
  metadata transport, compared with native range planning.
- `run_ftp_transport_oracle.cjs`: actual pinned M1308 and M1309 against anonymous
  loopback FTP, not a replacement FTP library: nine metadata/status/seek cases,
  six byte intervals and one exact RETR error. Includes MLSD fallback, failed
  MLSD followed by LIST, EPSV failure followed by PASV, Unix-list rejection,
  missing files, SIZE errors and empty REST feature values. Byte comparisons use
  the requested count from the oracle's downloaded suffix, matching HTTP framing.
- `run_ftp_tests.py --candidate server1_ftp_source_tests`: real loopback metadata,
  seek/nonseek byte intervals, missing metadata/RETR errors, cancellation,
  reconnect, destruction during transfer and explicit FTPS rejection. A
  dedicated 200 KiB nonseek case makes the fixture reject a second RETR, proving
  all four bounded pulls use a single transfer.
- The same fixture with `server1_ftp_routes_tests`: parser-dispatched real route
  creation, pending-key redirects, HEAD, complete/ranged bytes, nonseek rejection
  and abandoned response cleanup.

Not complete 1:1 parity:
- FTPS is explicitly rejected. M1308 disables certificate verification; this task
  does not authorize weakening TLS verification. No success is fabricated.
- Compressed-key simultaneous creation is not coalesced. Source's initEmitter
  notification bug is not reproduced; pending key consumers resolve and expire
  without retaining dead responses.
- Malformed/inverted wire lengths fail 500 rather than emitting invalid HTTP.
  Source pipes all bytes after REST despite bounded Content-Length; native emits
  only the advertised response body.
- Oversized metadata replies/listings fail explicitly instead of buffering without
  a cap. Alternate PASV hosts are not contacted.
- Directory listing support matches tested MLSD and ordinary Unix/DOS outcomes;
  obscure MLSD symlink transformations and invalid date normalization have not
  been exhaustively differentiated.
- The transfer differential is a focused lawful fixture matrix, not proof for
  every FTP server, disconnect timing, authentication mode or error string.
