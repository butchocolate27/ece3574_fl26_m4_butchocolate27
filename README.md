# AI Workspace M5

Add text files to the Workspace, then click Build Corpus. Once the build finishes, Search, Inspect, and Build Context use the new corpus. Adding a different file or pressing Reset cancels the old build. Click Build Corpus again when the Workspace is ready.

## Build and test

Run these commands inside the course Docker container.

```sh
cmake -S . -B build-m5
cmake --build build-m5 -j4
ctest --test-dir build-m5 --output-on-failure
```

To slow down each document for the demo, run the app with a delay in milliseconds.

```sh
./build-m5/ai_workspace_m5 --build-delay-ms 500
```

From a second terminal in the same container, send a file to the running app.

```sh
./build-m5/aiws_send add data/public/doc_ai.txt
```

Both programs accept `--ipc-socket PATH` (app) or `--socket PATH` (sender) for a separate session. The sender returns 0 when the file has been read and queued, 1 for a rejected request, or 2 for a connection error. An unreadable file leaves the Workspace alone. Adding the same path twice makes the next build report a duplicate document ID. Use Reset to start again.

## Walkthrough

For the cancellation comparison, build a second copy.

```sh
cmake -S . -B build-m5-no-cancel -DCMAKE_CXX_FLAGS=-DAIWS_FAULT_NO_CANCEL
cmake --build build-m5-no-cancel -j4
./build-m5-no-cancel/ai_workspace_m5 --build-delay-ms 500
```

With either version open, add six files from the second terminal.

```sh
demo_dir=$(mktemp -d)
for n in 1 2 3 4 5 6; do
    echo "Old document $n" > "$demo_dir/doc$n.txt"
    ./build-m5/aiws_send add "$demo_dir/doc$n.txt"
done
```

Click Build Corpus and press Reset during the first document. Add `data/public/doc_ai.txt` and build again. Watch how long it takes for Inspect to become available. Close the first app before running the second one on the same socket.
