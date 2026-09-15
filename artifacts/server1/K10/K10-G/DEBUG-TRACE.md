# K10-G debug trace

The first fixture reached connected and unchoked state but recorded zero native request
adds and zero framed requests. Its seeder emitted HAVE_ALL. K10-F had established that the
controlled adapter publishes availability from bitfield and HAVE callbacks, so this fixture
could not authorize the owned block. The test moved to the existing controlled peer, which
emits the required bitfield.

The controlled peer then rejected the handshake. The runner's prepared info-hash did not
match the candidate's because the candidate called the prepare path a second time. Removing
that duplicate preparation restored one shared hash and the real request completed.

The initial resume implementation checked for a surviving live peer. If none remained after
the one-shot transfer, it synchronously handed deferred connects to libtorrent from the
submit caller. The resume-network-drain mutation therefore escaped. The repair removed the
fallback: current resume now sets the drain request, and the torrent plugin tick consumes the
deferred connects on libtorrent's network thread. The unmutated case passes and replacing the
network-thread swap now fails on the deferred endpoint ownership deadline.
