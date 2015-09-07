#include <stdio.h>
#include <modbus-tcp.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <errno.h>
#define SERVER_ADDR "140.159.153.159"
#define SERVER_PORT 502

/*Variables for Read Register*/
uint16_t tab_reg[64];
int rc;
int i;


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
		fprintf(stderr, "Libmodbus Context Created\n");
	}
/*Connect to Server*/
	if (modbus_connect(ctx) == -1) {
	               fprintf(stderr, "Connection failed: %s\n", modbus_strerror(errno));
		       modbus_free(ctx);
			return -1;						               
	}

	else{
		fprintf(stderr, "Connection Successful\n");
	}

/*Read Registers*/
	rc = modbus_read_registers(ctx, 0, 3,tab_reg);
	   	if (rc == -1) {
			fprintf(stderr, "Read Register Failed: %s\n", modbus_strerror(errno));
			return -1;
		}

		for (i=0; i < rc; i++) {
			printf("reg[%d]=%d (0x%X)\n", i, tab_reg[i], tab_reg[i]);
		}
/*Close and Free Connection*/
	modbus_close(ctx);
	modbus_free(ctx);
return 0;

}
