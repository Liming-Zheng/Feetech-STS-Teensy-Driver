#include <Arduino.h>
#include "STSServoDriver.h"
#include "cmd_structs.h"
#include "cmd_handlers.h"
#include <Servo.h>

//config
//#define DEBUG
#define SERIAL_COMMS Serial3
#define SERIAL_COMMS_BAUDRATE 576000
#define DEFAULT_SERIAL_PORT_SERVOS 2
#define SERIAL_SERVOS_BAUDRATE 1000000
#define INIT_SERVOS_ON_STARTUP true

#define MOTOR1_PWM_PIN 0
#define MOTOR2_PWM_PIN 1

#define MOTOR_INITIAL_PWM 1000

//weird servo stuff
#define SERVO_MAX_COMD 4096

void sendData(uint8_t* data, size_t length);

STSServoDriver servos;

enum cmd_identifier {
  set_serial_port = 0,
  enable_servo = 1,
  set_speed = 2,
  set_position = 3,
  get_speed = 4,
  get_position = 5,
  get_load = 6,
  get_supply_volt = 7,
  get_temp = 8,
  get_isMoving = 9,
  get_all = 10,
  set_mode = 11,
  set_position_async = 12,
  set_speed_async = 13,
  trigger_action = 14,
  set_motor_speed = 15,
  set_zero_position = 16
};

enum reply_identifier {
  reply_get_speed_id = 0,
  reply_get_position_id = 1,
  reply_get_load_id = 2,
  reply_get_supply_volt_id = 3,
  reply_get_temp_id = 4,
  reply_get_isMoving_id = 5,
  reply_get_all_id = 6
};

enum motor_selectors {
  motor1 = 1,
  motor2 = 2,
  all_motors = 0
};

Servo ESC1;     // create servo object to control the ESC1
Servo ESC2;     // create servo object to control the ESC2

#ifdef DEBUG
unsigned long last_time = millis();
#endif

void setup() {
digitalWrite(LED_BUILTIN, HIGH);
#ifdef DEBUG
  delay(1000);
  digitalWrite(LED_BUILTIN, LOW);
  delay(1000);
  digitalWrite(LED_BUILTIN, HIGH);
  Serial.begin(9600);
  Serial.println("Starting Teensy");
#endif
  SERIAL_COMMS.begin(SERIAL_COMMS_BAUDRATE);

  //initialize the motor pins
  ESC1.attach(MOTOR1_PWM_PIN,1000,2000); // (pin, min pulse width, max pulse width in microseconds) 
  ESC2.attach(MOTOR2_PWM_PIN,1000,2000); // (pin, min pulse width, max pulse width in microseconds)
  ESC1.writeMicroseconds(MOTOR_INITIAL_PWM);
  ESC2.writeMicroseconds(MOTOR_INITIAL_PWM);

  if(INIT_SERVOS_ON_STARTUP)
      servos.init(DEFAULT_SERIAL_PORT_SERVOS, SERIAL_SERVOS_BAUDRATE, false);
}


void loop()
{
  if(SERIAL_COMMS.available() > 0)
  {
    //read the first 2 bytes in the serial buffer
    byte first_byte;
    SERIAL_COMMS.readBytes((char*) &first_byte, 1);

    if(first_byte != 0xBF)
      return;

    byte second_byte;
    SERIAL_COMMS.readBytes((char*) &second_byte, 1);

    
    #ifdef DEBUG
    Serial.print("found start bytes: ");
    #endif

    if(second_byte == 0xFF)
    {
      #ifdef DEBUG
      unsigned long current_time = millis();

      unsigned long time_since_last_message = current_time - last_time;
      Serial.print("Received start marker, time since last message: ");
      Serial.println(time_since_last_message);

      last_time = current_time;

      #endif

      #ifdef DEBUG
      Serial.println("received message");
      #endif

      // Read the command id
      uint8_t cmd_id = 0;
      SERIAL_COMMS.readBytes((char*) &cmd_id, 1);

      #ifdef DEBUG
      Serial.print("cmd_id: ");
      Serial.println(cmd_id);
      #endif

      //call the correct handler
      serial_cmd_handlers[cmd_id]();
    }
  }
}

void set_serial_id_cmd_hanlder()
{
  // Read the command struct from the serial buffer to the correct struct
  cmdstruct_set_serial_id cmd_set_serial_id;
  SERIAL_COMMS.readBytes((char*) &cmd_set_serial_id, sizeof(cmd_set_serial_id));

  #ifdef DEBUG
  Serial.print("cmd_set_serial_id.serialPort_id: ");
  Serial.println(cmd_set_serial_id.serialPort_id);
  #endif

  // initialize the servo
  servos.init(cmd_set_serial_id.serialPort_id, SERIAL_SERVOS_BAUDRATE, false);
}

void enable_servo_cmd_hanlder()
{
  // Read the command struct from the serial buffer to the correct struct
  cmdstruct_enable_servo cmd_enable_servo;
  SERIAL_COMMS.readBytes((char*) &cmd_enable_servo, sizeof(cmd_enable_servo));

  #ifdef DEBUG
  Serial.print("cmd_enable_servo.servo_id: ");
  Serial.println(cmd_enable_servo.servo_id);
  Serial.print("cmd_enable_servo.enable: ");
  Serial.println(cmd_enable_servo.enable);
  #endif

  servos.enableTorque(cmd_enable_servo.servo_id, cmd_enable_servo.enable);
}

void set_speed_cmd_hanlder()
{
  // Read the command struct from the serial buffer to the correct struct
  cmdstruct_set_speed cmd_set_speed;

  //read the message into a buffer
  byte serial_buffer[sizeof(cmd_set_speed) - 1];
  SERIAL_COMMS.readBytes((char*) &serial_buffer, sizeof(cmd_set_speed) - 1);

  cmd_set_speed.servo_id = serial_buffer[0];
  cmd_set_speed.speed = (serial_buffer[2] << 8) | serial_buffer[1];

  //#ifdef DEBUG
  //Serial.print("serial_buffer in hex: ");
  //for (int i = 0; i < sizeof(cmd_set_speed) - 1; i++)
  //{
  //  Serial.print(serial_buffer[i], HEX);
  //  Serial.print(" ");
  //}
  //#endif

  #ifdef DEBUG
  Serial.print("cmd_set_speed.servo_id: ");
  Serial.println(cmd_set_speed.servo_id);
  Serial.print("cmd_set_speed.speed: ");
  Serial.println(cmd_set_speed.speed);
  #endif

  //retrieve data from command struct
  int servo_id = cmd_set_speed.servo_id;
  int speed = cmd_set_speed.speed;
  servos.setTargetVelocity(servo_id, speed);
}

void set_position_cmd_hanlder()
{
  // Read the command struct from the serial buffer to the correct struct
  cmdstruct_set_position cmd_set_position;
  byte serial_buffer[sizeof(cmd_set_position) - 1];
  SERIAL_COMMS.readBytes((char*) &serial_buffer, sizeof(cmd_set_position) - 1);

  cmd_set_position.servo_id = serial_buffer[0];
  cmd_set_position.position = (serial_buffer[2] << 8) | serial_buffer[1];

  #ifdef DEBUG
  Serial.print("cmd_set_position.servo_id: ");
  Serial.println(cmd_set_position.servo_id);
  Serial.print("cmd_set_position.position: ");
  Serial.println(cmd_set_position.position);
  #endif

  //retrieve data from command struct
  int servo_id = cmd_set_position.servo_id;
  int position = cmd_set_position.position;
  servos.setTargetPosition(servo_id, position);
}

void get_speed_cmd_hanlder()
{
  // Read the command struct from the serial buffer to the correct struct
  cmdstruct_get_speed cmd_get_speed;
  SERIAL_COMMS.readBytes((char*) &cmd_get_speed, sizeof(cmd_get_speed));

  //retrieve data from command struct
  int servo_id = cmd_get_speed.servo_id;
  int speed = servos.getCurrentSpeed(servo_id);

  //send reply
  replystruct_get_speed reply_get_speed;
  reply_get_speed.identifier = reply_get_speed_id;
  reply_get_speed.servo_id = servo_id;
  reply_get_speed.speed = speed;

  sendData((uint8_t*) &reply_get_speed, sizeof(replystruct_get_speed));
}

void get_position_cmd_hanlder()
{
  // Read the command struct from the serial buffer to the correct struct
  cmdstruct_get_position cmd_get_position;
  SERIAL_COMMS.readBytes((char*) &cmd_get_position, sizeof(cmd_get_position));

  //retrieve data from command struct
  int servo_id = cmd_get_position.servo_id;
  int position = servos.getCurrentPosition(servo_id);

  //send reply
  replystruct_get_position reply_get_position;
  reply_get_position.identifier = reply_get_position_id;
  reply_get_position.servo_id = servo_id;
  reply_get_position.position = (int16_t)((position - SERVO_MAX_COMD/2) * 36000 / SERVO_MAX_COMD);;

  sendData((uint8_t*) &reply_get_position, sizeof(replystruct_get_position));
}

void get_load_cmd_hanlder()
{
  // Read the command struct from the serial buffer to the correct struct
  cmdstruct_get_volt cmd_get_volt;
  SERIAL_COMMS.readBytes((char*) &cmd_get_volt, sizeof(cmd_get_volt));

  //retrieve data from command struct
  int servo_id = cmd_get_volt.servo_id;
  int load = servos.getCurrentLoad(servo_id);

  //send reply
  replystruct_get_load reply_get_load;
  reply_get_load.identifier = reply_get_load_id;
  reply_get_load.servo_id = servo_id;
  reply_get_load.load = load;

  sendData((uint8_t*) &reply_get_load, sizeof(reply_get_load));
}

void get_supply_volt_cmd_hanlder()
{
  // Read the command struct from the serial buffer to the correct struct
  cmdstruct_get_volt cmd_get_volt;
  SERIAL_COMMS.readBytes((char*) &cmd_get_volt, sizeof(cmd_get_volt));

  //retrieve data from command struct
  int servo_id = cmd_get_volt.servo_id;
  int volt = servos.getCurrentSupplyVoltage(servo_id);

  //send reply
  replystruct_get_supply_volt reply_get_volt;
  reply_get_volt.identifier = reply_get_supply_volt_id;
  reply_get_volt.servo_id = servo_id;
  reply_get_volt.volt = volt;

  sendData((uint8_t*) &reply_get_volt, sizeof(replystruct_get_supply_volt));
}

void get_temp_cmd_hanlder()
{
  // Read the command struct from the serial buffer to the correct struct
  cmdstruct_get_temp cmd_get_temp;
  SERIAL_COMMS.readBytes((char*) &cmd_get_temp, sizeof(cmd_get_temp));

  //retrieve data from command struct
  int servo_id = cmd_get_temp.servo_id;
  int temp = servos.getCurrentTemperature(servo_id);

  //send reply
  replystruct_get_temp reply_get_temp;
  reply_get_temp.identifier = reply_get_temp_id;
  reply_get_temp.servo_id = servo_id;
  reply_get_temp.temp = temp;

  sendData((uint8_t*) &reply_get_temp, sizeof(replystruct_get_temp));
}

void get_isMoving_cmd_hanlder()
{
  // Read the command struct from the serial buffer to the correct struct
  cmdstruct_get_isMoving cmd_get_isMoving;
  SERIAL_COMMS.readBytes((char*) &cmd_get_isMoving, sizeof(cmd_get_isMoving));

  //retrieve data from command struct
  int servo_id = cmd_get_isMoving.servo_id;
  bool isMoving = servos.isMoving(servo_id);

  //send reply
  replystruct_get_isMoving reply_get_isMoving;
  reply_get_isMoving.identifier = reply_get_isMoving_id;
  reply_get_isMoving.servo_id = servo_id;
  reply_get_isMoving.isMoving = isMoving;

  sendData((uint8_t*) &reply_get_isMoving, sizeof(replystruct_get_isMoving));
}

void get_all_cmd_hanlder(){
  // Read the command struct from the serial buffer to the correct struct
  cmdstruct_get_all cmd_get_all;
  SERIAL_COMMS.readBytes((char*) &cmd_get_all, sizeof(cmd_get_all));

  #ifdef DEBUG
  Serial.print("cmd_get_all.servo_id: ");
  Serial.println(cmd_get_all.servo_id);
  #endif

  //retrieve data
  STSServoDriver::all_feedback feedback = servos.getAll(cmd_get_all.servo_id);


  #ifdef DEBUG
  Serial.print("position: ");
  Serial.println(feedback.position);
  Serial.print("speed: ");
  Serial.println(feedback.speed);
  Serial.print("load: ");
  Serial.println(feedback.load);
  Serial.print("temp: ");
  Serial.println(feedback.temperature);
  Serial.print("Voltage: ");
  Serial.println(feedback.voltage);
  #endif

  //send reply
  replystruct_get_all reply_get_all;
  reply_get_all.identifier = reply_get_all_id;
  reply_get_all.servo_id = cmd_get_all.servo_id;
  reply_get_all.position = feedback.position;
  reply_get_all.speed = feedback.speed;
  reply_get_all.load = feedback.load;
  reply_get_all.temp = feedback.temperature;
  reply_get_all.supply_volt = feedback.voltage;

  sendData((uint8_t*) &reply_get_all, sizeof(replystruct_get_all));
}

void set_position_async_cmd_hanlder()
{
  // Read the command struct from the serial buffer to the correct struct
  cmdstruct_set_position cmd_set_position_async;
  byte serial_buffer[sizeof(cmd_set_position_async) - 1];
  SERIAL_COMMS.readBytes((char*) &serial_buffer, sizeof(cmd_set_position_async) - 1);

  cmd_set_position_async.servo_id = serial_buffer[0];
  cmd_set_position_async.position = (serial_buffer[2] << 8) | serial_buffer[1];

  #ifdef DEBUG
  Serial.print("cmd_set_position_async.servo_id: ");
  Serial.println(cmd_set_position_async.servo_id);
  Serial.print("cmd_set_position_async.position: ");
  Serial.println(cmd_set_position_async.position);
  #endif

  //retrieve data from command struct
  int servo_id = cmd_set_position_async.servo_id;
  int position = cmd_set_position_async.position;
  servos.setTargetPosition(servo_id, position, true);
}

void set_speed_async_cmd_hanlder()
{
  // Read the command struct from the serial buffer to the correct struct
  cmdstruct_set_speed cmd_set_speed_async;

  //read the message into a buffer
  byte serial_buffer[sizeof(cmd_set_speed_async) - 1];
  SERIAL_COMMS.readBytes((char*) &serial_buffer, sizeof(cmd_set_speed_async) - 1);

  cmd_set_speed_async.servo_id = serial_buffer[0];
  cmd_set_speed_async.speed = (serial_buffer[2] << 8) | serial_buffer[1];

  #ifdef DEBUG
  Serial.print("cmd_set_speed_async.servo_id: ");
  Serial.println(cmd_set_speed_async.servo_id);
  Serial.print("cmd_set_speed_async.speed: ");
  Serial.println(cmd_set_speed_async.speed);
  #endif

  //retrieve data from command struct
  int servo_id = cmd_set_speed_async.servo_id;
  int speed = cmd_set_speed_async.speed;
  servos.setTargetVelocity(servo_id, speed, true);
}

void trigger_action_cmd_hanlder()
{
  servos.trigerAction();
}

void set_mode_cmd_hanlder()
{
  // Read the command struct from the serial buffer to the correct struct
  cmdstruct_set_mode cmd_set_mode;
  SERIAL_COMMS.readBytes((char*) &cmd_set_mode, sizeof(cmd_set_mode));

  #ifdef DEBUG
  Serial.print("cmd_set_mode.servo_id: ");
  Serial.println(cmd_set_mode.servo_id);
  Serial.print("cmd_set_mode.mode: ");
  Serial.println(cmd_set_mode.mode);
  #endif

  servos.setMode(cmd_set_mode.servo_id, cmd_set_mode.mode);
}

void set_motor_speed_cmd_hanlder()
{
  // Read the command struct from the serial buffer to the correct struct
  cmdstruct_set_motor_speed cmd_set_motor_speed;
  byte serial_buffer[sizeof(cmd_set_motor_speed) - 1];
  SERIAL_COMMS.readBytes((char*) &serial_buffer, sizeof(cmd_set_motor_speed) - 1);

  cmd_set_motor_speed.motor_id = serial_buffer[0];
  cmd_set_motor_speed.pwm = (serial_buffer[2] << 8) | serial_buffer[1];

  //retrieve data from command struct
  int motor_id = cmd_set_motor_speed.motor_id;
  int pwm = cmd_set_motor_speed.pwm;
  
  if (motor_id == motor1 || motor_id == all_motors)
  {
    ESC1.writeMicroseconds(pwm);
  }
  if (motor_id == motor2 || motor_id == all_motors)
  {
    ESC2.writeMicroseconds(pwm);
  }
}

void set_zero_position_cmd_hanlder()
{
  // Read the command struct from the serial buffer to the correct struct
  cmdstruct_set_zero_position cmd_set_zero_position;
  SERIAL_COMMS.readBytes((char*) &cmd_set_zero_position, sizeof(cmd_set_zero_position));

  #ifdef DEBUG
  Serial.print("cmd_set_zero_position.servo_id: ");
  Serial.println(cmd_set_zero_position.servo_id);
  #endif

  servos.setZeroPosition(cmd_set_zero_position.servo_id);
}


void sendData(uint8_t* data, size_t length) {
    SERIAL_COMMS.write(0xBF);
    SERIAL_COMMS.write(0xFF);
    SERIAL_COMMS.write(data, length);
}



