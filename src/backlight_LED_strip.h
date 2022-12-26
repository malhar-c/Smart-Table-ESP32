#include <FastLED.h>

#define DATA_PIN    18
#define LED_TYPE    WS2812B
#define COLOR_ORDER GRB
#define NUM_LEDS    73 //74 //5
CRGB leds[NUM_LEDS];

#define BRIGHTNESS         255
#define FRAMES_PER_SECOND  120

//Hue, Saturation and Brighness variables
int h;
int s;
int v;

//Hue, Sat and Brightness smooth transition effect
int h_smooth;
int s_smooth;
int v_smooth;

int FastLED_fade_counter;

void LED_setup() 
{
  delay(1000);
  
  // tell FastLED about the LED strip configuration
  FastLED.addLeds<LED_TYPE,DATA_PIN,COLOR_ORDER>(leds, NUM_LEDS).setCorrection(TypicalLEDStrip);
  //FastLED.addLeds<LED_TYPE,DATA_PIN,CLK_PIN,COLOR_ORDER>(leds, NUM_LEDS).setCorrection(TypicalLEDStrip);

  // set master brightness control
  FastLED.setBrightness(BRIGHTNESS);
}

struct table_backlight : Service::LightBulb {      // Addressable single-wire RGB LED Strand (e.g. NeoPixel)
 
  Characteristic::On power{0,true};
  Characteristic::Hue Hue{0,true};  // range = [0,360]
  Characteristic::Saturation Sat{0,true};  // range = [0,100]
  Characteristic::Brightness Brightness{100,true};  // range = [0,100]
  
  table_backlight(int data_pin) : Service::LightBulb(){

    Brightness.setRange(5,100,1);                      // sets the range of the Brightness to be from a min of 5%, to a max of 100%, in steps of 1%
    update();                                 // manually call update() to set pixel with restored initial values
  }

  boolean update() 
  {

    int p=power.getNewVal();
    
    // float h_raw=Hue.getNewVal<float>();       // range = [0,360]
    // float s_raw=Sat.getNewVal<float>();       // range = [0,100]
    // float v_raw=Brightness.getNewVal<float>();       // range = [0,100]

    //mapping the values in 0-255 range
    h = map(Hue.getNewVal(), 0, 360, 0, 255); //hue value mapping
    s = map(Sat.getNewVal(), 0, 100, 0, 255); //saturadtion value mapping
    v = map(Brightness.getNewVal(), 0, 100, 0, 255); //brightness value mapping

    //if the home app switch is off set he brightness to 0%
    if(!p){
        v = 0;
    }

    // Serial.println(v);

    // put HSV value to all LEDs
    // leds[0].setHSV(h,s,v);
    // FastLED.show();
          
    return(true);  
  }

  void loop()
  {

    for (int i=0; i<NUM_LEDS; i++)
    {
        // leds[i].setHSV(h_smooth, s_smooth, v_smooth);
        leds[i].setHSV(h, s, v);
    }

    FastLED.show(); 

    Serial.print(h);
    Serial.print(" ");
    Serial.print(s);
    Serial.print(" ");
    Serial.println(v);
    
  }
};

void  Scene_01_fade_up ()
{

  for ( int i = 0; i < NUM_LEDS; i++ )
      {
        leds[i] = CRGB::Orange; // Can be any colour
        leds[i].maximizeBrightness(FastLED_fade_counter);  // 'FastLED_fade_counter' How high we want to fade up to 255 = maximum.
      }

      FastLED.show();
      FastLED_fade_counter++ ;   // Increment

}