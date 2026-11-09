# Responsive AI Workspace (M4)

M3's backend and GUI now support background corpus builds and CR-1 Remote Add.
One standard C++ thread builds a private Workspace copy. A second receives
requests through a POSIX Unix domain stream socket.

Build and test in the course Docker container (Ubuntu 22.04 with Qt 6):

```sh
cmake -S . -B build
cmake --build build -j4
ctest --test-dir build --output-on-failure
```

CTest runs both suites with Qt's offscreen platform. To open the GUI and send a
file from another terminal:

```sh
./build/ai_workspace_m4 --build-delay-ms 500 --ipc-socket /tmp/my-aiws.sock
./build/aiws_send --socket /tmp/my-aiws.sock add data/public/doc_ai.txt
```

Both programs default to `/tmp/aiws-$USER.sock`. When USER is absent or empty,
they use the numeric user id. Relative document paths refer to the sender's
working directory. Quote paths with spaces in the shell.

`aiws_send` returns 0 when the receiver has read and queued the document, 1 for a
rejected request, or 2 for a connection/communication failure. The GUI applies
the queued document as soon as it processes events. The receiver does not wait
for the GUI before replying. The wire format is in `DESIGN.md`.

Build Corpus stays disabled until its job is collected. Add, selection, inputs,
Reset, and Exit remain usable while building. A successful Add invalidates the
old corpus. A duplicate does not. Confirmed Exit stops both workers and removes
the socket. Startup fails if the socket path already exists; it does not remove
another application's socket.

`student_tests` covers responsiveness, stale results, failures and retry,
search/context, repeated use, ownership, and exit during a build. `ipc_tests`
covers E1-E4, sender-relative and UTF-8/newline paths, split messages, replies
while the GUI is blocked, malformed/disconnected clients, timeouts, and shutdown.
Both required fault builds should fail the original fault-detection tests.

`DESIGN_NOTE.md` is the unchanged checkpoint. `HISTORY.txt` records actual Git
history. `DESIGN.md` contains the design and pair log. `CR1_DISCUSSION.md`
contains the message summary, missing-file test explanation, and discussion
questions. Session details in the pair log still need confirmation.

## Pair programming checklist / 结对编程清单

| 中文 | English |
| --- | --- |
| 确认你们已在 Canvas 的 M4 Pairs 配对。 | Confirm your pair in Canvas M4 Pairs. |
| 做两次会话：一次用你的 M4，一次用 partner 的 M4。 | Hold two sessions, one on each partner's own M4 code. |
| 使用同一台电脑，或共享、同步显示的屏幕。 | Use the same computer or a shared/duplicated screen. |
| 自己代码的会话中，至少一半时间当 driver；约 15 分钟或更久后交换角色。 | Drive for at least half of the session on your code; swap roles after about 15 minutes or more. |
| Driver 边改代码边解释；navigator 持续跟进、建议、审查并做笔记。 | The driver explains edits; the navigator stays engaged, suggests, reviews, and takes notes. |
| 一起处理复杂逻辑，例如部分消息读取、构建中远程添加、退出清理；发现问题后实际修改和验证。 | Work on challenging logic such as partial messages, Remote Add during builds, or shutdown; make and verify real changes when issues are found. |
| 各自提交自己的项目，不复制 partner 的代码。 | Submit your own project and do not copy your partner's code. |

Afterward, put these real details in `DESIGN.md` under **Pair programming log**:

- Partner's name / 同伴姓名。
- Each session's date, start/end times, whose code, and the part worked on /
  每次会话日期、起止时间、使用谁的代码、处理的部分。
- When roles changed / 何时交换角色。
- One navigator contribution from each person / 双方各一项 navigator 贡献。
- One sentence about the interaction that worked best / 一句话说明最有效的合作方式。

The pair log does not count toward the design record's one-page limit. The
separate pair-programming quiz counts toward the course grade, not M4 points.
结对日志不计入设计说明的一页限制；结对编程的单独课堂测验计入课程总评，不计入 M4 分数。

## Validation

Checked in Ubuntu 22.04 with GCC 11.4 and Qt 6.2.4:

- Normal CTest: student_tests passed 18 QtTest checks and ipc_tests passed 19,
  including setup/cleanup. The IPC checks include the missing-file regression test.
- Each required fault build failed the intended student tests within 60 seconds;
  the IPC suite passed in both fault builds.
- Real application/sender smoke checks passed with an explicit socket path, the
  USER default, and the numeric-uid fallback. Readable files returned 0 and
  unreadable files returned 1.
- Valgrind checks of split messages, repeated raw-socket clients, and shutdown
  reported 0 memory errors and no lost bytes. No socket descriptors remained.
  A separate run using QProcess clients reported a Qt waitid call with a null
  infop argument; that run is not claimed as a clean Valgrind result.
