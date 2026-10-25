
#ifndef SHELL_H
#define SHELL_H

#define MAXCOMMAND 128
#define MAX_ARGS 16

int comparar_cadenas(char *a, char *b);

void shell_ejecutar(void);

void procesar_comando(char *texto);

#endif
