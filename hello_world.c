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

#include <libbacnet/address.h>
#include <libbacnet/device.h>
#include <libbacnet/handlers.h>
#include <libbacnet/datalink.h>
#include <libbacnet/bvlc.h>
#include <libbacnet/client.h>
#include <libbacnet/txbuf.h>
#include <libbacnet/tsm.h>
#include <libbacnet/ai.h>
#include "src/bacnet_namespace.h"

#define BACNET_INSTANCE_NO      12
#define BACNET_PORT     0xBAC1
#define BACNET_INTERFACE        "lo"
#define BACNET_DATALINK_TYPE    "bvlc"
#define BACNET_SELECT_TIMEOUT_MS 1      /* ms */
#define RUN_AS_BBMD_CLIENT      1

#if RUN_AS_BBMD_CLIENT
#define BACNET_BBMD_PORT        0xBAC0
#define BACNET_BBMD_ADDRESS     "127.0.0.1"
#define BACNET_BBMD_TTL 90
#endif 


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

/* If you are trying out the test suite from home, this data matches the data
 * * stored in RANDOM_DATA_POOL for device number 12
 * * BACnet client will print "Successful match" whenever it is able to receive
 * * this set of data. Note that you will not have access to the RANDOM_DATA_POOL
 * * for your final submitted application. */

static uint16_t test_data[] = {
0xA4EC, 0x6E39, 0x8740, 0x1065, 0x9134, 0xFC8C };
#define NUM_TEST_DATA (sizeof(test_data)/sizeof(test_data[0]))

static pthread_mutex_t timer_lock = PTHREAD_MUTEX_INITIALIZER;

static int Update_Analog_Input_Read_Property(
BACNET_READ_PROPERTY_DATA *rpdata) {

static int index;
/* Update the values to be sent to the BACnet client here.
 * * The data should be read from the tail of a linked list. You are required
 * * to implement this list functionality.
 * *
 * * bacnet_Analog_Input_Present_Value_Set()
 * * First argument: Instance No
 * * Second argument: data to be sent
 * *
 * * Without reconfiguring libbacnet, a maximum of 4 values may be sent */
bacnet_Analog_Input_Present_Value_Set(0, test_data[index++]);
/* bacnet_Analog_Input_Present_Value_Set(1, test_data[index++]); */
/* bacnet_Analog_Input_Present_Value_Set(2, test_data[index++]); */
if (index == NUM_TEST_DATA) index = 0;
return bacnet_Analog_Input_Read_Property(rpdata);
}

static bacnet_object_functions_t server_objects[] = {
{bacnet_OBJECT_DEVICE,
	NULL,
	bacnet_Device_Count,
	bacnet_Device_Index_To_Instance,
	bacnet_Device_Valid_Object_Instance_Number,
	bacnet_Device_Object_Name,
	bacnet_Device_Read_Property_Local,
	bacnet_Device_Write_Property_Local,
	bacnet_Device_Property_Lists,
	bacnet_DeviceGetRRInfo,
	NULL, /* Iterator */
	NULL, /* Value_Lists */
	NULL, /* COV */
	NULL, /* COV Clear */
	NULL /* Intrinsic Reporting */
},

{bacnet_OBJECT_ANALOG_INPUT,
	bacnet_Analog_Input_Init,
	bacnet_Analog_Input_Count,
	bacnet_Analog_Input_Index_To_Instance,
	bacnet_Analog_Input_Valid_Instance,
	bacnet_Analog_Input_Object_Name,
	Update_Analog_Input_Read_Property,
	bacnet_Analog_Input_Write_Property,
	bacnet_Analog_Input_Property_Lists,
	NULL /* ReadRangeInfo */ ,
	NULL /* Iterator */ ,
	bacnet_Analog_Input_Encode_Value_List,
	bacnet_Analog_Input_Change_Of_Value,
	bacnet_Analog_Input_Change_Of_Value_Clear,
	bacnet_Analog_Input_Intrinsic_Reporting},
	{MAX_BACNET_OBJECT_TYPE}
};

static void register_with_bbmd(void) {
#if RUN_AS_BBMD_CLIENT
/* Thread safety: Shares data with datalink_send_pdu */
	bacnet_bvlc_register_with_bbmd(
	bacnet_bip_getaddrbyname(BACNET_BBMD_ADDRESS),
	htons(BACNET_BBMD_PORT),
	BACNET_BBMD_TTL);
#endif
}

static void *minute_tick(void *arg) {
	while (1) {
		pthread_mutex_lock(&timer_lock);
		/* Expire addresses once the TTL has expired */
		bacnet_address_cache_timer(60);
		/* Re-register with BBMD once BBMD TTL has expired */
		register_with_bbmd();
		/* Update addresses for notification class recipient list
		 * * Requred for INTRINSIC_REPORTING
		 * * bacnet_Notification_Class_find_recipient(); */
		/* Sleep for 1 minute */
		pthread_mutex_unlock(&timer_lock);
		sleep(60);
	}
return arg;
}

static void *second_tick(void *arg) {
	while (1) {
	pthread_mutex_lock(&timer_lock);
	/* Invalidates stale BBMD foreign device table entries */
	bacnet_bvlc_maintenance_timer(1);
	/* Transaction state machine: Responsible for retransmissions and ack
	 * * checking for confirmed services */
	bacnet_tsm_timer_milliseconds(1000);
	/* Re-enables communications after DCC_Time_Duration_Seconds
	 * * Required for SERVICE_CONFIRMED_DEVICE_COMMUNICATION_CONTROL
	 * bacnet_dcc_timer_seconds(1); */

	/* State machine for load control object
	 * Required for OBJECT_LOAD_CONTROL
	 * bacnet_Load_Control_State_Machine_Handler(); */

	/* Expires any COV subscribers that have finite lifetimes
	* Required for SERVICE_CONFIRMED_SUBSCRIBE_COV
	* bacnet_handler_cov_timer_seconds(1); */

	/* Monitor Trend Log uLogIntervals and fetch properties
	 * Required for OBJECT_TRENDLOG
	* bacnet_trend_log_timer(1); */
				
	/* Run [Object_Type]_Intrinsic_Reporting() for all objects in device
	* Required for INTRINSIC_REPORTING
	* bacnet_Device_local_reporting(); */
						
	/* Sleep for 1 second */
	pthread_mutex_unlock(&timer_lock);
	sleep(1);
	}
return arg;								
}

static void ms_tick(void) {
    /* Updates change of value COV subscribers.
     * Required for SERVICE_CONFIRMED_SUBSCRIBE_COV
     * bacnet_handler_cov_task(); */
    }

#define BN_UNC(service, handler) \
	bacnet_apdu_set_unconfirmed_handler(	\
	SERVICE_UNCONFIRMED_##service,		\
	bacnet_handler_##handler)
					    
#define BN_CON(service, handler)	 	\
	bacnet_apdu_set_confirmed_handler(	\
	SERVICE_CONFIRMED_##service,		\
	bacnet_handler_##handler)		\

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
/*Initialise Modbus Structure*/
static int initmodbus (void){					
	ctx = modbus_new_tcp(SERVER_ADDR, SERVER_PORT);
	if (ctx == NULL){
		fprintf(stderr, "Unable to allocate libmodbus context\n");
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

/*Function to get the first item on the linked list*/
static word_object *list_get_first(void){
	word_object * first_object;

	first_object = list_head;
	list_head = list_head->next;

	return first_object;
}

/*Function to Print Linked List*/
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

/*Function to Flush the List of Remaining Items on the Linked List*/
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

uint8_t rx_buf[bacnet_MAX_MPDU];
uint16_t pdu_len;
BACNET_ADDRESS src;
pthread_t minute_tick_id, second_tick_id;

bacnet_Device_Set_Object_Instance_Number(BACNET_INSTANCE_NO);
bacnet_address_init();

/* Setup device objects */

bacnet_Device_Init(server_objects);
BN_UNC(WHO_IS, who_is);
BN_CON(READ_PROPERTY, read_property);

bacnet_BIP_Debug = true;
bacnet_bip_set_port(htons(BACNET_PORT));
bacnet_datalink_set(BACNET_DATALINK_TYPE);
bacnet_datalink_init(BACNET_INTERFACE);
atexit(bacnet_datalink_cleanup);
memset(&src, 0, sizeof(src));

register_with_bbmd();

bacnet_Send_I_Am(bacnet_Handler_Transmit_Buffer);

pthread_create(&minute_tick_id, 0, minute_tick, NULL);
pthread_create(&second_tick_id, 0, second_tick, NULL);

/*Variable passed into add_to_list function*/
char reg_input[256];
pthread_t print_thread;

/*Run Modbus Initialisation*/
initmodbus();

/*Create Thread*/
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

pdu_len = bacnet_datalink_receive(
	&src, rx_buf, bacnet_MAX_MPDU, BACNET_SELECT_TIMEOUT_MS);

if (pdu_len) {

	/* May call any registered handler.
	* Thread safety: May block, however we still need to guarantee
	* atomicity with the timers, so hold the lock anyway */
	pthread_mutex_lock(&timer_lock);
	bacnet_npdu_handler(&src, rx_buf, pdu_len);
	pthread_mutex_unlock(&timer_lock);
}										ms_tick();

return 0;

}
