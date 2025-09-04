#include "DHT.h"                        // Biblioteca do sensor de temperatura/umidade DHT
#include <Adafruit_GFX.h>               // Núcleo gráfico (texto, pixels) da Adafruit
#include <Adafruit_SSD1306.h>           // Driver do display OLED SSD1306
#include <Wire.h>                       // I2C (SDA/SCL) usado pelo OLED

#define DHTPIN 4                        // GPIO do ESP32 ligado ao pino DATA do DHT
#define DHTTYPE DHT11                   // Tipo do sensor: DHT11
DHT dht(DHTPIN, DHTTYPE);               // Cria o objeto do DHT com pino e tipo

#define SCREEN_WIDTH 128                // Largura do OLED em pixels
#define SCREEN_HEIGHT 64                // Altura do OLED em pixels
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1); // Instância do display (I2C, sem pino de reset)

#define TIME_TO_SLEEP 10000000          // Tempo p/ acordar do deep sleep em microssegundos (10.000.000 us = 10 s)

void setup() {
  dht.begin();                          // Inicializa o DHT
  // delay(2000);                       // (Recomendado) aguardar ~2 s p/ o DHT estabilizar

  display.begin(SSD1306_SWITCHCAPVCC, 0x3C); // Inicializa OLED no endereço I2C 0x3C com charge pump
  display.clearDisplay();               // Limpa o buffer da tela
  display.setTextSize(1);               // Tamanho do texto (1 = 6x8 px/caractere)
  display.setTextColor(SSD1306_WHITE);  // Cor do texto: branco (monocromático)
  display.setCursor(0, 0);              // Posição inicial do cursor (x=0,y=0)
  display.print("Esp32 Acordou");       // Mensagem inicial
  display.display();                    // Envia o buffer para o OLED
  delay(1000);                          // Espera 1 s para visualizar

  float temperatura = dht.readTemperature(); // Lê temperatura em °C (padrão)
  float umidade = dht.readHumidity();        // Lê umidade relativa em %

  display.clearDisplay();               // Limpa a tela para nova mensagem
  display.setCursor(0, 0);              // Volta o cursor para o topo
  display.print("Leitura do sensor");   // Imprime título
  display.print("Temp: ");              // (Sem '\n' aqui, tudo sai na mesma linha)
  display.print(temperatura);           // Valor da temperatura
  display.print(" C");                  // Unidade
  display.print("Umid: ");              // Continua imprimindo na sequência
  display.print(umidade);               // Valor da umidade
  display.print(" %");                  // Unidade
  display.display();                    // Atualiza o OLED
  delay(2000);                          // Mantém a leitura na tela por 2 s
  display.clearDisplay();               // Limpa a tela
  display.setCursor(0, 0);              // Cursor no topo
  display.print("Dormindo...");         // Mensagem antes de dormir
  display.display();                    // Mostra a mensagem

  esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP); // Programa o wake-up por timer (RTC)
  esp_deep_sleep_start();               // Entra em deep sleep (ao acordar, roda setup() de novo)
}

void loop() {
  // nunca chega aqui, pois sempre dorme no setup()
}