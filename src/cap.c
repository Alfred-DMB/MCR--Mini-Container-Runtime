#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/capability.h>
#include <linux/capability.h>
#include <sys/prctl.h>
#include <errno.h>
#include "cap.h"
#define ACTIVAR_CAP(datos, cap) \
    do { \
        (datos)[0].effective   |= (1U << (cap)); \
        (datos)[0].permitted   |= (1U << (cap)); \
        (datos)[0].inheritable |= (1U << (cap)); \
    } while(0)


int reducir_capabilities(void){
    struct __user_cap_header_struct cabecera;
    struct __user_cap_data_struct datos_anteriores[2];
    struct __user_cap_data_struct nuevos_datos[2];
    
    cabecera.version = 0x20080522;  // _LINUX_CAPABILITY_VERSION_3
    cabecera.pid = 0;
    if(capget(&cabecera, datos_anteriores) == -1){
    	perror("capget ha tenido un error, arreglalo");
    	return 1;
    }
    nuevos_datos[0] = datos_anteriores[0];
    nuevos_datos[1] = datos_anteriores[1];

    nuevos_datos[0].effective = 0;
    nuevos_datos[0].permitted = 0;
    nuevos_datos[0].inheritable = 0;
    nuevos_datos[1].effective = 0;
    nuevos_datos[1].permitted = 0;
    nuevos_datos[1].inheritable = 0;

    // Solo lo esencial para aislamiento básico + cambio de usuario
ACTIVAR_CAP(nuevos_datos, CAP_SYS_CHROOT);   // pivot_root / chroot
ACTIVAR_CAP(nuevos_datos, CAP_SETUID);        // cambiar a usuario no-root
ACTIVAR_CAP(nuevos_datos, CAP_SETGID);        // cambiar a grupo no-root
ACTIVAR_CAP(nuevos_datos, CAP_KILL);          // señales al proceso hijo
ACTIVAR_CAP(nuevos_datos, CAP_NET_BIND_SERVICE); // si necesitas puertos <1024
ACTIVAR_CAP(nuevos_datos, CAP_DAC_OVERRIDE); // acceso a archivos dentro del rootfs

if(capset(&cabecera, nuevos_datos) == -1){
	perror("fallo capset cojones");
	return 1;
}
return 0;
}

	