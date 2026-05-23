#define _GNU_SOURCE
#include <sched.h>
#include <sys/wait.h>
#include <sys/mount.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/syscall.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <string.h>
#include <sys/sysinfo.h>
#include <sys/resource.h>

#include "rootfs.h"
#include "cgroup.h"
#include "seccomp.h"
#include "cap.h"

typedef struct {
    char **argv;
    char *rootfs;
    int slave_fd; // PTY: fd del extremo esclavo
    int pipefd;  
} containergs_args;

static int crear_user_namespaces(pid_t pid) {
    char map[256];
    char ruta[256];
    int fd;

    snprintf(map, sizeof(map), "0 %d 1\n", getuid());
    snprintf(ruta, sizeof(ruta), "/proc/%d/uid_map", pid);
    fd = open(ruta, O_WRONLY);
    if (fd < 0) {
        perror("error en uid_map");
        return 1;
    }
    write(fd, map, strlen(map));
    close(fd);

    snprintf(ruta, sizeof(ruta), "/proc/%d/setgroups", pid);
    fd = open(ruta, O_WRONLY);
    if (fd >= 0) {
        write(fd, "deny", 4);
        close(fd);
    }

    snprintf(map, sizeof(map), "0 %d 1\n", getgid());
    snprintf(ruta, sizeof(ruta), "/proc/%d/gid_map", pid);
    usleep(10000);
    fd = open(ruta, O_WRONLY);
    if (fd < 0) {
        perror("error gid_map");
        return 1;
    }
    write(fd, map, strlen(map));
    close(fd);

    return 0;
}
//HIJO
static int container_main(void *arg) {
 sleep(1);
containergs_args *args = (containergs_args *)arg;

    // Esperar a que el padre escriba los mapeos UID/GID
    char c;
    read(args->pipefd, &c, 1);
    close(args->pipefd);

    sethostname("mcr", 3);
    setup_rootfs(args->rootfs);
    setsid();  // nueva sesión (sin terminal controlante aún)

    // PTY: asignar slave como terminal controlante y redirigir stdin/out/err
    if (args->slave_fd >= 0) {
        ioctl(args->slave_fd, TIOCSCTTY, 0);
        dup2(args->slave_fd, STDIN_FILENO);
        dup2(args->slave_fd, STDOUT_FILENO);
        dup2(args->slave_fd, STDERR_FILENO);
        if (args->slave_fd > STDERR_FILENO)
            close(args->slave_fd);
    }

    // Limitar memoria del contenedor (funciona sin sudo)
    struct rlimit lim = {
        .rlim_cur = 128 * 1024 * 1024,
        .rlim_max = 128 * 1024 * 1024
    };
    setrlimit(RLIMIT_AS, &lim);

    if (reducir_capabilities() != 0) {
        fprintf(stderr, "reducir_capabilities fallo\n");
        exit(1);
    }

    if (cargar_filtro_seccomp() != 0) {
        fprintf(stderr, "Fallo al cargar seccomp\n");
        exit(1);
    }

    execvp(args->argv[0], args->argv);
    perror("execvp no chive");
    exit(1);
}

// PTY: bucle que retransmite datos entre la terminal real y el master PTY
static void relay_pty(int master_fd) {
    struct termios orig, raw;
    tcgetattr(STDIN_FILENO, &orig);
    raw = orig;
    cfmakeraw(&raw);
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);

    char buf[4096];
    fd_set fds;

    while (1) {
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);
        FD_SET(master_fd, &fds);

        if (select(master_fd + 1, &fds, NULL, NULL, NULL) < 0)
            break;

        if (FD_ISSET(STDIN_FILENO, &fds)) {
            ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
            if (n <= 0) break;
            write(master_fd, buf, n);
        }

        if (FD_ISSET(master_fd, &fds)) {
            ssize_t n = read(master_fd, buf, sizeof(buf));
            if (n <= 0) break;  // EIO = contenedor cerró su PTY
            write(STDOUT_FILENO, buf, n);
        }
    }

    tcsetattr(STDIN_FILENO, TCSANOW, &orig);  // restaurar terminal
}
//padre
int crear_contenedor(char *rootfs, char **argv_comando) {
   
    containergs_args args;
    args.argv = argv_comando;
    args.rootfs = rootfs;

    // PTY: abrir master
    int master_fd = posix_openpt(O_RDWR | O_NOCTTY);
    if (master_fd == -1) {
        perror("posix_openpt");
        return 1;
    }
    grantpt(master_fd);
    unlockpt(master_fd);

    int slave_fd = open(ptsname(master_fd), O_RDWR | O_NOCTTY);
    if (slave_fd == -1) {
        perror("open slave PTY");
        close(master_fd);
        return 1;
    }
    args.slave_fd = slave_fd;

    // PTY: propagar tamaño de ventana
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0)
        ioctl(master_fd, TIOCSWINSZ, &ws);

    char *stack = malloc(65536);
    if (!stack) {
        perror("malloc");
        close(master_fd);
        close(slave_fd);
        return 1;
    }

    //if (unshare(CLONE_NEWUSER) == -1) {
        //perror("no se pudo hacer newuser");
        //free(stack);
        //close(master_fd);
        //close(slave_fd);
        //return 1;
    //}

    int pipefd[2];
    pipe(pipefd);
    args.pipefd = pipefd[0];  // pasar fd de lectura al hijo
    int flags = CLONE_NEWUSER | CLONE_NEWNS | CLONE_NEWUTS | CLONE_NEWPID | CLONE_NEWNET | SIGCHLD;
    pid_t pid = clone(container_main, stack + 65536, flags, &args);
    if (pid == -1) {
        perror("clone falló");
        free(stack);
        close(master_fd);
        close(slave_fd);
        close(pipefd[0]);
        close(pipefd[1]);
        return 1;
    }

    close(pipefd[0]);
    crear_user_namespaces(pid);
    write(pipefd[1], "x", 1);  // nota: parece haber confusión en los extremos del pipe
    close(pipefd[1]);

    close(slave_fd);  // PTY: el padre no necesita el slave

    printf("PID del hijo: %d\n", pid);
    if (getuid() == 0) {
        printf("Llamando a crear_cgroup...\n");
        crear_cgroup("mcr", pid, "256M", "50000 100000", "10");
    } else {
        printf("[aviso] sin sudo: cgroups desactivados\n");
    }

    // Recursos del sistema al arrancar el contenedor
    {
        struct sysinfo si;
        long cpus = sysconf(_SC_NPROCESSORS_ONLN);
        printf("---------------------------\n");
        if (sysinfo(&si) == 0) {
            unsigned long total_mb = si.totalram  * si.mem_unit / (1024 * 1024);
            unsigned long libre_mb = si.freeram   * si.mem_unit / (1024 * 1024);
            printf("CPUs disponibles : %ld\n", cpus);
            printf("RAM total        : %lu MB\n", total_mb);
            printf("RAM libre        : %lu MB\n", libre_mb);
        }
        printf("---------------------------\n");
    }

    fflush(stdout);  // vaciar prints antes de que el relay tome el control
    relay_pty(master_fd);  // PTY: bloquea hasta que el contenedor salga

    waitpid(pid, NULL, 0);
    if (getuid() == 0)
        eliminar_cgroup(pid);
    close(master_fd);
    free(stack);
    return 0;
}