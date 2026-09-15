# K06-F2 review-repair debug trace

The reviewer identified that constructor invalidation trusted `VerificationBitmap::persist()` even though it never checked stream state. The deterministic RED used a directory as the bitmap path and reproduced silent success.

The first mutation-runner attempt stopped because Windows PowerShell promoted expected nonzero CTest stderr to a terminating error. `Invoke-Captured` now scopes error handling to collect native output and exit codes without treating the expected negative result as a runner crash.

The first complete run rejected M01-M07. The initial M08 removed only the open check, so the write check still threw and the focused test correctly stayed green. M08 was corrected to reproduce the entire historical silent implementation by removing open, write, and flush reporting together. The final isolated run rejected all 8/8 controls and exited 0.
