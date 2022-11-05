#include "FastAccelStepper.h"

// #define step_pin 25
// #define dir_pin 27
// #define sleep_pin 26

#define dirPinStepper 27
#define enablePinStepper 26
#define stepPinStepper 25

//standing desk crank, stepper steps and gear ratio calculations
#define turns_max_height 29  //this is the max number of turns with which the table can raise to max height
#define turns_to_stand 24  //this is the number of turns where optimal stand position height is reached
#define stepper_gear_ratio 19 //1:19 gear ratio
#define motor_steps_full 200  //200 steps per revolution if drove at full step
#define stepper_max_steps motor_steps_full*stepper_gear_ratio*turns_max_height

FastAccelStepperEngine engine = FastAccelStepperEngine();
FastAccelStepper *stepper = NULL;

void setup_stepper_init()
{
    engine.init();
    stepper = engine.stepperConnectToPin(stepPinStepper);
    if (stepper) 
    {
        stepper->setDirectionPin(dirPinStepper);
        stepper->setEnablePin(enablePinStepper);
        stepper->setAutoEnable(true);

        // If auto enable/disable need delays, just add (one or both):
        // stepper->setDelayToEnable(50);
        // stepper->setDelayToDisable(1000);

        //full step config
        // stepper->setSpeedInUs(1000);  // the parameter is us/step !!! 1000 in full step works fine
        // stepper->setAcceleration(600);
        
        //half step config
        stepper->setSpeedInUs(500);  // the parameter is us/step !!! 1000 in full step works fine
        stepper->setAcceleration(1000);


        // stepper->move(3800); //for test wheather stepper is moving or no
    }
    // Serial.println("running at the startup code");
}

long stepper_steps_target;
long stepper_steps_current;
int table_current_pos;
int table_target_pos;
int initial_steps_from_NVS;
// int stepper_max_steps = motor_steps_full*stepper_gear_ratio*turns_max_height;

struct Standing_Desk : Service::WindowCovering{

    SpanCharacteristic *targetPosition;
    SpanCharacteristic *currentPosition;
    SpanCharacteristic *positionState;
    SpanCharacteristic *obstacleDetected;
    
    // SpanCharacteristic *holdPosition;

    Standing_Desk() : Service::WindowCovering()
    {
        targetPosition = new Characteristic::TargetPosition(0, true);    //0 - 100 with step val 1
        currentPosition = new Characteristic::CurrentPosition(0, true);  //0-100 with step value 1
        positionState = new Characteristic::PositionState(0);
        /*position state valid values
        * 0 --- going to min value specified in metadata
        * 1 --- going to max value specified in metadata
        * 2 --- Stopped
        * 3-255 ----- Reserved
        */

        obstacleDetected = new Characteristic::ObstructionDetected(0); // 0 or 1-obstructed (bool)

        initial_steps_from_NVS = map(currentPosition->getNewVal(), 0, 100, 0, stepper_max_steps);

        stepper->setCurrentPosition(initial_steps_from_NVS); //convert from the state stored in nvs

        update();
    }

    boolean update()
    {
        table_target_pos= targetPosition->getNewVal();
        stepper_steps_target = map(table_target_pos, 0, 100, 0, stepper_max_steps);

        stepper->moveTo(stepper_steps_target);

        return (true);
    }

    void loop()
    {
        // stepper.moveTo(stepper_steps_target);
        // stepper.run();
        stepper_steps_current = stepper->getCurrentPosition();
        table_current_pos = map(stepper_steps_current, 0, stepper_max_steps, 0, 100);
        
        if(currentPosition->getNewVal() == targetPosition->getNewVal()){
            //turn off the stepper engine save some power
            // stepper->stopMove(); //doesn't work
        }

        // delay(1000); //intentional blocking the loop to see the efect

        if(currentPosition->timeVal()>500)
        {
            currentPosition->setVal(table_current_pos);
            // Serial.print("Target Position : ");
            // Serial.print(targetPosition->getNewVal());
            // Serial.print(" current position: ");
            // Serial.print(currentPosition->getNewVal());
            // Serial.print(" State: ");
            // Serial.println(positionState->getNewVal());
        }
    }
};
