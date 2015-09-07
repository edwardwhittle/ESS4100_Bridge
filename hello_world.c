#include <stdio.h>
#include <modbus-tcp.h>

int main(void) {
	modbus_t *ctx;
	ctx = modbus_new_tcp("127.0.0.1", 1502);
	printf("Hello world\n");
    	return 0;
}
