#include <JeeLib.h>

static int led = 10;
int led_status = 0;

/* todo:

Vcc reading via bandgap thingy
+v on the LDR shoud be a digital out set high, we can set it to input to avoid wasting power.
sleeping rather than delay()
switch the RFM12 off when we're not using it.
read Vcc and LDR while transmitting?

*/


static int ldrpin = 9; // ADC1

typedef struct { int counter; int ldr; int ver;} PayloadTX; // 
PayloadTX payload;

void setup () {
    pinMode(led, OUTPUT);

    // switch the rfm12 on
    bitSet(DDRB, 0); 
    bitClear(PORTB, 0);

    // and give it a chance to wake up, not sure if needed.
    delay(200);

    rf12_initialize(17, RF12_868MHZ, 210); // group 210, node id 17
    rf12_easyInit(5); // ???
    // see http://tools.jeelabs.org/rfm12b.html
    rf12_control(0xC040); // set low-battery level to 2.2V i.s.o. 3.1V
    rf12_sleep(RF12_SLEEP);

    // init our payload
    payload.counter = 0;
    payload.ldr = 0;
    payload.ver = 3; // increase ver before you compile and upload new code
                     // so you can be sure that the version thats running is the one you want.
}

void loop() {
  digitalWrite(led, led_status);
  if (led_status) {
     led_status = 0;
  } else {
    led_status = 1;
  }

  payload.ldr = analogRead(ldrpin);

  rf12_sleep(RF12_WAKEUP);
  // if ready to send + exit loop if it gets stuck as it seems too
  int i = 0; while (!rf12_canSend() && i<10) {rf12_recvDone(); i++;}
  rf12_sendStart(0, &payload, sizeof payload);
  // set the sync mode to 2 if the fuses are still the Arduino default
  // mode 3 (full powerdown) can only be used with 258 CK startup fuses
  // ^-- no idea.
  rf12_sendWait(2);
  rf12_sleep(RF12_SLEEP);
  payload.counter++;

  delay(1500);
}

