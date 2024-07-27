#include <Wire.h>
#include "output_i2s_tdm.h"
#include "input_i2s_tdm.h"
#include "control_AK4619VN.h"

// GUItool: begin automatically generated code
AudioInputI2S            i2s2;           //xy=105,63
AudioOutputI2S           i2s1;           //xy=470,120
AudioConnection          patchCord1(i2s2, 0, i2s1, 0);
AudioConnection          patchCord2(i2s2, 1, i2s1, 1);
AudioConnection          patchCord3(i2s2, 2, i2s1, 2);
AudioConnection          patchCord4(i2s2, 3, i2s1, 3);
// GUItool: end automatically generated code


AK4619VN codec(&Wire, AK4619VN_ADDR);


void setup() {
  // Audio connections require memory, and the record queue
  // uses this memory to buffer incoming audio.
  AudioMemory(512);
  codec.init();
}


void loop() {

}


