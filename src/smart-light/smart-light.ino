#include "ble_config.h"

/*
 * Smart Light
 * 
 * @author Dan Bunnell (bunnell.dan@gmail.com) 
 * 
 * This project controls a smart, interactive light built on the RedBear Duo platform.  It
 * allows a remote Bluetooth Low Energy device to connect and send commands controlling
 * the hue of the light.
 * 
 * The Bluetooth initialization code was borrowed from Liang He's project:
 * https://github.com/jonfroehlich/CSE590Sp2018/tree/master/A03-BLEAdvanced/RedBearDuoBLEAdvanced
 * 
 * The Library is created based on Bjorn's code for RedBear BLE communication: 
 * https://github.com/bjo3rn/idd-examples/tree/master/redbearduo/examples/ble_led
 */

#if defined(ARDUINO) 
SYSTEM_MODE(SEMI_AUTOMATIC); 
#endif

/******************************************************
 *               Global Definitions
 ******************************************************/
 
#define RECEIVE_MAX_LEN    3
#define SEND_MAX_LEN    3
#define BLE_SHORT_NAME_LEN 0x08 // must be in the range of [0x01, 0x09]
#define BLE_SHORT_NAME 'x','J','9','1','s','4','k' // length must be: BLE_SHORT_NAME_LEN - 1

#define CMD_CLIENT_ENABLE_SET_COLOR 0x01
#define CMD_CLIENT_SET_COLOR 0x02

#define CMD_SEND_HUE 0x01

#define DELAY 100
#define MIN_ANALOG_VALUE 0
#define MAX_ANALOG_VALUE 4095
#define MIN_PHOTO_VALUE 300
#define MAX_PHOTO_VALUE 4000
#define MIN_HUE_VALUE 0
#define MAX_HUE_VALUE 359
#define MIN_BRIGHTNESS_VALUE 0
#define MAX_BRIGHTNESS_VALUE 255
#define MAX_COLOR_VALUE 255
#define DEFAULT_SATURATION_VALUE 255
#define MAX_STRING_SIZE 256
#define ANALOG_READ_PIN_MODE INPUT // analogRead() always used INPUT type

#define COMMON_ANODE 1  // our RGB LED is CA-type

#define RED_INDEX 0
#define GREEN_INDEX 1
#define BLUE_INDEX 2



/******************************************************
 *               Input Definitions
 ******************************************************/
const int POT_INPUT_PIN = A0;
const int PHOTOCELL_INPUT_PIN = A1;



/******************************************************
 *               Output Definitions
 ******************************************************/
const int RGB_RED_PIN = D0;
const int RGB_GREEN_PIN = D1;
const int RGB_BLUE_PIN = D2;



/******************************************************
 *               Procedural Definitions
 ******************************************************/
uint16_t hue = MIN_HUE_VALUE;
static uint8_t rgb_colors[3];
static boolean remote_enabled = false;



/******************************************************
 *               Bluetooth Definitions
 ******************************************************/
 
// UUID is used to find the device by other BLE-abled devices
static uint8_t service1_uuid[16]    = { 0x71,0x3d,0x00,0x00,0x50,0x3e,0x4c,0x75,0xba,0x94,0x31,0x48,0xf1,0x8d,0x94,0x1e };
static uint8_t service1_tx_uuid[16] = { 0x71,0x3d,0x00,0x03,0x50,0x3e,0x4c,0x75,0xba,0x94,0x31,0x48,0xf1,0x8d,0x94,0x1e };
static uint8_t service1_rx_uuid[16] = { 0x71,0x3d,0x00,0x02,0x50,0x3e,0x4c,0x75,0xba,0x94,0x31,0x48,0xf1,0x8d,0x94,0x1e };

// Define the receive and send handlers
static uint16_t receive_handle = 0x0000; // recieve
static uint16_t send_handle = 0x0000; // send

static uint8_t receive_data[RECEIVE_MAX_LEN] = { 0x01 };
static uint8_t send_data[SEND_MAX_LEN] = { 0x00 };

// Define the configuration data
static uint8_t adv_data[] = {
  0x02,
  BLE_GAP_AD_TYPE_FLAGS,
  BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE, 
  
  BLE_SHORT_NAME_LEN,
  BLE_GAP_AD_TYPE_SHORT_LOCAL_NAME,
  BLE_SHORT_NAME, 
  
  0x11,
  BLE_GAP_AD_TYPE_128BIT_SERVICE_UUID_COMPLETE,
  0x1e,0x94,0x8d,0xf1,0x48,0x31,0x94,0xba,0x75,0x4c,0x3e,0x50,0x00,0x00,0x3d,0x71 
};

static btstack_timer_source_t send_characteristic;



/******************************************************
 *               Main Procedure
 ******************************************************/
 
/**
 * @brief Arduino initialization procedure.  
 *
 * @retval None
 */
void setup() {
  Serial.begin(115200);
  delay(5000);
  Serial.println("Simple Controls demo.");

  // Initialize ble_stack.
  ble.init();
  configureBLE(); //lots of standard initialization hidden in here - see ble_config.cpp
  // Set BLE advertising data
  ble.setAdvertisementData(sizeof(adv_data), adv_data);
  
  // Register BLE callback functions
  ble.onDataWriteCallback(recieveCallback);

  // Add user defined service and characteristics
  ble.addService(service1_uuid);
  receive_handle = ble.addCharacteristicDynamic(service1_tx_uuid, ATT_PROPERTY_NOTIFY|ATT_PROPERTY_WRITE|ATT_PROPERTY_WRITE_WITHOUT_RESPONSE, receive_data, RECEIVE_MAX_LEN);
  send_handle = ble.addCharacteristicDynamic(service1_rx_uuid, ATT_PROPERTY_NOTIFY, send_data, SEND_MAX_LEN);

  // BLE peripheral starts advertising now.
  ble.startAdvertising();
  Serial.println("BLE start advertising.");

  pinMode(PHOTOCELL_INPUT_PIN, ANALOG_READ_PIN_MODE);

  pinMode(RGB_RED_PIN, OUTPUT);
  pinMode(RGB_GREEN_PIN, OUTPUT);
  pinMode(RGB_BLUE_PIN, OUTPUT);
  
  // Start a task to check status.
  send_characteristic.process = &send_notify;
  ble.setTimer(&send_characteristic, 500);//2000ms
  ble.addTimer(&send_characteristic);
}

/**
 * @brief Arduino work to be done periodically.
 *
 * @retval None
 */
void loop() {
  // Get ambient brightness and set LED to be inversely proportional
  int photo_raw = read_photocell();
  uint8_t brightness = get_brightness(photo_raw, true);

  // Saturation is static
  uint8_t saturation = DEFAULT_SATURATION_VALUE;

  // if remote control disabled, set RGB from potentiometer
  if (!remote_enabled) {
    int pot = read_pot();
    hue = get_hue(pot);
  }
  
  get_rgb(hue, saturation, brightness, rgb_colors);
  
  set_led(rgb_colors);
  
  delay(DELAY);  
}



/******************************************************
 *               Bluetooth Event Handlers
 ******************************************************/
 
 /**
 * @brief Callback for receive event.
 *
 * @param[in]  value_handle  Handle to identify event type
 * @param[in]  *buffer       The buffer pointer of received data.
 * @param[in]  size          The length of received data.   
 *
 * @retval The status code
 */
int recieveCallback(uint16_t value_handle, uint8_t *buffer, uint16_t size) {
  if (receive_handle == value_handle) {
    memcpy(receive_data, buffer, RECEIVE_MAX_LEN);
    
    uint8_t command = receive_data[0];
    uint8_t upper = receive_data[1];
    uint8_t lower = receive_data[2];
    
    switch(command){
      case CMD_CLIENT_ENABLE_SET_COLOR:
        remote_enabled = (upper > 0);
        break;
      case CMD_CLIENT_SET_COLOR:
        if (remote_enabled) 
        {
          hue = get_16bit_value(upper, lower);
        }
        break;
      default:
        Serial.print("WARNING: unknown command: ");
        Serial.println(command, HEX);
    }
  }
  
  return 0;
}

/**
 * @brief Timer task for sending status change to client.
 * 
 * @param[in]  *ts
 * 
 * @retval None
 */
static void  send_notify(btstack_timer_source_t *ts) {
  Serial.print("send_notify current hue: ");
  Serial.println(hue, HEX);
    
  send_data[0] = CMD_SEND_HUE;
  send_data[1] = hue >> 8;
  send_data[2] = hue;
  ble.sendNotify(send_handle, send_data, SEND_MAX_LEN);
  
  // Restart timer.
  ble.setTimer(ts, 200);
  ble.addTimer(ts);
}



/******************************************************
 *               Arduino Control Code
 ******************************************************/

/**
 * @brief Sets the RGB LED.
 *
 * @param[in]  colors[3]  The RGB colors in byte array format.   
 *
 * @retval None
 */
void set_led(uint8_t colors[3]) {
    uint8_t red = colors[RED_INDEX];
    uint8_t green = colors[GREEN_INDEX];
    uint8_t blue = colors[BLUE_INDEX];
    
    #ifdef COMMON_ANODE
        red = MAX_COLOR_VALUE - colors[RED_INDEX];
        green = MAX_COLOR_VALUE - colors[GREEN_INDEX];
        blue = MAX_COLOR_VALUE - colors[BLUE_INDEX];
    #endif
    analogWrite(RGB_RED_PIN, red);
    analogWrite(RGB_GREEN_PIN, green);
    analogWrite(RGB_BLUE_PIN, blue);  
}

/**
 * @brief Reads the configured Photocell pin for an analog value.
 *
 * @retval The photocell value
 */
int read_photocell() {
  return analogRead(PHOTOCELL_INPUT_PIN);
}

/**
 * @brief Reads the configured Potentiometer pin for an analog value.
 *
 * @retval The potentiometer value
 */
int read_pot() {
   return analogRead(POT_INPUT_PIN);
}



/******************************************************
 *               Helper Functions
 ******************************************************/

/**
 * @brief Gets an RGB byte array from HSB values.
 *
 * @param[in]  hue           The hue  
 * @param[in]  sat           The saturation
 * @param[in]  brightness    The brightness
 * @param[out] *colors       The RGB byte array
 *
 * @retval None
 */
void get_rgb(uint16_t hue, uint8_t sat, uint8_t brightness, uint8_t colors[3]) { 
  uint8_t r;
  uint8_t g;
  uint8_t b;
  uint8_t base;

  if (sat == 0) {
    colors[0]=brightness;
    colors[1]=brightness;
    colors[2]=brightness;  
  } else  { 
    base = ((255 - sat) * brightness)>>8;

    switch(hue/60) {
      case 0:
        r = brightness;
        g = (((brightness-base)*hue)/60)+base;
        b = base;
        break;
  
      case 1:
        r = (((brightness-base)*(60-(hue%60)))/60)+base;
        g = brightness;
        b = base;
        break;
  
      case 2:
        r = base;
        g = brightness;
        b = (((brightness-base)*(hue%60))/60)+base;
        break;
  
      case 3:
        r = base;
        g = (((brightness-base)*(60-(hue%60)))/60)+base;
        b = brightness;
        break;
  
      case 4:
        r = (((brightness-base)*(hue%60))/60)+base;
        g = base;
        b = brightness;
        break;
  
      case 5:
        r = brightness;
        g = base;
        b = (((brightness-base)*(60-(hue%60)))/60)+base;
        break;
    }

    colors[0]=r;
    colors[1]=g;
    colors[2]=b; 
  }   
}

/**
 * @brief Gets the brightness corresponding to an analog value.
 *
 * @param[in]  analog_value  An analog value representing brightness
 * @param[in]  inverted      True if brightness should be inversely proportional to analog value, otherwise false
 *
 * @retval The brightness
 */
uint8_t get_brightness(int analog_value, boolean inverted) {
  uint16_t unbounded_brightness = map(analog_value, MIN_ANALOG_VALUE, MAX_ANALOG_VALUE, MIN_BRIGHTNESS_VALUE, MAX_BRIGHTNESS_VALUE);
  return inverted ? MAX_BRIGHTNESS_VALUE - min(MAX_BRIGHTNESS_VALUE, unbounded_brightness) : min(MAX_BRIGHTNESS_VALUE, unbounded_brightness);
}

/**
 * @brief Gets the hue corresponding to an analog value.
 *
 * @param[in]  analog_value  An analog value representing hue
 *
 * @retval The hue
 */
uint16_t get_hue(int analog_value) {
  uint16_t unbounded_hue = map(analog_value, MIN_ANALOG_VALUE, MAX_ANALOG_VALUE, MIN_HUE_VALUE, MAX_HUE_VALUE);
  return min(MAX_HUE_VALUE, unbounded_hue);
}

/**
 * @brief Gets a 16-bit word from an upper and lower byte.
 *
 * @param[in]  upper  The upper byte
 * @param[in]  lower  The lower byte
 *
 * @retval A 16-bit word
 */
uint16_t get_16bit_value(uint8_t upper, uint8_t lower) {
  uint16_t val = (upper << 8) | lower;
  Serial.print("Value: ");
  Serial.println(val, HEX);

  return val  ;
}
