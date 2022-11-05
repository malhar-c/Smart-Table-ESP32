#define PIR_ip 12

struct PIR_motion_sensor : Service::MotionSensor {      // Addressable single-wire RGB LED Strand (e.g. NeoPixel)

  SpanCharacteristic *motion;
  
  PIR_motion_sensor() : Service::MotionSensor()
  {
    motion=new Characteristic::MotionDetected(digitalRead(PIR_ip));
    update();                                 // manually call update() to set pixel with restored initial values
  }

  boolean update() 
  {
    
    return(true);  
  }

  void loop()
  {
    // Serial.println(motion->getNewVal());
    motion->setVal(digitalRead(PIR_ip));
  }
};