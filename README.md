# socketUDP-cliente-servidor
Simulación de la petición de un archivo a un servidor mediante un socket UDP
Para la ejecucion del servidor en alguna terminal tiene como opciones: 
./se -e 20 -p 10 -t 2000
./se 
./se -e 20
./se -p 10
./se -t 3000
Donde t simula el tiempo de espera en el servidor, e simula un error en el campo fcs y p la perdida de algún mensaje de datos
Para el Cliente basta con compilar y ejecutar:
./cli -f (nombre del archivo)
