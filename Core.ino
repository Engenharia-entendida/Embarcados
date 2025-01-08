
#define Relay1 23
#define Relay2 18

void setup() {
    pinMode(Relay1, OUTPUT);
    pinMode(Relay2, OUTPUT);
    Serial.begin(115200);
    //xTaskCreatePinnedToCore(funcao, nome, stackSize, parametros, prioridade, handler, core);
    xTaskCreatePinnedToCore(loop1, "loop1", 1024, NULL, 1, NULL, 0); // Core 0
    xTaskCreatePinnedToCore(loop2, "loop2", 1024, NULL, 1, NULL, 1); // Core 1

}
void loop1(void *pvParameters) {
  while(1){ 
    digitalWrite(Relay1,HIGH);
    delay(200);
    digitalWrite(Relay1,LOW);
    delay(200);
    Serial.println(xPortGetCoreID());
  }
}

void loop2(void *pvParameters) {
   while(1){ 
    digitalWrite(Relay2,HIGH);
    delay(2000);
    digitalWrite(Relay2,LOW);
    delay(2000);
    Serial.println(xPortGetCoreID());
  }
}

void loop() {
    // Main loop does nothing
}
