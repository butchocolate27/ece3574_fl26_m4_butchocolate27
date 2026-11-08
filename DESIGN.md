# M4 Decision Record

## Threads and ownership

`MainWindow` owns the Workspace, widgets, current corpus, `BuildController`, and
`IpcServer`. Only the GUI thread touches widgets or the Workspace.
`BuildController::start()` gives its worker a Workspace copy. `run()` builds a
separate `ProcessingCore`; `BuildChunker` adds the delay and checks cancellation.
The corpus moves through a promise/future into the window or destruction.
`finish_build()` clears the chunker's borrowed cancellation pointer before transfer.

The shared mutable application data is `BuildController::cancel_` and
`IpcServer::stop_`. The GUI writes these atomic bools and each worker reads its
own flag. Other worker inputs are fixed after startup. The socket is used only
by its receiver thread after setup. Futures and Qt's queued argument copies
provide the handoffs; no application queue or mutex is shared. The window
outlives the IPC thread that calls `invokeMethod()` on it.

Both workers make their futures ready at thread exit. GUI polling joins only
ready threads. Confirmed Exit requests both workers to stop and keeps processing
events until both are collected. Destructors also stop and join for direct
object destruction. Declining Exit changes nothing.

## Corpus state

Empty, Dirty, and Current are separate from job activity. Add and Reset increase
`workspace_version_`, cancel the build, and clear old output. Reset never resets
the version. `poll_build()` accepts only a successful matching-version result.
Only Current enables corpus operations. Build Corpus stays disabled until its
job is collected; other controls remain usable. A current build failure shows
its error and allows retry.

## Changes since the checkpoint and CR-1 impact

The background-build design is unchanged. A function-pointer task option allows
deterministic failure tests. The exit callback calls `close()` so workers can be
collected first.

CR-1 adds `IpcServer::handle_client()`, which reads the file and queues its path
and text without waiting for the GUI. `add_document_file()` and the new
`add_document_text()` slot share the existing duplicate check and invalidation.
`closeEvent()` and `poll_build()` now collect both workers. `main()` starts the
server; `tools/aiws_send.cpp` resolves relative file names in the sender's working
directory. Backend and component-widget behavior did not need changes.

## Wire format

Each connection carries one request and one reply. Both frames start with a
4-byte unsigned big-endian payload length, limited to 1,048,576 bytes. The
request payload is an absolute UTF-8 file path without NUL. A reply payload is
ASCII `0` for accepted, or ASCII `1` followed by a UTF-8 rejection reason.
Lengths count bytes, so spaces and newlines need no escaping. Reads and writes
loop until complete. Connections have a 5-second message deadline; polling
checks shutdown at most every 50 ms. The receiver serves one connection at a
time and removes its socket on exit. An existing socket path makes startup fail.

## Fault switches

`AIWS_FAULT_ACCEPT_STALE` bypasses the version check in `poll_build()`; the
`finished_build_after_add_is_discarded`, `finished_build_after_reset_is_discarded`,
and `reset_and_readd_cannot_reuse_old_version` tests catch it.
`AIWS_FAULT_LOSE_FAILURE` drops errors in `BuildController::poll()`; the
`failure_is_reported_and_retry_works` and `unknown_failure_is_reported` tests catch it.

## AI use

OpenAI Codex helped migrate M3, implement background builds and Remote Add, write
tests, and prepare this record.

## Pair programming log

Not completed yet. Actual partner, sessions, role swaps, and contributions remain
to be recorded after the pair work.
