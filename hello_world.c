#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <pthread.h>
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

/* Linked list object */
typedef struct s_word_object word_object;

//Define a structure called s_word_object containing a string (char) and a pointer containing the address of the next box of this linked list (the next box is of the same type)
struct s_word_object{
	char *word;
	word_object *next;
};

//Create a pointer to store the memory location of the current variable
static word_object *list_head;

/*Global Modbus Structure*/
modbus_t *ctx;

//Create list lock
pthread_mutex_t list_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  list_data_ready = PTHREAD_COND_INITIALIZER;
pthread_cond_t  list_data_flush = PTHREAD_COND_INITIALIZER;

/*Function to Add Register to List*/
static void add_to_list(char *word) {
//Create a pointer to store the memory location of the last word
	word_object *last_object, *tmp_object;

	char *tmp_string = strdup(word);
	tmp_object = malloc(sizeof(word_object));

	pthread_mutex_lock(&list_lock);

	if (list_head == NULL){
		last_object = tmp_object;
		list_head = last_object;
		pthread_mutex_unlock(&list_lock);
	}

	else {
		last_object = list_head;
		while(last_object->next){
			last_object = last_object->next;
		}
		last_object->next = tmp_object;
		last_object = last_object->next;
	}
	last_object->word = tmp_string;
	last_object->next = NULL;

	pthread_mutex_unlock(&list_lock);
	pthread_cond_signal(&list_data_ready);
}

static int initmodbus (void){
/*Initialise Modbus Structure*/					
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
return 0;
}

static word_object *list_get_first(void){
	word_object * first_object;

	first_object = list_head;
	list_head = list_head->next;

	return first_object;
}

/*Print and Free*/
static void *print_func(void *arg){
	word_object *current_object;
	while(1){
		pthread_mutex_lock(&list_lock);

		while(list_head == NULL){
			pthread_cond_wait(&list_data_ready, &list_lock);
		}

		current_object = list_get_first();

		pthread_mutex_unlock(&list_lock);

		printf("%s\n",current_object->word);
		free(current_object->word);
		free(current_object);

		pthread_cond_signal(&list_data_flush);
	}
	return arg;
}

static void list_flush(void){

	pthread_mutex_lock(&list_lock);

	while(list_head != NULL){
		pthread_cond_signal(&list_data_ready);
		pthread_cond_wait(&list_data_flush, &list_lock);
	}

	pthread_mutex_unlock(&list_lock);
}


/* Main Function */
int main(void) {
/*Variable passed into add_to_list function*/
char reg_input[256];
pthread_t print_thread;

/*Run Modbus Initialisation*/
initmodbus();

pthread_create(&print_thread, NULL, print_func, NULL);

/*Read Registers*/
	rc = modbus_read_registers(ctx, 0, 3,tab_reg);
	   	if (rc == -1) {
			fprintf(stderr, "Read Register Failed: %s\n", modbus_strerror(errno));
			return -1;
		}
		for (i=0; i < rc; i++) {
		sprintf(reg_input,"reg[%d]=%d (0x%X)", i, tab_reg[i], tab_reg[i]);
		add_to_list(reg_input);				   
		}
		list_flush();
/*Close and Free Connection*/
	modbus_close(ctx);
	modbus_free(ctx);
return 0;

}
