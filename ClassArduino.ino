//====================class controle LED======================//
class LED {
  private:
    int pino;
  public:
    LED(int PinoLED)
    {
     pino=PinoLED;
     pinMode(pino,OUTPUT); 
    }

    void LEDOn(float Time){
      digitalWrite(pino,HIGH);
    }
    void LEDOff(){
      digitalWrite(pino,LOW);
    }
    void LEDToggle(float Time){
      digitalWrite(pino,HIGH);
      delay(Time);
      digitalWrite(pino,LOW);
      delay(Time);

    }

};
//===========================================================//

LED led01(12);
LED led02(13);
LED led03(11);

void setup() {
  // put your setup code here, to run once:

}

void loop() {
  // put your main code here, to run repeatedly:
 led01.LEDToggle(2000);
}
