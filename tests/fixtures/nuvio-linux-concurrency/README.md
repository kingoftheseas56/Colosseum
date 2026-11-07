# Controlled Linux concurrency fixture

Four deliberately synthetic plugins each wait five seconds and return the same
public-domain Night of the Living Dead HTTP stream. They are fetched normally
from an exact-commit HTTPS repository and executed by the real bundled helper.
This proves concurrent worker scheduling, not live provider metadata lookup.
The separate pinned public-domain fixture performs the live metadata request.
