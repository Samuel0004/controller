#include <zephyr/types.h>
#include <stddef.h>
#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/sys/byteorder.h>

// For Joystick
#include <inttypes.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#include "led.h"
#include "gpios.h"
#include "functions.h"
#include "my_lbs.h"

#include <pthread.h>

static void start_scan(void);


static struct bt_le_adv_param *adv_param = BT_LE_ADV_PARAM(
	(BT_LE_ADV_OPT_CONNECTABLE |
	 BT_LE_ADV_OPT_USE_IDENTITY), /* Connectable advertising and use identity address */
	800, /* Min Advertising Interval 500ms (800*0.625ms) */
	801, /* Max Advertising Interval 500.625ms (801*0.625ms) */
	NULL); /* Set to NULL for undirected advertising */


#define DEVICE_NAME CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)

#define RUN_STATUS_LED DK_LED1
#define CON_STATUS_LED DK_LED2
#define USER_LED DK_LED3
#define USER_BUTTON DK_BTN1_MSK

#define STACKSIZE 1024
#define SEND_PRIORITY 6
#define JOYSTICK_PRIORITY 7
#define NOTIFY_INTERVAL 500
#define BLINK_MAX 1

/* ----------------------------------------------------- */
// Global variables

static struct bt_conn *default_conn;

uint8_t direction=' ';

// add for joystick
int32_t preX = 0 , perY = 0;
static const int ADC_MAX = 1023;
// static const int ADC_MIN = 0;
static const int AXIS_DEVIATION = ADC_MAX / 2;
int32_t nowX = 0;
int32_t nowY = 0;

int bluetooth_enabled=0;
int blink_count =0;
int do_search = 0;
int joystick_enable = 0;

/* ----------------------------------------------------- */
/* Node for Devicetree overlay*/
#if !DT_NODE_EXISTS(DT_PATH(zephyr_user)) || \
   !DT_NODE_HAS_PROP(DT_PATH(zephyr_user), io_channels)
#error "No suitable devicetree overlay specified"
#endif

#define DT_SPEC_AND_COMMA(node_id, prop, idx) \
   ADC_DT_SPEC_GET_BY_IDX(node_id, idx),

/* Data of ADC io-channels specified in devicetree. */
static const struct adc_dt_spec adc_channels[] = {
   DT_FOREACH_PROP_ELEM(DT_PATH(zephyr_user), io_channels,
              DT_SPEC_AND_COMMA)
};


static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),

};

static const struct bt_data sd[] = {
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_LBS_VAL),
};


/* ------------------------- Methods ---------------------------- */

// Thread

void send_data_thread(void)
{
	while (1) {
		/* Send notification, the function sends notifications only if a client is subscribed */
		my_lbs_send_sensor_notify(direction,default_conn);
		k_sleep(K_MSEC(NOTIFY_INTERVAL));
	}
}


/*-------- function called remotely by button press ---------------*/

void remote_bluetooth_init(){
    int bluetooth_enabled_local = bluetooth_enabled;
    int do_search_local = do_search;

   __asm__ volatile (
      // Load bluetooth_enabled into r0
      "ldr r0, =bluetooth_enabled \n"   
      "ldr r1, [r0] \n"                

      // Check if bluetooth_enabled == 0
      "cmp r1, #0 \n"                   
      "bne not_equal \n"          

      // If bluetooth_enabled == 0, do_search = 1 and bluetooth_enabled = 1
      "ldr r0, =do_search \n"          
      "mov r1, #1 \n"                 
      "str r1, [r0] \n"               

      "ldr r0, =bluetooth_enabled \n"   
      "mov r1, #1 \n"                   
      "str r1, [r0] \n"                 

      // End of conditional block
      "not_equal: \n"
      :
      :
      : "r0", "r1" // Clobbered registers
   );

    // Update local variables if necessary (not shown in inline assembly)
   bluetooth_enabled = bluetooth_enabled_local;
   do_search = do_search_local;
   return;
}

void remote_disconnect_bluetooth(){
   int ret;

   if(bluetooth_enabled==1){
      bluetooth_enabled=0;
      do_search=0;
      
      ret = gpio_pin_set_dt(&led0, 0);
      if (ret < 0) {
            return;
      }
   }

   if(default_conn!=NULL){
      printk("DISCONNECTED\n");
      bt_conn_disconnect(default_conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
   }
}

/*-------------- bluetooth methods ---------------*/

static void device_found(const bt_addr_le_t *addr, int8_t rssi, uint8_t type,
          struct net_buf_simple *ad)
{
   char addr_str[BT_ADDR_LE_STR_LEN];
   int err;

   if (default_conn) {
      return;
   }

   if(bluetooth_enabled==0){
      return;
   }

   /* We're only interested in connectable events */
   if (type != BT_GAP_ADV_TYPE_ADV_IND &&
       type != BT_GAP_ADV_TYPE_ADV_DIRECT_IND) {
      return;
   }

   bt_addr_le_to_str(addr, addr_str, sizeof(addr_str));
   printk("Device found: %s (RSSI %d)\n", addr_str, rssi);

   /* connect only to devices in close proximity */
   if (rssi < -70) {
      return;
   }

   if (bt_le_scan_stop()) {
      printk("Failed to stop\n");
      return;
   }

   const bt_addr_le_t target_addr = {
        .type = BT_ADDR_LE_PUBLIC,
        .a = { {0xCF, 0xB0, 0x07, 0x60, 0xDA, 0x98} }
    };

   char target_addr_str[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(&target_addr, target_addr_str, sizeof(target_addr_str));
    printk("Target address: %s\n", target_addr_str);

   if (bt_addr_le_cmp(addr, &target_addr) == 0){
      
      err = bt_conn_le_create(addr, BT_CONN_LE_CREATE_CONN,
            BT_LE_CONN_PARAM_DEFAULT, &default_conn);
      if (err) {
         printk("Create conn to %s failed (%d)\n", addr_str, err);
         start_scan();
      }
   }
   else start_scan();
}

static void start_scan(void)
{
   int err;

   /* This demo doesn't require active scan */
   err = bt_le_scan_start(BT_LE_SCAN_PASSIVE, device_found);
   if (err) {
      printk("Scanning failed to start (err %d)\n", err);
      return;
   }

   printk("Scanning successfully started\n");
}

static void connected(struct bt_conn *conn, uint8_t err)
{
   char addr[BT_ADDR_LE_STR_LEN];
   int ret;

   bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

   if (err) {
      printk("Failed to connect to %s (%u)\n", addr, err);

      bt_conn_unref(default_conn);
      default_conn = NULL;

      start_scan();
      return;
   }

   if (conn != default_conn) {
      return;
   }

   printk("Connected: %s\n", addr);
   do_search=0;
 
   ret = gpio_pin_set_dt(&led0, 1);

   if (ret < 0) {
         return;
   }

}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
   char addr[BT_ADDR_LE_STR_LEN];
   int ret;
   if (conn != default_conn) {
      return;
   }

   bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

   printk("Disconnected: %s (reason 0x%02x)\n", addr, reason);

   bt_conn_unref(default_conn);
   default_conn = NULL;
   
   // ret = gpio_pin_toggle_dt(&led0);
   ret = gpio_pin_set_dt(&led0, 0);
   if (ret < 0) {
         return;
   }

   bluetooth_enabled=0;
   
   start_scan();
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
   .connected = connected,
   .disconnected = disconnected,
};

/*-------------- joystick methods  ---------------*/
bool checkCondition_X(void)
{
    bool result = false; // Initialize result to false

    __asm__ volatile (
        // Load nowX into r0
        "ldr r0, =nowX \n"           
        "ldr r1, [r0] \n"            

        // Load preX - 50 into r2
        "ldr r2, =preX \n"           
        "ldr r3, [r2] \n"           
        "subs r3, r3, #50 \n"        

        // Load preX + 50 into r4
        "ldr r4, =preX \n"          
        "ldr r5, [r4] \n"            
        "adds r5, r5, #50 \n"        

        // Check if nowX < (preX - 50) || nowX > (preX + 50)
        "cmp r1, r3 \n"              
        "blt checkConditionX_true \n"
        "cmp r1, r5 \n"              
        "bgt checkConditionX_true \n"

        // If neither condition is met, set result to false
        "b checkConditionX_end \n"   

        // Label for true condition
        "checkConditionX_true: \n"
        "mov %[result], #1 \n"      

        // End of inline assembly block
        "checkConditionX_end: \n"
        : [result] "=r" (result)     
        :                           
        : "r0", "r1", "r2", "r3", "r4", "r5" 
    );

    return result;
}
bool checkCondition_Y(void)
{
    bool result = false; // Initialize result to false

    __asm__ volatile (
        // Load nowY into r0
        "ldr r0, =nowY \n"         
        "ldr r1, [r0] \n"          

        // Load perY - 50 into r2
        "ldr r2, =perY \n"          
        "ldr r3, [r2] \n"            
        "subs r3, r3, #50 \n"        

        // Load perY + 50 into r4
        "ldr r4, =perY \n"           
        "ldr r5, [r4] \n"            
        "adds r5, r5, #50 \n"        

        // Check if nowY < (perY - 50) || nowY > (perY + 50)
        "cmp r1, r3 \n"              
        "blt checkConditionY_true \n"
        "cmp r1, r5 \n"              
        "bgt checkConditionY_true \n"

        // If neither condition is met, set result to false
        "b checkConditionY_end \n"   

        // Label for true condition
        "checkConditionY_true: \n"
        "mov %[result], #1 \n"       

        // End of inline assembly block
        "checkConditionY_end: \n"
        : [result] "=r" (result)    
        :                            
        : "r0", "r1", "r2", "r3", "r4", "r5" 
    );

    return result;
}

bool isChange(void)
{
   if(checkCondition_X()){
      __asm__ volatile (
         // Load nowX into r0
         "ldr r0, =nowX \n"          
         "ldr r1, [r0] \n"            

         // Store nowX into preX
         "ldr r0, =preX \n"           
         "str r1, [r0] \n"          
         :
         :
         : "r0", "r1" // Clobbered registers
      );
      return true;
   }

   if(checkCondition_Y()){
      __asm__ volatile (
        // Load nowY into r0
        "ldr r0, =nowY \n"          
        "ldr r1, [r0] \n"          

        // Store nowY into perY
        "ldr r0, =perY \n"          
        "str r1, [r0] \n"           
        :
        :
        : "r0", "r1" // Clobbered registers
      );
      return true;
   }
   return false;
}


void joystick_thread(void)
{
	int err;

   uint32_t count = 0;
   uint16_t buf;
   struct adc_sequence sequence = {
      .buffer = &buf,
      /* buffer size in bytes, not number of samples */
      .buffer_size = sizeof(buf),
   };

   /* Joystick Read */
   while (1) {
      //blink_count++;
      __asm__ volatile (
         "ldr r0, =blink_count \n" 
         "ldr r1, [r0] \n"          
         "adds r1, r1, #1 \n"      
         "str r1, [r0] \n"          
         :                        
         :                         
         : "r0", "r1"              
      );

      if(blink_count >= BLINK_MAX){
         blink_count =0;
         if(do_search==1)
            gpio_pin_toggle_dt(&led0);
      }

      printk("ADC reading[%u]: ", count++);

      (void)adc_sequence_init_dt(&adc_channels[0], &sequence);
      err = adc_read(adc_channels[0].dev, &sequence);
      if (err < 0) {
         printk("Could not read (%d)\n", err);
         continue;
      }

      nowX = (int32_t)buf;

      (void)adc_sequence_init_dt(&adc_channels[1], &sequence);
      err = adc_read(adc_channels[1].dev, &sequence);
      if (err < 0) {
         printk("Could not read (%d)\n", err);
         continue;
      }

      nowY = (int32_t)buf;

      printk("Joy X: %" PRIu32 ", ", nowX);
      printk("Joy Y: %" PRIu32 ", ", nowY);


      if (nowX >= 65500 || nowY >= 65500){
         printk("Out of Range\n");
         k_sleep(K_MSEC(100));
         continue;
      }


      bool checkFlag = isChange();
      if(!checkFlag){
            printk("No Change\n");
         k_sleep(K_MSEC(100));
         continue;
      } else {
         led_off_all();
      }

      if (nowX == ADC_MAX && nowY == ADC_MAX){
         led_on_center();
         direction ='x';
         printk("Center");
      } else if (nowX < AXIS_DEVIATION && nowY == ADC_MAX){
         led_on_left();
         direction ='a';
         printk("Left");
      } else if (nowX > AXIS_DEVIATION && nowY == ADC_MAX){
         led_on_right();
         direction ='d';
         printk("Right");
      } else if (nowY > AXIS_DEVIATION && nowX == ADC_MAX){
         led_on_up();
         direction ='w';
         printk("Up");
      } else if (nowY < AXIS_DEVIATION && nowX == ADC_MAX){
         led_on_down();
         direction ='s';
         printk("Down");
      }

      printk("\n");

      k_sleep(K_MSEC(100));
   }
}

int main(void)
{

   int err;


   /* Configure channels individually prior to sampling. */
   for (size_t i = 0U; i < ARRAY_SIZE(adc_channels); i++) {
      if (!adc_is_ready_dt(&adc_channels[i])) {
         printk("ADC controller device %s not ready\n", adc_channels[i].dev->name);
         return 0;
      }

      err = adc_channel_setup_dt(&adc_channels[i]);
      if (err < 0) {
         printk("Could not setup channel #%d (%d)\n", i, err);
         return 0;
      }
   }
   led_init();
   gpio_init();


   /* Bluetooth Initialization */
   err = bt_enable(NULL);
   if (err) {
      printk("Bluetooth init failed (err %d)\n", err);
      return 0;
   }
   
   printk("Bluetooth initialized\n");

   err = my_lbs_init(NULL);
	if (err) {
		printk("Failed to init LBS (err:%d)\n", err);
		return -1;
	}
	printk("Bluetooth initialized\n");
	err = bt_le_adv_start(adv_param, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
	if (err) {
		printk("Advertising failed to start \n");
		return 1;
	}

   start_scan();

   return 0;
}

K_THREAD_DEFINE(joystick_thread_id, STACKSIZE, joystick_thread, NULL, NULL, NULL, JOYSTICK_PRIORITY, 0, 0);

K_THREAD_DEFINE(send_data_thread_id, STACKSIZE, send_data_thread, NULL, NULL, NULL, SEND_PRIORITY, 0, 0);
