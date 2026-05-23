# MCR — Mini Container Runtime

A Linux container runtime built from scratch in C, without Docker or any container library. Runs fully **rootless** using Linux user namespaces.

> Built as a learning project to understand how Docker works internally and to go deeper into Linux kernel interfaces — namespaces, capabilities, seccomp BPF, cgroups, and PTY handling.

## Features

- **Rootless** — no `sudo` required; uses `CLONE_NEWUSER` to map an unprivileged user to UID 0 inside the container
- **Namespace isolation** — PID, mount, UTS, network, and user namespaces via `clone()`
- **Filesystem isolation** — `pivot_root` (with `chroot` fallback) into an Alpine Linux rootfs
- **Interactive shell** — PTY (pseudo-terminal) relay for a fully interactive session
- **Seccomp BPF filter** — syscall whitelist; any unlisted syscall kills the process immediately
- **Capability dropping** — reduces Linux capabilities to the minimum before `execvp`
- **Memory limit** — enforces a 128 MB virtual address space limit via `setrlimit` (no cgroups needed)
- **cgroups v2** — memory, CPU, and PID limits when running as root

## Requirements

- Linux kernel ≥ 5.11 (user namespaces + cgroups v2)
- `gcc`
- An unpacked Alpine Linux rootfs at `./alpine/` (see setup below)

## Build

```bash
make
```

Clean build artifacts:

```bash
make clean
```

## Setup

Download and extract an Alpine Linux minirootfs **without sudo** so that `pivot_root` works correctly:

```bash
mkdir alpine
curl -LO https://dl-cdn.alpinelinux.org/alpine/v3.19/releases/x86_64/alpine-minirootfs-3.19.0-x86_64.tar.gz
tar xf alpine-minirootfs-3.19.0-x86_64.tar.gz -C alpine/
```

> If the rootfs was previously extracted with `sudo` (files owned by root), fix the root directory ownership:
> ```bash
> sudo chown $(whoami) ./alpine
> ```

## Usage

```bash
./mcr run --rootfs ./alpine -- /bin/sh
```

With root (enables cgroups v2 limits):

```bash
sudo ./mcr run --rootfs ./alpine -- /bin/sh
```

On startup the runtime prints system resource information:

```
PID del hijo: 9067
[aviso] sin sudo: cgroups desactivados
---------------------------
CPUs disponibles : 4
RAM total        : 3726 MB
RAM libre        : 157 MB
---------------------------
/ #
```

## Project Structure

```
src/
  main.c      — CLI argument parsing
  ns.c        — core runtime: clone, PTY, UID mapping, relay loop
  rootfs.c    — pivot_root / chroot, virtual filesystem mounts
  seccomp.c   — BPF syscall whitelist filter
  cap.c       — Linux capability reduction
  cgroup.c    — cgroups v2 setup (memory, CPU, PIDs)
  utils.c     — shared file I/O helpers
alpine/       — Alpine Linux rootfs (not included)
config/       — example configuration
```

## Security model

| Mechanism | What it restricts |
|---|---|
| User namespace | Maps host UID to container UID 0 without real root |
| Mount namespace | Container cannot see or affect host mounts |
| PID namespace | Container cannot see host processes |
| Network namespace | Isolated network stack |
| Seccomp BPF | Kills the process on any non-whitelisted syscall |
| Capability drop | Removes dangerous capabilities before exec |
| `setrlimit` | Hard 128 MB virtual memory cap (rootless) |
| cgroups v2 | Memory, CPU, and PID limits (root only) |

## Testing

Run the integration test suite:

```bash
make test
```

Expected output:

```
=== MCR integration tests ===

[1] Binary
  [PASS] mcr binary exists and is executable

[2] Isolation
  [PASS] UID is 0 inside container
  [PASS] hostname is 'mcr'
  [PASS] rootfs is Alpine

[3] Basic execution
  [PASS] echo works
  [PASS] sh -c works
  [PASS] working dir is /

[4] Filesystem isolation
  [PASS] /proc is mounted
  [PASS] /tmp is mounted

==============================
  Results: 9 passed, 0 failed
==============================
```

Tests are located in `test/run_tests.sh`.

## License

MIT

---

## Bugs encountered & how they were solved

A full account of every bug found and fixed during development is documented in [`docs/en/FIXES.md`](docs/en/FIXES.md) / [`docs/es/FIXES.md`](docs/es/FIXES.md).

Summary:

| Bug | File | Fix |
|---|---|---|
| Child stack overflow (silent crash) | `src/ns.c` | `malloc(8192)` → `malloc(65536)`, and `stack + 8192` → `stack + 65536` in `clone()` |
| `seccomp` failing with `EACCES` in user namespace | `src/seccomp.c` | Added `prctl(PR_SET_NO_NEW_PRIVS, 1, ...)` before loading the filter |
| `MS_PRIVATE` abort cutting rootfs setup short | `src/rootfs.c` | Removed `return 1` — failure is non-fatal in user namespaces |
| `pivot_root` always skipped | `src/rootfs.c` | Added `chroot` fallback; fixed by extracting rootfs without `sudo` |
| `reducir_capabilities` killed by seccomp | `src/ns.c` | Moved capability drop to before seccomp load — seccomp must always be last |
| `setrlimit` code placed at global scope | `src/cgroup.c` | Moved into `container_main` in `src/ns.c` where it belongs |

