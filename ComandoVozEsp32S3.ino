
/*
  EchoVision — Comando de Voz para Acionar um Pino
  Zion Academy · zionautomation.com.br

  Fluxo: microfone → detecta fala → grava → envia WAV ao servidor
         → Whisper transcreve → "liga"/"desliga" → GPIO 21

  Feedback visual : LED azul enquanto grava
  Feedback sonoro : 1 bipe (Do6) assim que a fala e' capturada

  Biblioteca necessaria : ArduinoJson (Gerenciador de Bibliotecas)
  Configuracoes da IDE  : Placa -> ESP32S3 Dev Module
                          PSRAM -> OPI PSRAM      (obrigatorio)
                          USB CDC On Boot -> Enabled
*/

// ── Includes ──────────────────────────────────────────────────
#include <WiFi.h>            // pilha Wi-Fi do ESP32 (conectar, obter IP)
#include <HTTPClient.h>      // cliente HTTP — faz POST como um navegador
#include <ArduinoJson.h>     // interpreta o JSON que o servidor devolve
#include "driver/i2s_pdm.h"  // API nova de I2S — modo PDM para o microfone
#include "driver/i2s_std.h"  // API nova de I2S — modo standard para o alto-falante

// ── Credenciais e servidor ────────────────────────────────────
const char* WIFI_SSID  = "SUA_REDE_WIFI";   // nome da rede Wi-Fi
const char* WIFI_SENHA = "SUA_SENHA_WIFI";  // senha da rede Wi-Fi
const char* SERVIDOR   = "192.168.1.23";    // IP fixo do servidor Whisper
const int   PORTA      = 8000;              // porta do uvicorn

// ── Pinos ────────────────────────────────────────────────────
#define MIC_CLK   39   // clock PDM do microfone MEMS (GPIO 39)
#define MIC_DATA  38   // dados PDM do microfone MEMS (GPIO 38)
#define LED_PIN    2   // LED RGB WS2812 da placa
#define RELE_PIN  21   // saida para o rele (livre: cam=4-18, spk=3/45/46/48)

// ── Parametros de audio ───────────────────────────────────────
#define TAXA_AMOSTRAGEM  16000  // 16 kHz: formato que o Whisper espera
#define LIMIAR_RMS         600  // energia minima para considerar fala — CALIBRE
#define SILENCIO_MS        700  // pausa apos a fala que encerra a gravacao
#define TETO_GRAVACAO_MS  4000  // limite maximo de gravacao (trava de seguranca)
#define CHUNK             256   // amostras lidas do DMA por vez (16 ms de audio)

// ── Variaveis globais ────────────────────────────────────────
i2s_chan_handle_t micHandle = NULL;   // identificador do canal I2S do microfone
i2s_chan_handle_t spkHandle = NULL;   // identificador do canal I2S do alto-falante

int16_t  bloco[CHUNK];               // buffer de trabalho: um "gole" do DMA
int16_t* gravacao = nullptr;         // buffer da fala completa (alocado na PSRAM)
const size_t MAX_AMOSTRAS =          // capacidade maxima: 16000 x 4 = 64000 amostras
    TAXA_AMOSTRAGEM * (TETO_GRAVACAO_MS / 1000);

bool          releLigado = false;    // estado atual do rele (para exibir no debug)
unsigned long ultimaAcao = 0;        // instante do ultimo comando (para o cooldown)

// ── LED ──────────────────────────────────────────────────────
void ledApagado() { rgbLedWrite(LED_PIN, 0, 0,  0); }  // apagado = aguardando
void ledAzul()    { rgbLedWrite(LED_PIN, 0, 0, 40); }  // azul fraco = gravando

// ── Rele ─────────────────────────────────────────────────────
void ligarRele() {
  releLigado = true;                               // atualiza o estado interno
  digitalWrite(RELE_PIN, HIGH);                    // coloca o pino em nivel ALTO
  Serial.println(">> RELE LIGADO  (GPIO21 = ALTO)");
}

void desligarRele() {
  releLigado = false;                              // atualiza o estado interno
  digitalWrite(RELE_PIN, LOW);                     // coloca o pino em nivel BAIXO
  Serial.println(">> RELE DESLIGADO (GPIO21 = BAIXO)");
}

// ── Alto-falante ─────────────────────────────────────────────
void initAltoFalante() {
  pinMode(46, OUTPUT);                             // pino de enable do amplificador NS4168
  digitalWrite(46, HIGH);                          // HIGH = amplificador ligado

  i2s_chan_config_t cfg =
      I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER); // porta 1, master
  i2s_new_channel(&cfg, &spkHandle, NULL);         // cria canal TX (NULL = sem RX)

  i2s_std_config_t std = {
    .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(16000),                  // clock 16 kHz
    .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(                 // protocolo Philips
                    I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),  // 16 bits, mono
    .gpio_cfg = {
      .mclk = I2S_GPIO_UNUSED,                     // master clock: NS4168 nao usa
      .bclk = (gpio_num_t)48,                      // bit clock
      .ws   = (gpio_num_t)45,                      // word select (canal esq/dir)
      .dout = (gpio_num_t)3,                       // dados de audio para o amplificador
      .din  = I2S_GPIO_UNUSED,                     // entrada de audio: nao usada
      .invert_flags = { .mclk_inv=false, .bclk_inv=false, .ws_inv=false },
    },
  };
  i2s_channel_init_std_mode(spkHandle, &std);      // aplica config (canal fica desabilitado)
}

void tocarTom(float freq, int duracaoMs) {
  const uint32_t sr    = 16000;
  const uint32_t total = sr * duracaoMs / 1000;   // total de amostras do tom
  const uint32_t fade  = sr * 10 / 1000;          // 10 ms de rampa — evita o "tec" de estalo
  int16_t buf[256];
  uint32_t i = 0;

  i2s_channel_enable(spkHandle);                   // liga o canal so durante o tom
  while (i < total) {
    size_t n = 0;
    while (n < 256 && i < total) {
      float env = 1.0f;
      if      (i < fade)         env = (float)i / fade;            // fade in
      else if (i > total - fade) env = (float)(total - i) / fade;  // fade out
      buf[n++] = (int16_t)(sinf(2 * PI * freq * i / sr)            // senoide pura
                           * env * 0.4f * 32767);                   // x envelope x volume x escala int16
      i++;
    }
    size_t w = 0;
    i2s_channel_write(spkHandle, buf, n * 2, &w, portMAX_DELAY);   // envia bloco ao DMA
  }
  i2s_channel_disable(spkHandle);                  // desliga o canal: silencio limpo em repouso
}

void bipe() { tocarTom(1047, 120); }               // Do6, 120 ms — "captei!"

// ── Microfone PDM ────────────────────────────────────────────
void initMicrofone() {
  i2s_chan_config_t cfg =
      I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER); // porta 0 (PDM RX so existe aqui)
  i2s_new_channel(&cfg, NULL, &micHandle);         // cria canal RX (NULL = sem TX)

  i2s_pdm_rx_config_t pdm = {
    .clk_cfg  = I2S_PDM_RX_CLK_DEFAULT_CONFIG(TAXA_AMOSTRAGEM),           // clock 16 kHz
    .slot_cfg = I2S_PDM_RX_SLOT_DEFAULT_CONFIG(                            // 16 bits, mono
                    I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
    .gpio_cfg = {
      .clk = (gpio_num_t)MIC_CLK,                 // clock PDM (GPIO 39)
      .din = (gpio_num_t)MIC_DATA,                // dados PDM (GPIO 38)
      .invert_flags = { .clk_inv = false },
    },
  };
  i2s_channel_init_pdm_rx_mode(micHandle, &pdm);  // configura modo PDM RX
  i2s_channel_enable(micHandle);                   // habilita: DMA comeca a encher buffers
  Serial.println("Microfone OK");
}

int16_t calcRMS(int16_t* buf, size_t n) {
  if (n == 0) return 0;

  int64_t soma = 0;
  for (size_t i = 0; i < n; i++) soma += buf[i];
  int32_t dc = soma / n;                           // media = offset DC do microfone PDM

  int64_t sq = 0;                                  // int64 evita estouro: 256 x 32768^2 > int32
  for (size_t i = 0; i < n; i++) {
    int32_t s = buf[i] - dc;                       // remove o offset DC de cada amostra
    sq += (int64_t)s * s;                           // acumula os quadrados
  }
  return (int16_t)sqrt((double)sq / n);            // raiz da media dos quadrados = RMS
}

void limparMic() {                                  // descarta audio acumulado no DMA
  size_t lido = 0;                                 // durante a requisicao HTTP
  unsigned long t = millis();                      // evita auto-disparo no eco do bipe
  while (millis() - t < 300)
    i2s_channel_read(micHandle, bloco, sizeof(bloco), &lido, pdMS_TO_TICKS(10));
}

// ── Wi-Fi ────────────────────────────────────────────────────
void conectarWiFi() {
  WiFi.mode(WIFI_STA);                             // STA = entra em rede existente
  WiFi.begin(WIFI_SSID, WIFI_SENHA);               // inicia conexao (nao bloqueia)
  Serial.print("Conectando");
  while (WiFi.status() != WL_CONNECTED) {          // aguarda ate conectar
    delay(400);
    Serial.print(".");
  }
  Serial.print("\nWiFi OK. IP da placa: ");
  Serial.println(WiFi.localIP());                  // exibe IP para checar sub-rede
}

// ── Cabecalho WAV ────────────────────────────────────────────
void cabecalhoWav(uint8_t* h, uint32_t bytes, uint32_t sr) {
  uint32_t byteRate  = sr * 2;                     // bytes/s: 16000 Hz x 2 bytes = 32000
  uint32_t chunkSize = 36 + bytes;                 // tamanho total - 8 bytes
  uint32_t fmt16 = 16;                             // tamanho fixo do bloco fmt para PCM
  uint16_t um=1, dois=2, bits=16;

  memcpy(h,    "RIFF", 4); memcpy(h+ 4, &chunkSize, 4); // assinatura + tamanho
  memcpy(h+8,  "WAVE", 4); memcpy(h+12, "fmt ",     4); // tipo + bloco de formato
  memcpy(h+16, &fmt16, 4); memcpy(h+20, &um,        2); // tamanho fmt + PCM=1
  memcpy(h+22, &um,    2); memcpy(h+24, &sr,        4); // mono + sample rate
  memcpy(h+28, &byteRate,4);memcpy(h+32, &dois,     2); // byte rate + block align
  memcpy(h+34, &bits,  2); memcpy(h+36, "data",     4); // bits/sample + marcador data
  memcpy(h+40, &bytes, 4);                               // tamanho dos dados de audio
}

// ── Envia o audio e age conforme a transcricao ───────────────
void enviarEAgir(int16_t* amostras, size_t total) {
  uint32_t dataBytes = total * 2;                  // amostras x 2 bytes cada
  uint8_t* payload = (uint8_t*)ps_malloc(44 + dataBytes); // aloca na PSRAM
  if (!payload) { Serial.println("Sem memoria PSRAM!"); return; }

  uint8_t hdr[44];
  cabecalhoWav(hdr, dataBytes, TAXA_AMOSTRAGEM);   // gera os 44 bytes do cabecalho WAV
  memcpy(payload,      hdr,      44);              // cabecalho nos primeiros 44 bytes
  memcpy(payload + 44, amostras, dataBytes);       // audio logo em seguida (contiguo)

  HTTPClient http;
  String url = String("http://") + SERVIDOR + ":" + PORTA + "/listen";
  http.begin(url);
  http.addHeader("Content-Type", "audio/wav");     // informa o tipo do conteudo ao servidor
  http.setTimeout(20000);                          // aguarda ate 20 s pela resposta do Whisper
  http.setConnectTimeout(5000);                    // erro em 5 s = IP errado ou sem rota

  Serial.println("Enviando para o servidor...");
  unsigned long t0 = millis();
  int codigo = http.POST(payload, 44 + dataBytes); // envia WAV e bloqueia ate receber resposta
  Serial.printf("Resposta em %lu ms (HTTP %d)\n", millis() - t0, codigo);
  free(payload);                                   // libera a PSRAM imediatamente apos o uso

  if (codigo != 200) {
    Serial.printf("Erro HTTP %d. Verifique o servidor.\n", codigo);
    http.end();
    return;
  }

  String resposta = http.getString();              // corpo da resposta: {"transcricao":"..."}
  http.end();

  JsonDocument doc;
  deserializeJson(doc, resposta);
  const char* texto = doc["transcricao"];          // extrai o campo do JSON

  if (!texto || strlen(texto) == 0) {              // NULL = campo ausente | 0 = vazio
    Serial.println("Nao entendi. Tente novamente.");
    return;
  }

  Serial.print("Voce disse: \"");
  Serial.print(texto);
  Serial.println("\"");

  String t = String(texto);
  t.toLowerCase();                                 // normaliza: "Liga" → "liga"

  // ORDEM IMPORTA: "desliga" contem "liga" — sempre teste o negativo antes do positivo!
  if      (t.indexOf("desliga") >= 0 || t.indexOf("apaga")  >= 0) desligarRele();
  else if (t.indexOf("liga")    >= 0 || t.indexOf("acende") >= 0) ligarRele();
  else    Serial.println("Nenhum comando reconhecido.");
}

// ── Grava a fala e envia ─────────────────────────────────────
void gravar() {
  if (!gravacao) {                                 // alocacao preguicosa: so na primeira vez
    gravacao = (int16_t*)ps_malloc(MAX_AMOSTRAS * 2); // 128 KB na PSRAM externa
    if (!gravacao) { Serial.println("Sem memoria PSRAM!"); return; }
  }

  size_t        gravado   = 0;
  unsigned long ultimaVoz = millis();
  unsigned long inicio    = millis();
  bool          temFala   = false;

  Serial.println("--- Ouvindo... ---");
  ledAzul();                                       // azul = "estou gravando"

  while (millis() - inicio < TETO_GRAVACAO_MS) {
    size_t lido = 0;
    i2s_channel_read(micHandle, bloco, sizeof(bloco),
                     &lido, portMAX_DELAY);        // le do DMA (bloqueia ate ter dados)
    size_t n = lido / 2;                           // bytes lidos → numero de amostras
    if (n == 0) continue;

    if (gravado + n <= MAX_AMOSTRAS) {             // checa limite ANTES de copiar
      memcpy(gravacao + gravado, bloco, lido);     // acumula no buffer de gravacao
      gravado += n;                                // avanca o ponteiro de escrita
    }

    if (calcRMS(bloco, n) > LIMIAR_RMS) {          // energia acima do limiar = tem fala
      ultimaVoz = millis();                        // renova o carimbo de "ultima voz ouvida"
      temFala   = true;                            // marca que ja detectou fala pelo menos uma vez
    }

    if (temFala && millis() - ultimaVoz > SILENCIO_MS) break; // pausa = frase encerrada
  }

  ledApagado();

  if (gravado < TAXA_AMOSTRAGEM / 4) {             // menos de 250 ms = estalo, nao e' fala
    Serial.println("Audio muito curto, ignorado.");
    return;
  }

  Serial.printf("Gravado: %.1f s\n", gravado / (float)TAXA_AMOSTRAGEM);

  bipe();                                          // feedback imediato antes do HTTP sair
  enviarEAgir(gravacao, gravado);                  // monta WAV, envia ao servidor, age
  limparMic();                                     // descarta eco acumulado no DMA durante o HTTP
  ultimaAcao = millis();                           // registra instante para o cooldown
}

// ── Setup ────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(2000);                                     // aguarda USB CDC enumerar no computador
  Serial.println("\n=== EchoVision - Voz para Rele ===");

  pinMode(RELE_PIN, OUTPUT);                       // define como saida — SEMPRE PRIMEIRO
  desligarRele();                                  // estado seguro antes de qualquer init
  ledApagado();

  initMicrofone();                                 // porta I2S 0 — PDM RX
  initAltoFalante();                               // porta I2S 1 — standard TX
  conectarWiFi();

  Serial.println("\nPronto! Fale 'liga' ou 'desliga'.");
  Serial.printf("Calibre LIMIAR_RMS=%d pelos valores [mic] abaixo.\n\n", LIMIAR_RMS);
}

// ── Loop ─────────────────────────────────────────────────────
void loop() {
  size_t lido = 0;
  i2s_channel_read(micHandle, bloco, sizeof(bloco),
                   &lido, pdMS_TO_TICKS(10));      // timeout 10 ms: nao trava o loop

  if (lido == 0) return;                           // sem dados novos, recomeça o loop

  int16_t rms = calcRMS(bloco, lido / 2);          // calcula a energia do bloco atual

  static unsigned long ultimoPrint = 0;            // static: persiste entre chamadas do loop
  if (millis() - ultimoPrint > 500) {              // imprime a cada 500 ms para calibracao
    Serial.printf("[mic] rms=%d (limiar=%d) | rele=%s\n",
                  rms, LIMIAR_RMS, releLigado ? "LIGADO" : "DESLIGADO");
    ultimoPrint = millis();
  }

  if (rms > LIMIAR_RMS && millis() - ultimaAcao > 1500) // fala detectada + cooldown ok?
    gravar();                                      // inicia gravacao e transcricao
}
