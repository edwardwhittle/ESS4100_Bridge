#include <stdio.h>
#include <modbus-tcp.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#define SERVER_ADDR "140.159.153.159"
#define SERVER_PORT 502

unsigned short errno;

static void client(int count){
}


int main(void) {
/*Initialise Modbus Structure*/
modbus_t *ctx;						
	ctx = modbus_new_tcp(SERVER_ADDR, SERVER_PORT);
	if (ctx == NULL){
		fprintf(stderr, "Unable to allocate libmodbus context\n"); //Error message if unable to execute command
		return -1;
	}

	if (modbus_connect(ctx) == -1) {
		fprintf(stderr, "Connection failed: %s\n", modbus_strerror(errno));
		modbus_free(ctx);
		return -1;
	}
	else{
		fprintf(stderr, "Connection Successful\n");
	}

    	return 0;
}
