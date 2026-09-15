# K10-F debugging trace

The first compiled real-wire run timed out before the initial snapshot. Its run directory had no peer wire log. The runner had passed an empty string after `--second-pieces`; `Start-Process` dropped that argument, so the peer process could not parse its command line. The runner now passes `,` as an explicit empty piece list and fails immediately if the listener does not report ready.

The next run showed `BITFIELD_SENT connection=1 pieces=1,3`, followed by immediate disconnect and no snapshot. The ordinary fixture had only two pieces, so advertising piece 3 violated BitTorrent spare-bit validity and libtorrent rejected the bitfield before the plugin callback. K10-F now prepares a dedicated four-piece fixture; production logic was not weakened to accept invalid wire data.

After those test-harness corrections, the focused production path passed twice consecutively.
