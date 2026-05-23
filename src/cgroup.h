#ifndef CGROUP_H
#define CGROUP_H
#include <sys/types.h>

int crear_cgroup(char *nombre, pid_t pid, char *mem_max, char *cpu_max, char *pids_max);
int eliminar_cgroup(pid_t pid);

#endif
