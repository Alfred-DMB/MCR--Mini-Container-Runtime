#define _GNU_SOURCE
#include "rootfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <linux/limits.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

int setup_rootfs(char *rootfs) {
    char abs_rootfs[PATH_MAX];

    if (realpath(rootfs, abs_rootfs) == NULL) {
        perror("realpath falló");
        return 1;
    }

    // 1. Aislar el namespace de mounts del host (puede fallar en user namespaces, no es fatal)
    mount(NULL, "/", NULL, MS_PRIVATE | MS_REC, NULL);

    // 2. Bind mount de /dev del host en el nuevo rootfs ANTES de pivot/chroot
    //    mount() no necesita permiso de escritura en el directorio destino, solo CAP_SYS_ADMIN
    {
        char dev_path[PATH_MAX + 8];
        snprintf(dev_path, sizeof(dev_path), "%s/dev", abs_rootfs);
        mkdir(dev_path, 0755);  // puede fallar si ya existe, está bien
        if (mount("/dev", dev_path, NULL, MS_BIND | MS_REC, NULL) == 0)
            mount(NULL, dev_path, NULL, MS_PRIVATE | MS_REC, NULL);
    }

    // 3. Bind mount del nuevo rootfs sobre sí mismo (necesario para pivot_root)
    if (mount(abs_rootfs, abs_rootfs, NULL, MS_BIND | MS_REC, NULL) == -1) {
        perror("bind mount falló");
        return 1;
    }

    // 4. Refrescar CWD al tope del bind mount
    if (chdir(abs_rootfs) == -1) {
        perror("chdir post-bind falló");
        return 1;
    }

    // 5. Intentar pivot_root
    //    Si el rootfs es de root y corremos sin sudo, mkdir("old_root") falla
    //    con EACCES dentro del user namespace → usamos chroot como fallback
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
        // Fallback: chroot (funciona con CAP_SYS_CHROOT en user namespace)
        if (chroot(abs_rootfs) == -1) {
            perror("chroot falló");
            return 1;
        }
        chdir("/");
    }

    // 6. Montar sistemas de archivos virtuales
    mount("proc", "/proc", "proc", 0, NULL);
    mount("sysfs", "/sys", "sysfs", 0, NULL);
    mkdir("/tmp", 0755);
    mount("tmpfs", "/tmp", "tmpfs", 0, NULL);

    return 0;
}