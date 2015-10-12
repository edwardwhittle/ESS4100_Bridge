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

#define BACNET_INSTANCE_NO      34
#define BACNET_PORT     0xBAC1
#define BACNET_INTERFACE        "lo"
#define BACNET_DATALINK_TYPE    "bvlc"
#define BACNET_SELECT_TIMEOUT_MS 1      /* ms */
#define RUN_AS_BBMD_CLIENT      1

#if RUN_AS_BBMD_CLIENT
#define BACNET_BBMD_PORT        0xBAC0
#define BACNET_BBMD_ADDRESS     "140.159.160.7"
#define BACNET_BBMD_TTL 90
#endif 

#define NUM_LISTS 4



/*Variables for Read Register*/
uint16_t tab_reg[64];
int rc;
int i;

/* Linked list object */
typedef struct s_number_object number_object;

//Define a structure called s_word_object containing a number (int) and a pointer containing the address of the next box of this linked list (the next box is of the same type)
struct s_number_object{
	int number;
	number_object *next;
};

//Create a pointer to store the memory location of the current variable
static number_object *list_heads[NUM_LISTS];

/*Global Modbus Structure*/
modbus_t *ctx;

//Create list lock
pthread_mutex_t list_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  list_data_ready = PTHREAD_COND_INITIALIZER;
pthread_cond_t  list_data_flush = PTHREAD_COND_INITIALIZER;

/*Create Timer Lock Mutex*/
static pthread_mutex_t timer_lock = PTHREAD_MUTEX_INITIALIZER;


static int Update_Analog_Input_Read_Property(BACNET_READ_PROPERTY_DATA *rpdata){
	int output;
	number_object *current_object;
	static int index;
	//int instance_no = bacnet_Analog_Input_Instance_To_Index(rpdata->object_instance);
	if (rpdata->object_property != bacnet_PROP_PRESENT_VALUE) goto not_pv;

	//printf("AI_Present_Value request for instance %i\n", instance_no);
	//
	/* Update the values to be sent to the BACnet client here.
	 * * The data should be read from the tail of a linked list. You are required
	 * * to implement this list functionality.
	 * * bacnet_Analog_Input_Present_Value_Set()
	 * * First argument: Instance No
	 * * Second argument: data to be sent
	 * * Without reconfiguring libbacnet, a maximum of 4 values may be sent */
	printf("Sending %i to Output 1\n",list_heads[0]->number);
	bacnet_Analog_Input_Present_Value_Set(0, list_heads[0]->number);
	bacnet_Analog_Input_Present_Value_Set(1, list_heads[1]->number); 
	bacnet_Analog_Input_Present_Value_Set(2, list_heads[2]->number);
	bacnet_Analog_Input_Present_Value_Set(3, list_heads[3]->number);
//if (index == number_object) index = 0;

not_pv:
	return bacnet_Analog_Input_Read_Property(rpdata);
}

/*Create BACnet Devices*/
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
static void add_to_list(number_object **list_head, int number) {

//Create a pointer to store the memory location of the last word
	number_object *last_object, *tmp_object;
	int tmp_number;

	tmp_number = number;
	tmp_object=malloc(sizeof(number_object));
	tmp_object->number = tmp_number;
	tmp_object->next = NULL;
	

	pthread_mutex_lock(&list_lock);

	if (*list_head == NULL) {
	/* The list is empty, just place our tmp_object at the head */
		*list_head = tmp_object;
	} 
	
	else {	
	/* Iterate through the linked list to find the last object */
		last_object = *list_head;
		while (last_object->next) {
			last_object = last_object->next;
		}
	
	/* Last object is now found, link in our tmp_object at the tail */
	last_object->next = tmp_object;
	}
	
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
static number_object *list_get_first(number_object **list_head){
	number_object * first_object;

	first_object = *list_head;
	*list_head = (*list_head)->next;

	return first_object;
}

/*Function to Print Linked List*/
/*
static void *print_func(void *arg){
	number_object **list_head = (number_object **) arg;
	number_object *current_object;

	while(1){
		pthread_mutex_lock(&list_lock);

		while(*list_head == NULL){
			pthread_cond_wait(&list_data_ready, &list_lock);
		}

		current_object = list_get_first(list_head);

		pthread_mutex_unlock(&list_lock);

		printf("%i\n",current_object->number);
//		free(current_object->number);
//		free(current_object);

		pthread_cond_signal(&list_data_flush);
	}
	return arg;
}
*/


/* Main Function */
int main(int argc, char **argv) {

uint8_t rx_buf[bacnet_MAX_MPDU];
uint16_t pdu_len;

/*Initialise BACnet Stack*/
BACNET_ADDRESS src;
pthread_t minute_tick_id, second_tick_id;

bacnet_Device_Set_Object_Instance_Number(BACNET_INSTANCE_NO);
bacnet_address_init();

/* Setup device objects */

bacnet_Device_Init(server_objects);
BN_UNC(WHO_IS, who_is);
BN_CON(READ_PROPERTY, read_property);//For Analog Reads

/*Setup Network Stack*/
bacnet_BIP_Debug = true;
bacnet_bip_set_port(htons(BACNET_PORT));
bacnet_datalink_set(BACNET_DATALINK_TYPE);
bacnet_datalink_init(BACNET_INTERFACE);
atexit(bacnet_datalink_cleanup);
memset(&src, 0, sizeof(src));

register_with_bbmd();

bacnet_Send_I_Am(bacnet_Handler_Transmit_Buffer);

/*Maintenance Timers*/
pthread_create(&minute_tick_id, 0, minute_tick, NULL);
pthread_create(&second_tick_id, 0, second_tick, NULL);

/*Variable passed into add_to_list function*/
char reg_input[256];
pthread_t print_thread;

/*Run Modbus Initialisation*/
initmodbus();

/*Create Thread*/
//pthread_create(&print_thread, NULL, print_func, &list_heads[0]);

/*Read Registers*/
	rc = modbus_read_registers(ctx, 34, 4,tab_reg);
	   	if (rc == -1) {
			fprintf(stderr, "Read Register Failed: %s\n", modbus_strerror(errno));
			return -1;
		}
		for (i=0; i < rc; i++) {
		sprintf(reg_input,"reg[%d]=%d (0x%X)", i, tab_reg[i], tab_reg[i]);
		printf("%s\n",reg_input);
		add_to_list(&list_heads[i], tab_reg[i]);
		}


/*Close and Free Connection*/
modbus_close(ctx);
modbus_free(ctx);

while(1){
	pdu_len = bacnet_datalink_receive(&src, rx_buf, bacnet_MAX_MPDU, BACNET_SELECT_TIMEOUT_MS);
	if (pdu_len) {
	
	/* May call any registered handler.
	* Thread safety: May block, however we still need to guarantee
	* atomicity with the timers, so hold the lock anyway */
	pthread_mutex_lock(&timer_lock);
		bacnet_npdu_handler(&src, rx_buf, pdu_len);
		pthread_mutex_unlock(&timer_lock);
	}
	ms_tick();}

return 0;
}


