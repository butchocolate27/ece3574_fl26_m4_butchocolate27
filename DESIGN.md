# M4 Decision Record

## Threads and ownership

`MainWindow` owns the Workspace, widgets, current corpus, and `BuildController`.
GUI functions run only on the GUI thread. `BuildController::start()` copies the
Workspace into one `std::thread`. `run()` creates a separate `ProcessingCore`.
`BuildChunker` adds the delay inside real chunking and checks cancellation in
short worker sleeps.

The only shared mutable application data is `BuildController::cancel_`: the GUI
writes this atomic bool and the worker reads it. The task function pointer is
constant. The worker owns its Workspace copy and corpus. A promise/future hands
the result to the GUI; neither thread uses the corpus during the other's
ownership. `BuildChunker::finish_build()` clears its borrowed cancellation pointer
before transfer. Future state is managed by the standard library.

The corpus moves from the worker into `BuildOutcome`, then into
`MainWindow::processing_core_` or destruction if discarded. Invalidation and
closing release the current corpus. `set_value_at_thread_exit()` makes the future
ready only when the worker exits. `poll()` checks readiness before getting the
result and joining. No thread is detached. The controller destructor also
requests cancellation and joins for callers that destroy it directly.

## States and validity

The corpus is Empty, Dirty, or Current; job activity is separate. Add and Reset
increase `workspace_version_`, cancel the job, and clear old output. Reset never
resets the version counter. `poll_build()` accepts only a successful result whose
version matches the present Workspace. Only Current enables corpus operations.
Build Corpus is disabled until the old job is collected; Add, selection, inputs,
Reset, and Exit remain available while building.

A failed current build shows its error and enables retry. Confirmed Exit calls
`closeEvent()`, requests cancellation, and keeps the event loop running until the
job is collected. Declining Exit leaves the build alone.

## Changes since the checkpoint

The main design is unchanged. I added a function-pointer task option and a
controller-taking window constructor so tests can produce failures deterministically.
The exit callback now calls `close()` instead of quitting the event loop directly,
so the window can finish collecting its worker first.

## Fault switches and tests

`AIWS_FAULT_ACCEPT_STALE` bypasses the version check in `MainWindow::poll_build()`.
`finished_build_after_add_is_discarded`, `finished_build_after_reset_is_discarded`,
and `reset_and_readd_cannot_reuse_old_version` catch it. They finish a worker
without processing GUI events, change the Workspace, then collect its result.

`AIWS_FAULT_LOSE_FAILURE` drops an error in `BuildController::poll()` and leaves
the job busy. `failure_is_reported_and_retry_works` and
`unknown_failure_is_reported` catch it through the actual window.

## CR-1 status

Remote Add is pending. `add_document_file()` is the common file-add entry point;
no socket server or `aiws_send` is implemented yet.

## AI use

OpenAI Codex helped migrate M3, implement background builds, write tests, and
prepare this record.

## Pair programming log

Not recorded yet. Add the actual partner and session details after pair work.
