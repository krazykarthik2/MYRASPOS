# Boot Sequence

This document outlines the overall boot sequence for MYRASPOS and points to the detailed implementation notes.

- Boot code: see [boot/start.s.md](boot/start.s.md) for the AArch64 assembly entry path.
- Linker scripts: see [boot/linker-files.md](boot/linker-files.md) for memory layout.
- Scheduler hand-off: see [kernel/03-TASK-SCHEDULING.md](kernel/03-TASK-SCHEDULING.md) for how control moves into task scheduling.

