#define _GNU_SOURCE
#include "utils.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

int crear_cgroup(char *nombre, pid_t pid, char *mem_max, char *cpu_max, char *pids_max) {
    char ruta[256];
    (void)nombre;
    
    printf("Creando estructura de cgroups para PID %d...\n", pid);

    // ANTES de crear /minicr, habilitar en el root
escribir_en_archivo("/sys/fs/cgroup/cgroup.subtree_control", "+memory +pids +cpu");

// luego crear el padre
mkdir("/sys/fs/cgroup/minicr", 0755);

// luego habilitar en el padre
escribir_en_archivo("/sys/fs/cgroup/minicr/cgroup.subtree_control", "+memory +pids +cpu");
    
    // 0. Crear cgroup padre (nuestra propia jerarquía)
    const char *cgroup_padre = "/sys/fs/cgroup/minicr";
    
    if (mkdir(cgroup_padre, 0755) == -1) {
        if (errno != EEXIST) {
            perror("mkdir del cgroup padre falló");
            return 1;
        }
        printf("Cgroup padre ya existe, continuando...\n");
    }
    
    // 1. Habilitar controladores en el padre
    printf("Habilitando controladores en cgroup padre...\n");
    escribir_en_archivo("/sys/fs/cgroup/minicr/cgroup.subtree_control", 
                        "+memory +pids +cpu");
    
    // 2. Crear el directorio del cgroup hijo
    snprintf(ruta, sizeof(ruta), "/sys/fs/cgroup/minicr/mcr-%d", pid);
    printf("Creando cgroup hijo: %s\n", ruta);
    if (mkdir(ruta, 0755) == -1) {
        if (errno != EEXIST) {
            perror("mkdir del cgroup hijo falló");
            return 1;
        }
    }
    
    // 3. Configurar límite de memoria
    if (mem_max != NULL) {
        snprintf(ruta, sizeof(ruta), "/sys/fs/cgroup/minicr/mcr-%d/memory.max", pid);
        printf("Configurando limite de memoria: %s\n", mem_max);
        if (access(ruta, W_OK) == 0)
            escribir_en_archivo(ruta, mem_max);
    }
    
    // 4. Configurar límite de CPU
    if (cpu_max != NULL) {
        snprintf(ruta, sizeof(ruta), "/sys/fs/cgroup/minicr/mcr-%d/cpu.max", pid);
        printf("Configurando limite de CPU: %s\n", cpu_max);
        if (access(ruta, W_OK) == 0)
            escribir_en_archivo(ruta, cpu_max);
    }
    
    // 5. Configurar límite de procesos
    if (pids_max != NULL) {
        snprintf(ruta, sizeof(ruta), "/sys/fs/cgroup/minicr/mcr-%d/pids.max", pid);
        printf("Configurando limite de PIDs: %s\n", pids_max);
        if (access(ruta, W_OK) == 0)
            escribir_en_archivo(ruta, pids_max);
    }
    
    // 6. Agregar el proceso al cgroup
    snprintf(ruta, sizeof(ruta), "/sys/fs/cgroup/minicr/mcr-%d/cgroup.procs", pid);
    printf("Agregando PID %d al cgroup...\n", pid);
    char pid_str[32];
    snprintf(pid_str, sizeof(pid_str), "%d\n", pid);

    escribir_en_archivo(ruta, pid_str);
    
    return 0;
}

int eliminar_cgroup(pid_t pid) {
    char ruta[256];
    snprintf(ruta, sizeof(ruta), "/sys/fs/cgroup/minicr/mcr-%d", pid);
    if (rmdir(ruta) == -1) {
        perror("Error al eliminar cgroup");
        return 1;
    }
    return 0;
}