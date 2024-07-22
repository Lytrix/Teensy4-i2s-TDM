#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <arm_math.h>
#include <FreqCount.h>
#include "DMAChannel.h"
#include "AudioConfig.h"
#include "input_i2s_tdm.h"
#include "output_i2s_tdm.h"
#include "control_AK4619VN.h"
#include "i2s_timers.h"
#include "WavWriter.h"

WavWriter<32768> writer;

// Setup classes for Audio codec and I2S
AK4619VN codec(&Wire, AK4619VN_ADDR);
AudioOutputI2S audioOutputI2S;
AudioInputI2S audioInputI2S;

// Use these with the Teensy 3.5 & 3.6 & 4.1 SD card
#define SDCARD_CS_PIN    BUILTIN_SDCARD
#define SDCARD_MOSI_PIN  11  // not actually used
#define SDCARD_SCK_PIN   13  // not actually used


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

void recordAudio(int32_t** inputs, int32_t** outputs)
{
  // Test input
  //for(int i=0;i < 4;i++){
  //   //Serial.println(sizeof(inputs[0]));
  //   Serial.println(inputs[0][i]);
  // }
  //Read samples:
  for (size_t i = 0; i < AUDIO_BLOCK_SAMPLES; i++)
  {
    writer.Sample((int32_t *)inputs[0][i]);
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
  Serial.println("Started setup");

  // Initialize the SD card
  SPI.setMOSI(SDCARD_MOSI_PIN);
  SPI.setSCK(SDCARD_SCK_PIN);
  if (!(SD.begin(SDCARD_CS_PIN))) {
    // stop here, but print a message repetitively
    while (1) {
      Serial.println("Unable to access the SD card");
      delay(500);
    }
  } 

  // Start the I2S interrupts
  audioOutputI2S.begin();
  audioInputI2S.begin();

  // Start the Codec
  codec.init();
  // Assign the callback function
  i2sAudioCallback = processAudio;
 
  // need to wait a bit before configuring codec, otherwise something weird happens and there's no output...
  // delay(1000); 
  // Enable the audio CODEC and set the volume
  //audioShield.enable();
  //audioShield.volume(0.5);
  
  //Update Time in microseconds
  FreqCount.begin(1000000); 
  i2sAudioCallback = recordAudio;
  writer.WavInit();
  writer.OpenFile("FileName.wav");
}

unsigned long myTime;

byte modeSet = 1;

void loop(void)
{
  //debugCPU();
  //debugClockFreq();
  myTime = millis();
    if (myTime < 10000) { 
        writer.Write();
    }
    else if (myTime == 10000) {
       writer.SaveFile();
    } else {
      if(modeSet){
        Serial.println("Completed");
        modeSet--;
      }
      
    }
}
