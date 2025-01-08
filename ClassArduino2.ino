class Relay{
  private:
    int pin;
  public:
    Relay(int Pino)
    {
      pin=Pino;
      pinMode(pin,OUTPUT);
    }
    void RelayON(int Time){
      digitalWrite(pin,HIGH);
      delay(Time);
      digitalWrite(pin,LOW);
    }

};
Relay Rele01(23);
Relay Rele02(19); 
void setup() {
  // put your setup code here, to run once:

}

void loop() {
  // put your main code here, to run repeatedly:
Rele01.RelayON(5000);
Rele02.RelayON(2000);
}
