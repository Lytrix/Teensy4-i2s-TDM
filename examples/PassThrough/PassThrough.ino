#include "Teensy4i2s.h"
#include <Wire.h>
#include <SPI.h>
#include <arm_math.h>
#include "control_AK4619VN.h"
#include <FreqCount.h>
#include "output_i2s_tdm.h"

//AudioControlSGTL5000 audioShield;
AK4619VN codec(&Wire, AK4619VN_ADDR);

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
  InitI2s();
  // Enable the Audio codec
  pinMode(22, OUTPUT); //PWN on
  digitalWrite(22, HIGH);
  delay(1000);
  
  codec.begin(18, 19); // SDA, SCL for Teensy 4.1
  /*    pin  6 : SDIN
        pin  7 : SDOUT
        pin 23 : MCLK
        pin 21 : BICK
        pin 20 : SYNC */
  
    //Set CODEC to reset state for initialization
  codec.setRstState(true); 
  
  //Enable PWR of DAC1&2 and ADC1&2
  codec.pwrMgm(true, true, true, true); 

  // Set Slot start (LR (false) or Slot length), BICK Edge falling/rising, SDOut speed slow/fast
  codec.audioFormatMode(AK4619VN::AK_TDM128_I2S_32B, false, false);
  // codec.audioFormatMode(AK4619VN::AK_I2S_STEREO, false, false);
  // codec.audioFormatMode(AK4619VN::AK_MSB_STEREO, false, false);
  // codec.audioFormatMode(AK4619VN::AK_TDM256_I2S_32B, false, false);
  // codec.audioFormatMode(AK4619VN::AK_TDM128_MSB_32B, true, false);
  
  // Set TDM mode and Slot Length for DAC 1&2 and ADC1&2
  codec.audioFormatSlotLen(AK4619VN::AK_SLOT, AK4619VN::AK_32BIT, AK4619VN::AK_24BIT);
  
  // Set sample rate to 192kHz
  codec.sysClkSet(AK4619VN::AK_128FS_192KS);
  // codec.sysClkSet(AK4619VN::AK_256FS_96KS);
  // codec.sysClkSet(AK4619VN::AK_256FS_8_48KS);
  
  // Set all 4 ADC gains to 0dB
  codec.micGain(AK4619VN::AK_MIC_GAIN_0DB, AK4619VN::AK_MIC_GAIN_0DB, AK4619VN::AK_MIC_GAIN_0DB, AK4619VN::AK_MIC_GAIN_0DB); 
 
  // // Set all 4 ADC digital volume to 0dB
  // codec.inputGain(AK4619VN::AK_IN_GAIN_0DB, AK4619VN::AK_IN_GAIN_0DB, AK4619VN::AK_IN_GAIN_0DB, AK4619VN::AK_IN_GAIN_0DB); 

  //Set all DAC1 LR gains to 0dB
  codec.outputGain(false, AK4619VN::AK_DAC1B, AK4619VN::AK_OUT_GAIN_0DB); 
  
  //Set all DAC2 LR gains to 0dB
  codec.outputGain(false, AK4619VN::AK_DAC2B, AK4619VN::AK_OUT_GAIN_0DB);

  //Set Input config to Single ended 1 on both ADCs
  codec.inputConf(AK4619VN::AK_IN_SE1, AK4619VN::AK_IN_SE1, AK4619VN::AK_IN_SE1, AK4619VN::AK_IN_SE1);
  
  //DAC2 to SDOUT2, DAC1 to SDOUT1
  codec.outputConf(AK4619VN::AK_OUT_SDIN2, AK4619VN::AK_OUT_SDIN1); 
  // codec.outputConf(AK4619VN::AK_OUT_SDOUT1, AK4619VN::AK_OUT_SDOUT1); 

  //Release Reset state
  codec.setRstState(false); 
  
  delay(500);
  Serial.println("Audio Codec Setup completed:");
  delay(500);
  //Verify settings
  codec.printRegs(0x0, 21);

  // need to wait a bit before configuring codec, otherwise something weird happens and there's no output...
  // delay(1000); 
  // Enable the audio CODEC and set the volume
  //audioShield.enable();
  //audioShield.volume(0.5);
  
  //Update Time in microseconds
  FreqCount.begin(1000000); 
}

void loop(void)
{
  debugCPU();
  debugClockFreq();
}
