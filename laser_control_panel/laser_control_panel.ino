// ********** import the used libraries
#include <DallasTemperature.h>    //from  https://github.com/milesburton/Arduino-Temperature-Control-Library
#include <OneWire.h>
#include <LiquidCrystal_I2C.h>

// ********** configuration
#define ENABLE_CALLBACKS true     //When a safety input get false, pause the machine etc.

#define TEMP_RESOLUTION 10
#define TEMP_READ_DELAY (750/ (1 << (12-TEMP_RESOLUTION)))

#define TEMP_VALUE_ORANGE 20      // Temperature of outgoing water to trigger the orange leds
#define TEMP_VALUE_RED 23         // Temperature of outgoing water to trigger the red leds and the safety callbacks

#define WARNING_BLINK_COUNT 5

#define GRBL_PULSE_DURATION 50

// ********** Define pin numbers
// Relay controls
#define BUTTON_LASER    35
#define BUTTON_MOTOR    37
#define BUTTON_AIR      39
#define BUTTON_EXHAUST  41
#define BUTTON_WATER    43
#define BUTTON_POSITION 45
#define BUTTON_LIGHTS   47

#define LED_LASER     24
#define LED_MOTOR     26
#define LED_AIR       28
#define LED_EXHAUST   30
#define LED_WATER     32
#define LED_POSITION  34
#define LED_LIGHTS    36

#define RELAY_LASER     22      // 5V signal, so not really a relay but it does use the same controller class
#define RELAY_MOTOR     8       // 5V signal, so not really a relay but it does use the same controller class
#define RELAY_AIR       A8
#define RELAY_EXHAUST   A9
#define RELAY_WATER     A10
#define RELAY_POSITION  7      // 5V signal, so not really a relay but it does use the same controller class
#define RELAY_LIGHTS    A15

// grbl controls
#define BUTTON_GRBL_CONTINUE  49
#define BUTTON_GRBL_HOLD      51
#define BUTTON_GRBL_RESET     53

#define LED_GRBL_HALTED    46

#define GRBL_CONTROL_CONTINUE  9
#define GRBL_CONTROL_HOLD      11
#define GRBL_CONTROL_RESET     10

#define GRBL_INPUT_STEPPER_ENABLE  12

// safety in-/outputs
#define LED_INPUT_WATER_FLOW  38
#define LED_INPUT_DOOR_TOP    40
#define LED_INPUT_DOOR_FRONT  42
#define LED_INPUT_E_STOP      44

#define LED_TEMP_RED       48
#define LED_TEMP_ORANGE    50
#define LED_TEMP_GREEN     52

#define ONE_WIRE_BUS 2    // Temperature sensors. Same data bus

#define INPUT_WATER_FLOW   4
#define INPUT_DOOR_TOP     5
#define INPUT_DOOR_FRONT   6
#define INPUT_E_STOP       3

#define RELAY_MAINS_POWER  A12  // output equal to e_stop input. ensures controlpanel is running before power-on and cuts both live and neutral

// lcd screen with i2c backpack is connected at scl and sda
#define LCD_BACKLIGHT 13

#define UNUSED_RELAY_1 A11
#define UNUSED_RELAY_2 A13
#define UNUSED_RELAY_3 A14


// ********** Global variables
unsigned long last_temp_time;
bool temp_callback_triggered = false;


// ********** prepare the classes
class Button {
  private:
    const int debounceDelay = 90;

    byte pin;
    bool state;
    bool lastReading = LOW;
    unsigned long lastDebounceTime = 0;
    bool fallingDetected = false;

  public:
    Button(byte pin) {
      this->pin = pin;
      init();
    }
    void init() {
      pinMode(pin, INPUT_PULLUP);
      update();
    }
    void update() {
      // Handle the debounce
      bool newReading = digitalRead(pin);

      if (newReading != lastReading) {
        lastDebounceTime = millis();
      }
      if (millis() - lastDebounceTime > debounceDelay) {
        // Update the 'state' attribute only if button had time to debounce
        if (state == HIGH && newReading == LOW) {
          fallingDetected = true;
        }
        state = newReading;
      }
      lastReading = newReading;
    }
    byte getState() {
      update();
      return state;
    }
    bool isPressed() {
      return (getState() == LOW);
    }

    bool fallingEdge() {
      update();
      bool detected = fallingDetected;
      fallingDetected = false;
      return detected;
    }
};

class Led {
  private:
    byte pin;
    bool state = false;
    int blink_count = 0;
    bool blink_infinite = false;
    unsigned long last_blink_millis = 0;
    bool _invert_output = false;
    int blink_period = 120;

  public:
    Led(byte pin) {
      this->pin = pin;
      init();
    }

    void init() {
      pinMode(pin, OUTPUT);
      off();
    }

    void update() {
      if (blink_infinite || blink_count > 0) {
        Serial.print(blink_count);
        Serial.print(", \t");
        Serial.print(millis());
        Serial.print(", \t");
        Serial.print(last_blink_millis);
        Serial.print(", \t");
        Serial.print(blink_period);
        if ((millis() - last_blink_millis) >= blink_period) {
          Serial.print(", \tToggling now!!!");
          toggle();
          last_blink_millis = millis();
          blink_count -= 1;
        }
        Serial.println(".");
      }
    }

    void on() {
      digitalWrite(pin, !_invert_output);
      state = HIGH;
      //
      //      if (blink_count > 0) {
      //        if ((state && blink_count % 2 == 0) || (!state && blink_count % 2 == 1)) {
      //          // after blinking state is on. Do not change anything
      //        } else {
      //          // after blinking state is off. correct this
      //          blink_count -= 1;
      //        }
      //      }
    }

    void off() {
      digitalWrite(pin, _invert_output);
      state = LOW;
      //
      //      if (blink_count > 0) {
      //        if ((!state && blink_count % 2 == 0) || (state && blink_count % 2 == 1)) {
      //          // after blinking state is on. Do not change anything
      //        } else {
      //          // after blinking state is off. correct this
      //          blink_count -= 1;
      //        }
      //      }
    }


    void toggle() {
      if (state) {
        off();
      }
      else {
        on();
      }
    }

    void blink(bool do_blink) {
      blink_infinite = do_blink;
    }

    void blink(int count) {
      blink_count = count;
    }

    void invert_output(bool do_invert) {
      _invert_output = do_invert;
    }

    bool getState() {
      return state;
    }

    bool is_blinking() {
      return (blink_infinite || blink_count > 0);
    }
};

class Relay {
  private:
    byte pin;
    bool state;
    bool _invert_output = false;

  public:
    Led led;

    Relay(byte relay_pin, byte led_pin) : led(led_pin) {
      pin = relay_pin;
      init();
    }

    void init() {
      pinMode(pin, OUTPUT);
      off();
    }

    void on() {
      digitalWrite(pin, !_invert_output);
      led.on();
      state = HIGH;
    }

    void off() {
      digitalWrite(pin, _invert_output);
      led.off();
      state = LOW;
    }

    void toggle() {
      if (state) {
        off();
      }
      else {
        on();
      }
    }

    bool getState() {
      return state;
    }


    void invert_output(bool do_invert) {
      _invert_output = do_invert;
    }
};


Button button_laser(BUTTON_LASER);
Button button_motor(BUTTON_MOTOR);
Button button_air(BUTTON_AIR);
Button button_exhaust(BUTTON_EXHAUST);
Button button_water(BUTTON_WATER);
Button button_position(BUTTON_POSITION);
Button button_lights(BUTTON_LIGHTS);

Button button_grbl_continue(BUTTON_GRBL_CONTINUE);
Button button_grbl_hold(BUTTON_GRBL_HOLD);
Button button_grbl_reset(BUTTON_GRBL_RESET);

Relay relay_laser(RELAY_LASER, LED_LASER);
Relay relay_motor(RELAY_MOTOR, LED_MOTOR);
Relay relay_air(RELAY_AIR, LED_AIR);
Relay relay_exhaust(RELAY_EXHAUST, LED_EXHAUST);
Relay relay_water(RELAY_WATER, LED_WATER);
Relay relay_position(RELAY_POSITION, LED_POSITION);
Relay relay_lights(RELAY_LIGHTS, LED_LIGHTS);

Led led_grbl_halted(LED_GRBL_HALTED);
Led led_input_water_flow(LED_INPUT_WATER_FLOW);
Led led_input_door_top(LED_INPUT_DOOR_TOP);
Led led_input_door_front(LED_INPUT_DOOR_FRONT);
Led led_input_e_stop(LED_INPUT_E_STOP);
Led led_temp_red(LED_TEMP_RED);
Led led_temp_orange(LED_TEMP_ORANGE);
Led led_temp_green(LED_TEMP_GREEN);


// set up the temperature sensor
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

// set up the LCD address for a 16 chars and 2 line display
LiquidCrystal_I2C lcd(0x38, 16, 2);

byte degree_c[] = {
  B11000,
  B11000,
  B00111,
  B01000,
  B01000,
  B01000,
  B01000,
  B00111
};




// ********** main setup
void setup()
{
  Serial.begin(115200);

  pinMode(INPUT_WATER_FLOW, INPUT);
  pinMode(INPUT_DOOR_TOP, INPUT);
  pinMode(INPUT_DOOR_FRONT, INPUT);
  pinMode(INPUT_E_STOP, INPUT);
  pinMode(GRBL_INPUT_STEPPER_ENABLE, INPUT);

  pinMode(GRBL_CONTROL_CONTINUE, OUTPUT);
  pinMode(GRBL_CONTROL_HOLD, OUTPUT);
  pinMode(GRBL_CONTROL_RESET, OUTPUT);
  digitalWrite(GRBL_CONTROL_CONTINUE, HIGH);
  digitalWrite(GRBL_CONTROL_HOLD, HIGH);
  digitalWrite(GRBL_CONTROL_RESET, HIGH);

  pinMode(RELAY_MAINS_POWER, OUTPUT);
  digitalWrite(RELAY_MAINS_POWER, HIGH);

  pinMode(LCD_BACKLIGHT, OUTPUT);
  analogWrite(LCD_BACKLIGHT, 150);
  // pinmodes of the other pins are handled within the classes

  // dont use the unused relays floating
  pinMode(UNUSED_RELAY_1, OUTPUT);
  pinMode(UNUSED_RELAY_2, OUTPUT);
  pinMode(UNUSED_RELAY_3, OUTPUT);
  digitalWrite(UNUSED_RELAY_1, HIGH);
  digitalWrite(UNUSED_RELAY_2, HIGH);
  digitalWrite(UNUSED_RELAY_3, HIGH);

  led_input_water_flow.invert_output(true);
  led_input_door_top.invert_output(true);
  led_input_door_front.invert_output(true);
  led_input_e_stop.invert_output(true);

  relay_laser.invert_output(true);
  relay_air.invert_output(true);
  relay_exhaust.invert_output(true);
  relay_water.invert_output(true);
  relay_lights.invert_output(true);

  relay_laser.off();
  relay_air.off();
  relay_exhaust.off();
  relay_water.on();
  relay_lights.on();
  relay_position.on();

  sensors.begin();
  sensors.setResolution(TEMP_RESOLUTION);
  sensors.setWaitForConversion(false);
  sensors.requestTemperatures(); // request temperatures
  last_temp_time = millis();

  lcd.init();                      // initialize the lcd
  lcd.backlight();
  lcd.createChar(0, degree_c);

  start_animation();

  // Prepare LCD text
  lcd.setCursor(0, 0);
  lcd.print("Temp in:  99.99");
  lcd.write(0);
  lcd.setCursor(0, 1);
  lcd.print("Temp out: 99.99");
  lcd.write(0);

  Serial.print("Temp update rate: ");
  Serial.println(TEMP_READ_DELAY);
  Serial.println("setup finished, Starting main loop now.");
}




// ********** Main loop
void loop() {
  unsigned long loop_start_time = millis();

  // ********** update leds to handle blinking
  relay_laser.led.update();
  relay_motor.led.update();
  relay_air.led.update();
  relay_exhaust.led.update();
  relay_water.led.update();
  relay_position.led.update();
  relay_lights.led.update();
  led_grbl_halted.update();
  led_input_water_flow.update();
  led_input_door_top.update();
  led_input_door_front.update();
  led_input_e_stop.update();
  led_temp_red.update();
  led_temp_orange.update();
  led_temp_green.update();

  // ********** update temp readings
  if ((millis() - last_temp_time) > TEMP_READ_DELAY) {
    float temp_in = sensors.getTempCByIndex(0);   // read last send time
    float temp_out = sensors.getTempCByIndex(1);   // read last send time
    sensors.requestTemperatures(); // request new temperatures
    last_temp_time = millis();

    lcd.setCursor(10, 0);
    lcd.print(String(temp_in, 2));
    lcd.setCursor(10, 1);
    lcd.print(String(temp_out, 2));

    digitalWrite(LED_TEMP_GREEN, HIGH);

    if (temp_out >= TEMP_VALUE_ORANGE) {
      digitalWrite(LED_TEMP_ORANGE, HIGH);
    } else {
      digitalWrite(LED_TEMP_ORANGE, LOW);
    }
    if (temp_out >= TEMP_VALUE_RED) {
      digitalWrite(LED_TEMP_RED, HIGH);
      if (ENABLE_CALLBACKS and !temp_callback_triggered) {
        safety_callback();
        temp_callback_triggered = true;
      }
    } else {
      digitalWrite(LED_TEMP_RED, LOW);
      temp_callback_triggered = false;
    }
  }

  // ********** Check the inputs
  if (!digitalRead(INPUT_WATER_FLOW)) { // is safe
    led_input_water_flow.on();
  } else if (led_input_water_flow.getState() && !led_input_water_flow.is_blinking()) {
    led_input_water_flow.blink(WARNING_BLINK_COUNT);
    if (ENABLE_CALLBACKS) {
      safety_callback();
    }
  }

  if (!digitalRead(INPUT_DOOR_TOP)) {
    led_input_door_top.on();
  } else if (led_input_door_top.getState() && !led_input_door_top.is_blinking()) {
    led_input_door_top.blink(WARNING_BLINK_COUNT);
    if (ENABLE_CALLBACKS) {
      safety_callback();
    }
  }
  if (!digitalRead(INPUT_DOOR_FRONT)) {
    led_input_door_front.on();
  } else if (led_input_door_front.getState() && !led_input_door_front.is_blinking()) {
    led_input_door_front.blink(WARNING_BLINK_COUNT);
    if (ENABLE_CALLBACKS) {
      safety_callback();
    }
  }

  if (digitalRead(INPUT_E_STOP)) {
    digitalWrite(RELAY_MAINS_POWER, LOW);
    led_input_e_stop.on();
  } else {
    digitalWrite(RELAY_MAINS_POWER, HIGH);
    if (led_input_e_stop.getState() && !led_input_e_stop.is_blinking()) {
      led_input_e_stop.blink(WARNING_BLINK_COUNT);
      if (ENABLE_CALLBACKS) {
        safety_callback();
      }
    }
  }

  // GRBL wants to enable the motor, but the button isn't pressed yet
  if (!digitalRead(GRBL_INPUT_STEPPER_ENABLE) && !relay_motor.getState()) {
    relay_motor.led.blink(true);
  } else {
    relay_motor.led.blink(false);
    if (relay_motor.getState()) {
      relay_motor.led.on();
    } else {
      relay_motor.led.off();
    }
  }

  // ********** Handle relay control buttons
  if (button_laser.fallingEdge()) {
    if (digitalRead(!INPUT_WATER_FLOW)) { // is safe
      led_input_water_flow.on();
      if (relay_laser.getState()) {
        relay_laser.off();
      } else {
        bool all_ready = true;
        if (!relay_air.getState()) {
          all_ready = false;
          relay_air.led.blink(4);
        }
        if (!relay_water.getState()) {
          all_ready = false;
          relay_water.led.blink(4);
        }

        if (all_ready) {
          relay_laser.on();
        }
      }
    } else {
      led_input_water_flow.blink(6);
      relay_water.led.blink(6);
    }
  }

  if (button_motor.fallingEdge()) {
    relay_motor.toggle();
  }
  if (button_air.fallingEdge()) {
    relay_air.toggle();
  }
  if (button_exhaust.fallingEdge()) {
    relay_exhaust.toggle();
  }
  if (button_water.fallingEdge()) {
    relay_water.toggle();
  }
  if (button_position.fallingEdge()) {
    relay_position.toggle();
  }
  if (button_lights.fallingEdge()) {
    relay_lights.toggle();
  }

  if (button_grbl_continue.fallingEdge()) {
    digitalWrite(GRBL_CONTROL_CONTINUE, LOW);
    led_grbl_halted.on();
    delay(GRBL_PULSE_DURATION);
    digitalWrite(GRBL_CONTROL_CONTINUE, HIGH);
    led_grbl_halted.off();
  }

  if (button_grbl_hold.fallingEdge()) {
    halt_grbl();
  }

  if (button_grbl_reset.fallingEdge()) {
    digitalWrite(GRBL_CONTROL_RESET, LOW);
    delay(GRBL_PULSE_DURATION);
    digitalWrite(GRBL_CONTROL_RESET, HIGH);
  }

  Serial.print("Finished loop in ");
  Serial.print(millis() - loop_start_time);
  Serial.println(" ms.");
}

void safety_callback() {
  halt_grbl();
}

void halt_grbl() {
  digitalWrite(GRBL_CONTROL_HOLD, LOW);
  led_grbl_halted.on();
  delay(GRBL_PULSE_DURATION);
  digitalWrite(GRBL_CONTROL_HOLD, HIGH);
}

void start_animation() {
  int wait_time = 60;

  lcd.setCursor(0, 0);
  lcd.print("It's Laser Time!");

  led_input_water_flow.toggle();
  delay(wait_time);
  led_input_door_top.toggle();
  delay(wait_time);
  led_input_door_front.toggle();
  delay(wait_time);
  led_input_e_stop.toggle();
  delay(wait_time);
  led_temp_red.toggle();
  delay(wait_time);
  led_temp_orange.toggle();
  delay(wait_time);
  led_temp_green.toggle();
  delay(wait_time);
  led_input_water_flow.toggle();
  delay(wait_time);
  led_input_door_top.toggle();
  delay(wait_time);
  led_input_door_front.toggle();
  delay(wait_time);
  led_input_e_stop.toggle();
  delay(wait_time);
  led_temp_red.toggle();
  delay(wait_time);
  led_temp_orange.toggle();
  delay(wait_time);
  led_temp_green.toggle();
}
