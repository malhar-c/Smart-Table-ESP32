#include "Arduino.h"
#include "HomeSpan.h"
#include "Standing_Desk.h"

void setup()
{
    Serial.begin(115200);

    initStepper();
    initOled();

    homeSpan.begin(Category::Windows, "Smart Desk");

    new SpanAccessory();
        new Service::AccessoryInformation();
            new Characteristic::Identify();
            new Characteristic::Name("Standing Desk");
        new Standing_Desk();
}

void loop()
{
    homeSpan.poll();
}
