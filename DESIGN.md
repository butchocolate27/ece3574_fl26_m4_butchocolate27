# M4 Decision Record

## Threads and ownership

`MainWindow` owns the Workspace, widgets, current corpus, `BuildController`, and
`IpcServer`. Only the GUI thread touches widgets or the Workspace.
`BuildController::start()` gives its worker a Workspace copy. `run()` builds a
separate `ProcessingCore`; `BuildChunker` adds delay and checks cancellation.
The corpus moves through a promise/future into the window or destruction.
`finish_build()` clears the chunker's borrowed cancellation pointer before transfer.

Shared mutable application data consists of `BuildController::cancel_` and
`IpcServer::stop_`. The GUI writes these atomic bools; each worker reads its own
flag. Other worker inputs are fixed after startup. The receiver alone uses its
socket after setup. Futures and Qt's queued argument copies handle transfers;
no application queue or mutex is shared. The window outlives the IPC thread
that calls `invokeMethod()` on it.

Both workers make their futures ready at thread exit. GUI polling joins only
ready threads. Confirmed Exit stops both workers and keeps processing events
until both are collected. Destructors also stop and join for direct destruction.
Declining Exit changes nothing.

## Corpus state

Empty, Dirty, and Current are separate from job activity. Add and Reset increase
`workspace_version_`, cancel the build, and clear old output. Reset never resets
the version. `poll_build()` accepts only a successful matching-version result.
Only Current enables corpus operations. Build Corpus stays disabled until its
job is collected; other controls remain usable. A current build failure shows
its error and allows retry.

## Changes since the checkpoint and CR-1 impact

The background-build design is unchanged. A function-pointer task option permits
deterministic failure tests. The exit callback calls `close()` so workers can be
collected first.

CR-1 adds `IpcServer::handle_client()`, which reads the file and queues its path
and text without waiting for the GUI. `add_document_file()` and
`add_document_text()` share duplicate checking and invalidation. An unreadable
file is rejected before changing the Workspace, so its current corpus remains
usable; `unreadable_remote_file_keeps_current_corpus` tests this.
`closeEvent()` and `poll_build()` collect both workers. `main()` starts the
server; `tools/aiws_send.cpp` resolves sender-relative file paths. Backend and
component-widget behavior did not need changes.

## Wire format

Each connection carries one request and one reply. Frames start with a 4-byte
unsigned big-endian payload length, limited to 1,048,576 bytes. The request is an
absolute UTF-8 path without NUL. A reply is ASCII `0` for accepted, or ASCII `1`
followed by a UTF-8 rejection reason. Lengths count bytes, so spaces and newlines
need no escaping. Reads/writes loop until complete. Connections have a 5-second
message deadline; polling checks shutdown every 50 ms. The receiver serves one
connection at a time and removes its socket on exit. Existing paths make startup
fail.

## Fault switches

`AIWS_FAULT_ACCEPT_STALE` bypasses the version check in `poll_build()`;
`finished_build_after_add_is_discarded`, `finished_build_after_reset_is_discarded`,
and `reset_and_readd_cannot_reuse_old_version` catch it.
`AIWS_FAULT_LOSE_FAILURE` drops errors in `BuildController::poll()`;
`failure_is_reported_and_retry_works` and `unknown_failure_is_reported` catch it.

## AI use

OpenAI Codex helped with coding and debugging.

## Pair programming log

The Zoom meeting ran from 13:30 to 14:00 on November 8, 2026. We spent
15 minutes on each project and switched roles at 13:45.

### Session 1 - Gary Jia's code

- Partner: Kodela, Varshitha.
- Date: November 8, 2026.
- Time: 13:30-13:45, 15 minutes.
- Driver: Gary Jia. Navigator: Kodela, Varshitha.
- Format: Zoom.
- Code discussed: `tests/ipc_tests.cpp`, especially
  `unreadable_remote_file_keeps_current_corpus`.

I proposed adding a test to check that a failed Remote Add does not invalidate
a corpus that is already working. I entered the code and explained the expected
behavior while Varshitha reviewed the test and suggested changes.

We worked through using the existing `IpcFixture` to add a valid file and build
a corpus, then using `send_file` to send a missing file path through the real
`aiws_send`. We checked that the sender returns 1 with an error message while
the original document, enabled corpus actions, and search result remain
available. We also discussed waiting for Search to become enabled after the
background build, processing queued window calls, and searching again after
the rejection to check that the application remains usable.

At 13:45, we moved to Varshitha's code and switched driver/navigator roles.
Working through one short test at a time helped keep our discussion focused.

A fuller technical explanation is in `CR1_DISCUSSION.md`.

### Session 2 - Varshitha's code

- Date: November 8, 2026.
- Time: 13:45-14:00, 15 minutes.
- Format: Zoom, continuing the same meeting.
- Driver: Kodela, Varshitha. Navigator: Gary Jia.

Varshitha drove this part while I reviewed her code as navigator.

We reviewed her `aiws_send` and discussed why a separate client sends paths to
the running application. She explained her 4-byte length prefix and the
`encode_length()` and `decode_length()` functions in her project. We discussed
reading the complete path without treating spaces or newlines as separators.

We also reviewed her `IpcServer`. I suggested keeping the client and server
responsibilities separate and the classes concise: `aiws_send` sends requests,
while `IpcServer` receives and processes them. Varshitha's navigator contribution
on my code was reviewing the test's event sequence and button-click coverage;
mine on her code was the suggestion about separating responsibilities.
Reviewing one implementation at a time made our feedback concrete.

### Messages and preparation

We exchanged iMessages about sharing our own explanations and coding ideas.
My partner asked to see my notes and offered to share their own when finished.
