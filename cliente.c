//gcc cli.c -o cli -Wall
//./cli -f 5000x.dat
//diff -s 5000x.dat ../Servidor/5000x.dat 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <stddef.h>

#define PUERTO 4950
#define PAYLOAD_SIZE 1024
#define MAX_LONGITUD_MSG (sizeof(struct Mensaje) + PAYLOAD_SIZE)

#define PETICION 0
#define RESPUESTA 1
#define OK 200
#define NO_ENCONTRADO 404
#define ACK 300

#pragma pack(1)
int num_mens=0;
struct Mensaje {
    unsigned char nseq;
    int tipo;
    int codigo;
    char carga_util[PAYLOAD_SIZE];
    uint32_t fcs;
};

void error(char *mensaje_error) {
    perror(mensaje_error);
    exit(EXIT_FAILURE);
}

uint32_t crc32(const uint8_t *data, size_t length) {
    uint32_t crc = 0xFFFFFFFF;
    while (length--) {
        crc ^= *data++;
        for (int k = 0; k < 8; k++) {
            crc = crc & 1 ? (crc >> 1) ^ 0xEDB88320 : crc >> 1;
        }
    }
    return crc ^ 0xFFFFFFFF;
}

int crear_socket() {
    int socket_cliente;
    if ((socket_cliente = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
        error("Error al crear el socket");
    }
    return socket_cliente;
}

void inicializar_direccion_servidor(struct sockaddr_in *direccion_servidor) {
    memset(direccion_servidor, 0, sizeof(struct sockaddr_in));
    direccion_servidor->sin_family = AF_INET;
    direccion_servidor->sin_port = htons(PUERTO);
    direccion_servidor->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
}

void enviar_peticion(int socket_cliente, struct sockaddr_in *direccion_servidor, char *archivo, char *obuffer) {
    struct Mensaje *omensaje = (struct Mensaje *)obuffer;
    memset(obuffer, '\0', MAX_LONGITUD_MSG);
    omensaje->tipo = PETICION;
    omensaje->codigo = 0;
    strcpy(omensaje->carga_util, archivo);

    if (sendto(socket_cliente, obuffer, sizeof(struct Mensaje)  , 0, (struct sockaddr *)direccion_servidor, sizeof(*direccion_servidor)) == -1) {
        error("Error al enviar");
    }
    printf("Solicitud enviada al servidor.\n");
}

void recibir_respuesta(int socket_cliente, struct sockaddr_in *direccion_servidor, char *ibuffer) {
    socklen_t len = sizeof(struct sockaddr);
    if (recvfrom(socket_cliente, ibuffer, MAX_LONGITUD_MSG, 0, (struct sockaddr *)direccion_servidor, &len) == -1) {
        error("Error al recibir");
    }
}

void manejar_respuesta(int socket_cliente, struct sockaddr_in *direccion_servidor, char *ibuffer, char *obuffer, char *nombre_archivo) {
    struct Mensaje *imensaje = (struct Mensaje *)ibuffer;
    struct Mensaje *omensaje = (struct Mensaje *)obuffer;
    socklen_t len = sizeof(struct sockaddr);

    if (imensaje->codigo == OK) {
        printf("Respuesta desde el servidor: %d El archivo está disponible\n", imensaje->codigo);

        FILE *archivo = fopen(nombre_archivo, "wb");
        if (archivo == NULL) {
            error("Error al abrir el archivo para escritura");
        }

        int bytes_totales_recibidos = 0;
        unsigned char nseq_esperado = 0;
        size_t bytes_escritos;
        ssize_t bytes_recibidos;

        do {
            bytes_recibidos = recvfrom(socket_cliente, ibuffer, MAX_LONGITUD_MSG, 0, (struct sockaddr *)direccion_servidor, &len);
            uint32_t crc_calculado = crc32((const uint8_t *)imensaje, bytes_recibidos - sizeof(uint32_t));

            if (crc_calculado != imensaje->fcs && bytes_recibidos <= 0) {
                printf("Error de CRC: esperado %08x, recibido %08x. Reenviando ACK para reintentar...\n", crc_calculado, imensaje->fcs);
                break;
            }

            if (imensaje->nseq == nseq_esperado) {
                bytes_escritos = fwrite(imensaje->carga_util, 1, bytes_recibidos - (sizeof(struct Mensaje) - PAYLOAD_SIZE), archivo);

                if (bytes_escritos != (size_t)(bytes_recibidos - (sizeof(struct Mensaje) - PAYLOAD_SIZE))) {
                    error("Error al escribir en el archivo");
                }

                bytes_totales_recibidos += bytes_recibidos - (sizeof(struct Mensaje) - PAYLOAD_SIZE);
                nseq_esperado = imensaje->nseq + 1;
                num_mens++;
                
                printf("\n\n%u número de secuencia\n", nseq_esperado);
                printf("Bytes recibidos en esta iteración: %zd\n", bytes_recibidos - (sizeof(struct Mensaje) - PAYLOAD_SIZE));
                printf("FCS: %08x\n", crc_calculado);
                //printf("calculado: %08x\n", imensaje->fcs);
                printf("bytes_escritos: %zu\n", bytes_escritos);
                printf("bytes_recibidos: %zu\n", bytes_recibidos);

                omensaje->tipo = RESPUESTA;
                omensaje->codigo = ACK;
                omensaje->nseq = nseq_esperado;
                omensaje->fcs = crc_calculado;

                if (sendto(socket_cliente, obuffer, sizeof(struct Mensaje), 0, (struct sockaddr *)direccion_servidor, len) == -1) {
                    error("Error al enviar ACK");
                }
            } else {
                omensaje->tipo = RESPUESTA;
                omensaje->codigo = ACK;
                omensaje->nseq = nseq_esperado;

                if (sendto(socket_cliente, obuffer, sizeof(struct Mensaje), 0, (struct sockaddr *)direccion_servidor, len) == -1) {
                    error("Error al enviar ACK");
                }
            }

        } while (bytes_escritos == PAYLOAD_SIZE);

        if (bytes_recibidos != 0) {
            fclose(archivo);
            printf("%d mensajes enviados\n", num_mens);
            num_mens=0;
            printf("\nArchivo guardado: %s (%d bytes recibidos)\n", nombre_archivo, bytes_totales_recibidos);
            
        } else {
            printf("\nNo se recibió ningún bloque. El archivo está vacío o no se pudo recibir.\n");
        }
    } else {
        printf("Respuesta desde el servidor: %d El archivo no está disponible\n", imensaje->codigo);
    }
}

int main(int argc, char *argv[]) {
    if (argc != 3 || strcmp(argv[1], "-f") != 0) {
        fprintf(stderr, "Uso: %s -f <nombre_archivo>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int socket_cliente = crear_socket();
    struct sockaddr_in direccion_servidor;
    inicializar_direccion_servidor(&direccion_servidor);

    if (connect(socket_cliente, (struct sockaddr *)&direccion_servidor, sizeof(direccion_servidor)) == -1) {
        fprintf(stderr, "Error al conectar con el servidor: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    char *obuffer = (char *)malloc(MAX_LONGITUD_MSG);
    char *ibuffer = (char *)malloc(MAX_LONGITUD_MSG);

    enviar_peticion(socket_cliente, &direccion_servidor, argv[2], obuffer);
    recibir_respuesta(socket_cliente, &direccion_servidor, ibuffer);
    manejar_respuesta(socket_cliente, &direccion_servidor, ibuffer, obuffer, argv[2]);

    free(obuffer);
    free(ibuffer);
    close(socket_cliente);

    return 0;
}

