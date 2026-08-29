
/*
  ============================================================
  FASE 1 - Calculo de tensao RMS real (calibrado)
  Placa: Z-S3 ADS | ESP32-S3 N16R8 | ADS1115 | OLED SSD1306 0.96"
  ============================================================

  Pre-requisitos ja validados nas etapas anteriores:
   - I2C: SDA=GPIO37, SCL=GPIO38
   - ADS1115: GAIN_ONE (+-4.096V), 860SPS, canal A3
   - Divisor resistivo ZMPT101B->A3: R1=2.2k (serie) + R2=3.3k (p/ GND)

  COMO CALIBRAR (faça isso antes de confiar na leitura):
   1) Deixe CAL_FACTOR = 1.0 (como esta abaixo) e grave este codigo.
   2) Com a tensao real da tomada aplicada, anote o valor "V_ADC_RMS"
      que aparece no Serial/OLED.
   3) Meça a MESMA tomada com o multimetro True-RMS.
   4) Calcule: CAL_FACTOR = (leitura do multimetro) / (V_ADC_RMS anotado)
   5) Atualize a constante abaixo com esse valor e grave de novo.
      A partir dai, "Tensao" no display ja sai calibrada em Volts reais.
*/
#include <Adafruit_NeoPixel.h>
#include <Wire.h>
#include <Adafruit_ADS1X15.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define I2C_SDA 37
#define I2C_SCL 38

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_ADDR 0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

Adafruit_ADS1115 ads;
#define ZMPT_CHANNEL 3
#define Relay 4
#define Button 48
#define PIN_DADOS 1
#define NUM_LEDS 1
Adafruit_NeoPixel pixels(NUM_LEDS, PIN_DADOS, NEO_GRB + NEO_KHZ800);

#define JANELA_MS 500       // ~30 ciclos em 60Hz por leitura
#define MAX_SAMPLES 600     // limite de seguranca do buffer
int16_t buffer[MAX_SAMPLES];

#define CAL_FACTOR 489.07    // calibrado em 220V: 220 / 0.410

// Coleta amostras por 'janela_ms', remove o offset DC (bias) e retorna
// o RMS da parte AC, em Volts, medido na entrada do ADS1115 (pos-divisor).
float lerVrmsADC(uint16_t janela_ms) {
  int n = 0;
  unsigned long t0 = millis();
  while (millis() - t0 < janela_ms && n < MAX_SAMPLES) {
    buffer[n++] = ads.readADC_SingleEnded(ZMPT_CHANNEL);
  }
  if (n < 10) return 0.0; // amostras de menos, algo errado

  long soma = 0;
  for (int i = 0; i < n; i++) soma += buffer[i];
  float media = (float)soma / n;

  double somaQuad = 0;
  for (int i = 0; i < n; i++) {
    float desvio = buffer[i] - media;
    somaQuad += (double)desvio * desvio;
  }
  float rmsCounts = sqrt(somaQuad / n);
  
  return rmsCounts * 0.000125; // GAIN_ONE: 0.125 mV/bit
}
bool Status=true;


#include "thingProperties.h"

void setup() {
  
  Serial.begin(9600);
  delay(1500); 
  initProperties();
  ArduinoCloud.begin(ArduinoIoTPreferredConnection);
  setDebugMessageLevel(2);
  ArduinoCloud.printDebugInfo();
  
  //===================================================================//
  pinMode(Button,INPUT_PULLUP);
  pinMode(Relay,OUTPUT);
  digitalWrite(Relay,HIGH); 
  pixels.begin(); // Inicializa o pino de dados
  pixels.setBrightness(50); // Define brilho (0 a 255) - Cuidado com consumo em fitas longas!
  Serial.begin(115200);
  delay(500);
  Wire.begin(I2C_SDA, I2C_SCL);
  display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
  ads.begin();
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  ads.setGain(GAIN_ONE);
  ads.setDataRate(RATE_ADS1115_860SPS);
  Serial.println("Setup concluido. Calculando RMS...");
}

void loop() {
  ArduinoCloud.update();
  // Your code here 
  float vAdcRms = lerVrmsADC(JANELA_MS);
  float vMains = vAdcRms * CAL_FACTOR;

  Serial.printf("V_ADC_RMS: %.4f V | Tensao calibrada: %.2f V\n", vAdcRms, vMains);

  if(vMains>240){
    pixels.setPixelColor(0, pixels.Color(255, 0, 0));
    pixels.show();  // Envia os dados para o hardware
    bool ButtonStatus=true;
    while(ButtonStatus==true){
    digitalWrite(Relay,LOW);
    delay(80);
    ButtonStatus=digitalRead(Button);
 
    }
  }
 
  else digitalWrite(Relay,Status);
  
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("Zion Automation");
  display.setTextSize(2);
  display.setCursor(0, 16);
  display.print("AC:");
  display.print(vMains, 1); 
  display.println("V");
  display.display();
  ZRI=vMains;
}

/*
  Since Zri is READ_WRITE variable, onZriChange() is
  executed every time a new value is received from IoT Cloud.
*/
void onZriChange()  {
  // Add your code here to act upon Zri change
}
/*
  Since RelayReset is READ_WRITE variable, onRelayResetChange() is
  executed every time a new value is received from IoT Cloud.
*/
void onRelayResetChange()  {
  // Add your code here to act upon RelayReset change
  Status=!Status;
  digitalWrite(Relay,Status);
  delay(90);
}
