#include "sdkconfig.h"
#ifndef CONFIG_BLUEPAD32_PLATFORM_ARDUINO
#error "Must only be compiled when using Bluepad32 Arduino platform"
#endif  // !CONFIG_BLUEPAD32_PLATFORM_ARDUINO

#include <Arduino.h>
#include <Bluepad32.h>
#include <DShotRMT.h>
#include <driver/timer.h>
#include <Freenove_WS2812_Lib_for_ESP32.h>
#include <apa102.h>
#include <uni_log.h>

#include "orientator.h"

#define LED_COUNT 1U
#define LED_PIN GPIO_NUM_48
#define LED_STRIP_RMT_INTR_NUM 11U
#define RMT_CHANNEL	RMT_CHANNEL_0

#define LEFT_DSHOT_RMT_CHANNEL RMT_CHANNEL_1
#define LEFT_DSHOT_GPIO GPIO_NUM_16
#define RIGHT_DSHOT_RMT_CHANNEL RMT_CHANNEL_2
#define RIGHT_DSHOT_GPIO GPIO_NUM_17

#define CONTROLLER_RESPONSE_TIMEOUT 3000000U
#define STICK_DEAD_ZONE 0.08
#define SPIN_POWER_THRESHOLD 0.02
#define DRIVE_SENSITIVITY 0.1
#define TURN_SENSITIVITY 0.015
//
// README FIRST, README FIRST, README FIRST
//
// Bluepad32 has a built-in interactive console.
// By default it is enabled (hey, this is a great feature!).
// But it is incompatible with Arduino "Serial" class.
//
// Instead of using "Serial" you can use Bluepad32 "Console" class instead.
// It is somewhat similar to Serial but not exactly the same.
//
// Should you want to still use "Serial", you have to disable the Bluepad32's console
// from "sdkconfig.defaults" with:
//    CONFIG_BLUEPAD32_USB_CONSOLE_ENABLE=n

GamepadPtr myGamepad;
DShotRMT esc_l, esc_r;
bool reversed = false;
bool button_b = false;
bool controller_idle = false;
esp_timer_create_args_t create_timer;
esp_timer_handle_t test_timer;

Freenove_ESP32_WS2812 LED1 = Freenove_ESP32_WS2812(LED_COUNT, LED_PIN, RMT_CHANNEL, TYPE_GRB);
//apa102_driver_t LED;



// This callback gets called any time a new gamepad is connected.
void onConnectedGamepad(GamepadPtr gp) {
    bool foundEmptySlot = false;
    if (myGamepad == nullptr) {
        Console.printf("CALLBACK: Gamepad is connected\n");
        //LED1.set_pixel(0, 10, 0, 0);
        //LED1.show();

        // Additionally, you can get certain gamepad properties like:
        // Model, VID, PID, BTAddr, flags, etc.
        GamepadProperties properties = gp->getProperties();
        Console.printf("Gamepad model: %s, VID=0x%04x, PID=0x%04x\n", gp->getModelName(), properties.vendor_id,
                        properties.product_id);
        myGamepad = gp;
        foundEmptySlot = true;
    }
    if (!foundEmptySlot) {
        Console.println("CALLBACK: Gamepad connected, but could not find empty slot");
    }
}

void onDisconnectedGamepad(GamepadPtr gp) {
    bool foundGamepad = false;

    if (myGamepad == gp) {
        Console.printf("CALLBACK: Gamepad is disconnected\n");
        //LED1.set_pixel(0, 5, 0, 10);
        //LED1.show();
        myGamepad = nullptr;
        foundGamepad = true;
    }

    if (!foundGamepad) {
        Console.println("CALLBACK: Gamepad disconnected, but not found in myGamepads");
    }
}

void setMotorPower(float left_power, float right_power) {
    //Console.printf("Motor Power: left: %02f, right: %02f\n", left_power, right_power);
    if (left_power < 0) {
        esc_l.sendThrottle(-left_power*1000 + 48);
    } else {
        esc_l.sendThrottle(left_power*1000 + 1048);
    }
    if (right_power < 0) {
        esc_r.sendThrottle(-right_power*1000 + 48);
    } else {
        esc_r.sendThrottle(right_power*1000 + 1048);
    }
}

static void controllerDisconnectedCallback(void *args) {

    reversed = !reversed;
    if (reversed) {
        digitalWrite(10, HIGH);
    } else {
        digitalWrite(10, LOW);
    }

}

uint32_t spin_data[128];
int readCounter = 0;
int cooldown = 0;
orientator sensor;
// Arduino setup function. Runs in CPU 1
void setup() {

    //LED1.begin();
    //LED1.setBrightness(3);
    //LED1.set_pixel(0, 0, 5, 10);
    //LED1.show();
    LEDSTRIP.init(1);
    pinMode(10, OUTPUT);
    pinMode(9, INPUT);
    pinMode(18, OUTPUT);

    Console.printf("Firmware: %s\n", BP32.firmwareVersion());
    const uint8_t* addr = BP32.localBdAddress();
    Console.printf("BD Addr: %2X:%2X:%2X:%2X:%2X:%2X\n", addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);

    // Setup the Bluepad32 callbacks
    BP32.setup(&onConnectedGamepad, &onDisconnectedGamepad);

    BP32.forgetBluetoothKeys();

    esc_l.install(LEFT_DSHOT_GPIO, LEFT_DSHOT_RMT_CHANNEL);
    esc_r.install(RIGHT_DSHOT_GPIO, RIGHT_DSHOT_RMT_CHANNEL);
    //esc_l.init(true);
    //esc_r.init(true);
    create_timer.callback = &controllerDisconnectedCallback;
    create_timer.dispatch_method = ESP_TIMER_TASK;
    esp_timer_create(&create_timer, &test_timer);
    esp_timer_start_periodic(test_timer, 3000000);

    sensor.setup(9);
    sensor.setOffset(3);

    //LED1.set_pixel(0, 5, 0, 10);
    //LED1.show();

    for (int i = 0; i < 64; i++) {
        spin_data[i] = (uint64_t)0;
    }
}

// Arduino loop function. Runs in CPU 1
double hue = 0;
void loop() {
    // This call fetches all the gamepad info from the NINA (ESP32) module.
    // Just call this function in your main loop.
    // The gamepads pointer (the ones received in the callbacks) gets updated
    // automatically.
    BP32.update();
    //Console.println(sensor.getPeriod());
    digitalWrite(18, digitalRead(9));
    hue += 0.1;
    if (hue >= 360) hue = 0;
    
    LEDSTRIP.leds[0] = HSV(hue, 1, 1, 255);
    
    LEDSTRIP.update();

    if (myGamepad && myGamepad->isConnected()) {

        controller_idle = esp_timer_get_time() - myGamepad->getUpdateTime() > CONTROLLER_RESPONSE_TIMEOUT;

        if (!controller_idle) {

            float left_power = 0;
            float right_power = 0;
            float y = -((float)myGamepad->axisY())/512;
            float x = ((float)myGamepad->axisRX())/512;
            float spin_power = (float)(myGamepad->throttle() - myGamepad->brake())/1024;
            if (abs(spin_power) < SPIN_POWER_THRESHOLD) { // run in tank drive if not spinning
                if (abs(y) > STICK_DEAD_ZONE) {
                    left_power += y * DRIVE_SENSITIVITY;
                    right_power -= y * DRIVE_SENSITIVITY;
                }
                if (abs(x) > STICK_DEAD_ZONE) {
                    left_power += x * TURN_SENSITIVITY;
                    right_power += x * TURN_SENSITIVITY;
                }
            } else { // melty mode
                left_power = spin_power;
                right_power = spin_power;
                // TODO: add melty mode
            }

            if (abs(left_power) > 1 || abs(right_power) > 1) {
                float max_power = max(abs(left_power), abs(right_power));
                left_power /= max_power;
                right_power /= max_power;
            }
            setMotorPower(left_power, right_power);

            if (myGamepad->b()) {
                if (!button_b) {
                    button_b = true;
                    //sensor.fillArray(spin_data);
                    for (int i = 0; i < 128; i++) {
                        Console.printf("%d, ", spin_data[i]);
                    }
                    Console.print("\n");
                }
            } else {
                button_b = false;
            }

            if (myGamepad->a()) {
                if (cooldown <= 0) {
                    if (readCounter < 128) {
                        sensor.updatePeriod();
                        spin_data[readCounter] = sensor.getPeriod();
                        readCounter++;
                    }
                    cooldown = 32;
                } else {
                    cooldown--;
                }
            }

            if (myGamepad->miscHome()) {
                myGamepad->disconnect();
            }
        } else {
            setMotorPower(0,0);
        }
    }
    // The main loop must have some kind of "yield to lower priority task" event.
    // Otherwise the watchdog will get triggered.
    // If your main loop doesn't have one, just add a simple `vTaskDelay(1)`.
    // Detailed info here:
    // https://stackoverflow.com/questions/66278271/task-watchdog-got-triggered-the-tasks-did-not-reset-the-watchdog-in-time
    vTaskDelay(1);
    //delay(15);
}
