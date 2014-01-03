#include <JeeLib.h>

static int led = 10;
int led_status = 0;

/* todo:

Vcc reading via bandgap thingy
+v on the LDR shoud be a digital out set high, we can set it to input to avoid wasting power.
switch the RFM12 off when we're not using it.
read Vcc and LDR while transmitting?

https://github.com/mharizanov/Funky-Sensor/blob/master/examples/Funky_InternalTemperatureSensor/Funky_InternalTemperatureSensor.ino

*/


static int ldrpin = 9; // ADC1

typedef struct { int temp; int ldr; int ver;} PayloadTX; // 
PayloadTX payload;

ISR(WDT_vect) { Sleepy::watchdogEvent(); } // interrupt handler for JeeLabs Sleepy power saving

#define TEMPERATURE_ADJUSTMENT 26-43
#define EXTREMES_RATIO 5
#define MAXINT 32767
#define MININT -32767

int offset=TEMPERATURE_ADJUSTMENT;
float coefficient=1;
int readings[30];
int pos=0;

void setup () {
    pinMode(led, OUTPUT);

    // switch the rfm12 on
    bitSet(DDRB, 0); 
    bitClear(PORTB, 0);

    // and give it a chance to wake up, not sure if needed.
    Sleepy::loseSomeTime(200);

    rf12_initialize(17, RF12_868MHZ, 210); // group 210, node id 17
    rf12_easyInit(5); // ???
    // see http://tools.jeelabs.org/rfm12b.html
    rf12_control(0xC040); // set low-battery level to 2.2V i.s.o. 3.1V
    rf12_sleep(RF12_SLEEP);

    // init our payload
    payload.temp = 0;
    payload.ldr = 0;
    payload.ver = 6; // increase ver before you compile and upload new code
                     // so you can be sure that the version thats running is the one you want.
                     
    PRR = bit(PRTIM1); // only keep timer 0 going
}

void loop() {
  unsigned int ldrReading;
  digitalWrite(led, led_status);
  if (led_status) {
     led_status = 0;
  } else {
    led_status = 1;
  }

  bitClear(PRR, PRADC); // power up the ADC
  ADCSRA |= bit(ADEN); // enable the ADC
  Sleepy::loseSomeTime(16); // Allow 10ms for the sensor to be ready



  analogRead(ldrpin); // throw away the first reading
  for(int i = 0; i < 10 ; i++) // take 10 more readings
  {
    ldrReading += analogRead(ldrpin); // accumulate readings
  }
  payload.ldr = ldrReading / 10 ; // calculate the average
  
  int_sensor_init();
  payload.temp = in_c() * 100; // Convert temperature to an integer, reversed at receiving end

//  temptx.supplyV = readVcc(); // Get supply voltage

  rf12_sleep(RF12_WAKEUP);
  // if ready to send + exit loop if it gets stuck as it seems too
  int i = 0; while (!rf12_canSend() && i<10) {rf12_recvDone(); i++;}
  rf12_sendStart(0, &payload, sizeof payload);
  // set the sync mode to 2 if the fuses are still the Arduino default
  // mode 3 (full powerdown) can only be used with 258 CK startup fuses
  // ^-- no idea.
  rf12_sendWait(2);
  rf12_sleep(RF12_SLEEP);


  ADCSRA &= ~ bit(ADEN); // disable the ADC
  bitSet(PRR, PRADC); // power down the ADC


  Sleepy::loseSomeTime(2500);
}

void int_sensor_init() {
  //analogReference( INTERNAL1V1 );
  // Configure ADMUX

  ADMUX = B00100010; // Select temperature sensor
  ADMUX &= ~_BV( ADLAR ); // Right-adjust result
  ADMUX |= _BV( REFS1 ); // Set Ref voltage
  ADMUX &= ~( _BV( REFS0 ) ); // to 1.1V
  // Configure ADCSRA
  ADCSRA &= ~( _BV( ADATE ) |_BV( ADIE ) ); // Disable autotrigger, Disable Interrupt
  ADCSRA |= _BV(ADEN); // Enable ADC
  ADCSRA |= _BV(ADSC); // Start first conversion
  // Seed samples
  int raw_temp;
  while( ( ( raw_temp = raw() ) < 0 ) );
  for( int i = 0; i < 30; i++ ) {
    readings[i] = raw_temp;
  }
}

int in_lsb() {
  int readings_dup[30];
  int raw_temp;
  // remember the sample
  if( ( raw_temp = raw() ) > 0 ) {
    readings[pos] = raw_temp;
    pos++;
    pos %= 30;
  }
  // copy the samples
  for( int i = 0; i < 30; i++ ) {
    readings_dup[i] = readings[i];
  }
  // bubble extremes to the ends of the array
  int extremes_count = 6;
  int swap;
  for( int i = 0; i < extremes_count; ++i ) { // percent of iterations of bubble sort on small N works faster than Q-sort
    for( int j = 0;j<29;j++ ) {
      if( readings_dup[i] > readings_dup[i+1] ) {
        swap = readings_dup[i];
        readings_dup[i] = readings_dup[i+1];
        readings_dup[i+1] = swap;
      }
    }
  }
  // average the middle of the array
  int sum_temp = 0;
  for( int i = extremes_count; i < 30 - extremes_count; i++ ) {
    sum_temp += readings_dup[i];
  }
  return sum_temp / ( 30 - extremes_count * 2 );
}


int in_c() {
  return in_k() - 273;
}

int in_k() {
  return in_lsb() + offset; // for simplicty I'm using k=1, use the next line if you want K!=1.0
  //return (int)( in_lsb() * coefficient ) + offset;
}

int raw() {
  if( ADCSRA & _BV( ADSC ) ) {
    return -1;
  } else {
    int ret = ADCL | ( ADCH << 8 ); // Get the previous conversion result
    ADCSRA |= _BV(ADSC); // Start new conversion
    return ret;
  }
}
