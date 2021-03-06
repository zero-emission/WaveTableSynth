/**
 * Describes the voices
 */
#include "Log.h"   // 4096 // log and antilog table
const uint32_t ISR_RATE = 25000;  // interrupt rate 40000Hz

const uint16_t MAXVOICE = 6;      // 5 

const uint16_t ATTACKTIME = 2;  //  sec.
const uint16_t RELEASETIME = 4; //  sec.
const uint16_t DECAYTIME = 2;   //  sec.

const uint16_t VOICES = 0;
const uint16_t FILTER = 1;

/**
 * ADSR state codes
 */
const uint16_t ATTACK = 1;
const uint16_t DECAY = 2;
const uint16_t SUSTAIN = 3;
const uint16_t RELEASE = 4;
const uint16_t STOP = 0;
const uint16_t STOPPING = 100; 

// only 2 sets of adsr value, one for the VOICE, one for the FILTER
typedef struct {
  uint32_t A_Step;      // attack step 
  uint32_t D_Step;      // decay step
  uint16_t sustainVal;  // sustain value
  uint32_t R_Step;      // release step
 } ADSR_type;


/**
 * 2 sets of adsr values 
 */
ADSR_type adsrValues[2]; 

/**
 * Per voice we have a set of adsr values actually running
 */
typedef struct {
  unsigned long ATableIndex;
  uint32_t ADSR_mode;
  uint16_t output;
  uint16_t lastVal; // the last value, when note off came
} ADSR_OfVoice_type;

uint16_t filter = MAXVOICE;
ADSR_OfVoice_type vAdsr[MAXVOICE+1]; // MAXVOICE is the filter adsr index (last in the array)

bool doMid;

unsigned long VirtualTableIndexMax; // here the log-tables overflows

/**
 * change the state of the adsr
 */
void setADSR_mode(int16_t m, int16_t channel) {
  vAdsr[channel].ADSR_mode = m;
}
/**
 * Initialize adsr
 */
void initADSR(bool m) {
  doMid = m;
  for (int16_t n = 0; n < MAXVOICE; n++) {
	  setADSR_mode(STOP, n);
    vAdsr[n].output = 0;
  }
  // Init ADSR Value sets
  for (int n = 0; n < 2; n++) {
    adsrValues[n].A_Step = 0;
    adsrValues[n].D_Step = 0;
    adsrValues[n].sustainVal = 0;
    adsrValues[n].R_Step = 0;
  }
   VirtualTableIndexMax = (LogLength << 10) - 1; 
}

/**
 * Handle the gate-on signal (start attack mode)
 */
void setGateOn(int16_t channel) {
  //Serial.print(channel);
  //Serial.println(" setGateOn()");
  if (!doMid){
    Serial.print(channel);
    Serial.println(" Att");
  }
  vAdsr[channel].ATableIndex = 0;   // reset table counter 
  vAdsr[MAXVOICE].ATableIndex = 0;  // reset table counter for filter adsr
  setADSR_mode(ATTACK, channel);    // start attack
  setADSR_mode(ATTACK, filter);     // just ad mode!
  
}

/**
 * handle the gate-off signal (start release mode)
 */
void setGateOff(int16_t channel) {
  vAdsr[channel].lastVal = vAdsr[channel].output; // the initial amplitude value for the release mode
  vAdsr[channel].ATableIndex = 0;
  setADSR_mode(RELEASE, channel); // start release
}

/**
 * Calculate the step with which we travel in the adsr tables
 * Analog value goes from 0-4095
 * The log-table goes from 0-4095, the values in there from 0-255
 */
uint32_t calculateScale(uint16_t scaleValue, uint16_t time) {
  unsigned long z = (LogLength * LogLength) / scaleValue; 
  z = z << 10; // *1024 to increase precision of table pointer calculation
  unsigned long n = time * ISR_RATE;
  z /= n;
  return z;
}

/**
 * Set attack rate (0-4sec)
 * The table goes from 0 to 4096, attack value determines
 * how fast we travel trough the table.
 * In the end we always should reach the table-end (4096)
 * The Formula for Increments/ISRTick is:
 * Increment/Tick  z = Tablelength^2 / (ScaleValue * TableTime * IRSRate)
 * A scale of 0 = instant attack, 4096 = longest Attack Time
 * @param scaleValue the new attack value
 * @param channel voices or filter setting
 */
void setAttackScale(uint16_t scaleValue, int16_t channel) {
  //Serial.print("Attack value=");
  //Serial.println(scaleValue);
   if (scaleValue < 5)
    scaleValue = 5;
    
  adsrValues[channel].A_Step = calculateScale(scaleValue, ATTACKTIME);
}

/**
 * Set attack rate (0-4sec)
 * @param f the new deacy value
 * @param channel voices or filter setting
 */
void setDecayScale(uint16_t scaleValue, int16_t channel) {
  if (scaleValue <= 5)
    scaleValue = 5;
  adsrValues[channel].D_Step = calculateScale(scaleValue, DECAYTIME);
}

/**
 * Set attack rate (0-4sec)
 * @param f the new release value
 * @param channel voices or filter setting
 */
void setReleaseScale(uint16_t scaleValue, int16_t channel) {
  if (scaleValue < 5)
    scaleValue = 5;
  adsrValues[channel].R_Step = calculateScale(scaleValue, RELEASETIME);
}

/**
 * Set Sustain Value
 * @param f the new release
 * @param channel voices or filter setting
 */
void setSustainValue(uint16_t f, int16_t channel) {
  adsrValues[channel].sustainVal = f;
}

uint16_t getTableIndex(int16_t channel) {
  return uint16_t (vAdsr[channel].ATableIndex >> 10); // remember: the tableIndex was multiplied by 1024
}

/**
 * read the actual attack value
 * @param channel index of channel
 */
 uint16_t getAttackVal(int16_t channel) {
    return getAttack(getTableIndex(channel));
 }
 
 uint16_t getDecayVal(int16_t channel) {
    return getDecay(getTableIndex(channel));
 }

/**
 * Increase active ADSR-pointers
 * and get the acutal active adsr output value
 * This is called 40000 times a second
 * make channel=filter only do AD, not SR
 * @return the new adsr output value
 */
void addADSRStep(int n, int mde) {
  switch (vAdsr[n].ADSR_mode) {
    case ATTACK:
        vAdsr[n].ATableIndex += adsrValues[mde].A_Step; 
        if ( vAdsr[n].ATableIndex >= VirtualTableIndexMax) {
          // end of attack
          
          if (!doMid){
            Serial.print(n);
            Serial.println(" EoA");
          }
          //Serial.println(mil);
          /*if (!doMid) {
            Serial.print(", D ");
            Serial.println(eoa);
          }
          */
          setADSR_mode(DECAY, n); // switch to decay mode, stop for test
          vAdsr[n].ATableIndex = 0;
        }
        else {
          vAdsr[n].output = getAttackVal(n);
          //Serial.print("Attack val=");
          //Serial.println( vAdsr[n].output);
          /*Serial.print("outVal=");
          Serial.print(outVal);*/
        }
    break;
    case DECAY:
        vAdsr[n].ATableIndex += adsrValues[mde].D_Step; 
        if ( vAdsr[n].ATableIndex >= VirtualTableIndexMax) {
          // end of Decay
          
          if (!doMid){
            Serial.print(n);
            Serial.println(" EoD");
            //Serial.println(" S");
          }
         // Serial.println(mil);
          setADSR_mode(SUSTAIN, n); // switch to Sustain mode
          vAdsr[n].ATableIndex = 0;
        }
        else {
          // Table goes from 4096 to 0
          vAdsr[n].output = getDecayVal(n);
          //Serial.print("Decay val=");
          //Serial.println( vAdsr[n].output);
          if ( vAdsr[n].output <= adsrValues[mde].sustainVal) {
            
            if (!doMid) {
              Serial.print(n);
              Serial.println(" EoD");
              //Serial.println(", S");
            }
            // Serial.println(mil);
             setADSR_mode(SUSTAIN, n); // switch to Sustain mode
             vAdsr[n].ATableIndex = 0;
          }
        }
    break;
    case SUSTAIN:
      vAdsr[n].output = adsrValues[mde].sustainVal;
    break;
    case RELEASE:
        vAdsr[n].ATableIndex += adsrValues[mde].R_Step; 
        if ( vAdsr[n].ATableIndex >= VirtualTableIndexMax) {
          // end of Release
          vAdsr[n].ATableIndex = 0;
          vAdsr[n].ADSR_mode = STOPPING; // switch off
          vAdsr[n].output = 0;
          if (!doMid) {
            Serial.print(n);
            Serial.println(" EoR");
          }
        }
        else {
          // Table goes from 4096 to 0, sustain value is the starting point
          uint16_t v = getDecayVal(n);   // table value (4096-0)
          v = v * vAdsr[n].lastVal / LogLength;    // weighted with the sustain value
          if ( v <= 1){
            // end of Release
            if (!doMid) {
              Serial.print(n);
              Serial.println(" EoR");
            }
            vAdsr[n].ADSR_mode = STOPPING; // switch off
            vAdsr[n].ATableIndex = 0;
            v = 0;
            //digitalWrite(PC13, 0); // turn on
          }
         /* Serial.print("last= ");
          Serial.print(vAdsr[n].lastVal);
          Serial.print(" Re ");
          Serial.println(v);
          */
          vAdsr[n].output = v;
        }
    break;
  }

}
   
