
#include <WiFi.h>

void setup() {
  Serial.begin(115200); // configurada a taxa de transferência em 115200 bits por segundo para a transmissão serial
  WiFi.mode(WIFI_MODE_STA); // configura o WIFi para o Modo de estação WiFi
  delay(200);
  Serial.print("Endereço MAC: ");
  Serial.println(WiFi.macAddress()); // retorna o endereço MAC do dispositivo

  while(1); // loop infinito
}

void loop() {}
