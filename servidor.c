//gcc se.c -o se -Wall
//./se -e 20 -p 10 -t 2000
//./se 
//./se -e 20
//./se -p 10
//./se -t 3000

//iptables -A INPUT -i lo -p udp --dport 4950 -j DROP//Bloquear
//iptables -D INPUT -i lo -p udp --dport 4950 -j DROP//Desbloquear

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/time.h>
#include <errno.h>
#include <stddef.h>
#include <getopt.h>

#define PUERTO 4950
#define PAYLOAD_SIZE 1024
#define MAX_LONGITUD_MSG (sizeof(struct Mensaje) + PAYLOAD_SIZE)

#define PETICION 0
#define RESPUESTA 1
#define OK 200
#define NO_ENCONTRADO 404
#define ACK 300

#pragma pack(1)

float tasa_error = 0.0;
float tasa_perdida = 0.0;
int tiempo_retransmision = 2000;
int num_mens=0;

struct Mensaje {
    unsigned char nseq;
    int tipo;
    int codigo;
    char carga_util[PAYLOAD_SIZE];
    uint32_t fcs;
};

void error(const char *mensaje_error) {
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

bool volado(float probabilidad) {
    return ((float)rand() / RAND_MAX) < probabilidad;
}

int inicializar_socket_servidor() {
    int socket_servidor;
    if ((socket_servidor = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
        error("Error al crear el socket");
    }

    struct timeval timeout;
    timeout.tv_sec = tiempo_retransmision / 1000;
    timeout.tv_usec = 0;
    if (setsockopt(socket_servidor, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
        error("Error al configurar timeout");
    }

    struct sockaddr_in direccion_servidor;
    memset(&direccion_servidor, 0, sizeof(direccion_servidor));
    direccion_servidor.sin_family = AF_INET;
    direccion_servidor.sin_addr.s_addr = htonl(INADDR_ANY);
    direccion_servidor.sin_port = htons(PUERTO);

    if (bind(socket_servidor, (struct sockaddr *)&direccion_servidor, sizeof(direccion_servidor)) == -1) {
        error("Error al enlazar");
    }

    return socket_servidor;
}

void procesar_peticion(int socket_servidor, char *ibuffer, char *obuffer, struct sockaddr_in *direccion_cliente, socklen_t longitud_cliente) {
    struct Mensaje *imensaje = (struct Mensaje *)ibuffer;
    struct Mensaje *omensaje = (struct Mensaje *)obuffer;

    ssize_t bytes_recibidos = recvfrom(socket_servidor, ibuffer, MAX_LONGITUD_MSG, 0, (struct sockaddr *)direccion_cliente, &longitud_cliente);
     ssize_t bytes_recibidos2;

    if (bytes_recibidos == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return;
        } else {
            perror("Error al recibir");
            return;
        }
    }

    printf("Petición recibida del cliente: %s\n", imensaje->carga_util);

    if (access(imensaje->carga_util, F_OK) != -1) {
        omensaje->codigo = OK;
        if (sendto(socket_servidor, obuffer, sizeof(struct Mensaje), 0, (struct sockaddr *)direccion_cliente, longitud_cliente) == -1) {
            error("Error al enviar");
        }

        FILE *archivo = fopen(imensaje->carga_util, "rb");
        if (archivo == NULL) {
            error("Error al abrir el archivo");
        }

        unsigned char nseq_esperado = 1;
        int bytes_leidos;
        char *bloque_guardado = (char *)malloc(PAYLOAD_SIZE);
        int bytes_guardados = 0;
        bool esperando_ack = true;
        struct timeval actual;
        int tiempo_espera_ms = tiempo_retransmision;
        

        do {
            if (esperando_ack) {
                bytes_leidos = fread(omensaje->carga_util, 1, PAYLOAD_SIZE, archivo);
                size_t longitud_sin_fcs = offsetof(struct Mensaje, carga_util);
                omensaje->fcs = crc32((const uint8_t *)omensaje, longitud_sin_fcs + bytes_leidos);
                if (bytes_leidos < 0) {
                    error("Error al leer el archivo");
                }

	        num_mens++;


                printf("%u número de secuencia\n", omensaje->nseq);
                printf("%d bytes enviados\n", bytes_leidos);

                // Simulación de error de transmisión
                if (volado(tasa_error)) {
                    printf("Simulando error de transmisión en el FCS\n");
                    //omensaje->fcs ^= 0xFFFFFFFF; 
                    bytes_recibidos2 = recvfrom(socket_servidor, ibuffer, MAX_LONGITUD_MSG, 0, (struct sockaddr *)direccion_cliente, &longitud_cliente);
		    if (bytes_recibidos2 == -1) {
		        if (errno == EAGAIN || errno == EWOULDBLOCK) {
		            gettimeofday(&actual, NULL);
		            long tiempo_transcurrido = (actual.tv_sec) * 1000 + (actual.tv_usec) / 1000;
		            if (tiempo_transcurrido >= tiempo_espera_ms) {
		                printf("\nReenviando bloque %d\n", omensaje->nseq);
		                if (sendto(socket_servidor, obuffer, bytes_leidos + sizeof(struct Mensaje) - PAYLOAD_SIZE, 0, (struct sockaddr *)direccion_cliente, longitud_cliente) == -1) {
		            error("Error al enviar");
		        }
		            }
		           
		        }
		    }
		    num_mens++; 
                }
		//omensaje->fcs = crc32((const uint8_t *)omensaje, longitud_sin_fcs + bytes_leidos);
		
                // Simulación de pérdida de mensaje
                if (volado(tasa_perdida)) {
                    printf("Simulando pérdida de mensaje\n");
                    
                    bytes_recibidos2 = recvfrom(socket_servidor, ibuffer, MAX_LONGITUD_MSG, 0, (struct sockaddr *)direccion_cliente, &longitud_cliente);
		    if (bytes_recibidos2 == -1) {
		        if (errno == EAGAIN || errno == EWOULDBLOCK) {
		            gettimeofday(&actual, NULL);
		            long tiempo_transcurrido = (actual.tv_sec) * 1000 + (actual.tv_usec) / 1000;
		            if (tiempo_transcurrido >= tiempo_espera_ms) {
		                printf("\nReenviando bloque %d\n", omensaje->nseq);
		                if (sendto(socket_servidor, obuffer, bytes_leidos + sizeof(struct Mensaje) - PAYLOAD_SIZE, 0, (struct sockaddr *)direccion_cliente, longitud_cliente) == -1) {
		            error("Error al enviar");
		        }
		            }
		           
		        }
		    }
		    num_mens++; 
                }

                if (bytes_leidos < 1024) {
                    omensaje->fcs = crc32((const uint8_t *)omensaje, longitud_sin_fcs + bytes_leidos);
                    if (sendto(socket_servidor, obuffer, bytes_leidos + sizeof(struct Mensaje) - PAYLOAD_SIZE, 0, (struct sockaddr *)direccion_cliente, longitud_cliente) == -1) {
                        error("Error al enviar el último bloque");
                    }
                    
                }
		
                memcpy(bloque_guardado, omensaje->carga_util, bytes_leidos);
                bytes_guardados = bytes_leidos;
		
                if (sendto(socket_servidor, obuffer, bytes_leidos + sizeof(struct Mensaje) - PAYLOAD_SIZE, 0, (struct sockaddr *)direccion_cliente, longitud_cliente) == -1) {
                    error("Error al enviar");
                }
                esperando_ack = true;
            }

            bytes_recibidos = recvfrom(socket_servidor, ibuffer, MAX_LONGITUD_MSG, 0, (struct sockaddr *)direccion_cliente, &longitud_cliente);
            if (bytes_recibidos == -1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    gettimeofday(&actual, NULL);
                    long tiempo_transcurrido = (actual.tv_sec) * 1000 + (actual.tv_usec) / 1000;
                    if (tiempo_transcurrido >= tiempo_espera_ms) {
                        printf("\nReenviando bloque %d\n", nseq_esperado);
                        if (sendto(socket_servidor, obuffer, bytes_guardados + sizeof(struct Mensaje) - PAYLOAD_SIZE, 0, (struct sockaddr *)direccion_cliente, longitud_cliente) == -1) {
                            error("Error al reenviar");
                        }
                    }
                    esperando_ack = false;
                }
            } else {
                if (imensaje->tipo == RESPUESTA && imensaje->nseq == nseq_esperado && imensaje->fcs == omensaje->fcs) {
                    if (imensaje->codigo == ACK) {
                        omensaje->nseq++;
                        if (omensaje->nseq == 0) {
                            nseq_esperado = 0;
                        }
                        nseq_esperado++;
                        printf("%u Seqnum del cliente\n", imensaje->nseq);
                        printf("FCS: %08x\n\n", imensaje->fcs);
                        printf("-ACK Confirmado-\n\n");
                        esperando_ack = true;
                    } else {
                        printf("-ACK No Confirmado-\n\n");
                    }
                } else {
                    printf("\n");

                    memcpy(omensaje->carga_util, bloque_guardado, bytes_guardados);
                    if (sendto(socket_servidor, obuffer, bytes_guardados + sizeof(struct Mensaje) - PAYLOAD_SIZE, 0, (struct sockaddr *)direccion_cliente, longitud_cliente) == -1) {
                        error("Error al enviar");
                    }
                    esperando_ack = false;
                }
            }
            
        } while (bytes_leidos == PAYLOAD_SIZE);
        printf("%d mensajes enviados\n", num_mens);
        num_mens=0;
        fclose(archivo);
        printf("\nSe cerró el archivo correctamente..\n");
        free(bloque_guardado);
    } else {
        omensaje->codigo = NO_ENCONTRADO;
        if (sendto(socket_servidor, obuffer, sizeof(struct Mensaje), 0, (struct sockaddr *)direccion_cliente, longitud_cliente) == -1) {
            error("Error al enviar");
        }
    }

    memset(obuffer, '\0', MAX_LONGITUD_MSG);
    omensaje->tipo = RESPUESTA;
}

int main(int argc, char *argv[]) {
    srand(time(NULL));

    int opt;
    while ((opt = getopt(argc, argv, "e:p:t:")) != -1) {
        switch (opt) {
            case 'e':
                tasa_error = atof(optarg) / 100.0;
                break;
            case 'p':
                tasa_perdida = atof(optarg) / 100.0;
                break;
            case 't':
                tiempo_retransmision = atoi(optarg);
                break;
            default:
                fprintf(stderr, "Uso: %s -e porcentaje_error -p porcentaje_perdida -t tiempo_retransmision_ms\n", argv[0]);
                exit(EXIT_FAILURE);
        }
    }
    
    int socket_servidor = inicializar_socket_servidor();

    char *obuffer = (char *)malloc(MAX_LONGITUD_MSG);
    char *ibuffer = (char *)malloc(MAX_LONGITUD_MSG);

    struct sockaddr_in direccion_cliente;
    socklen_t longitud_cliente = sizeof(direccion_cliente);

    printf("Servidor esperando conexiones en el puerto %d...\n", PUERTO);

    while (true) {
        procesar_peticion(socket_servidor, ibuffer, obuffer, &direccion_cliente, longitud_cliente);
    }

    free(obuffer);
    free(ibuffer);
    close(socket_servidor);

    return 0;
}

