# M5 Design: Thread-Safe Build Worker

**1. What I fixed in M4.** The old worker bypassed the default `ProcessingCore` constructor, and the GUI skipped a second Add of the same path. Now `add_document_text` adds each readable file. `BuildController::build` uses the default backend, which reports duplicate IDs during rebuild. On failure the GUI shows the error, returns to `Dirty` and disables processing. Reset and add the file once to recover.

**2. Shared data.** The GUI owns the window, Workspace and state objects. One build worker owns its snapshot and waits on `changed_` for a request or shutdown. Qt queues its read-only result and the IPC thread’s file contents for the GUI.

| Data | Who uses it | Why it is safe |
|---|---|---|
| `pending_` | GUI replaces it, worker takes and clears it | Both hold `mutex_` |
| `stopping_` | GUI sets it, worker reads it | Same mutex as the wait condition |
| `cancelled_` | GUI sets it, worker reads and clears it | Atomic flag |
| `IpcServer::stop_` | GUI writes it, IPC thread reads it | Atomic flag |

**3. States.** Reset takes any open state to `Empty`. On confirmed Exit, `shutdown` wakes and joins the build worker. The window also stops the IPC thread and enters `Closing`.

| State | Meaning | Other transitions |
|---|---|---|
| Empty | No documents | Add → Dirty |
| Dirty | Documents waiting for a build | Build → Building |
| Building | Request outstanding | Matching success → Current, failure or successful Add → Dirty |
| Current | Corpus ready to search | Add → Dirty, Build → Building |
| Closing | Window closing | Ignores late events |

**4. Example.** Start with six documents, Reset during Build, then add one and rebuild.

1. GUI — `start` saves the six-document snapshot (version 6) and wakes the worker.
2. Worker — `run` takes it, clears `cancelled_` and builds outside the lock.
3. GUI — `Building::reset` calls `request_cancel`. `clear_workspace` empties the Workspace and advances its version to 7.
4. GUI — `add_document_text` advances the version to 8. Build calls `start` with the new snapshot.
5. Worker — `DocumentBuildStep::check_cancelled` stops the old job. `run` posts its result and takes version 8.
6. GUI — `Building::build_finished` ignores the late version 6 result and accepts the successful version 8 result.

**5. Decisions.** `Building` leaves Build disabled and ignores repeated `build_requested` calls. A successful remote Add shows the new file. `Building::document_added` cancels the stale build. Rebuilding by hand lets the user finish adding files. In `run`, the request tells the worker what to build, cancellation ends one job, and stopping ends the thread.

**6. Tests.** `AIWS_FAULT_NO_CANCEL` in `cancel_locked` keeps the worker busy with old documents. `reset_rebuild_skips_remaining_documents` starts with ten files and gives the replacement 650 ms to become searchable. `AIWS_FAULT_SILENT_SHUTDOWN` makes the idle wait in `run` ignore Exit. When the `idle_exit_after_build` child hangs in `join`, its parent fails after four seconds.

**7. AI use.** I used ChatGPT to analyze the design, debug Docker results, and check this write-up.
