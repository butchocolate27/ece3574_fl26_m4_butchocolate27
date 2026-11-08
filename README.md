# Responsive AI Workspace (M4)

This project carries the M3 backend and GUI into M4. Build Corpus runs on one
standard C++ worker thread. Add and Reset invalidate old results, and the window
collects the worker before closing.

Build and test inside the course Docker container (Ubuntu 22.04 with Qt 6):

```sh
cmake -S . -B build
cmake --build build -j4
ctest --test-dir build --output-on-failure
```

The CTest entry sets Qt's offscreen platform. To open the GUI with a slower build:

```sh
./build/ai_workspace_m4 --build-delay-ms 500
```

The delay is extra milliseconds per document. Build Corpus stays disabled until
the active job is collected. Other controls stay usable during the build.

`student_tests` covers GUI responsiveness, file validation, M3 search/context,
Add and Reset during a build, completed but stale results, build failures and
retry, repeated use, corpus ownership, and exit during a build.

CR-1 is not implemented yet. Its specification is in Canvas course Files as
[M4_CR1_Remote_Add.pdf](https://canvas.vt.edu/courses/234585/files/47887719?wrap=1).
The final submission still needs Remote Add and the real pair programming log.
`DESIGN_NOTE.md` is the original checkpoint and is kept unchanged.

Validation on Ubuntu 22.04, GCC 11.4, and Qt 6.2.4:

- Normal build: 18 QtTest checks passed (including setup and cleanup).
- Stale-result fault build: the three stale-result tests failed, as intended.
- Lost-failure fault build: both failure-reporting tests failed, as intended.
- Both fault builds finished testing in less than 60 seconds.
- Valgrind checked ownership transfer, repeated build/reset, and destruction:
  0 errors, 0 definitely/indirectly/possibly lost bytes.

To check either required fault, configure a separate build directory with
`-DCMAKE_CXX_FLAGS=-D` followed by its name from the specification, then build
and run CTest there. The faulted tests are supposed to fail.
