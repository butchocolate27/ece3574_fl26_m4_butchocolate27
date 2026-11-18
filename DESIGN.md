# M5 design

Draft outline. Replace each prompt with your own words. Aim for one page, about 500 words including the table.

## M4 fix

What happened when the default ProcessingCore constructor failed? What does the GUI show now, and what happens on retry?

## Threads and shared data

What do the GUI, build worker and socket receiver each own? For pending_, stopping_, cancelled_ and IpcServer::stop_, name the reader, writer and protection. How does the build result get back to the GUI?

## States

| State | What it means | Events and next states |
| --- | --- | --- |
| Empty | | |
| Dirty | | |
| Building | | |
| Current | | |
| Closing | | |

## One event sequence

Trace Reset during a long build, then adding one file and starting a new build. Write one step per line. Name the thread and function for each step. What happens to a late completion?

## Three decisions

1. How does Building handle a second Build click? Why?
2. What does the user see when a remote Add succeeds during a build? Who starts the next build?
3. Why does the worker need a pending request, cancellation and shutdown information? What can stay on the GUI thread?

## Faults and tests

Name both fault switches, their functions, the tests that catch them, and what the user would observe.

## AI use

Describe the help you used for the design, code, tests and debugging.
