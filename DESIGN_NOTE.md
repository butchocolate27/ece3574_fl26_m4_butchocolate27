# M4 Design Note

## 1. Components and threads

I will keep `MainWindow`, the Workspace, and widgets on the GUI thread. A `BuildController` starts and collects one `std::thread` job. The worker gets a Workspace copy and builds its own `ProcessingCore`. A `BuildChunker` wrapper checks cancellation and adds the per-document delay in short worker sleeps, using the existing strategy API. A GUI `QTimer` calls the member slot `poll_build()` to check the future without waiting. The only shared application variable is an atomic cancellation flag, written by the GUI and read by the worker.

## 2. States

The corpus states are Empty, Dirty, and Current. Add or Build makes it Dirty; Reset makes it Empty. A valid success makes it Current. Failure for the present version leaves it Dirty. Only Current enables Search, Term Inspector, and Context. Job activity is tracked separately. Build Corpus is disabled until the active job is collected; Add, Reset, and Exit still work.

## 3. Event sequences and risks

Each Workspace change, including Reset, increases a version number that never resets. A job carries its starting version. The GUI accepts its corpus only if the version matches and the window is not closing.

- **Add during a build (R2):** (1) Build starts at version 4. (2) Add succeeds, making version 5 and requesting cancellation. (3) The old build finishes anyway. (4) I discard version 4. The corpus stays Dirty until a build for version 5 succeeds.
- **Exit during a build (R1, R4):** (1) The user confirms Exit. (2) The controller requests cancellation. (3) The GUI keeps processing events while the worker stops. (4) The controller collects the outcome and joins the thread, then closes the window.
- A slow build runs entirely on the worker (R1). Reset cancels the job and clears the display; its late result fails the version check (R2). Repeated Build clicks cannot start overlapping jobs.
- Worker exceptions go through the future. `poll_build()` reports failures and clears the busy state without overwriting newer corpus state (R3). Cancellation is separate from failure. Every finished job, including failed and stale jobs, is collected and joined (R4).

## 4. Corpus ownership

The worker creates a `unique_ptr<ProcessingCore>`. Ownership moves through the promise/future to the GUI, then into the current-corpus member or destruction if stale. Add, Reset, a replacement build, or Exit releases the previous corpus. The worker stops using it before handing it over. Every thread is joined before its owner is destroyed.

## 5. Alternative

I considered using the live Workspace in the worker. Add and Reset could change its data during a build. A private copy is easier for me to keep track of and avoids Workspace locks.
