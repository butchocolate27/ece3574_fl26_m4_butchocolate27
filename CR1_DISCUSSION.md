# CR-1 Discussion Notes

## Messages and preparation

My partner asked what I had prepared for my section and said we did not have to
meet if I could not make it. I replied after getting home and explained that my
notes covered the concept of my part and some coding ideas. I offered to share
them, and my partner asked to see them. My partner also offered to share their
notes when finished so we could explain our work if asked later.

## Zoom discussion on my code

Partner: Kodela, Varshitha. We met on Zoom on November 8, 2026, from 13:30 to
13:50, for 20 minutes, to discuss the missing-file regression test in my M4 code.
The discussion below summarizes the technical topics rather than quoting the
conversation word for word. The pair programming log is in `DESIGN.md`.

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

We exchanged driver/navigator roles during the session and reviewed the checks
together. The exact role-swap time is not recorded here. Working through one
short test at a time helped keep our discussion focused.

## Discussion on Varshitha's code

In the next part of the same Zoom meeting, Varshitha drove and explained her
`aiws_send` implementation. We discussed using a separate client to send file
paths to a running AI Workspace application.

Her message format uses a 4-byte length prefix. Her `encode_length()` and
`decode_length()` functions convert the length between an integer and the four
bytes sent through the socket. We discussed how the receiver can use this
length to read a complete path, including spaces or newlines.

We then reviewed her `IpcServer`, which manages the application-side Unix domain
socket. I suggested keeping client and server responsibilities separate and
concise. The sender handles outgoing requests; the server handles receiving and
processing them. These named encode/decode functions describe her project;
my project's framing helpers are in `src/ipc_protocol.cpp`.

The roles changed when we moved from reviewing my test to reviewing her code.
The exact start and end times of the second code-review part still need
clarification.

## Test idea and expected behavior

The case is a missing file sent through Remote Add after a valid corpus has
already been built. A failed request should return 1 with an error message.
It should preserve the Workspace, current corpus, and existing search results.
A readable new document changes the Workspace and invalidates the old corpus;
a rejected file leaves the current corpus usable.

The test `unreadable_remote_file_keeps_current_corpus` in
`tests/ipc_tests.cpp` checks this through the real `aiws_send`:

1. Add a readable file containing `apple` and build the corpus.
2. Wait for Search to become available, then search and check the one result.
3. Send a path to a file that does not exist in the temporary directory.
4. Check exit code 1 and a nonempty error message.
5. Process queued window calls, then check the document count, enabled corpus
   actions, and existing result.
6. Search again and confirm that it still returns the expected result.

Building first establishes a good state to protect. Return code 1 means the
application rejected the file; 2 means a connection or communication failure.
`QCoreApplication::sendPostedEvents()` is used only in the test to handle queued
window calls before checking state. Searching again verifies continued use,
beyond leaving an old result visible.

## Technical points we discussed

We discussed starting with a valid corpus instead of an empty Workspace. That
makes the test check preservation of a working state, rather than only checking
that a bad file is rejected.

We distinguished exit code 1 from 2. A missing file is rejected by a reachable
server, so it should produce 1. Code 2 would mean the connection or communication
failed, which would not exercise the same behavior.

We reviewed waiting until Search becomes available after the background build.
This makes the test follow the application state instead of assuming a build
finishes after a fixed sleep. Before checking the post-request state, the test
processes queued window calls.

Finally, we discussed checking both the preserved state and another search.
Keeping an old result visible alone does not establish that Search can still
run. The test checks the document count, enabled actions, existing result, and
the result of searching again.

## Verification

Run these commands inside the course Docker container from the project root:

```sh
cmake -S . -B build
cmake --build build -j4
QT_QPA_PLATFORM=offscreen ./build/ipc_tests \
  unreadable_remote_file_keeps_current_corpus
ctest --test-dir build --output-on-failure
```

The selected regression test and both CTest suites should pass.
