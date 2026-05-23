#define _GNU_SOURCE
#include "utils.h"
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int escribir_en_archivo(const char *ruta, const char *contenido) {
    int fd = open(ruta, O_WRONLY);
    if (fd == -1) {
        perror("Error al abrir");
        printf("  -> Ruta: %s\n", ruta);
        return 1;
    }

    ssize_t bytes_escritos = write(fd, contenido, strlen(contenido));
    if (bytes_escritos == -1) {
        perror("Error al escribir");
        printf("  -> Ruta: %s, Contenido: %s\n", ruta, contenido);
        close(fd);
        return 1;
    }

    close(fd);
    return 0;
}
