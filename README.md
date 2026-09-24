# System-Programming-HW1
# FSS (FileSync System) 

> This document describes **exactly what the submitted code does**, including
> its known deviations from the assignment specification.

---

## 1. Overview

This project implements the **FileSync System (FSS)**, a Unix file synchronization
system written in C. It mirrors files from a *source* directory to a *target*
directory in real time. The implementation uses:

- **`inotify`** for monitoring directories (create / modify / delete events)
- **`fork()` + `exec()`** to spawn worker processes
- **Anonymous pipes** for worker → manager reporting (`EXEC_REPORT`)
- **Named pipes (FIFOs)** `fss_in` and `fss_out` for console ↔ manager communication
- **Low-level I/O syscalls** (`open`, `read`, `write`, `unlink`, `close`) inside the worker

The system consists of four executables and one bash script:

| Component | Purpose |
|-----------|---------|
| `fss_manager` | Central daemon: monitors directories, manages workers, handles commands |
| `fss_console` | User interface: sends commands, shows results, logs them |
| `worker`      | Performs a single sync job (full directory or single file) |
| `fss_script.sh` | Report generation and cleanup utility |

---

## 2. High-Level Architecture

```
        +-------------+         fss_in          +---------------+
        | fss_console | ----------------------> |  fss_manager  |
        |             | <---------------------- |               |
        +-------------+         fss_out         +-------+-------+
                                                      |
                                       fork() + exec()| (pipe for stdout)
                                                      v
                                             +-----------------+
                                             |     worker      |
                                             | (one per job)   |
                                             +-----------------+
                                                      |
                                                      v
                                          source_dir  →  target_dir
```

- The **console** writes user commands to the `fss_in` FIFO.
- The **manager** reads them, executes them, and writes responses to `fss_out`.
- The **manager** watches directories via `inotify` and spawns a **worker** on change.
- The **worker** syncs files and writes an `EXEC_REPORT` back to the manager
  through an anonymous pipe (its `stdout` is redirected to the pipe by the manager).
- The **manager** parses the report, updates its in-memory structures, and appends
  a formatted entry to `manager_logfile.txt`.
- The **console** prints the response to the screen and logs it to `console_logfile.txt`.

---

## 3. Project Structure

```
.
├── Makefile
├── README.md
├── fss_manager.c
├── fss_console.c
├── worker.c
├── commands.c            / commands.h
├── log_worker_utils.c    / log_worker_utils.h
├── sync_info_track.c     / sync_info_track.h
├── fss_script.sh
├── config_file.txt       (example)
├── manager_logfile.txt   (generated)
└── console_logfile.txt   (generated)
```

---

## 4. Component Details

### 4.1 `fss_manager`

**Command line (as implemented):**

```
./fss_manager -c <config_file> [-n <worker_limit>]
```

> **Note:** the `-l <manager_logfile>` option from the spec is **not parsed at all**.
> The manager log file is hardcoded as `manager_logfile.txt`
> (see `#define log_file "manager_logfile.txt"` in `log_worker_utils.c`).

**Startup sequence:**
1. Parses `-c` (required) and `-n` (optional). The value of `-n` is read into a
   local variable but is **always overwritten** with the constant
   `WORKER_LIMIT = 5`.
2. Calls `sigchld_handler(SIGCHLD)`.
   *(This is a plain function call. The handler is **not** installed with
   `signal()` or `sigaction()`, so it never actually fires on child exit.
   Zombie workers are therefore not reaped — see §10.)*
3. Creates the two named pipes with `mkfifo("fss_in", 0600)` and
   `mkfifo("fss_out", 0600)`. **No `unlink()` is performed beforehand.**
4. Opens `fss_in` with `O_RDONLY` and `fss_out` with `O_WRONLY`.
5. Opens the config file. Each line is parsed with:
   ```c
   sscanf(message, "(%[^,],%[^)])", source, target);
   ```
   → the **expected config file format is `(<source>,<target>)` per line**,
   e.g. `(/home/user/docs,/backup/docs)`.
   **This differs from the spec**, which uses a space separator
   (`/home/user/docs /backup/docs`).
6. For each pair it:
   - Launches a full-sync worker via `worker_on(source, target, "ALL", "FULL")`.
   - Stores the pair with `add_sync_info(source, target)`.
   - Registers the source with `inotify_add_watch` for
     `IN_CREATE | IN_MODIFY | IN_DELETE`.
   - Records the mapping with `add_watch_entry(wd, source)`.
   - **No "Added directory" / "Monitoring started" messages are printed
     or logged for config-file entries** (contrary to the spec).
7. Enters the main loop.

**Main loop (`select()` on two FDs):**
- `fd_in` (the `fss_in` FIFO from the console)
- `inotify_fd`

If `fd_in` is readable, the manager parses the command and dispatches:
`shutdown`, `add`, `cancel`, `status`, `sync`. Unknown input produces
`Invalid input: <cmd>`.

If `inotify_fd` is readable, the manager walks the `inotify_event` buffer,
determines the operation (`ADDED` / `MODIFIED` / `DELETED`), finds the source
directory from the watch descriptor (`lookup_source_from_wd`) and its target
(`get_target_for_source`), and calls `worker_on` with the modified file name.

**Data structures (`sync_info_track.c`):**
- `SyncInfo` — singly linked list holding
  `{source, target, status, last_sync_time, active, error_count}`.
- `WatchEntry` — singly linked list mapping a `wd` (inotify descriptor) to its
  `source` path.

**Command handling (in `commands.c`):**

| Command | Behaviour |
|---------|-----------|
| `add <src> <dst>` | Prints "Added directory" + "Monitoring started", logs them, and calls `worker_on(src, dst, "ALL", "FULL")`. **No duplicate check** — if `<src>` is already registered, it syncs again. |
| `cancel <src>` | Always prints "Monitoring stopped for <src>" and removes the watch. **No check** for whether `<src>` is currently monitored. |
| `status <src>` | Calls `find_sync_info`. **Does not check for NULL** — if `<src>` is unknown the process **segfaults**. |
| `sync <src>` | Calls `find_sync_info`. **Does not check for NULL** — same segfault risk. No "Sync already in progress" check. Always forks a new worker. |
| `shutdown` | Prints the four shutdown messages and returns. The main loop then `break`s **without waiting for active workers** and **without processing any queue** (there is no queue). |

**Logging:**
The manager writes to `manager_logfile.txt` through two helpers:
- `log_entry_file()` — simple `[TIMESTAMP] <message>` entries (used for
  "Added directory", "Monitoring started", "Syncing directory", etc.).
- `log_entry_file1()` — full-format entries:
  ```
  [TIMESTAMP] [SOURCE] [TARGET] [WORKER_PID] [OP] [RESULT] [DETAILS]
  ```
  Used only for the worker's `EXEC_REPORT`.

> The two formats are **not the same**, whereas the spec implies a uniform
> format for all manager log entries.

### 4.2 `fss_console`

**Command line (as implemented):**

```
./fss_console -l <console_logfile>
```

> **Note:** the `-l` argument is validated (the program exits if the first
> argument is not `-l`) but the value is **ignored**. The log file is hardcoded
> as `console_logfile.txt` (see `#define log_file "console_logfile.txt"`).

- Opens `fss_in` with `O_WRONLY` and `fss_out` with `O_RDWR`.
- Reads a line from `stdin`, strips the trailing newline, and logs it via
  `log_entry_file()` (as `Command <cmd>` if it is one of the recognised commands,
  otherwise as a plain message).
- Writes the command to `fss_in`.
- Uses `select()` with a **1-second timeout** on `fss_out` to collect all
  lines the manager sends back. Loops until the timeout expires (no more data).
- Each received line is printed to stdout via `log_entry()` and logged to
  `console_logfile.txt` via `log_entry_file()`.
- Exits when the user types `shutdown` (after receiving the manager's responses).

### 4.3 `worker`

Invoked by the manager as:

```
./worker <source> <target> <filename> <operation>
```

where `operation ∈ { FULL, ADDED, MODIFIED, DELETED }`.

- **`FULL` + filename `ALL`** → calls `copy_directory()` which opens the source
  directory with `opendir()`, creates the target with `mkdir()`, and iterates
  over entries using `readdir()`. Sub-directories are recursed with
  `copy_directory()`, regular files are copied with `copy_file()`.
  *(Although the spec says flat directories are enough, the code handles
  subdirectories defensively.)*
- **`ADDED` / `MODIFIED`** → `copy_file(source/filename, target/filename)`.
- **`DELETED`** → `unlink(target/filename)`.
- All I/O is done with low-level syscalls (`open`, `read`, `write`,
  `unlink`, `close`, `stat`, `mkdir`, `opendir`, `readdir`).
  No `system()`, `cp`, or `rsync` is used.
- On any syscall returning `-1`, `strerror(errno)` is appended to an
  in-memory `error_log[]` buffer.
- At the end the worker builds an `EXEC_REPORT` and writes it to
  `STDOUT_FILENO` (which the manager redirected to a pipe):

```
EXEC_REPORT_START
STATUS: SUCCESS | PARTIAL | ERROR
DETAILS: <n> files copied, <m> skipped
ERRORS:
- <reason>
EXEC_REPORT_END
```

The worker returns `0` on completion.

### 4.4 `fss_script.sh`

**Usage:**

```
./fss_script.sh -p <path> -c <command>
```

| Command | Behaviour |
|---------|-----------|
| `listAll` | Parses a log file line by line, strips `[` `]`, and prints `source -> target [Last Sync: <date> <time>] [<status>]`. |
| `listMonitored` | Same parsing, prints only lines whose status is `SUCCESS` or `PARTIAL`. |
| `listStopped` | Same parsing, prints only lines whose status is `ERROR`. |
| `purge` | If `-p` is a regular file, `rm` it. If it is a directory, it reads **hardcoded** `manager_logfile.txt` and refuses to delete a source directory; otherwise `rm -rf` the target directory. |

> **Note:** for the directory case, the script reads `manager_logfile.txt`
> unconditionally instead of deriving it from the `-p` path, because the target
> directory would already be gone by the time we need to look up whether it is
> a source or a target.

The parser reads arguments in a small loop (`for ((i=1; i<=$#; i++))`) and
checks each one against `-p` or `-c`. Only `-p` and `-c` are recognised.

---

## 5. IPC Summary

| Channel | Type | Direction | Purpose |
|---------|------|-----------|---------|
| `fss_in` | FIFO | console → manager | User commands |
| `fss_out` | FIFO | manager → console | Responses |
| `pipe()` | Anonymous pipe | worker → manager | `EXEC_REPORT` |
| `inotify_fd` | inotify fd | kernel → manager | Filesystem events |

The manager multiplexes `fss_in` and `inotify_fd` with `select()`.
The console uses `select()` with a 1-second timeout on `fss_out`
to drain all reply lines belonging to one command.

---

## 6. In-Memory Structures

### `SyncInfo` (linked list)
```c
typedef struct SyncInfo {
    char source[256];
    char target[256];
    char status[20];
    char last_sync_time[20];
    int  active;
    int  error_count;
    struct SyncInfo* next;
} SyncInfo;
```

### `WatchEntry` (linked list)
```c
typedef struct WatchEntry {
    int  wd;                  // inotify watch descriptor
    char source[256];
    struct WatchEntry *next;
} WatchEntry;
```

Both are simple linked lists — chosen for simplicity, since the number of
monitored directories is small.

---

## 7. Build & Run

### 7.1 Build

```bash
make all
```

Produces three executables: `fss_manager`, `fss_console`, `worker`.

> **Note on the Makefile:** although `MANAGER_OBJS` is defined, the target
> compiles all manager sources in a single `gcc` invocation rather than
> compiling each `.c` to a `.o` and linking them separately. This is **not
> true separate compilation** as requested by the spec. Functionally it works.

To also make the script executable:

```bash
make fss_script
```

### 7.2 Configure

Create `config_file.txt` with **one `(source,target)` pair per line**, e.g.:

```
(/home/user/docs,/backup/docs)
(/home/user/photos,/backup/photos)
```

**This is different from the assignment's space-separated format.**
If you supply a space-separated config file, the manager's `sscanf` will fail
and no directories will be monitored.

Only **flat** directories (no subdirectories) are assumed.

### 7.3 Run

In two separate terminals.

**Terminal A — manager:**
```bash
make run_man
```
The Makefile target runs:
```bash
./fss_manager -l "manager_logfile.txt" -c "config_file.txt" -n 5
```
*(The `-l` flag is passed but the manager ignores it — the log path is
hardcoded to `manager_logfile.txt`.)*

**Terminal B — console:**
```bash
make run_cons
```
The Makefile target runs:
```bash
./fss_console -l "console_logfile.txt"
```
*(The `-l` flag is passed but the console ignores its value — the log path is
hardcoded to `console_logfile.txt`.)*

### 7.4 Interactive commands

```
> add <source> <target>
> cancel <source>
> status <source>
> sync <source>
> shutdown
```

### 7.5 Cleanup

```bash
make clean
```

Removes object files, binaries, log files, and leftover FIFOs
(`*.o`, `fss_manager`, `fss_console`, `worker`, `manager_logfile.txt`,
`console_logfile.txt`, `fss_in`, `fss_out`).

---

## 8. Log Formats

### 8.1 `manager_logfile.txt`

**Worker result entries** (produced by `log_entry_file1()`):

```
[TIMESTAMP] [SOURCE] [TARGET] [PID] [OP] [RESULT] [DETAILS]
```

Example:

```
[2025-02-10 10:00:01] [/home/user/docs] [/backup/docs] [1234] [FULL] [SUCCESS] [10 files copied]
[2025-02-10 10:15:10] [/home/user/docs] [/backup/docs] [1237] [ADDED] [SUCCESS] [File: report.pdf]
[2025-02-10 10:30:10] [/home/user/docs] [/backup/docs] [1249] [MODIFIED] [ERROR] [File: budget.xlsx - Permission Denied]
```

**Other manager messages** (produced by `log_entry_file()`, used for commands
like "Added directory", "Monitoring started", "Syncing directory"):

```
[TIMESTAMP] <message>
```

Example:

```
[2025-02-10 10:00:01] Added directory: /home/user/docs -> /backup/docs
[2025-02-10 10:00:01] Monitoring started for /home/user/docs
```

> So the manager log file **mixes two formats**, whereas the spec asks for a
> uniform `[TIMESTAMP] [SOURCE] [TARGET] [PID] [OP] [RESULT] [DETAILS]` format.

### 8.2 `console_logfile.txt`

```
[TIMESTAMP] Command <cmd>
[TIMESTAMP] <response line>
```

Example:

```
[2025-02-10 10:00:01] Command add /home/user/docs /backup/docs
[2025-02-10 10:00:01] Added directory: /home/user/docs -> /backup/docs
[2025-02-10 10:00:01] Monitoring started for /home/user/docs
```

All timestamps use the format `%Y-%m-%d %H:%M:%S`.

---

## 9. Design Choices

1. **`select()` for multiplexing** — the manager must wait on two unrelated FDs
   (the FIFO from the console and the inotify descriptor). `select()` is the
   simplest portable choice and avoids introducing threads.
2. **One worker per job, forked on demand** — each directory change triggers a
   fresh `fork()` + `exec()` of the `worker` binary. This keeps the workers
   stateless and isolates failures.
3. **Anonymous pipe per worker for the EXEC_REPORT** — the child redirects its
   `stdout` to the pipe with `dup2()`, so any `write(STDOUT_FILENO, ...)` inside
   the worker arrives in the manager's buffer without shared memory.
4. **`SIGCHLD` + `waitpid(WNOHANG)`** — the *intent* is to reap terminated
   workers and avoid zombies. **However, the handler is currently not installed
   (the code only calls `sigchld_handler(SIGCHLD)` without registering it via
   `signal()`/`sigaction()`), so this mechanism is inactive and zombie workers
   can accumulate.** See §10 for details.
5. **Linked lists for `SyncInfo` and `WatchEntry`** — small N, no need for
   hash tables.
6. **Low-level I/O in the worker** — `open / read / write / unlink / close`
   (plus `opendir / readdir` for the full copy). No `cp`, `rsync`, or `system()`.
7. **Error aggregation in the worker** — all failures are accumulated into
   `error_log[]` and emitted once at the end of the `EXEC_REPORT`, so the
   manager only has to parse one block per job.
8. **Blocking FIFOs** — the manager uses `O_RDONLY` on `fss_in` and `O_WRONLY`
   on `fss_out`; no `O_NONBLOCK` is used. Startup order (manager first, console
   second) is assumed.

---

## 10. Known Limitations / Deviations from the Spec

The current implementation does **not** cover the full assignment specification.
The main gaps are:

- **`-l` manager log argument ignored.** The manager log path is hardcoded to
  `manager_logfile.txt` inside `log_worker_utils.c`. The `-l` flag is never parsed.
- **`-l` console log argument ignored (value).** The console validates that the
  first argument is `-l`, but never uses the path; the log file is hardcoded to
  `console_logfile.txt`.
- **`SIGCHLD` handler is not installed.** The code calls
  `sigchld_handler(SIGCHLD)` at startup without `signal()`/`sigaction()`.
  As a result the handler never runs, finished workers are not reaped, and
  zombies can accumulate.
- **No `worker_limit` queue.** The `-n` value is read but always overwritten by
  `WORKER_LIMIT = 5`. There is **no queue**: every event spawns a worker
  immediately, regardless of how many are running.
- **Config file format mismatch.** The manager expects `(source,target)` per
  line (parentheses + comma), not the space-separated format shown in the spec.
- **FIFOs are not cleaned up before `mkfifo`.** If `fss_in` / `fss_out` already
  exist from a previous run, `mkfifo` fails and the subsequent `open` fails,
  preventing startup.
- **No "Added directory" / "Monitoring started" logging for config-file entries.**
  The spec requires these messages on startup for each config-file pair; the
  code only prints them for runtime `add` commands.
- **No duplicate-`add` check.** `add` always starts a new full sync even if the
  source is already registered. The spec wants `Already in queue: <src>`.
- **No "Directory not monitored" response for `cancel`.** `cancel` always logs
  `Monitoring stopped for <src>` regardless of whether the directory is watched.
  The spec wants `Directory not monitored: <src>` for unknown sources.
- **No "Sync already in progress" handling for `sync`.** `sync` always forks a
  new worker. The spec wants `Sync already in progress <src>` when a sync is
  already running for that source.
- **`status_mode` and `sync_mode` do not NULL-check.** If `find_sync_info`
  returns `NULL`, `new->source` / `new->target` is dereferenced and the manager
  **segfaults**. The spec wants `Directory not monitored: <src>` in that case.
- **No graceful shutdown.** `shutdown_mode` prints four messages and returns;
  the main loop then `break`s out without waiting for active workers and without
  processing any queue (there is no queue to process anyway).
- **Inconsistent manager log format.** Worker results use the full
  `[TS] [SRC] [TGT] [PID] [OP] [RESULT] [DETAILS]` format, but all other
  manager messages use `[TS] <message>`. The spec implies a uniform format.
- **`Makefile` does not do true separate compilation.** `MANAGER_OBJS` is
  defined but unused; all manager sources are compiled in a single `gcc` call
  instead of object-by-object compilation + linking.
- **`fss_script.sh purge` reads a hardcoded `manager_logfile.txt`** for the
  source/target lookup instead of using the `-p` path, because the target
  directory would already be gone by the time the lookup would happen.

---

## 11. Testing Checklist

| Scenario | Actual result in this implementation |
|----------|--------------------------------------|
| Start manager with a 2-line `(src,tgt)` config | Two `FULL` worker entries; two inotify watches; **no "Added directory" messages** |
| Start manager with a space-separated config | **No watches registered** (sscanf fails silently) |
| Start manager twice without `make clean` | **`mkfifo` fails**, then `open` fails → manager exits |
| `add /src /dst` | "Added directory" + "Monitoring started" in both logs; new worker |
| `add` a source that is already known | Syncs again (no `Already in queue` message) |
| Create a file in a monitored source | `ADDED` entry; file copied to target |
| Modify a file in a monitored source | `MODIFIED` entry; target overwritten |
| Delete a file in a monitored source | `DELETED` entry; target file removed |
| `cancel /src` | "Monitoring stopped for /src" (even if `/src` was never monitored) |
| `status /src` (known) | Directory / Target / Last Sync / Errors / Status |
| `status /src` (unknown) | **Segfault** |
| `sync /src` (known) | "Syncing directory" then "Sync completed"; new worker |
| `sync /src` (unknown) | **Segfault** |
| `shutdown` | Four shutdown messages; manager and console exit without waiting for workers |
| `./fss_script.sh -p manager_logfile.txt -c listAll` | One line per directory |
| `./fss_script.sh -p /backup/pics -c purge` | Target directory removed |

---

## 12. Quick Reference

```bash
# Build everything
make all

# Run (two terminals)
make run_man    # ./fss_manager -l manager_logfile.txt -c config_file.txt -n 5
make run_cons   # ./fss_console -l console_logfile.txt

# Clean
make clean

# Reports
./fss_script.sh -p manager_logfile.txt -c listAll
./fss_script.sh -p manager_logfile.txt -c listMonitored
./fss_script.sh -p manager_logfile.txt -c listStopped
./fss_script.sh -p /backup/pics        -c purge
```
./fss_script.sh -p /backup/pics        -c purge
```
