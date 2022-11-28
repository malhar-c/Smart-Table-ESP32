Interrupt Issue:
1. LOW method not working
2. FALLING method restarting ESP32 and executing automatically

Possible solutions:
https://esp32.com/viewtopic.php?t=8364
https://stackoverflow.com/questions/52358664/arduino-interrupt-wont-ignore-falling-edge

SOLUTION:
P.S. - I'm a moron
https://forum.arduino.cc/t/esp32-using-a-basic-interrupt-crashes/954541/3


Problem:
noise in interrupt line
Solution:
space the wires apart from Stepper motor wires....

Problem:
fluctuations in motor voltage supply triggers the interrupt

UNSOLVED!!!

Potential solutions:
1. only activate the interrupt when the motor is active (will still be an issue when the motor is turning)
2. IDK what