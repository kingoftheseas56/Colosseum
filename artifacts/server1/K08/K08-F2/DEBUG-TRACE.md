# K08-F2 debug trace

The first fresh-regression invocation configured successfully but passed a whitespace-tainted `CMAKE_BUILD_PARALLEL_LEVEL` value through `cmd.exe`; CMake rejected that tooling value before compilation. The corrected invocation used `cmake --build ... --parallel 1`. It built all nine steps and the three K08 tests passed.

This was a command-environment issue, not a source or contract failure. No source change followed it.
