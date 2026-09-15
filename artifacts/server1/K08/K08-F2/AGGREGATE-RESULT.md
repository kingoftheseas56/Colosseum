# K08-F2 aggregate result

The current INT-W3 aggregate configured and built the candidate, including the new K08-F2 case. K08-F2 passed.

The full run produced 58/60: unchanged fixed-port K10 failure-drain and cancel-drain readiness lanes failed. An isolated rerun made cancel-drain pass; failure-drain again found both controlled peers listening but the unchanged adapter did not mark them ready. The K08-F2 diff contains no K10 source, test, runner, or evidence change.

The proportionate aggregate excluded K10 and K10-E and passed 43/43. It includes every other Server 1.0 packet, K08 4/4, P08-T, and the INT-W3 combined spine-link test.
