# MCR — Bug fix history for rootless support

## Context

The goal was to run the container **without `sudo`** using `CLONE_NEWUSER` (Linux user namespaces).  
Running `./mcr run --rootfs ./alpine -- /bin/sh` caused the process to appear and disappear silently, with no prompt.

---

## Bug 1 — Silent stack overflow (most critical)

**File:** `src/ns.c`

**Problem:**  
The child's stack was created with `malloc(8192)` (8 KB). That base pointer was passed to `clone()` as `stack + 8192`, pointing to the top of those 8 KB.

Inside `setup_rootfs()` there are two local arrays on the stack:
```c
char abs_rootfs[PATH_MAX];   // 4096 bytes
char dev_path[PATH_MAX + 8]; // 4104 bytes
```
Together they sum ~8200 bytes, plus the frame overhead → the stack overflowed. The kernel killed the child silently (no visible signal, no message).

**Fix:**
```c
// Before:
char *stack = malloc(8192);
pid_t pid = clone(container_main, stack + 8192, flags, &args);

// After:
char *stack = malloc(65536);
pid_t pid = clone(container_main, stack + 65536, flags, &args);
```

**Important:** changing only the `malloc` is not enough. The second argument to `clone()` is the **top of the stack** (x86 grows downward). Forgetting to update `stack + 8192` → `stack + 65536` means the child still has exactly the same 8 KB of stack even though 65536 were allocated.

---

## Bug 2 — Seccomp blocked in user namespace

**File:** `src/seccomp.c`

**Problem:**  
`prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, ...)` requires one of:
1. `CAP_SYS_ADMIN` in the **initial namespace** (the host's).
2. The `no_new_privs` bit set.

In rootless mode, the process has `CAP_SYS_ADMIN` in the *new* user namespace but not the host's. On some kernels this is sufficient; on others it is not. Without `no_new_privs` the `prctl` failed with `EACCES` and the child died before reaching `execvp`, printing nothing (stderr already pointed to the PTY slave and the relay finished before reading the message).

**Fix:**
```c
// Add BEFORE loading the filter:
prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
```

This is the standard method used by Docker, runc and containerd to load seccomp without root privileges.

---

## Bug 3 — MS_PRIVATE cut setup_rootfs short with return 1

**File:** `src/rootfs.c`

**Problem:**  
In some user namespace contexts, `mount(NULL, "/", NULL, MS_PRIVATE | MS_REC, NULL)` can fail. The code had:
```c
if (mount(...) == -1) {
    perror("mount --make-rprivate / failed");
    return 1;  // ← early exit before chroot
}
```
`container_main` ignored the return value of `setup_rootfs`, so `execvp("/bin/sh")` ran on the **host** filesystem instead of Alpine. The host shell was then killed by seccomp (blocked syscalls).

**Fix:** remove the `return 1`. The call fails silently but is not fatal — the mount namespace is already isolated by `CLONE_NEWNS`.
```c
// Mount namespace already isolated by CLONE_NEWNS; failure is non-fatal
mount(NULL, "/", NULL, MS_PRIVATE | MS_REC, NULL);
```

---

## Bug 4 — pivot_root impossible → fallback to chroot

**File:** `src/rootfs.c`

**Problem:**  
Alpine was extracted with `sudo` and owned by `root` on the host. Inside the user namespace, `UID 0 in namespace = UID 1000 on host (Alfred)`. To the kernel, Alfred is "other" on a root-owned directory. `mkdir("old_root", 0755)` failed with `EACCES` → `pivot_root` was never attempted.

**Fix:** attempt `pivot_root` and fall back to `chroot` if `mkdir` fails:
```c
int pivot_ok = 0;
if (mkdir("old_root", 0755) == 0) {
    if (syscall(SYS_pivot_root, ".", "old_root") == 0) {
        pivot_ok = 1;
        chdir("/");
        umount2("/old_root", MNT_DETACH);
        rmdir("/old_root");
    } else {
        rmdir("old_root");
    }
}
if (!pivot_ok) {
    chroot(abs_rootfs);  // CAP_SYS_CHROOT is available in user namespace
    chdir("/");
}
```

---

## Dead code removed

| File | What was removed | Why |
|---|---|---|
| `src/seccomp.c` | `strict_filter[]` array | Never used; was a minimal 5-syscall filter replaced by `filtro[]` |
| `src/cgroup.c` | `leer_archivo()` function | Never called |
| `src/cgroup.c` | `int resultado;` variable | Declared but never assigned or read |
| `src/cgroup.c` | `nombre` parameter | Received but never used; replaced with `(void)nombre;` |

---

## Final result

```
$ ./mcr run --rootfs ./alpine -- /bin/sh
Child PID: 130712
[warning] no sudo: cgroups disabled
/ #
```

The container runs **completely without `sudo`** using Linux user namespaces.  
With `sudo` it also works and enables cgroups v2.
