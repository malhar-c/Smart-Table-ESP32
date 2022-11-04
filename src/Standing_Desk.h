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

    //    holdPosition = new Characteristic::HoldPosition(0); //bool
    }

    boolean update()
    {
        return (true);
    }

    void loop()
    {
        Serial.print("Target Position : ");
        Serial.print(targetPosition->getNewVal());
        Serial.print(" current position: ");
        Serial.print(currentPosition->getNewVal());
        Serial.print(" State: ");
        Serial.println(positionState->getNewVal());
    }
};
