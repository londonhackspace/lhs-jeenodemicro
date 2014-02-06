#include <JeeLib.h>
#include <avr/sleep.h>

volatile bool adcDone;

// for low-noise/-power ADC readouts, we'll use ADC completion interrupts
ISR(ADC_vect) { adcDone = true; }

/* todo:

Vcc reading via bandgap thingy
+v on the LDR shoud be a digital out set high, we can set it to input to avoid wasting power.
switch the RFM12 off when we're not using it.
read Vcc and LDR while transmitting?

https://github.com/mharizanov/Funky-Sensor/blob/master/examples/Funky_InternalTemperatureSensor/Funky_InternalTemperatureSensor.ino

*/

#define LDR 3 // ADC7

typedef struct { int temp; int ldr; int bat_vcc; int vcc;} PayloadTX; // 
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

static byte vccRead (byte count =4) {
  set_sleep_mode(SLEEP_MODE_ADC);
  // use VCC as AREF and internal bandgap as input
  ADMUX = 33;

  bitSet(ADCSRA, ADIE);
  while (count-- > 0) {
    adcDone = false;
    while (!adcDone)
      sleep_mode();
  }
  bitClear(ADCSRA, ADIE);
  // convert ADC readings to fit in one byte, i.e. 20 mV steps:
  // 1.0V = 0, 1.8V = 40, 3.3V = 115, 5.0V = 200, 6.0V = 250
  return (55U * 1024U) / (ADC + 1) - 50;
}

static byte bat_vccRead (byte count =4) {
  set_sleep_mode(SLEEP_MODE_ADC);
  ADMUX = 32;

  bitSet(ADCSRA, ADIE);
  while (count-- > 0) {
    adcDone = false;
    while (!adcDone)
      sleep_mode();
  }
  bitClear(ADCSRA, ADIE);
  // convert ADC readings to fit in one byte, i.e. 20 mV steps:
  // 1.0V = 0, 1.8V = 40, 3.3V = 115, 5.0V = 200, 6.0V = 250
  return (55U * 1024U) / (ADC + 1) - 50;
}

void setup () {
  cli();
  CLKPR = bit(CLKPCE);
  CLKPR = 0; // div 1, i.e. speed up to 8 MHz
  sei();

  // switch the rfm12 on
  bitSet(DDRB, 0); 
  bitClear(PORTB, 0);

  // need to enable the pull-up to get a voltage drop over the LDR
  pinMode(14+LDR, INPUT);
  digitalWrite(14+LDR, 1); // pull-up
  analogReference(INTERNAL);

    // and give it a chance to wake up, not sure if needed.
    Sleepy::loseSomeTime(200);

    rf12_initialize(17, RF12_868MHZ, 210); // group 210, node id 17
//    rf12_easyInit(5); // ???
    // see http://tools.jeelabs.org/rfm12b.html
    rf12_control(0xC040); // set low-battery level to 2.2V i.s.o. 3.1V
    rf12_sleep(RF12_SLEEP);

    // init our payload
    payload.temp = 0;
    payload.ldr = 0;
    payload.bat_vcc = 0;
    payload.vcc = 0;
                     
    PRR = bit(PRTIM1); // only keep timer 0 going
}

void read_ldr(void) {
  unsigned int ldrReading;
  
  Sleepy::loseSomeTime(16); // Allow 10ms for the sensor to be ready

  analogRead(LDR); // throw away the first reading
  for(int i = 0; i < 10 ; i++) // take 10 more readings
  {
    ldrReading += analogRead(LDR); // accumulate readings
  }
  payload.ldr = ldrReading / 10 ; // calculate the average
}

void loop() {

  bitClear(PRR, PRADC); // power up the ADC
  ADCSRA |= bit(ADEN); // enable the ADC
  
  read_ldr();
  
  int_sensor_init();
  payload.temp = in_c() * 100; // Convert temperature to an integer, reversed at receiving end

  payload.bat_vcc = bat_vccRead(); //analogRead(0) >> 2;
  payload.vcc = vccRead();

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

