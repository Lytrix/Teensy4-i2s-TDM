#include "Teensy4i2s.h"
#include <Wire.h>
#include <SPI.h>
#include <arm_math.h>
#include "control_AK4619VN.h"
#include <FreqCount.h>
#include "output_i2s.h"

//AudioControlSGTL5000 audioShield;

int acc = 0;

void processAudio(int32_t** inputs, int32_t** outputs)
{
  for (size_t i = 0; i < AUDIO_BLOCK_SAMPLES; i++)
  {
    // use can use regular sinf() as well, but it's highly recommended 
    // to use these optimised arm-specific functions whenever possible
    int sig = (int)(arm_sin_f32(acc * 0.01f * 2.0f * M_PI) * 1000000000.0f);
    
    outputs[0][i] = inputs[0][i] + sig;
    outputs[1][i] = inputs[1][i] + sig;
    outputs[2][i] = inputs[2][i] + sig;
    outputs[3][i] = inputs[3][i] + sig;
    acc++;
    if (acc >= 100)
      acc -= 100;
  }
}
AK4619VN codec(&Wire, AK4619VN_ADDR);

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
  delay(200);
  
  codec.begin(18, 19); // SDA, SCL for Teensy 4.1
  /*    pin  6 : SDIN
        pin  7 : SDOUT
        pin 23 : MCLK
        pin 21 : BICK
        pin 20 : SYNC */
  
  //Set CODEC to reset state for initialization
  uint8_t error = 0;
  error = codec.setRstState(true); 
  if(error){
    Serial.println("Unable to set codec reset state");
  }
  //Enable PWR of DAC1&2 and ADC1&2
  error = codec.pwrMgm(true, true, true, true); 
  if(error){
    Serial.println("Unable to set codec PWR");
  }
  // Set Slot start (LR (false) or Slot length) and Slot Length for DAC 1&2 and ADC1&2
  error = codec.audioFormatSlotLen(AK4619VN::AK_SLOT, AK4619VN::AK_32BIT, AK4619VN::AK_24BIT);
  if(error){
    Serial.println("Unable to set slot length.");
  }
  // Set Mode I2S/TDM, BICK Edge falling/rising, SDOut speed slow/fast
  //error = codec.audioFormatMode(AK4619VN::AK_I2S_STEREO, false, false);
  //error = codec.audioFormatMode(AK4619VN::AK_MSB_STEREO, false, false);
  //error = codec.audioFormatMode(AK4619VN::AK_TDM256_I2S_32B, false, false);
  error = codec.audioFormatMode(AK4619VN::AK_TDM128_I2S_32B, true, false);
  //error = codec.audioFormatMode(AK4619VN::AK_TDM128_MSB_32B, true, false);
  
  if(error){
    Serial.println("Unable to set audio format mode.");
  }  
  // Set sample rate to 96kHz
  //error = codec.sysClkSet(AK4619VN::AK_256FS_96KS);
  //error = codec.sysClkSet(AK4619VN::AK_256FS_8_48KS);
  error = codec.sysClkSet(AK4619VN::AK_128FS_192KS);
  if(error){
    Serial.println("Unable to set system clock mode.");
  }
  // Set all 4 ADC gains to 0dB
  error = codec.micGain(AK4619VN::AK_MIC_GAIN_0DB, AK4619VN::AK_MIC_GAIN_0DB, AK4619VN::AK_MIC_GAIN_0DB, AK4619VN::AK_MIC_GAIN_0DB); 
  if(error){
    Serial.println("Unable to set codec mic input gain.");
  }
  // // Set all 4 ADC digital volume to 0dB
  // error = codec.inputGain(AK4619VN::AK_IN_GAIN_0DB, AK4619VN::AK_IN_GAIN_0DB, AK4619VN::AK_IN_GAIN_0DB, AK4619VN::AK_IN_GAIN_0DB); 
  // if(error){
  //   Serial.println("Unable to set codec digital input input gain.");
  // }
  //Set all DAC1 LR gains to 0dB
  error = codec.outputGain(false, AK4619VN::AK_DAC1B, AK4619VN::AK_OUT_GAIN_0DB); 
  if(error){
    Serial.println("Unable to set DAC1 gain.");
  }
  //Set all DAC2 LR gains to 0dB
  error = codec.outputGain(false, AK4619VN::AK_DAC2B, AK4619VN::AK_OUT_GAIN_0DB);
  if(error){ 
    Serial.println("Unable to set DAC2 gain.");
  }
  //Set Input config to Single ended 1 on both ADCs
  error = codec.inputConf(AK4619VN::AK_IN_SE1, AK4619VN::AK_IN_SE1, AK4619VN::AK_IN_SE1, AK4619VN::AK_IN_SE1);
  if(error){
    Serial.println("Unable to set DAC input configuration.");
  }
  //DAC2 to SDOUT2, DAC1 to SDOUT1
  error = codec.outputConf(AK4619VN::AK_OUT_SDIN1, AK4619VN::AK_OUT_SDIN1); 
  //error = codec.outputConf(AK4619VN::AK_OUT_SDOUT1, AK4619VN::AK_OUT_SDOUT1); 
  if(error){
    Serial.println("Unable to set DAC input configuration.");
  }
  
  //Release Reset state
  error = codec.setRstState(false); 
  if(error){
    Serial.println("Unable to clear codec reset state.");
  }
  delay(100);
  Serial.println("Audio Codec Setup done.");
  
  //Verify settings
  codec.printRegs(0x0, 21);

  // need to wait a bit before configuring codec, otherwise something weird happens and there's no output...
  delay(1000); 
  // Enable the audio CODEC and set the volume
  //audioShield.enable();
  //audioShield.volume(0.5);
  
  //Update Time in microseconds
  FreqCount.begin(1000000); 
}

void loop(void)
{
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
  if (FreqCount.available()) {
    unsigned long count = FreqCount.read();
    Serial.println(count);
  }
}
