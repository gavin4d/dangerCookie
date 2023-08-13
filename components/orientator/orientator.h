#ifndef ORIENTATOR_H
#define ORIENTATOR_H

#include <Arduino.h>
#include <driver/timer.h>
#include <bitset>

class orientator {

    public:
        orientator();
        ~orientator();

        void setup(uint8_t);
        boolean updatePeriod();
        double getOrientation();
        void setOffset(double);
        double getOffset();
        uint32_t getPeriod();
        void fillArray(uint32_t[]);

    private:
        static std::bitset<500> IRData; // 500 bit array for incomming IR data
        static uint8_t pin;
        double offset;
        uint32_t rotationPeriod = 0; // microseconds
        esp_timer_handle_t update_timer;

        static void checkIRCallback(void *args);

};
#endif