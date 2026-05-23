#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif
//#include <sys/mount.h>
//#include <sys/syscall.h>
//#include <sys/wait.h>
//#include <linux/seccomp.h>
//#include <linux/filter.h>
//#include <sys/capability.h>

int crear_contenedor(char *rootfs, char **argv_comando);
void mostrar_ayuda(char *nombre_programa){
printf("Uso: %s run --rootfs <ruta> -- <comando [args y no se que poner]\n", nombre_programa);
printf("Ejemplo: %s run --rootfs ./alpine -- /bin/sh\n", nombre_programa);
}

int main(int argc, char **argv){
	if(argc < 5){
		mostrar_ayuda(argv[0]);
		return 1;
	}	
	
	if(strcmp(argv[1], "run") != 0) {
		printf("Error: el primer argumento debe ser 'run'\n");
		mostrar_ayuda(argv[0]);
		return 1;
	}
 	
	
	//variables de guardado de parseo
	char *rootfs = NULL;
	char *comando = NULL;
	int indice_comando = 0;

	for (int i = 2; i < argc; i++) {
		if(strcmp(argv[i], "--rootfs") == 0){
			if(i + 1 < argc){
				rootfs = argv[i + 1];
				i = i +1;
			} else {
				printf("Erro: --rootfs necesita una ruta\n");
				return 1;
			}
	}		
			else if(strcmp (argv[i], "--") == 0) {
				if(i + 1 < argc){
				comando = argv[i +1];
				indice_comando = i +1;
				break;
			} else {
				printf("ERROR: -- necesitas un comando\n");
				return 1;
			}
		}
	}
			
if(rootfs == NULL){
	printf("Error: falta --rootfs\n");
	mostrar_ayuda(argv[0]);
	return 1;
}
char abs_rootfs[PATH_MAX];
if (realpath(rootfs, abs_rootfs) == NULL) {
    perror("realpath falló en main");
    return 1;
}
printf("Ruta absoluta: %s\n", abs_rootfs);
if(comando == NULL){
	printf("Error: comando no funcional usar --\n");
	mostrar_ayuda(argv[0]);
	return 1;
}
printf("rootfs: %s\n", rootfs);
printf("comando: %s\n", comando);
for(int i = indice_comando; i < argc; i++){
	printf("arg[%d]: %s\n", i - indice_comando, argv[i]);
}
crear_contenedor(abs_rootfs, &argv[indice_comando]);	
return 0;
}
	