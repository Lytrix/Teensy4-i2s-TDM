#include "AudioConfig.h"
#include "input_i2s_tdm.h"
#include "output_i2s_tdm.h"
#include <Wire.h>
#include <SPI.h>
#include <arm_math.h>
#include "control_AK4619VN.h"
#include <FreqCount.h>
#include "i2s_timers.h"

AK4619VN codec(&Wire, AK4619VN_ADDR);
AudioOutputI2S audioOutputI2S;
AudioInputI2S audioInputI2S;

int acc = 0;

void processAudio(int32_t** inputs, int32_t** outputs)
{
  for (size_t i = 0; i < AUDIO_BLOCK_SAMPLES; i++)
  {
    outputs[0][i] = -inputs[0][i];
    outputs[1][i] = -inputs[1][i];
    
    if (CHANNELS > 2) {
      outputs[2][i] = -inputs[2][i];
      outputs[3][i] = -inputs[3][i];
    }  
  }
}

// Use pin9 to test Clock Frequencies on pin 20/21/23
// https://www.pjrc.com/teensy/td_libs_FreqCount.html    
void debugClockFreq() {
  if (FreqCount.available()) {
    unsigned long count = FreqCount.read();
    Serial.print("Clock Frequency: ");
    Serial.print(count/1000);
    Serial.println("kHz");
  }
}

// Show CPU usage and processing time
void debugCPU() {
  delay(1000);
  float avg = Timers::GetPeak(Timers::TIMER_TOTAL);
  float period = Timers::GetAvgPeriod();
  float percent = avg / period * 100;
  Serial.print("CPU Usage: ");
  Serial.print(percent, 4);
  Serial.print("%");
  Serial.print(" -- Processing period: ");
  Serial.print(period/1000, 3);
  Serial.println("ms");
}

void setup(void)
{
  Serial.begin(9600);

  // Assign the callback function
  i2sAudioCallback = processAudio;

  // Start the I2S interrupts
  audioOutputI2S.begin();
  audioInputI2S.begin();
  
  // Enable the Audio codec
  codec.init();
    
  //Update Time in microseconds
  FreqCount.begin(1000000); 
}

void loop(void)
{
  debugCPU();
  debugClockFreq();
}
