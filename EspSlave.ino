#include <WiFi.h>
#include <esp_now.h>
#define Led 2
bool ledstatus = 1;
char MSN[20] = "";
//
void receive(const esp_now_recv_info *info, const uint8_t *msn, int len){
  for(int i=0;i<len;i++){
    MSN[i]=(char)msn[i];
  }
  if(strcmp(MSN, "Toggle") == 0){
    ledstatus=!ledstatus;
    digitalWrite(Led,ledstatus);
  }
}

void setup() {
  pinMode(Led,OUTPUT);
  WiFi.mode(WIFI_STA);
  esp_now_init();
  esp_now_register_recv_cb(receive);
}


void loop() {
 //Nothing here...

}
