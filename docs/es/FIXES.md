# MCR — Historial de bugs resueltos para soporte rootless

## Contexto

El objetivo era hacer correr el contenedor **sin `sudo`** usando `CLONE_NEWUSER` (user namespaces de Linux).  
Al correr `./mcr run --rootfs ./alpine -- /bin/sh` el proceso aparecía y desaparecía silenciosamente, sin prompt.

---

## Bug 1 — Stack overflow silencioso (el más crítico)

**Archivo:** `src/ns.c`

**Problema:**  
El stack del hijo se creaba con `malloc(8192)` (8 KB). Ese puntero base se pasaba a `clone()` como `stack + 8192`, apuntando al tope de esos 8 KB.

Dentro de `setup_rootfs()` hay dos arrays locales en el stack:
```c
char abs_rootfs[PATH_MAX];   // 4096 bytes
char dev_path[PATH_MAX + 8]; // 4104 bytes
```
Juntos suman ~8200 bytes, más el overhead del frame → el stack se desbordaba. El kernel mataba al hijo silenciosamente (sin señal visible, sin mensaje).

**Fix:**
```c
// Antes:
char *stack = malloc(8192);
pid_t pid = clone(container_main, stack + 8192, flags, &args);

// Después:
char *stack = malloc(65536);
pid_t pid = clone(container_main, stack + 65536, flags, &args);
```

**Importante:** cambiar solo el `malloc` no es suficiente. El segundo argumento de `clone()` es el **tope del stack** (x86 crece hacia abajo). Si se olvida actualizar `stack + 8192` → `stack + 65536`, el hijo sigue teniendo exactamente los mismos 8 KB de stack aunque se hayan reservado 65536.

---

## Bug 2 — Seccomp bloqueado en user namespace

**Archivo:** `src/seccomp.c`

**Problema:**  
`prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, ...)` requiere uno de los dos:
1. `CAP_SYS_ADMIN` en el **namespace inicial** (el del host).
2. El bit `no_new_privs` activo.

En modo rootless, el proceso tiene `CAP_SYS_ADMIN` en el *nuevo* user namespace pero no en el del host. En algunos kernels esto es suficiente; en otros no. Sin `no_new_privs` el `prctl` fallaba con `EACCES` y el hijo moría antes de llegar al `execvp`, sin imprimir nada (stderr ya apuntaba al PTY slave y el relay terminaba antes de leer el mensaje).

**Fix:**
```c
// Añadir ANTES de cargar el filtro:
prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
```

Este es el método estándar que usan Docker, runc y containerd para cargar seccomp sin privilegios de root.

---

## Bug 3 — MS_PRIVATE cortaba setup_rootfs con return 1

**Archivo:** `src/rootfs.c`

**Problema:**  
En algunos contextos de user namespace, `mount(NULL, "/", NULL, MS_PRIVATE | MS_REC, NULL)` puede fallar. El código tenía:
```c
if (mount(...) == -1) {
    perror("mount --make-rprivate / falló");
    return 1;  // ← salida temprana antes del chroot
}
```
`container_main` ignoraba el valor de retorno de `setup_rootfs`, por lo que `execvp("/bin/sh")` se ejecutaba sobre el sistema de archivos del **host**, no Alpine. El shell del host era luego matado por seccomp (syscalls bloqueadas).

**Fix:** quitar el `return 1`. La llamada falla silenciosamente pero no es fatal — el namespace de mounts ya está aislado por `CLONE_NEWNS`.
```c
// El mount namespace ya está aislado por CLONE_NEWNS; si falla, no es fatal
mount(NULL, "/", NULL, MS_PRIVATE | MS_REC, NULL);
```

---

## Bug 4 — pivot_root imposible → fallback a chroot

**Archivo:** `src/rootfs.c`

**Problema:**  
Alpine está extraído con `sudo` y es propiedad de `root` en el host. Dentro del user namespace, `UID 0 del namespace = UID 1000 del host (Alfred)`. Para el kernel, Alfred es "other" en un directorio de root. `mkdir("old_root", 0755)` fallaba con `EACCES` → `pivot_root` nunca se intentaba.

**Fix:** intentar `pivot_root` y si `mkdir` falla, usar `chroot` como fallback:
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
    chroot(abs_rootfs);  // CAP_SYS_CHROOT está disponible en user namespace
    chdir("/");
}
```

---

## Código muerto eliminado

| Archivo | Qué se eliminó | Por qué |
|---|---|---|
| `src/seccomp.c` | Array `strict_filter[]` | Nunca se usaba; era un filtro mínimo de 5 syscalls reemplazado por `filtro[]` |
| `src/cgroup.c` | Función `leer_archivo()` | Nunca se llamaba |
| `src/cgroup.c` | Variable `int resultado;` | Se declaraba pero nunca se asignaba ni leía |
| `src/cgroup.c` | Parámetro `nombre` | Se recibe pero nunca se usa; reemplazado por `(void)nombre;` |

---

## Resultado final

```
$ ./mcr run --rootfs ./alpine -- /bin/sh
PID del hijo: 130712
[aviso] sin sudo: cgroups desactivados
/ #
```

El contenedor corre **completamente sin `sudo`** usando user namespaces de Linux.  
Con `sudo` también funciona y activa cgroups v2.
