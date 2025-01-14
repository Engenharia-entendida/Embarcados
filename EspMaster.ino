 
#include <WiFi.h>
#include <esp_now.h>
#define button 4
uint8_t slave[] =   {0xC4,0x4F,0x33,0x1B,0x7B,0xA1};  //Endereço MAC do escravo 
char msn[20] = "";                                    //Aponta para localização da mensagem 

void setup() {
  pinMode(button,INPUT_PULLUP);                       //Button como input
  WiFi.mode(WIFI_STA);                                //Configura esp como estação 
  esp_now_init();                                     //Inicia o Esp-Now
  esp_now_peer_info_t slaveInfo;                      //Estrutura de configuração 
  memcpy(slaveInfo.peer_addr,slave,6);                //endereço do escravo
  slaveInfo.channel = 0;                              //Channel de comunicação 
  slaveInfo.encrypt = false;                          //Desabilita a criptografia 
  esp_now_add_peer(&slaveInfo);                       //Registro das informações de slaveInfo
}

void loop() {
  if(digitalRead(button)==LOW){
    strcpy(msn, "Toggle");                              //Se button for pressionado, aponte "toggle"
    esp_now_send(slave,(uint8_t *)msn, strlen(msn));    //Envia a mensagem
    delay(300);                                         //Delay
    }  
}