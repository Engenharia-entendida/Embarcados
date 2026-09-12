
/*
  ==================================================================
  Z-S3 ADS | ESP32-S3 N16R8 | ADS1115 | OLED SSD1306 0.96" | ArduinoCloud
  Mede TENSAO (ZMPT101B) e CORRENTE (ACS712-5A) em AC RMS.
  Publica na nuvem: ZRI (tensao, V) e Current (corrente, A).
  ==================================================================

  Canais do ADS1115:
    A3 -> ZMPT101B  (tensao)   [divisor R1=2.2k serie + R2=3.3k p/ GND]
    A0 -> ACS712-5A (corrente) <<< AJUSTE 'ACS_CHANNEL' p/ o canal que voce usou

  CALIBRACAO (mesma receita p/ tensao e corrente):
    1) Deixe o CAL_FACTOR correspondente = 1.0 e grave.
    2) Aplique um valor CONHECIDO
         - tensao : multimetro True-RMS na tomada
         - corrente: alicate amperimetro na carga
    3) Anote o "_ADC_RMS" que aparece no Serial.
    4) CAL_FACTOR = (valor do instrumento) / (_ADC_RMS anotado).
    5) Escreva o resultado na constante e grave de novo.
*/
#include "arduino_secrets.h"  
#include <Adafruit_NeoPixel.h>        // LED RGB de status (WS2812)
#include <Wire.h>                     // barramento I2C
#include <Adafruit_ADS1X15.h>         // driver do ADS1115 (ADC externo 16 bits)
#include <Adafruit_GFX.h>             // base grafica do display
#include <Adafruit_SSD1306.h>         // driver do OLED SSD1306
#include "thingProperties.h"          // variaveis da nuvem: ZRI, Current, RelayReset...

// ---------- Pinos / I2C ----------
#define I2C_SDA 37                    // dado do I2C
#define I2C_SCL 38                    // clock do I2C

// ---------- OLED ----------
#define SCREEN_WIDTH 128              // largura do display em px
#define SCREEN_HEIGHT 64              // altura do display em px
#define OLED_ADDR 0x3C                // endereco I2C do OLED
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);  // objeto do display

// ---------- ADS1115 ----------
Adafruit_ADS1115 ads;                 // objeto do ADC externo
#define ZMPT_CHANNEL 3                // canal A3 = tensao
#define ACS_CHANNEL  2                // canal A0 = corrente  
// ---------- Saidas / entradas ----------
#define Relay  4                      // pino do rele (HIGH = ligado, LOW = desarma)
#define Button 48                     // botao de rearme da protecao
#define PIN_DADOS 1                   // pino de dados do LED RGB
#define NUM_LEDS 1                    // quantidade de LEDs na fita
Adafruit_NeoPixel pixels(NUM_LEDS, PIN_DADOS, NEO_GRB + NEO_KHZ800);  // objeto do LED

// ---------- Amostragem ----------
#define JANELA_MS 500                 // tempo de coleta por canal (~30 ciclos em 60Hz)
#define MAX_SAMPLES 600               // teto do buffer (860SPS * 0.5s ~= 430 amostras)
int16_t buffer[MAX_SAMPLES];          // buffer reutilizado a cada leitura

// ---------- Fatores de calibracao ----------
#define CAL_FACTOR   489.07           // tensao: 220 / 0.410 
#define CAL_FACTOR_I 155.0            // corrente: A por V-rms no ADS  
#define I_ZERO       0.614            // abaixo disso considera  (ruido do ACS712 em vazio)

bool Status = true;                   // estado desejado do rele 

// ==================================================================
// Le um canal do ADS por 'janela_ms', remove o offset DC (bias) e
// devolve o RMS da parte AC, em Volts, medido na ENTRADA do ADS.`
// ==================================================================
float lerVrmsADC(uint8_t canal, uint16_t janela_ms) {
  int n = 0;                                              // contador de amostras
  unsigned long t0 = millis();                            // marca o inicio da janela
  while (millis() - t0 < janela_ms && n < MAX_SAMPLES) {  // coleta ate estourar tempo ou buffer
    buffer[n++] = ads.readADC_SingleEnded(canal);         // le 1 amostra do canal escolhido
  }
  if (n < 10) return 0.0;                                 // amostras de menos = algo errado

  long soma = 0;                                          // soma p/ achar a media (offset DC)
  for (int i = 0; i < n; i++) soma += buffer[i];          // acumula todas as amostras
  float media = (float)soma / n;                          // media = componente DC (bias)

  double somaQuad = 0;                                    // soma dos desvios ao quadrado
  for (int i = 0; i < n; i++) {                           // percorre de novo o buffer
    float desvio = buffer[i] - media;                     // remove o bias (fica so o AC)
    somaQuad += (double)desvio * desvio;                  // acumula desvio^2
  }
  float rmsCounts = sqrt(somaQuad / n);                   // RMS em "counts" do ADS
  return rmsCounts * 0.000125;                            // GAIN_ONE: 0.125 mV/count -> Volts
}

// ==================================================================
void setup() {
  Serial.begin(115200);                                  // porta serial p/ debug/calibracao
  delay(1500);                                           // respira antes de subir a nuvem

  initProperties();                                      // registra as variaveis da nuvem
  ArduinoCloud.begin(ArduinoIoTPreferredConnection);     // conecta ao Arduino IoT Cloud
  setDebugMessageLevel(2);                               // nivel de log da nuvem
  ArduinoCloud.printDebugInfo();                         // imprime info de conexao

  pinMode(Button, INPUT_PULLUP);                         // botao com pull-up (solto = HIGH)
  pinMode(Relay, OUTPUT);                                // rele como saida
  digitalWrite(Relay, HIGH);                             // liga o rele no boot

  pixels.begin();                                        // inicia o LED RGB
  pixels.setBrightness(50);                              // brilho 0-255 (cuidado c/ consumo)

  Wire.begin(I2C_SDA, I2C_SCL);                          // sobe o I2C nos pinos definidos
  display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);        // inicia o OLED
  display.clearDisplay();                                // limpa o buffer do display
  display.setTextColor(SSD1306_WHITE);                   // cor do texto

  ads.begin();                                           // inicia o ADS1115
  ads.setGain(GAIN_ONE);                                 // faixa +-4.096V (0.125 mV/count)
  ads.setDataRate(RATE_ADS1115_860SPS);                  // 860 amostras/s (max util)

  Serial.println("Setup ok. Medindo Tensao e Corrente...");
}

// ==================================================================
void loop() {
  ArduinoCloud.update();                                 // mantem a sincronia com a nuvem

  // ---- Tensao ----
  float vAdcRms = lerVrmsADC(ZMPT_CHANNEL, JANELA_MS);   // RMS bruto (V) no canal da tensao
  float vMains  = vAdcRms * CAL_FACTOR;                  // aplica calibracao -> tensao real (V)

  // ---- Corrente ----
  float iAdcRms = lerVrmsADC(ACS_CHANNEL, JANELA_MS);    // RMS bruto (V) no canal da corrente
  float iLoad   = iAdcRms * CAL_FACTOR_I;                // aplica calibracao -> corrente real (A)
  if (iLoad < I_ZERO) iLoad = 0.0;                       // zera o ruido de fundo do ACS712

  // ---- Log (use p/ calibrar) ----
  Serial.printf("V_ADC_RMS:%.4f Tensao:%.1fV | I_ADC_RMS:%.4f Corrente:%.3fA\n",
                vAdcRms, vMains, iAdcRms, iLoad);        // mostra bruto e calibrado

  // ---- Publica na nuvem ----
  ZRI     = vMains;                                      // envia tensao p/ o dashboard
  Current = iLoad;                                       // envia corrente p/ o dashboard
  Watts = vMains*iLoad;
  // ---- Protecao de sobretensao (trava ate apertar o botao) ----
  if (vMains > 240) {                                    // passou do limite seguro?
    pixels.setPixelColor(0, pixels.Color(255, 0, 0));    // LED vermelho = alarme
    pixels.show();                                       // atualiza o LED
    bool botao = true;                                   // solto = HIGH = true
    while (botao == true) {                              // fica preso enquanto NAO apertar
      digitalWrite(Relay, LOW);                          // mantem rele desarmado
      delay(80);                                         // pequena pausa (antibounce)
      botao = digitalRead(Button);                       // le o botao (LOW quando aperta)
    }                                                    // saiu = usuario rearmou
  } else {                                               // tensao normal:
    digitalWrite(Relay, !Status);                         // rele segue o estado desejado
  }

  // ---- OLED ----
  display.clearDisplay();                                // limpa a tela
  display.setTextSize(1);                                // fonte pequena p/ o titulo
  display.setCursor(0, 0);                               // canto superior esquerdo
  display.println("Zion Automation");                    // cabecalho
  display.setTextSize(2);                                // fonte grande p/ os valores
  display.setCursor(0, 16);                              // linha da tensao
  display.print(vMains, 0);                              // tensao sem casas decimais
  display.println(" V");                                 // unidade
  display.setCursor(0, 40);                              // linha da corrente
  display.print(iLoad, 2);                               // corrente com 2 casas
  display.println(" A");                                 // unidade
  display.display();                                     // manda tudo p/ o hardware
}

// ==================================================================
// Callbacks da nuvem (executam quando a nuvem escreve na variavel)
// ==================================================================
void onZriChange() {                                     // ZRI recebeu novo valor
  // (nada a fazer: ZRI e so leitura no dashboard)
}

void onRelayResetChange() {                               // botao/switch da nuvem mexeu no rele
  Status = !Status;                                       // inverte o estado desejado
  digitalWrite(Relay, Status);                            // aplica no rele
  delay(90);                                              // pequena pausa
}

void onCurrentChange() {                                 // Current recebeu novo valor da nuvem
  // (nada a fazer: Current e so leitura no dashboard)
}

/*
  Since Watts is READ_WRITE variable, onWattsChange() is
  executed every time a new value is received from IoT Cloud.
*/
void onWattsChange()  {
  // Add your code here to act upon Watts change
}
