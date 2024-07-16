#include "control_AK4619VN.h"
#include <Wire.h>

AK4619VN::AK4619VN(TwoWire *i2c, uint8_t i2cAddress) {
  if(i2c == NULL){
    while(1);
  }
  
  m_i2c = i2c;
  m_addr = i2cAddress;
}

void AK4619VN::begin(uint8_t SDA, uint8_t SCL ) {
  m_i2c->setSDA(SDA);
  m_i2c->setSCL(SCL);
  m_i2c->begin();
}

void AK4619VN::begin(void){
  m_i2c->begin();
}

#if not defined ESP_ARDUINO_VERSION_MAJOR
// Write single register
uint8_t AK4619VN::writeReg(uint8_t deviceReg, uint8_t regVal) {
  
  m_i2c->beginTransmission(m_addr);
  m_i2c->write(deviceReg);
  m_i2c->write(regVal);
  return(m_i2c->endTransmission(true)); // Send STOP
}

// Read single register
uint8_t AK4619VN::readReg(uint8_t deviceReg, uint8_t * regVal) {
  
  m_i2c->beginTransmission(m_addr);
  m_i2c->write(deviceReg);
  m_i2c->endTransmission(true);
  
  uint8_t numbytes = 0;
  numbytes = m_i2c->requestFrom(m_addr, (uint8_t)1, (uint8_t)false);
  
  if((bool)numbytes){
    Wire.readBytes(regVal, numbytes);
  }
  
  return(m_i2c->endTransmission(true)); //Send STOP
}

// Read multiple registers
uint8_t AK4619VN::readRegMulti(uint8_t startReg, uint8_t len, uint8_t * vals) {
  
  m_i2c->beginTransmission(m_addr);
  m_i2c->write(startReg);
  m_i2c->endTransmission(false);
  
  uint8_t numbytes = 0;
  numbytes = m_i2c->requestFrom((uint8_t)m_addr, len, (uint8_t)false);
  
  if((bool)numbytes){
    Wire.readBytes(vals, numbytes);
  }
  
  return(m_i2c->endTransmission(true)); //Send STOP
}
#endif

//Set reset state 
/*
 * Use to set CODEC in reset state.
 * true = reset state
 * false = normal operation
 * CODEC should be configured in reset state, 
 * release it after configuration
 */
uint8_t AK4619VN::setRstState(bool state){
  uint8_t regval = 0;
  uint8_t error = 0;
  
  error = readReg(PWRMGM, &regval);
  if(error){
    return error;
  }
  
  regval |= !state;
  
  return (writeReg(PWRMGM, regval));
}

//Power MGM of codec DAC&ADC
uint8_t AK4619VN::pwrMgm(bool ADC2, bool ADC1, bool DAC2, bool DAC1){
  uint8_t regval = 0;
  uint8_t rstState = 0;
  uint8_t error = 0;
  
  regval = (ADC2 << 5 | ADC1 << 4 | DAC2 << 2 | DAC1 << 1);
  
  //Check for RESET state of CODEC
  error = readReg(PWRMGM, &rstState);
  if(error){
    return error;
  }
  
  //   if( !((rstState >> 0) & 1) ){
  //     Serial.println("Codec is in reset state");
  //   }
  
  return (writeReg(PWRMGM, regval));
  
}

//Set audio format, I&O data length and MCLK and sample freq
/*
 * SLOT = 0 - LRCK edge | 1 - Slot length
 *
 * IDL: (Input Data length)
 * 00 - 24bit
 * 01 - 20bit
 * 10 - 16bit
 * 11 - 32bit
 * 
 * ODL: (Output Data length)
 * Same as IDL except
 * 11 - N/A 
 * 
 * FS: (Frequency Sample)
 * bits   MCLK    fs range
 * 000 - 256fs    8 kHz ≦ fs ≦ 48 kHz
 * 001 - 256fs    fs = 96 kHz
 * 010 - 384fs    8 kHz ≦ fs ≦ 48 kHz
 * 011 - 512fs    8 kHz ≦ fs ≦ 48 kHz
 * 1xx - 128fs    fs = 192 kHz
 * ** For all FS BICK in range 32, 48, 64, 128, 256fs
 * ** except 1xx which only allows 128fs
 */

uint8_t AK4619VN::audioFormatSlotLen(slot_start_t SLOT, data_bit_length_t IDL, data_bit_length_t ODL){
  
  uint8_t regval = 0;
  uint8_t error = 0;
  
  error = readReg(AUDFORM2, &regval);
  if(error){
    return error;
  }
  
  uint8_t tempval = (SLOT << 4 | IDL << 2 | ODL);
  
  regval &= 0xF0;
  regval |= tempval;
  
  error = readReg(AUDFORM2, &regval);
  if(error){
    return error;
  }
  
  //return (writeReg(AUDFORM2, 0b00011100));
  return (writeReg(AUDFORM2, tempval));
  
}

//Set audio format mode 
uint8_t AK4619VN::audioFormatMode(audio_iface_format_t FORMAT, bool BICK_RISING, bool SDOUT_FAST_MODE){
  
  uint8_t regval = 0;
  uint8_t error = 0;
  
  error = readReg(AUDFORM1, &regval);
  if(error){
    return error;
  }
  //Clear upper 6 bits, shift FORMAT by 2 and set it
  //regval &= 0x000000000;
  regval = (FORMAT << 2 | BICK_RISING << 1 | SDOUT_FAST_MODE);
  
  // regval1 &= ~(0x08); //Mask 4th bit and NOT it
  // int tempval = (FORMAT & 0x01); // Set tempval to 1st bit of FORMAT
  // regval1 |= (tempval << 3); // Set 4th bit to tempval
  
  error = writeReg(AUDFORM1, regval);
  if(error){
    return error;
  }
  
  return (writeReg(AUDFORM1, regval));
  
}

//  0000 0XXX SYSCLKSET 5 options 
uint8_t AK4619VN::sysClkSet(clk_fs_t FS){
  
  uint8_t regval = 0;
  uint8_t error = 0;
  
  regval = FS;

  error = writeReg(SYSCLKSET, regval);
  if(error){
    return error;
  }
  
  error = readReg(AUDFORM1, &regval);
  if(error){
    return error;
  }
  
  return (writeReg(AUDFORM1, regval));
}

// Set timing digital vollume, ADC mutes and HPF filter
/* atspad  - time interval digital volume 4/fs 21ms @ 48kHz (default= false) or 16/fs 128ms @ 48kHz (true)
 * ad2mute - ADC2 Digital soft mute, default = false
 * ad1mute - ADC1 Digital soft mute, default = false
 * ad2hpfn - ADC2 DC offset cancel HPF, default enabled = false
 * ad1hpfn - ADC1 DC offset cancel HPF, default enabled = false
 */

uint8_t AK4619VN::muteADCHPF(bool atspad, bool ad2mute, bool ad1mute, bool ad1hpfn, bool ad2hpfn){
  uint8_t regval = 0;
  uint8_t error = 0;
  
  regval = (atspad << 7 | ad2mute << 6 | ad1mute << 5| ad1hpfn << 2 | ad2hpfn << 1);
  
  error = writeReg(ADCMUTEHPF, regval);
  if(error){
    return error;
  }

  return (writeReg(ADCMUTEHPF, regval));
}

uint8_t AK4619VN::micGain(mic_gain_t MGN1L, mic_gain_t MGN1R, mic_gain_t MGN2L, mic_gain_t MGN2R){
  
  uint8_t regval0 = 0;
  uint8_t regval1 = 0;
  uint8_t error = 0;
  
  regval0 = (MGN1L << 4 | MGN1R);
  regval1 = (MGN2L << 4 | MGN2R);
  
  error = writeReg(MICGAIN1, regval0);
  if(error){
    return error;
  }
  
  return (writeReg(MICGAIN2, regval1));
  
}

uint8_t AK4619VN::inputGainChange(bool relative, bool MGN1L, bool MGN1R, bool MGN2L, bool MGN2R, int8_t gain){  
  uint8_t regvals[2] = {0};
  int8_t gains[4] ={0};
  uint8_t error = 0;
  
  if(relative){
    
    error = readRegMulti(MICGAIN1, 2, regvals); //Read input gain regvals
    if(error){
      return error;
    }
    
    //Get current gains in regs
    gains[0] = (regvals[0] >> 4) & 0x0F; // Shift right by 4 bits and mask with 0x0F
    gains[1] = regvals[0] & 0x0F; // Mask with 0x0F to get the lower 4 bits
    gains[2] = (regvals[1] >> 4) & 0x0F;
    gains[3] = regvals[1] & 0x0F;
    
    if(MGN1L) gains[0] += gain;
    if(MGN1R) gains[1] += gain;
    if(MGN2L) gains[2] += gain;
    if(MGN2R) gains[3] += gain;
    
    //Check for min/max
    for(uint8_t i=0; i<=3;i++){
      if (gains[i] < 0)
        gains[i] = 0;
      else if (gains[i] > 11)
        gains[i] = 11;
    }
    
    regvals[0] = ((uint8_t)gains[0] << 4 | (uint8_t)gains[1]);
    regvals[1] = ((uint8_t)gains[2] << 4 | (uint8_t)gains[3]);
    
    error = writeReg(MICGAIN1, regvals[0]);
    if(error){
      return error;
    }
    
    return (writeReg(MICGAIN2, regvals[1]));
    
  }
  else{
    
    if (gain < 0)
      gain = 0;
    else if (gain > 11)
      gain = 11;
    
    if(MGN1L) gains[0] = gain;
    if(MGN1R) gains[1] = gain;
    if(MGN2L) gains[2] = gain;
    if(MGN2R) gains[3] = gain;
    
    regvals[0] = (gains[0] << 4 | gains[1]);
    regvals[1] = (gains[2] << 4 | gains[3]);
    
    error = writeReg(MICGAIN1, regvals[0]);
    if(error){
      return error;
    }
    
    return (writeReg(MICGAIN2, regvals[1]));
    
  }
  return 0;
}

//Set DAC output gain
/*
 * relative - change gain relatively by gainVal
 * channel -  DACxB - change LR
 *            DACxL/RL - change only one
 * gainVal -  0x00 - MAX +24dB
 *            0xFF - Muted
 *            Steps in 0.5dB
 *            0dB = 0x30 - CODEC default
 */

uint8_t AK4619VN::outputGain(bool relative, output_gain_t channel, int16_t gainVal){
  
  uint8_t regvals[4] = {0};
  uint8_t error = 0;
  
  if(relative){
    error = readRegMulti(DAC1LVOL, 4, regvals); //Read output gain regvals
    
    if(error){
      return error;
    }
    
    switch(channel){
      case AK4619VN::AK_DAC1B :
        
        regvals[0] = modifyGainRange(gainVal, regvals[0]);
        regvals[1] = modifyGainRange(gainVal, regvals[1]);
        
        error = writeReg(DAC1LVOL, regvals[0]);
        if(error){
          return error;
        }
        
        error = writeReg(DAC1RVOL, regvals[1]);
        if(error){
          return error;
        }
        break;
        
      case AK4619VN::AK_DAC2B :
        regvals[2] = modifyGainRange(gainVal, regvals[2]);
        regvals[3] = modifyGainRange(gainVal, regvals[3]);
        
        error = writeReg(DAC2LVOL, regvals[2]);
        if(error){
          return error;
        }
        
        error = writeReg(DAC2RVOL, regvals[3]);
        if(error){
          return error;
        }
        break; 
        
      case AK4619VN::AK_DAC1L :
        regvals[0] = modifyGainRange(gainVal, regvals[0]);
        return (writeReg(DAC1LVOL, regvals[0]));
        break;
        
      case AK4619VN::AK_DAC1R :
        regvals[1] = modifyGainRange(gainVal, regvals[1]);
        return (writeReg(DAC1RVOL, regvals[1]));
        break;
        
      case AK4619VN::AK_DAC2L :
        regvals[2] = modifyGainRange(gainVal, regvals[2]);
        return (writeReg(DAC2LVOL, regvals[2]));
        break;
        
      case AK4619VN::AK_DAC2R :
        regvals[3] = modifyGainRange(gainVal, regvals[3]);
        return (writeReg(DAC2RVOL, regvals[3]));
        break;
        
      default:
        break;
    }
  }
  else{
    switch(channel){
      case AK4619VN::AK_DAC1B :
        regvals[0] = checkGainRange(gainVal);
        regvals[1] = checkGainRange(gainVal);
        
        error = writeReg(DAC1LVOL, regvals[0]);
        if(error){
          Serial.print("DAC1LVOL Err: ");
          Serial.println(error, DEC);
          return error;
        }
        
        error = writeReg(DAC1RVOL, regvals[1]);
        if(error){
          Serial.print("DAC1RVOL Err: ");
          Serial.println(error, DEC);
          return error;
        }
        break;
        
      case AK4619VN::AK_DAC2B :
        regvals[2] = checkGainRange(gainVal);
        regvals[3] = checkGainRange(gainVal);
        
        error = writeReg(DAC2LVOL, regvals[2]);
        if(error){
          Serial.print("DAC2LVOL Err: ");
          Serial.println(error, DEC);
          return error;
        }
        
        error = writeReg(DAC2RVOL, regvals[3]);
        if(error){
          Serial.print("DAC2RVOL Err: ");
          Serial.println(error, DEC);
          return error;
        }
        break;
        
      case AK4619VN::AK_DAC1L :
        regvals[0] = checkGainRange(gainVal);
        return (writeReg(DAC1LVOL, regvals[0]));
        break;
        
      case AK4619VN::AK_DAC1R :
        regvals[1] = checkGainRange(gainVal);
        return (writeReg(DAC1RVOL, regvals[1]));
        break;
        
      case AK4619VN::AK_DAC2L :
        regvals[2] = checkGainRange(gainVal);
        return (writeReg(DAC2LVOL, regvals[2]));
        break;
        
      case AK4619VN::AK_DAC2R :
        regvals[3] = checkGainRange(gainVal);
        return (writeReg(DAC2RVOL, regvals[3]));
        break;
        
      default:
        break;
    }
  }
  return 0;
}

//ADC input configuration
uint8_t AK4619VN::inputConf(intput_conf_t ADC1L, intput_conf_t ADC1R, intput_conf_t ADC2L, intput_conf_t ADC2R){
  
  uint8_t regval = 0;
  
  regval = ( ADC1L << 6 | ADC1R << 4 | ADC2L << 2 | ADC2R);
  
  return (writeReg(ADCAIN, regval));
}

//DAC input configuration
uint8_t AK4619VN::outputConf(output_conf_t DAC2, output_conf_t DAC1){
  
  uint8_t regval = 0;
  
  regval = ( DAC2 << 2 | DAC1);
  
  return (writeReg(DACDIN, regval));
}

// Modify regVal by inVal, check for under/overflow and adjust regVal to min or max
uint8_t AK4619VN::modifyGainRange(int16_t inVal, uint8_t regVal){
  if (inVal > 0 && (0xFF - regVal) < inVal) {
    // Overflow occurred, return maximum value
    return 0xFF;
  } else if (inVal < 0 && regVal < (-inVal)) {
    // Underflow occurred, return minimum value
    return 0;
  } else {
    // No overflow or underflow, perform the update
    return regVal + inVal;
  }
}

// Check if inVal is in range of uint8_t
uint8_t AK4619VN::checkGainRange(int16_t inVal){
  if (inVal > 0xFF) {
    // Value exceeds the maximum of uint8_t
    return 0xFF;
  } else if (inVal < 0) {
    // Value is negative, return minimum value of uint8_t
    return 0;
  } else {
    // Value is within the range of uint8_t, return as is
    return (uint8_t)inVal;
  }
}

// Print Current set 8 bit Registers to Serial
uint8_t AK4619VN::printRegs(uint8_t startReg, uint8_t len){
  uint8_t regvals[128] = {0};
  uint8_t error = 0;
  
  error = readRegMulti(startReg, len, regvals);
  if(error){
    return error;
  }
  
  for(int idx = 0; idx < len; idx++){
    Serial.print(idx+startReg, HEX);
    Serial.print(": \t");
    
    for (int8_t i = 7; i >= 0; i--) {
      uint8_t bit = (regvals[idx] >> i) & 1;
      Serial.print(bit);
      if (i % 4 == 0)
        Serial.print(" ");
    }
    Serial.print("(");
    Serial.print(regvals[idx], HEX);
    Serial.print(")");
    Serial.print("\t");
    Serial.println(controlParams[idx]);  
    //Serial.println();  // Prints a new line after printing the bits
    
  }
  
  return 0;
}

#if ESP_ARDUINO_VERSION_MAJOR == 1
uint8_t AK4619VN::writeReg(uint8_t deviceReg, uint8_t regVal) {
  m_i2c->beginTransmission(m_addr);
  m_i2c->write(deviceReg);
  m_i2c->write(regVal);
  m_i2c->endTransmission(true);
  
  return (m_i2c->lastError());
}

uint8_t AK4619VN::readReg(uint8_t deviceReg, uint8_t * regVal) {
  m_i2c->beginTransmission(m_addr);
  m_i2c->write(deviceReg);
  m_i2c->endTransmission(true);
  
  m_i2c->beginTransmission(m_addr);
  m_i2c->requestFrom((int8_t)m_addr, 1);
  * regVal = m_i2c->read();
  m_i2c->endTransmission(true);
  
  return (m_i2c->lastError());
}

uint8_t AK4619VN::readRegMulti(uint8_t startReg, uint8_t len, uint8_t * vals) {
  m_i2c->beginTransmission(m_addr);
  m_i2c->write(startReg);
  m_i2c->endTransmission(false);
  
  m_i2c->requestFrom((int8_t)m_addr, (int8_t)len);
  
  for (uint8_t i = 0; i <= (len - 1); i++) {
    vals[i] = m_i2c->read();
  }
  
  return (m_i2c->lastError());
}
#endif

#if ESP_ARDUINO_VERSION_MAJOR == 2
/*
  Name: readReg

  Description:
    Read a register from the codec.

  Input Parameters:
    reg - Register address.

  Return:
    Register value.
*/

// uint8_t AK4619VN::readReg(uint8_t reg) {
//   int data;

//   m_i2c->beginTransmission(m_addr);
//   if (m_i2c->write(reg) == 0) {
//     log_e("Error writing register address 0x%02x.", reg);
//     return 0;
//   }

//   if (m_i2c->endTransmission(false) != 0) {
//     log_e("Error ending transmission.");
//     return 0;
//   }

//   if (!m_i2c->requestFrom(m_addr, (uint8_t)1)) {
//     log_e("Error requesting data.");
//     return 0;
//   }

//   if ((data = m_i2c->read()) < 0) {
//     log_e("Error reading data.");
//     return 0;
//   }

//   return (uint8_t)data;
// }

/*
  Name: writeReg

  Description:
    Write a register to the ES8388 codec.

  Input Parameters:
    reg - Register address.
    data - Data to write.
*/

// void AK4619VN::writeReg(uint8_t reg, uint8_t data) {
//   m_i2c->beginTransmission(_addr);

//   if (m_i2c->write(reg) == 0) {
//     log_e("Error writing register address 0x%02x.", reg);
//     return;
//   }

//   if (m_i2c->write(data) == 0) {
//     log_e("Error writing data 0x%02x.", data);
//     return;
//   }

//   if (m_i2c->endTransmission(true) != 0) {
//     log_e("Error ending transmission.");
//     return;
//   }
// }

uint8_t AK4619VN::writeReg(uint8_t deviceReg, uint8_t regVal) {
  
  m_i2c->beginTransmission(m_addr);
  m_i2c->write(deviceReg);
  m_i2c->write(regVal);
  return(m_i2c->endTransmission(true)); // Send STOP
}

uint8_t AK4619VN::readReg(uint8_t deviceReg, uint8_t * regVal) {
  
  m_i2c->beginTransmission(m_addr);
  m_i2c->write(deviceReg);
  m_i2c->endTransmission(true);
  
  uint8_t numbytes = 0;
  numbytes = m_i2c->requestFrom(m_addr, (uint8_t)1, (uint8_t)false);
  
  if((bool)numbytes){
    Wire.readBytes(regVal, numbytes);
  }
  
  return(m_i2c->endTransmission(true)); //Send STOP
}

uint8_t AK4619VN::readRegMulti(uint8_t startReg, uint8_t len, uint8_t * vals) {
  
  m_i2c->beginTransmission(m_addr);
  m_i2c->write(startReg);
  m_i2c->endTransmission(false);
  
  uint8_t numbytes = 0;
  numbytes = m_i2c->requestFrom((uint8_t)m_addr, len, (uint8_t)false);
  
  if((bool)numbytes){
    Wire.readBytes(vals, numbytes);
  }
  
  return(m_i2c->endTransmission(true)); //Send STOP
}
#endif

