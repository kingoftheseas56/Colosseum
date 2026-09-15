# K08-F2 debug trace

The first fresh-regression invocation configured successfully but passed a whitespace-tainted `CMAKE_BUILD_PARALLEL_LEVEL` value through `cmd.exe`; CMake rejected that tooling value before compilation. The corrected invocation used `cmake --build ... --parallel 1`. It built all nine steps and the three K08 tests passed.

This was a command-environment issue, not a source or contract failure. No source change followed it.

The first production green run failed the sibling-isolation assertion. Investigation showed both readers generated source token `1`; a more specific assertion reproduced the collision before failure injection. Moving allocation to a process-wide atomic token source fixed the ownership boundary and the full K08-F2 case passed.

The 60-test aggregate passed K08-F2 and 57 other cases, but unchanged fixed-port K10 failure-drain and cancel-drain lanes failed peer readiness. Both peers in the persistent failure-drain rerun reached their listening state, but the unchanged K10 candidate reported that they never became ready. An isolated rerun made cancel-drain pass; failure-drain remained unable to connect. No K10 source, test, runner, or evidence file is changed by K08-F2. The proportionate aggregate excluding K10/K10-E then passed 43/43.

Independent production review found that the older `closeState()` still called `cancelRead()` while range-iterating `activeReads`. New explicit-close and destructor tests made those cancellations complete synchronously and reproduced a segmentation fault at `fc739b27`. Refactoring failure and close paths through shared detach/release operations removed the reentrant iterator mutation. No transport code changed, and the earlier K10 raw runs were preserved.
