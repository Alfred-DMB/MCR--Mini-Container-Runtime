# Por qué el contenedor moría silenciosamente

## El síntoma

```
Agregando PID 123884 al cgroup...
Eliminando cgroup para PID 123884...   ← inmediato, sin shell
```

El cgroup se creaba y se eliminaba al instante. Nunca aparecía el prompt `/ #`.

---

## Diagnóstico por capas

### Capa 1 — ¿Era el cgroup?

No. El cgroup se eliminaba *después* de `waitpid()`, que es lo correcto.
El problema era que `waitpid()` retornaba inmediatamente porque el hijo ya había muerto.

### Capa 2 — ¿Moría antes de exec?

Se probó con `/bin/echo "hola"` y `/bin/sh -c "echo dentro"`.
Ambos funcionaron. Conclusión: el problema era específico del shell **interactivo**.

### Capa 3 — ¿Era falta de TTY?

Sí, en parte. Sin PTY, `ash` detecta que no tiene terminal controlante y sale inmediatamente.
**Solución parcial:** implementar PTY con `posix_openpt` + relay padre↔hijo.

Pero después de implementar el PTY, el shell seguía muriendo.

### Capa 4 — ¿Era seccomp?

Aquí estaba el bug real. El orden en `container_main` era:

```c
// ORDEN INCORRECTO
cargar_filtro_seccomp();   // filtro activo
setsid();                  // ← __NR_setsid NO estaba en la whitelist → SIGSYS → muerto
ioctl(TIOCSCTTY, ...);
execvp("/bin/sh", ...);
```

`SECCOMP_RET_KILL` mata el proceso sin mensaje, sin señal catcheable, sin log visible.
El hijo moría antes de llegar a `execvp`.

**Primera corrección:** mover `setsid()` y la configuración del PTY **antes** de aplicar seccomp.

Pero el shell seguía muriendo.

### Capa 5 — Syscalls que faltaban en la whitelist

Con el orden corregido, `ash` llegaba a ejecutarse pero moría durante su inicialización.
Los syscalls que `ash` necesita al arrancar y que **no estaban en el filtro**:

| Syscall | Nº | Para qué lo usa ash |
|---|---|---|
| `getpgid` | 121 | Job control: obtener process group de un PID |
| `getsid` | 124 | Verificar la sesión actual |
| `getresuid` | 118 | Leer UID real/efectivo/saved |
| `getresgid` | 120 | Leer GID real/efectivo/saved |
| `sigaltstack` | 131 | Stack alternativo para señales (libc startup) |
| `statx` | 332 | Versión moderna de `stat` usada por musl en kernels 4.11+ |

---

## Por qué era tan difícil de detectar

### 1. `SECCOMP_RET_KILL` no deja rastro visible

Cuando seccomp bloquea un syscall con `SECCOMP_RET_KILL`:
- El proceso muere con SIGSYS
- No hay mensaje en stderr
- No hay core dump por defecto
- El padre solo ve que el hijo salió (exit code por señal)
- No se imprime "execvp no chilve" porque el proceso muere antes o durante `execvp`

### 2. El PTY ocultaba la salida de error

Después de `dup2(slave_fd, STDERR_FILENO)`, cualquier error del hijo iba al PTY esclavo.
El relay del padre (`relay_pty`) no empezaba hasta después de `crear_cgroup` (~50ms).
Para entonces el hijo ya estaba muerto y el buffer del PTY se perdía con el HUP.

### 3. Los tests parciales funcionaban

`/bin/echo` y `/bin/sh -c "echo"` pasaban seccomp porque son comandos no-interactivos:
- No llaman `setpgid`/`getpgid` para job control
- No configuran terminal
- No leen archivos de perfil
Solo `ash` interactivo activa toda la inicialización que usa los syscalls bloqueados.

### 4. El bug tenía dos causas independientes encadenadas

```
Sin PTY  → ash sale inmediatamente (sin error visible)
Con PTY  → ash muere por seccomp    (sin error visible)
```

Resolver el PTY sin saber del seccomp hacía que el comportamiento externo fuese idéntico,
lo que dificultaba saber si se había avanzado.

---

## La solución final

### En `ns.c` — orden correcto en `container_main`

```c
sethostname("mcr", 3);
setup_rootfs(args->rootfs);

setsid();                          // 1. nueva sesión
ioctl(args->slave_fd, TIOCSCTTY);  // 2. asignar TTY controlante
dup2(args->slave_fd, STDIN_FILENO);
dup2(args->slave_fd, STDOUT_FILENO);
dup2(args->slave_fd, STDERR_FILENO);

cargar_filtro_seccomp();           // 3. seccomp AL FINAL, después del setup
execvp(args->argv[0], args->argv);
```

### En `seccomp.c` — ampliar la whitelist

Añadir los syscalls que `ash` y `musl` necesitan al arrancar:

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

## Resultado

```
/ #
```
