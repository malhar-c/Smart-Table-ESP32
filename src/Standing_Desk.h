#include "FastAccelStepper.h"


#define dirPinStepper 4
#define enablePinStepper 15
#define stepPinStepper 2

//standing desk crank, stepper steps and gear ratio calculations
#define turns_max_height 29  //this is the max number of turns with which the table can raise to max height
#define turns_to_stand 24  //this is the number of turns where optimal stand position height is reached
#define stepper_gear_ratio 19.2 //1:19 gear ratio but its more like 19.2
#define motor_steps_full 400  //200 steps per revolution if drove at full step
#define stepper_max_steps motor_steps_full*stepper_gear_ratio*turns_max_height

// --- SAFETY SECTION ---

//Vibration sensor (to detect Stepper vibration, missing steps, overload, etc)
//NOTE: have to implement inturrpts for this one
#define vibration_sensor 26
//homing sensor (reed switch) this will stop the motor instantly and set the lower limit
#define homing_Sensor  23

//interrupt debounce (though this shoul dnot be necessary)
#define DBOUNCE 100

//vibration sensor sensitivity ---> higher vlaue = lower sens
#define obstacle_detection_sensitivity 10

//Ultrasonic distance sensor config
#define trigPin 32
#define echoPin 35 //input only

//manual push buttons config
#define up_Button     27
#define down_Button   25
#define sit_Button    33
#define stand_Button  16

#define manual_push_btn_count 4

#define index_up_Button     0
#define index_down_Button   1
#define index_sit_Button    2
#define index_stand_Button  3


FastAccelStepperEngine engine = FastAccelStepperEngine();
FastAccelStepper *stepper = NULL;

void setup_stepper_init()
{
    //vibration sensor
    pinMode(vibration_sensor, INPUT);

    //homing sensor (reed switch)
    pinMode(homing_Sensor, INPUT_PULLUP);

    //push buttons
    pinMode(up_Button, INPUT_PULLUP);
    pinMode(down_Button, INPUT_PULLUP);
    pinMode(sit_Button, INPUT_PULLUP);
    pinMode(stand_Button, INPUT_PULLUP);

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
        // stepper->setAcceleration(1000);
        
        //half step config
        // stepper->setSpeedInUs(600);  // the parameter is us/step !!! 1000 in full step works fine
        // stepper->setSpeedInHz(1600);
        // stepper->setAcceleration(1200);

        //after stepper installation to desk config... wlill go from super slow to reduce problems
        //probably speed cannot be more than 1000 Hz

        //note: while going up Speed cannot be more than 600Hz @12V (and the driver gets uncomfortably hot)
        // when going down speed can be max 1000Hz at 12V
        // stepper->setSpeedInHz(600);
        // stepper->setAcceleration(1200);

        // Switch to 16V - max speed while going up = 1575 Hz (and driver not heating up as much)

        //1/2 stepping
        // stepper->setSpeedInHz(1575);
        // stepper->setAcceleration(1000);

        //1/4 stepping
        // stepper->setSpeedInHz(1575*2);
        // stepper->setAcceleration(1000*2);

        //16V --- a4988 - for sound comparison
        //1/2 step
        // stepper->setSpeedInHz(1575);
        // stepper->setAcceleration(1000);

        // // Switch to 32V - max speed while going up = 2000Hz (but driver heating up)
        //1/2 stepping
        stepper->setSpeedInHz(1600);
        stepper->setAcceleration(1200);

        //1/4 stepping
        // stepper->setSpeedInHz(4000);
        // stepper->setAcceleration(2400);

        //TMC2208 driver config (after switching to TMC2208 the speed reduced even at full step)
        // stepper->setSpeedInUs(1000);  // the parameter is us/step !!! 1000 in full step works fine
        // stepper->setAcceleration(1000);

        //move 1 revolution for testing wheather stepper is moving or not
        // stepper->move(motor_steps_full);  
        // stepper->move(stepper_gear_ratio*motor_steps_full); //for geared motor
    }
    // Serial.println("running at the startup code");
}

// interrupt definition, functions and setups

bool intr_test = 0;

volatile byte home_state = LOW; //ISR flag, triggers code in main loop
//ISR variable for vibration sensor everytime vibration is detected this will be incremented by 1
volatile short obstacle_detection = 0;

volatile byte obstacle_avoidance_triggered = LOW;

//For push buttons
// volatile byte up_btn = LOW;
// volatile byte down_btn = LOW;
// volatile byte sit_btn = LOW;
// volatile byte stand_btn = LOW;

volatile unsigned long difference;

void IRAM_ATTR DESK_HOME()
{
    intr_test = !intr_test;
    //check if the homing sensor is still active, steper is active and table travelling down
    if(!digitalRead(homing_Sensor) && !digitalRead(stepper->getDirectionPin()) && !digitalRead(enablePinStepper))
    {
        // stepper->forceStop(); //aparently this doesn't work here :)
        digitalWrite(enablePinStepper, HIGH); //manully pulling the enable pin high to immedately stop motor
        home_state = HIGH;
    }
}

void IRAM_ATTR STEP_SKIPS()
{
    //if stepper is active, increase the variable and stop the motor if exceeds max allowed vibrations
    if(!digitalRead(enablePinStepper)){
        obstacle_detection++;
        if(obstacle_detection > obstacle_detection_sensitivity){
            obstacle_avoidance_triggered = HIGH;
            digitalWrite(enablePinStepper, HIGH); //manually stop the motor
        }
    }  
}

// void IRAM_ATTR ISR_up()
// {
//     up_btn = !up_btn;
// }

// void IRAM_ATTR ISR_down()
// {
//     down_btn = !down_btn;
// }

void intrpt_setup()
{
    attachInterrupt(digitalPinToInterrupt(homing_Sensor), DESK_HOME, FALLING);
    attachInterrupt(vibration_sensor, STEP_SKIPS, RISING);
    // attachInterrupt(digitalPinToInterrupt(up_Button), ISR_up, RISING);
    // attachInterrupt(digitalPinToInterrupt(down_Button), ISR_down, RISING);
    // attachInterrupt(digitalPinToInterrupt(sit_Button), ISR_sit, FALLING);
    // attachInterrupt(digitalPinToInterrupt(stand_Button), ISR_stand, FALLING);
}

//Distance measurement configs 
// void distance_sensor_setup()
// {
//     pinMode(trigPin, OUTPUT);
//     pinMode(echoPin, INPUT);
// }
// // defines variables
// long duration; // variable for the duration of sound wave travel
// int distance; // variable for the distance measurement

bool previous_direction = 0;

long stepper_steps_target;
long stepper_steps_current;
int table_current_pos;
int table_target_pos;
int initial_steps_from_NVS;
// int stepper_max_steps = motor_steps_full*stepper_gear_ratio*turns_max_height;

// for button debounce and filtering
bool previousState[manual_push_btn_count];
bool currentState[manual_push_btn_count];

//function prototypes
bool buttonPressed(int, short);
bool direction_sign(signed long);
void cal_distance();

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
        obstacleDetected->setVal(0);
        table_target_pos= targetPosition->getNewVal();
        stepper_steps_target = map(table_target_pos, 0, 100, 0, stepper_max_steps);

        // stepper->moveTo(stepper_steps_target);
        // delay(10);
        // stepper->forceStop();

        // if target position is greater than current potision +
        // if target pos is < current -
        // if((targetPosition->getNewVal() - currentPosition->getNewVal()) > 0)


        // previous travelling direction is not = current, 
        // travel some steps in current direction to couteract the slacking
        if(direction_sign(targetPosition->getNewVal() - currentPosition->getNewVal()) != previous_direction)
        {
            //doo some epic shit
            //stop the motor (deccelerate)
            stepper->stopMove();
            // delay(2000);
            while(stepper->isRunning()){
                //do nothing
                //just block the time when stepper is deccelarating
            }
            //increase the accel and speed
            stepper->setSpeedInHz(1700);
            stepper->setAcceleration(10000);
            if(direction_sign(targetPosition->getNewVal() - currentPosition->getNewVal()) > 0)
            {
                stepper->move((motor_steps_full * stepper_gear_ratio)/6);
            }
            else{
                stepper->move(-(motor_steps_full * stepper_gear_ratio)/6);
            }
            while(stepper->isRunning()){
                //do nothing
                //just block the time when stepper is moving
            }
            // stepper->forceStop();
            // decrease the accel and speed back to normmal
            stepper->setSpeedInHz(1600);
            stepper->setAcceleration(1200);
        }
        
        stepper->moveTo(stepper_steps_target);

        return (true);
    }

    void loop()
    {
        // stepper.moveTo(stepper_steps_target);
        // stepper.run();
        stepper_steps_current = stepper->getCurrentPosition();
        table_current_pos = map(stepper_steps_current, 0, stepper_max_steps, 0, 100); //will store 0-100 value

        // Serial.println(digitalRead(vibration_sensor));

        if(buttonPressed(up_Button, index_up_Button)) //when up button is pressed
        {
            //increase the table target position by 5%
            if(targetPosition->getNewVal() != 100){
                targetPosition->setVal(((targetPosition->getNewVal()+ 5) / 5) * 5); //increament of 5
                // ((targetPosition->getNewVal()+ 5 + 4) / 5) * 5
            }

            // //decrease the table target position by 1%
            // if(targetPosition->getNewVal() != 0){
            //     // targetPosition->setVal(((targetPosition->getNewVal()+ 5) / 5) * 5); //increament of 5
            //     targetPosition->setVal(targetPosition->getNewVal()-1); //increament of 5
            //     // ((targetPosition->getNewVal()+ 5 + 4) / 5) * 5
            // }

            //increase the table target position by 1%
            // if(targetPosition->getNewVal() != 100){
            //     // targetPosition->setVal(((targetPosition->getNewVal()+ 5) / 5) * 5); //increament of 5
            //     targetPosition->setVal(targetPosition->getNewVal()+1); //increament of 5
            //     // ((targetPosition->getNewVal()+ 5 + 4) / 5) * 5
            // }
            update();
        }

        if(buttonPressed(down_Button, index_down_Button)) //when down button is pressed
        {
            //increase the table target position by 5%
            if(targetPosition->getNewVal() != 0){
                targetPosition->setVal(((targetPosition->getNewVal()- 1) / 5) * 5); //increament of 5
                // ((targetPosition->getNewVal()+ 5 + 4) / 5) * 5
            }
            update();
        }

        //debugggin
        // Serial.print("Target Position: ");
        // Serial.print(targetPosition->getNewVal());
        // Serial.print(" current position: ");
        // Serial.print(currentPosition->getNewVal());
        // // Serial.print(" State: ");
        // // Serial.print(positionState->getNewVal());
        // Serial.print(" Direction pin output: ");
        // Serial.print(digitalRead(stepper->getDirectionPin()));
        // Serial.print(" To Go direction: ");
        // Serial.print(direction_sign(targetPosition->getNewVal() - currentPosition->getNewVal()));
        // // Serial.print(" enablePIN state: ");
        // // Serial.print(digitalRead(enablePinStepper));
        // // Serial.print("  Intrpt Triggered ? : ");
        // // Serial.print(intr_test);
        // // Serial.print(" vibration detected count: ");
        // // Serial.print(obstacle_detection);
        // // Serial.print(" Button States - ");
        // // Serial.print(up_btn);
        // // Serial.print(down_btn);
        // Serial.print(" | Homing Sensor: ");
        // Serial.println(digitalRead(homing_Sensor));

        // cal_distance();

        //stepper is having issues / missing steps, jittering, etc, stop the motor immediately 
        // if(digitalRead(vibration_sensor) == 1){
        //     stepper->forceStop();
        //     obstacleDetected->setVal(1);
        //     targetPosition->setVal(table_current_pos); //update the position visually in Home App
        // }
        
        //when obstable avoidance interrupt triggers
        if(obstacle_avoidance_triggered)
        {
            Serial.println("OBSTACLE (VIBRATION IN TABLE) DETECTED!!!!!!");
            Serial.print("Vibration sensor op: ");
            Serial.println(digitalRead(vibration_sensor));
            stepper->forceStop();
            obstacleDetected->setVal(1);
            targetPosition->setVal(table_current_pos); //update the position visually in Home App
            obstacle_detection = 0;
            obstacle_avoidance_triggered = LOW;
            delay(1000); //10sec delay for debugging 
        }
        
        if(digitalRead(enablePinStepper)){
            obstacle_detection = 0;
        }
        else{
            //get the direction
            //0 - travelling down
            //1 - travelling up
            previous_direction = digitalRead(stepper->getDirectionPin());
        }

        //homing sensor trigger
        if(home_state == 1)
        {
            Serial.println(" HOMING SENSOR TRIGGERED !!!!!saved from a crash you moron!!!!!!");

            //homeing triggred... i.e. table is at minimum height
            stepper->forceStop(); //need this for the reverse action to work
            delay(100);
            while(stepper->isRunning()){
                //do nothing
                //just block the time when stepper is moving
            }
            
            currentPosition->setVal(0);
            targetPosition->setVal(0);
            stepper->setCurrentPosition(0);
            home_state = LOW;
            
            delay(10000); //10sec delay to debug
        }

        // delay(1000); //intentional blocking the loop to see the efect

        //update the Homekit current position every 0.5 sec
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

bool buttonPressed(int button, short index)
{
    bool res_state = 0;
        
    previousState[index] = currentState[index];
    currentState[index] = digitalRead(button);
        
    if(previousState[index] == 0 && currentState[index] == 1)
    {
        //button is pressed
        delay(100);
        res_state = 1;
    }

    return(res_state);
}

//this will return if the
bool direction_sign(signed long value){
    return(value > 0);
}

// void cal_distance()
// {
//     //measure the distance and return the value cms
//     digitalWrite(trigPin, LOW);
//     delayMicroseconds(2);
//     // Sets the trigPin HIGH (ACTIVE) for 10 microseconds
//     digitalWrite(trigPin, HIGH);
//     delayMicroseconds(10);
//     digitalWrite(trigPin, LOW);
//     // Reads the echoPin, returns the sound wave travel time in microseconds
//     duration = pulseIn(echoPin, HIGH);
//     // Calculating the distance
//     distance = duration * 0.034 / 2; // Speed of sound wave divided by 2 (go and back)
//     // Displays the distance on the Serial Monitor
//     Serial.print("Distance: ");
//     Serial.print(distance);
//     Serial.println(" cm");
// }