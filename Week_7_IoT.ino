#include <WiFi.h>
#include <HCSR04.h>
#include <PubSubClient.h>

#define WHEEL_L_1 16
#define WHEEL_L_2 17 // Zal hier niet gebruikt worden maar kan gebruikt worden om achteruit te rijden
#define WHEEL_R_1 18
#define WHEEL_R_2 19 // Zal hier niet gebruikt worden maar kan gebruikt worden om achteruit te rijden

#define LED_LOST 27
#define LED_PROBLEM 26
#define LED_OK 25

#define TRIG 5
#define ECHO 4
UltraSonicDistanceSensor echo_mod(TRIG, ECHO);

#define IR_L 32
#define IR_M 35
#define IR_R 34
#define BUTTON 15

#define BLACK_LINE 1
#define WHITE_LINE 0
#define STOP_OBJ 10 // de afstand in cm van de ultrasone module tot het dichtste toegelaten object
// de auto zal in dit voorbeeld op 10cm van een object stoppen.

int color_line = BLACK_LINE;
bool interrupt = false;

const char* ssid = "SSID"; // voer hier uw netwerk's SSID in
const char* password = "PASSWORD"; // voer hier het wachtwoord in van het netwerk dat u hierboven heeft meegegeven
const char* mqttServer = "255.255.255.255"; // hier voert u het IP adres in van uw MQTT server
const int mqttPort = 1883; // vervang 1883 met de poort die uw MQTT server gebruikt
const char* mqttUser = "Jeff"; // hier voert u uw MQTT user ID in, als u deze gebruikt
const char* mqttPassword = "123"; // voer hier het wachtwoord van uw MQTT gebruiker in
const char* clientID = "Car"; // MQTT client ID - deze moet uniek zijn voor elke client op het MQTT netwerk.

void callback(char* topic, byte* payload, unsigned int length) {
  String message = String(topic); // de callback functie die controlleerd of het MQTT bericht command/continue is
  if (message == "command/continue") { // en als dit zo is, dan zal de auto doorrijden
    interrupt = true;
  }
}

void IRAM_ATTR isr() { // het declareren van een interrupt functie zodat de drukknop van het autootje zelf altijd werkt
  interrupt = true;
}

WiFiClient espClient; // het klaarzetten van de WiFi
PubSubClient client(mqttServer, mqttPort, callback, espClient); // MQTT klaarzetten

void setup() {
  Serial.begin(115200);
  pinMode(BUTTON, INPUT_PULLUP);
  attachInterrupt(BUTTON, isr, FALLING); // de drukknop een interrupt maken

  pinMode(WHEEL_L_1, OUTPUT);
  pinMode(WHEEL_L_2, OUTPUT);
  pinMode(WHEEL_R_1, OUTPUT);
  pinMode(WHEEL_R_2, OUTPUT);

  pinMode(LED_LOST, OUTPUT); // zorg ervoor dat de LEDs kunnen schijnen
  pinMode(LED_PROBLEM, OUTPUT);
  pinMode(LED_OK, OUTPUT);

  pinMode(IR_L, INPUT); // de IR sensoren leesbaar maken
  pinMode(IR_M, INPUT);
  pinMode(IR_R, INPUT);

  client.setServer(mqttServer, mqttPort);
  WiFi.begin(ssid, password); // de WiFi aanzetten
  while (WiFi.status() != WL_CONNECTED) { // verbinden met de WiFi
    delay(500);
    Serial.println("Connecting to WiFi..");
  }
}

void signal_led(int led_nr) {
  switch (led_nr) {
    case (LED_LOST): digitalWrite(LED_LOST, HIGH); digitalWrite(LED_PROBLEM, LOW); digitalWrite(LED_OK, LOW); break;
    case (LED_PROBLEM): digitalWrite(LED_LOST, LOW); digitalWrite(LED_PROBLEM, HIGH); digitalWrite(LED_OK, LOW); break;
    case (LED_OK): digitalWrite(LED_LOST, LOW); digitalWrite(LED_PROBLEM, LOW); digitalWrite(LED_OK, HIGH); break;
    default: Serial.println("Error"); break;
  }
}

void turn_left() {
  digitalWrite(WHEEL_L_1, LOW);
  digitalWrite(WHEEL_R_1, HIGH);
}
void turn_right() {
  digitalWrite(WHEEL_L_1, HIGH);
  digitalWrite(WHEEL_R_1, LOW);
}
void stop_car() {
  digitalWrite(WHEEL_L_1, LOW);
  digitalWrite(WHEEL_R_1, LOW);
}
void drive_forward() {
  digitalWrite(WHEEL_L_1, HIGH);
  digitalWrite(WHEEL_R_1, HIGH);
}

bool sees_line(int IR_x) { // deze fucntie controleert of de lijn die gevolgd word (zwart of wit, bepaalpaar bovenaan) zichtbaar is voor de sensor
  bool line_seen = digitalRead(IR_x) == color_line;
  return line_seen;
}

void determine_drive() {
  double dist_obs = echo_mod.measureDistanceCm();
  bool left_eye = sees_line(IR_L);
  bool right_eye = sees_line(IR_R);
  bool mid_eye = sees_line(IR_M);
  interrupt = false; // voor de drukknop op de auto of de Raspberry Pi terug te "resetten" - deze staat hier in het geval dat de knop per ongeluk werd ingedrukt wanneer de auto niet stilstond
  signal_led(LED_OK);
  if (dist_obs < STOP_OBJ) {
    stop_car();
    signal_led(LED_PROBLEM);
    while (echo_mod.measureDistanceCm() < STOP_OBJ) {
    }
    signal_led(LED_OK);
    drive_forward();
    return;
  } else if (left_eye && !right_eye) { // Ik vraag hier het criteria van het middelste oog niet aan omdat dit niet relevant is als ik enkel links of rechts een lijn zie.
    turn_left();
  } else if (!left_eye && right_eye) { // Ik vraag hier het criteria van het middelste oog niet aan omdat dit niet relevant is als ik enkel links of rechts een lijn zie.
    turn_right();
  } else if (!mid_eye) { // Het middelste oog ziet geen lijn meer, de auto zal dan direct weten dat het de lijn kwijt is en zal dus stoppen.
    signal_led(LED_LOST);
    stop_car();
    while (! interrupt) { // Als deze functie word opgeroepen wilt dit zeggen dat de auto gaat wachten tot als iemand op de knop drukt
      client.loop();
    }
  } else if (left_eye && right_eye) { // omdat ik al heb gevraagd of het middelste oog de lijn niet ziet, ben ik zeker dat in deze gevallen mid_eye de lijn wel ziet.
    while (sees_line(IR_L) && sees_line(IR_R)) {
      // ik laat de auto rijden zolang deze beide lijnen ziet zodat de sensoren over de lijn staan, dan wacht de auto 20 seconden zoals gewenst
      drive_forward();
      if (! sees_line(IR_M)) { // in het heel uitzonderlijke geval dat de auto de lijn kwijtraakt terwijl deze vooruit rijd zal deze terugspringen uit deze tak
        return;
      }
    }
    stop_car();
    for (int i = millis(); (i + 20000 > millis()); i) {
      //  ik hou hier  de auto voor 20 seconden vast, tenzij de auto niet stilstaat Ik liet de auto stoppen de lijn hierboven.
      // de enige  manier dat de auto terug begint te  rijden is als de 20 seconden gedaan  zijn of als ik via MQTT de auto weer laat rijden
      client.loop();
      if (interrupt) {
        return;
      }
    }
  } else if (!(left_eye || right_eye)) { // hier pas ik LeMorgan's regel toe [NOT x AND NOT Y] is equivalent met NOT[x OR Y] of kortweg NOR
    // kan  ook vervangen worden door "else" zonder if en conditie omdat dit het geval is als geen van de bovenstaande waar is (al de andere combinaties zijn uitgesloten) - zie waarheidstabel
    drive_forward();
  }
}
//IR sensoren zien zwart als 1 & alles anders als 0 --> werken met zwarte lijn is IR rechtstreeks uitlezen m.a.w. sensor(IR)==1

void loop() {
  while (!client.connected()) {
    Serial.println("Connecting to MQTT...");
    if (client.connect(clientID, mqttUser, mqttPassword )) {
      Serial.println("connected");
      client.subscribe("command/continue");
    } else {
      Serial.print("failed with state ");
      Serial.print(client.state());
      delay(2000);
    }
  }
  determine_drive();
}
