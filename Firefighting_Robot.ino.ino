#include <WiFi.h>
#include <WebServer.h>
#include <ESP32Servo.h>

const char* ssid = "FireRobot";
const char* password = "12345678";

WebServer server(80);

#define FLAME_LEFT    34
#define FLAME_CENTER  35
#define FLAME_RIGHT   32

#define FIRE_DETECTED LOW

#define TRIG_PIN 5
#define ECHO_PIN 18

#define OBSTACLE_DISTANCE 20

#define IN1 27
#define IN2 26
#define IN3 25
#define IN4 33

#define ENA 12
#define ENB 4
#define MOTOR_SPEED 200

#define SERVO_PIN 13

Servo nozzleServo;

#define SERVO_LEFT    45
#define SERVO_CENTER  90
#define SERVO_RIGHT   135

#define RELAY_PIN 14

#define RELAY_ACTIVE_LOW false

bool autoMode = true;

enum RobotState
{
  STATE_IDLE,
  STATE_FIRE_APPROACH,
  STATE_FIRE_ALIGN,
  STATE_FIRE_SPRAY,
  STATE_OBSTACLE_BACK,
  STATE_OBSTACLE_TURN
};

enum FireDirection
{
  FIRE_NONE,
  FIRE_LEFT,
  FIRE_CENTER,
  FIRE_RIGHT
};

RobotState currentState = STATE_IDLE;
FireDirection fireDirection = FIRE_NONE;
unsigned long stateStartTime = 0;

const unsigned long SERVO_SETTLE_TIME   = 300;
const unsigned long SPRAY_TIME          = 3000;
const unsigned long OBSTACLE_BACK_TIME  = 500;
const unsigned long OBSTACLE_TURN_TIME  = 700;

const long FIRE_SPRAY_DISTANCE = 25;

const unsigned long APPROACH_TIMEOUT = 6000;

void setMotorSpeed(int speed)
{
  analogWrite(ENA, speed);
  analogWrite(ENB, speed);
}

void moveForward()
{
  setMotorSpeed(MOTOR_SPEED);

  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);

  digitalWrite(IN3, HIGH);
  digitalWrite(IN4, LOW);
}

void moveBackward()
{
  setMotorSpeed(MOTOR_SPEED);

  digitalWrite(IN1, LOW);
  digitalWrite(IN2, HIGH);

  digitalWrite(IN3, LOW);
  digitalWrite(IN4, HIGH);
}

void turnLeft()
{
  setMotorSpeed(MOTOR_SPEED);

  digitalWrite(IN1, LOW);
  digitalWrite(IN2, HIGH);

  digitalWrite(IN3, HIGH);
  digitalWrite(IN4, LOW);
}

void turnRight()
{
  setMotorSpeed(MOTOR_SPEED);

  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);

  digitalWrite(IN3, LOW);
  digitalWrite(IN4, HIGH);
}

void stopRobot()
{
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);

  digitalWrite(IN3, LOW);
  digitalWrite(IN4, LOW);

  setMotorSpeed(0);
}

void pumpOn()
{
  digitalWrite(RELAY_PIN, RELAY_ACTIVE_LOW ? LOW : HIGH);
}

void pumpOff()
{
  digitalWrite(RELAY_PIN, RELAY_ACTIVE_LOW ? HIGH : LOW);
}

long readDistanceRaw()
{
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);

  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);

  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH, 30000);

  if (duration == 0)
    return -1;

  return duration * 0.034 / 2;
}

long getDistance()
{
  long a = readDistanceRaw();
  long b = readDistanceRaw();
  long c = readDistanceRaw();

  if (a < 0) a = 400;
  if (b < 0) b = 400;
  if (c < 0) c = 400;

  long mx = max(a, max(b, c));
  long mn = min(a, min(b, c));
  long median = (a + b + c) - mx - mn;

  return median;
}

FireDirection readFireDirection()
{
  int left   = digitalRead(FLAME_LEFT);
  int center = digitalRead(FLAME_CENTER);
  int right  = digitalRead(FLAME_RIGHT);

  if (center == FIRE_DETECTED) return FIRE_CENTER;
  if (left == FIRE_DETECTED)   return FIRE_LEFT;
  if (right == FIRE_DETECTED)  return FIRE_RIGHT;

  return FIRE_NONE;
}

int servoAngleForDirection(FireDirection dir)
{
  switch (dir)
  {
    case FIRE_LEFT:  return SERVO_LEFT;
    case FIRE_RIGHT: return SERVO_RIGHT;
    default:         return SERVO_CENTER;
  }
}

void automaticMode()
{
  unsigned long now = millis();

  switch (currentState)
  {
    case STATE_IDLE:
    {
      FireDirection dir = readFireDirection();

      if (dir != FIRE_NONE)
      {
        Serial.println("FIRE DETECTED - approaching");

        fireDirection = dir;
        stateStartTime = now;
        currentState = STATE_FIRE_APPROACH;
        return;
      }

      long distance = getDistance();

      if (distance > 0 && distance < OBSTACLE_DISTANCE)
      {
        Serial.println("OBSTACLE - backing up");

        stopRobot();
        moveBackward();

        stateStartTime = now;
        currentState = STATE_OBSTACLE_BACK;
      }
      else
      {
        moveForward();
      }

      break;
    }

    case STATE_FIRE_APPROACH:
    {
      FireDirection dir = readFireDirection();

      if (dir == FIRE_NONE)
      {
        Serial.println("Fire lost during approach");
        stopRobot();
        currentState = STATE_IDLE;
        return;
      }

      fireDirection = dir;

      long distance = getDistance();
      bool closeEnough = (distance > 0 && distance <= FIRE_SPRAY_DISTANCE);
      bool timedOut = (now - stateStartTime >= APPROACH_TIMEOUT);

      if (closeEnough || timedOut)
      {
        if (timedOut)
          Serial.println("Approach timeout - spraying from current position");
        else
          Serial.println("Close enough - stopping to spray");

        stopRobot();

        int angle = servoAngleForDirection(fireDirection);
        nozzleServo.write(angle);

        stateStartTime = now;
        currentState = STATE_FIRE_ALIGN;
        return;
      }

      switch (fireDirection)
      {
        case FIRE_LEFT:
          turnLeft();
          break;
        case FIRE_RIGHT:
          turnRight();
          break;
        case FIRE_CENTER:
        default:
          moveForward();
          break;
      }

      break;
    }

    case STATE_FIRE_ALIGN:
    {
      if (now - stateStartTime >= SERVO_SETTLE_TIME)
      {
        pumpOn();
        stateStartTime = now;
        currentState = STATE_FIRE_SPRAY;
      }
      break;
    }

    case STATE_FIRE_SPRAY:
    {
      if (now - stateStartTime >= SPRAY_TIME)
      {
        pumpOff();
        fireDirection = FIRE_NONE;
        currentState = STATE_IDLE;
      }
      break;
    }

    case STATE_OBSTACLE_BACK:
    {
      if (now - stateStartTime >= OBSTACLE_BACK_TIME)
      {
        stopRobot();
        turnRight();

        stateStartTime = now;
        currentState = STATE_OBSTACLE_TURN;
      }
      break;
    }

    case STATE_OBSTACLE_TURN:
    {
      if (now - stateStartTime >= OBSTACLE_TURN_TIME)
      {
        stopRobot();
        currentState = STATE_IDLE;
      }
      break;
    }
  }
}

String webpage()
{
  String page = R"rawliteral(

<!DOCTYPE html>
<html>
<head>
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Fire Fighting Robot</title>
<style>
body { font-family: Arial; text-align: center; background: #eeeeee; }
h1 { color: #222; }
button { width: 120px; height: 60px; margin: 8px; font-size: 18px; border-radius: 12px; border: none; }
.forward { background: #4CAF50; color: white; }
.stop { background: #f44336; color: white; }
.mode { background: #2196F3; color: white; }
.water { background: #00BCD4; color: white; }
.servo { background: #9C27B0; color: white; }
</style>
</head>
<body>
<h1>Fire Fighting Robot</h1>
<h2>Control Mode</h2>
<button class="mode" onclick="send('auto')">AUTO</button>
<button class="mode" onclick="send('manual')">MANUAL</button>
<h2>Movement</h2>
<div><button class="forward" onclick="send('forward')">Forward</button></div>
<div>
<button onclick="send('left')">Left</button>
<button class="stop" onclick="send('stop')">Stop</button>
<button onclick="send('right')">Right</button>
</div>
<div><button onclick="send('backward')">Back</button></div>
<h2>Water Pump</h2>
<button class="water" onclick="send('pumpOn')">Pump ON</button>
<button class="stop" onclick="send('pumpOff')">Pump OFF</button>
<h2>Nozzle</h2>
<button class="servo" onclick="send('servoLeft')">Left</button>
<button class="servo" onclick="send('servoCenter')">Center</button>
<button class="servo" onclick="send('servoRight')">Right</button>
<script>
function send(command) { fetch('/' + command); }
</script>
</body>
</html>

)rawliteral";

  return page;
}

void handleRoot()
{
  server.send(200, "text/html", webpage());
}

void handleAuto()
{
  autoMode = true;
  currentState = STATE_IDLE;
  fireDirection = FIRE_NONE;
  stopRobot();
  pumpOff();
  server.send(200, "text/plain", "AUTO MODE");
}

void handleManual()
{
  autoMode = false;
  currentState = STATE_IDLE;
  fireDirection = FIRE_NONE;
  stopRobot();
  pumpOff();
  server.send(200, "text/plain", "MANUAL MODE");
}

void handleForward()
{
  if (!autoMode) moveForward();
  server.send(200, "text/plain", "FORWARD");
}

void handleBackward()
{
  if (!autoMode) moveBackward();
  server.send(200, "text/plain", "BACKWARD");
}

void handleLeft()
{
  if (!autoMode) turnLeft();
  server.send(200, "text/plain", "LEFT");
}

void handleRight()
{
  if (!autoMode) turnRight();
  server.send(200, "text/plain", "RIGHT");
}

void handleStop()
{
  stopRobot();
  pumpOff();
  currentState = STATE_IDLE;
  fireDirection = FIRE_NONE;
  server.send(200, "text/plain", "STOP");
}

void handlePumpOn()
{
  if (!autoMode) pumpOn();
  server.send(200, "text/plain", "PUMP ON");
}

void handlePumpOff()
{
  pumpOff();
  server.send(200, "text/plain", "PUMP OFF");
}

void handleServoLeft()
{
  nozzleServo.write(SERVO_LEFT);
  server.send(200, "text/plain", "NOZZLE LEFT");
}

void handleServoCenter()
{
  nozzleServo.write(SERVO_CENTER);
  server.send(200, "text/plain", "NOZZLE CENTER");
}

void handleServoRight()
{
  nozzleServo.write(SERVO_RIGHT);
  server.send(200, "text/plain", "NOZZLE RIGHT");
}

void setup()
{
  Serial.begin(115200);

  pinMode(FLAME_LEFT, INPUT);
  pinMode(FLAME_CENTER, INPUT);
  pinMode(FLAME_RIGHT, INPUT);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);

  pinMode(ENA, OUTPUT);
  pinMode(ENB, OUTPUT);

  pinMode(RELAY_PIN, OUTPUT);

  pumpOff();
  stopRobot();

  nozzleServo.attach(SERVO_PIN);
  nozzleServo.write(SERVO_CENTER);

  WiFi.softAP(ssid, password);

  Serial.println();
  Serial.println("=================================");
  Serial.println(" FIRE FIGHTING ROBOT");
  Serial.println("=================================");
  Serial.print("WiFi Name: ");
  Serial.println(ssid);
  Serial.print("Password: ");
  Serial.println(password);
  Serial.print("Robot IP: ");
  Serial.println(WiFi.softAPIP());

  server.on("/", handleRoot);
  server.on("/auto", handleAuto);
  server.on("/manual", handleManual);
  server.on("/forward", handleForward);
  server.on("/backward", handleBackward);
  server.on("/left", handleLeft);
  server.on("/right", handleRight);
  server.on("/stop", handleStop);
  server.on("/pumpOn", handlePumpOn);
  server.on("/pumpOff", handlePumpOff);
  server.on("/servoLeft", handleServoLeft);
  server.on("/servoCenter", handleServoCenter);
  server.on("/servoRight", handleServoRight);

  server.begin();

  Serial.println("Web server started.");
  Serial.println("Connect phone to FireRobot WiFi.");
}

void loop()
{
  server.handleClient();

  if (autoMode)
  {
    automaticMode();
  }
}