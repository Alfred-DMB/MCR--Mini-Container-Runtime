# Why the container died silently

## The symptom

```
Adding PID 123884 to cgroup...
Removing cgroup for PID 123884...   ← immediate, no shell
```

The cgroup was created and removed instantly. The `/ #` prompt never appeared.

---

## Diagnosis by layers

### Layer 1 — Was it the cgroup?

No. The cgroup was removed *after* `waitpid()`, which is correct.
The problem was that `waitpid()` returned immediately because the child was already dead.

### Layer 2 — Was it dying before exec?

Tested with `/bin/echo "hello"` and `/bin/sh -c "echo inside"`.
Both worked. Conclusion: the problem was specific to the **interactive** shell.

### Layer 3 — Was it a missing TTY?

Partially. Without a PTY, `ash` detects it has no controlling terminal and exits immediately.  
**Partial fix:** implement a PTY with `posix_openpt` + parent↔child relay.

But after implementing the PTY, the shell kept dying.

### Layer 4 — Was it seccomp?

This was the real bug. The order in `container_main` was:

```c
// WRONG ORDER
cargar_filtro_seccomp();   // filter active
setsid();                  // ← __NR_setsid NOT in whitelist → SIGSYS → dead
ioctl(TIOCSCTTY, ...);
execvp("/bin/sh", ...);
```

`SECCOMP_RET_KILL` kills the process silently — no message, no catchable signal, no visible log.
The child died before reaching `execvp`.

**First fix:** move `setsid()` and PTY setup **before** applying seccomp.

But the shell kept dying.

### Layer 5 — Syscalls missing from the whitelist

With the corrected order, `ash` reached execution but died during initialization.
The syscalls `ash` needs at startup that **were not in the filter**:

| Syscall | No. | Why ash needs it |
|---|---|---|
| `getpgid` | 121 | Job control: get process group of a PID |
| `getsid` | 124 | Check current session |
| `getresuid` | 118 | Read real/effective/saved UID |
| `getresgid` | 120 | Read real/effective/saved GID |
| `sigaltstack` | 131 | Alternate signal stack (libc startup) |
| `statx` | 332 | Modern `stat` used by musl on kernels 4.11+ |

---

## Why it was so hard to detect

### 1. `SECCOMP_RET_KILL` leaves no visible trace

When seccomp blocks a syscall with `SECCOMP_RET_KILL`:
- The process dies with SIGSYS
- No message on stderr
- No core dump by default
- The parent only sees that the child exited (signal exit code)
- No "execvp failed" is printed because the process dies before or during `execvp`

### 2. The PTY hid the error output

After `dup2(slave_fd, STDERR_FILENO)`, any error from the child went to the PTY slave.
The parent's relay (`relay_pty`) did not start until after `crear_cgroup` (~50 ms).
By then the child was already dead and the PTY buffer was lost with the HUP.

### 3. Partial tests passed

`/bin/echo` and `/bin/sh -c "echo"` passed seccomp because they are non-interactive:
- They don't call `setpgid`/`getpgid` for job control
- They don't configure a terminal
- They don't read profile files
Only interactive `ash` triggers the full initialization that uses the blocked syscalls.

### 4. The bug had two independent chained causes

```
Without PTY → ash exits immediately (no visible error)
With PTY    → ash killed by seccomp (no visible error)
```

Fixing the PTY without knowing about seccomp made the external behavior identical,
making it hard to tell whether any progress had been made.

---

## The final solution

### In `ns.c` — correct order in `container_main`

```c
sethostname("mcr", 3);
setup_rootfs(args->rootfs);

setsid();                          // 1. new session
ioctl(args->slave_fd, TIOCSCTTY);  // 2. assign controlling TTY
dup2(args->slave_fd, STDIN_FILENO);
dup2(args->slave_fd, STDOUT_FILENO);
dup2(args->slave_fd, STDERR_FILENO);

cargar_filtro_seccomp();           // 3. seccomp LAST, after all setup
execvp(args->argv[0], args->argv);
```

### In `seccomp.c` — extend the whitelist

Add the syscalls that `ash` and `musl` need at startup:

```c
ALLOW_SYSCALL(__NR_setpgid),
ALLOW_SYSCALL(__NR_getpgrp),
ALLOW_SYSCALL(__NR_getpgid),
ALLOW_SYSCALL(__NR_getsid),
ALLOW_SYSCALL(__NR_setsid),
ALLOW_SYSCALL(__NR_getresuid),
ALLOW_SYSCALL(__NR_getresgid),
ALLOW_SYSCALL(__NR_sigaltstack),
ALLOW_SYSCALL(__NR_statx),
```

---

## Result

```
/ #
```
