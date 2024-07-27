/* Audio Library for Teensy 3.X  (adapted for Teensy 4.x)
 * Copyright (c) 2014, Paul Stoffregen, paul@pjrc.com
 *
 * Development of this audio library was funded by PJRC.COM, LLC by sales of
 * Teensy and Audio Adaptor boards.  Please support PJRC's efforts to develop
 * open source software by purchasing Teensy or other PJRC products.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice, development funding notice, and this permission
 * notice shall be included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

// This code was adapter for Paul Stoffregen's original I2S block from the Teensy audio library.
// It is set up to specifically work with the Teensy 4, and does not support other models.
// It provides low-overhead I2S audio input and output.

#include <Arduino.h>
#include <cstdlib>
#include "AudioStream32.h"
#include "output_i2s_tdm.h"
#include "input_i2s_tdm.h"
#include <cmath>
#include "utility/imxrt_hw.h"
#include "imxrt.h"
#include "i2s_timers.h"
// high-level explanation of how this I2S & DMA code works:
// https://forum.pjrc.com/threads/65229?p=263104&viewfull=1#post263104

audio_block_t * AudioOutputI2S::block_ch1_1st = NULL;
audio_block_t * AudioOutputI2S::block_ch2_1st = NULL;
audio_block_t * AudioOutputI2S::block_ch3_1st = NULL;
audio_block_t * AudioOutputI2S::block_ch4_1st = NULL;
audio_block_t * AudioOutputI2S::block_ch1_2nd = NULL;
audio_block_t * AudioOutputI2S::block_ch2_2nd = NULL;
audio_block_t * AudioOutputI2S::block_ch3_2nd = NULL;
audio_block_t * AudioOutputI2S::block_ch4_2nd = NULL;
uint16_t  AudioOutputI2S::ch1_offset = 0;
uint16_t  AudioOutputI2S::ch2_offset = 0;
uint16_t  AudioOutputI2S::ch3_offset = 0;
uint16_t  AudioOutputI2S::ch4_offset = 0;
bool AudioOutputI2S::update_responsibility = false;

DMAMEM __attribute__((aligned(32))) static uint32_t i2s_tx_buffer[AUDIO_BLOCK_SAMPLES * CHANNELS];
DMAChannel AudioOutputI2S::dma(false);

static const int32_t zerodata[AUDIO_BLOCK_SAMPLES/4] = {0};

void AudioOutputI2S::begin()
{
	dma.begin(true); // Allocate the DMA channel first
	config_i2s();

	block_ch1_1st = NULL;
	block_ch2_1st = NULL;
	block_ch3_1st = NULL;
	block_ch4_1st = NULL;

	// Minor loop = each individual transmission, in this case, 4 bytes of data
	// Major loop = the buffer size, events can run when we hit the half and end of the major loop
	// To reset Source address, trigger interrupts, etc.
	CORE_PIN7_CONFIG  = 3;  //1:TX_DATA0
	dma.TCD->SADDR = i2s_tx_buffer;
	dma.TCD->SOFF = 4; // how many bytes to jump from current address on the next move
	dma.TCD->ATTR = DMA_TCD_ATTR_SSIZE(2) | DMA_TCD_ATTR_DSIZE(2); // 1=16bits, 2=32 bits. size of source, size of dest
	dma.TCD->NBYTES_MLNO = 4; // number of bytes to move, (minor loop?)
	dma.TCD->SLAST = -sizeof(i2s_tx_buffer); // how many bytes to jump when hitting the end of the major loop. In this case, jump back to start of buffer
	dma.TCD->DOFF = 0; // how many bytes to move the destination at each minor loop. In this case we're always writing to the same memory register.
	dma.TCD->CITER_ELINKNO = sizeof(i2s_tx_buffer) / 4; // how many iterations are in the major loop
	dma.TCD->DLASTSGA = 0; // how many bytes to jump the destination address at the end of the major loop
	dma.TCD->BITER_ELINKNO = sizeof(i2s_tx_buffer) / 4; // beginning iteration count
	dma.TCD->CSR = DMA_TCD_CSR_INTHALF | DMA_TCD_CSR_INTMAJOR; // Tells the DMA mechanism to trigger interrupt at half and full population of the buffer
	dma.TCD->DADDR = (void *)((uint32_t)&I2S1_TDR0 + 0); // Destination address. for 16 bit values we use +2 byte offset from the I2S register. for 32 bits we use a zero offset.
	dma.triggerAtHardwareEvent(DMAMUX_SOURCE_SAI1_TX); // run DMA at hardware event when new I2S data transmitted.
	update_responsibility = update_setup();
	dma.enable();

	// Enabled transmitting and receiving

  // Receive Control  : Enable resets, interrupt, error flag fields.
// 	I2S1_RCSR = 
//     I2S_RCSR_RE       // Receiver Enabled
//   | I2S_RCSR_BCE;     // Receiver Bit Clock Enabled

  // Transmit Control : Enable resets, interrupt, error flag fields.
	I2S1_TCSR = 
    I2S_TCSR_TE       // Transmitter Enabled
  | I2S_TCSR_BCE      // Transmitter Bit Clock Enabled
  | I2S_TCSR_FRDE;    // FIFO Request Interrupt Enable
	dma.attachInterrupt(isr);
}

// This gets called twice per block, when buffer is half full and completely full
// Every other call, after we've pushed the second half of the current block onto the tx_buffer, we trigger the
// process() call again, computing a new block of data
void AudioOutputI2S::isr(void)
{
	uint32_t saddr;
	const int32_t *src1, *src2, *src3, *src4;
	const int32_t *zeros = (const int32_t *)zerodata;
	int32_t *dest;

	saddr = (uint32_t)(dma.TCD->SADDR);
	dma.clearInterrupt();
	if (saddr < (uint32_t)i2s_tx_buffer + sizeof(i2s_tx_buffer) / 2) {
		// DMA is transmitting the first half of the buffer
		// so we must fill the second half
		dest = (int32_t *)&i2s_tx_buffer[AUDIO_BLOCK_SAMPLES];
		if (update_responsibility) update_all();
	} else {
		dest = (int32_t *)i2s_tx_buffer;
	}

	src1 = (block_ch1_1st) ? block_ch1_1st->data + ch1_offset : zeros;
	src2 = (block_ch2_1st) ? block_ch2_1st->data + ch2_offset : zeros;
	src3 = (block_ch3_1st) ? block_ch3_1st->data + ch3_offset : zeros;
	src4 = (block_ch4_1st) ? block_ch4_1st->data + ch4_offset : zeros;

	//Serial.println((int32_t)src1[0]);
	
	// about this code: https://forum.pjrc.com/threads/64508
	for (int i=0; i < AUDIO_BLOCK_SAMPLES/2; i++) {
		*dest++ = *src1++;
		*dest++ = *src3++;
		*dest++ = *src2++;
		*dest++ = *src4++;
	}

	arm_dcache_flush_delete(dest, sizeof(i2s_tx_buffer) / 2 );

	if (block_ch1_1st) {
		if (ch1_offset == 0) {
			ch1_offset = AUDIO_BLOCK_SAMPLES/2;
		} else {
			ch1_offset = 0;
			release(block_ch1_1st);
			block_ch1_1st = block_ch1_2nd;
			block_ch1_2nd = NULL;
		}
	}
	if (block_ch2_1st) {
		if (ch2_offset == 0) {
			ch2_offset = AUDIO_BLOCK_SAMPLES/2;
		} else {
			ch2_offset = 0;
			release(block_ch2_1st);
			block_ch2_1st = block_ch2_2nd;
			block_ch2_2nd = NULL;
		}
	}
	if (block_ch3_1st) {
		if (ch3_offset == 0) {
			ch3_offset = AUDIO_BLOCK_SAMPLES/2;
		} else {
			ch3_offset = 0;
			release(block_ch3_1st);
			block_ch3_1st = block_ch3_2nd;
			block_ch3_2nd = NULL;
		}
	}
	if (block_ch4_1st) {
		if (ch4_offset == 0) {
			ch4_offset = AUDIO_BLOCK_SAMPLES/2;
		} else {
			ch4_offset = 0;
			release(block_ch4_1st);
			block_ch4_1st = block_ch4_2nd;
			block_ch4_2nd = NULL;
		}
	}
}

void AudioOutputI2S::update(void)
{
	audio_block_t *block, *tmp;

	block = receiveReadOnly(0); // channel 1
	if (block) {
		__disable_irq();
		if (block_ch1_1st == NULL) {
			block_ch1_1st = block;
			ch1_offset = 0;
			__enable_irq();
		} else if (block_ch1_2nd == NULL) {
			block_ch1_2nd = block;
			__enable_irq();
		} else {
			tmp = block_ch1_1st;
			block_ch1_1st = block_ch1_2nd;
			block_ch1_2nd = block;
			ch1_offset = 0;
			__enable_irq();
			release(tmp);
		}
	}
	block = receiveReadOnly(1); // channel 2
	if (block) {
		__disable_irq();
		if (block_ch2_1st == NULL) {
			block_ch2_1st = block;
			ch2_offset = 0;
			__enable_irq();
		} else if (block_ch2_2nd == NULL) {
			block_ch2_2nd = block;
			__enable_irq();
		} else {
			tmp = block_ch2_1st;
			block_ch2_1st = block_ch2_2nd;
			block_ch2_2nd = block;
			ch2_offset = 0;
			__enable_irq();
			release(tmp);
		}
	}
	block = receiveReadOnly(2); // channel 3
	if (block) {
		__disable_irq();
		if (block_ch3_1st == NULL) {
			block_ch3_1st = block;
			ch3_offset = 0;
			__enable_irq();
		} else if (block_ch3_2nd == NULL) {
			block_ch3_2nd = block;
			__enable_irq();
		} else {
			tmp = block_ch3_1st;
			block_ch3_1st = block_ch3_2nd;
			block_ch3_2nd = block;
			ch3_offset = 0;
			__enable_irq();
			release(tmp);
		}
	}
	block = receiveReadOnly(3); // channel 4
	if (block) {
		__disable_irq();
		if (block_ch4_1st == NULL) {
			block_ch4_1st = block;
			ch4_offset = 0;
			__enable_irq();
		} else if (block_ch4_2nd == NULL) {
			block_ch4_2nd = block;
			__enable_irq();
		} else {
			tmp = block_ch4_1st;
			block_ch4_1st = block_ch4_2nd;
			block_ch4_2nd = block;
			ch4_offset = 0;
			__enable_irq();
			release(tmp);
		}
	}
}

// This function sets all the necessary PLL and I2S flags necessary for running
void AudioOutputI2S::config_i2s(bool only_bclk)
{
	CCM_CCGR5 |= CCM_CCGR5_SAI1(CCM_CCGR_ON);

	// if either transmitter or receiver is enabled, do nothing
	if ((I2S1_TCSR & I2S_TCSR_TE) != 0 || (I2S1_RCSR & I2S_RCSR_RE) != 0)
	{
	  if (!only_bclk) // if previous transmitter/receiver only activated BCLK, activate the other clock pins now
	  {
	    CORE_PIN23_CONFIG = 3;  //1:MCLK
	    CORE_PIN20_CONFIG = 3;  //1:RX_SYNC (LRCLK)
	  }
	  return ;
	}

	//PLL:
	int fs = AUDIO_SAMPLE_RATE;
	// PLL between 27*24 = 648MHz und 54*24=1296MHz
	int n1 = 4; //SAI prescaler 4 => (n1*n2) = multiple of 4
	int n2 = 1 + (24000000 * 27) / (fs * 256 * n1);

	double C = ((double)fs * 256 * n1 * n2) / 24000000;
	int c0 = C;
	int c2 = 10000;
	int c1 = C * c2 - (c0 * c2);
	set_audioClock(c0, c1, c2);

	// Clear SAI1_CLK register locations
	CCM_CSCMR1 = (CCM_CSCMR1 & ~(CCM_CSCMR1_SAI1_CLK_SEL_MASK))
		   | CCM_CSCMR1_SAI1_CLK_SEL(2); // &0x03 // (0,1,2): PLL3PFD0, PLL5, PLL4
	CCM_CS1CDR = (CCM_CS1CDR & ~(CCM_CS1CDR_SAI1_CLK_PRED_MASK | CCM_CS1CDR_SAI1_CLK_PODF_MASK))
		   | CCM_CS1CDR_SAI1_CLK_PRED(n1-1) // &0x07
		   | CCM_CS1CDR_SAI1_CLK_PODF(n2-1); // &0x3f

	// Select MCLK
	IOMUXC_GPR_GPR1 = (IOMUXC_GPR_GPR1
		& ~(IOMUXC_GPR_GPR1_SAI1_MCLK1_SEL_MASK))
		| (IOMUXC_GPR_GPR1_SAI1_MCLK_DIR | IOMUXC_GPR_GPR1_SAI1_MCLK1_SEL(0));

	if (!only_bclk)
	{
	  CORE_PIN23_CONFIG = 3;  //1:MCLK
	  CORE_PIN20_CONFIG = 3;  //1:RX_SYNC  (LRCLK)
	}
	CORE_PIN21_CONFIG = 3;  //1:RX_BCLK

  // Synchronous Audio Interface (SAI) Setup
  // See page 2005: https://www.pjrc.com/teensy/IMXRT1060RM_rev3.pdf

  // Transmit Mask
  I2S1_TMR = 0;                 // Allows masked words in each frame to change from frame to frame. 0=no mask

	// SAI Transmit Configuration 1: Watermark level for all enabled transmit channels
  I2S1_TCR1 = I2S_TCR1_RFW(CHANNELS - 1); // Transmit FIFO Watermark

  // SAI Transmit Configuration 2: SYNC mode and clock setting fields
	I2S1_TCR2 = 
    I2S_TCR2_SYNC(1)            // Synchronous Mode     : 1=sync, 0=async
  | I2S_TCR2_BCP                // Bit Clock Polarity   : 1=Outputs falling edge, inputs on rising edge, 0 is inverse.
	| I2S_TCR2_BCD                // Bit Clock Direction  : 1=Bit clock is generated internally in Master mode. 0=Slave, externally
  | I2S_TCR2_DIV(0)             // Bit Clock Divide     : Divides the mclk as (DIV + 1) * 2
  | I2S_TCR2_MSEL(1);           // Master Clock Select  : 0=bus clock, 1=I2S0_MCLK
	
  // SAI Transmit Configuration 3: Transmit channel settings
  I2S1_TCR3 = I2S_TCR3_TCE;     // Transmit Channel Enable: Sets the channel to transmit operation.
  
  // SAI Transmit Configuration 4: FIFO Combine Mode, FIFO Packing Mode, and frame sync settings.
	I2S1_TCR4 = 
    I2S_TCR4_FRSZ(CHANNELS - 1) // Frame Size           : Number of words (=channels) in each frame (minus one)
  | I2S_TCR4_SYWD(BIT_DEPTH -1) // Sync Width           : Length of the frame sync in number of bit clocks (minus one)
  | I2S_TCR4_MF                 // MSB First            : 0=LSB First, 1=MSB First
  | I2S_TCR4_FSE                // Frame Sync Early     : 1=One bit before the frame, 0=With the first bit of the frame
  | I2S_TCR4_FSP                // Frame Sync Polarity  : 1=Active low, 0=Active high
  | I2S_TCR4_FSD;               // Frame Sync Direction : 1=Internally in Master mode, 0=Externally in Slave mode.
  // SAI Transmit Configuration 5: Word width and bit index settings
	I2S1_TCR5 = 
    I2S_TCR5_W0W(BIT_DEPTH - 1)  // Word 0 Width        : Number of Bits per word, first frame
  | I2S_TCR5_WNW(BIT_DEPTH - 1)  // Word N Width        : Number of Bits per word, nth frame
  | I2S_TCR5_FBT(BIT_DEPTH - 1); // First Bit Shifted   : Bit index for the first bit for each word in the frame minus one.

  // Receive Mask
	I2S1_RMR = 0;                 // Allows masked words in each frame to change from frame to frame. 0=no mask
	
  // SAI Receive Configuration 1
	I2S1_RCR1 = I2S_RCR1_RFW(CHANNELS -1); // Transmit FIFO Watermark, Frame size in words (minus one)

  // SAI Receive Configuration 2
	I2S1_RCR2 = 
    I2S_RCR2_SYNC(0)             // Synchronous Mode     : 1=sync with transmitter, 0=async
  | I2S_RCR2_BCP                 // Bit Clock Polarity   : 1=Outputs falling edge, inputs on rising edge, 0 is inverse.
	| I2S_RCR2_BCD                 // Bit Clock Direction  : 1=Bit clock is generated internally in Master mode. 0=Slave, externally
  | I2S_RCR2_DIV(0)              // Bit Clock Divide     : Divides the mclk as (DIV + 1) * 2
  | I2S_RCR2_MSEL(1);            // Master Clock Select  : 0=bus clock, 1=I2S0_MCLK
	
   // SAI Receive Configuration 3
	I2S1_RCR3 = I2S_RCR3_RCE;      // Receive Channel Enable: Sets the channel to receive operation.

   // SAI Receive Configuration 4
	I2S1_RCR4 = 
    I2S_RCR4_FRSZ(CHANNELS - 1)   // Frame Size           : Number of words (=channels) in each frame (minus one)
  | I2S_RCR4_SYWD(BIT_DEPTH -1)   // Sync Width           : Length of the frame sync in number of bit clocks (minus one)
  | I2S_RCR4_MF                   // MSB First            : 0=LSB First, 1=MSB First
	| I2S_RCR4_FSE                  // Frame Sync Early     : 1=One bit before the frame, 0=With the first bit of the frame
  | I2S_RCR4_FSD                  // Frame Sync Direction : 1=Internally in Master mode, 0=Externally in Slave mode.
  | I2S_RCR4_FSP;                 // Frame Sync Polarity  : 1=Active low, 0=Active high 

   // SAI Receive Configuration 5
	I2S1_RCR5 = 
    I2S_RCR5_WNW(BIT_DEPTH - 1)   // Word 0 Width        : Number of Bits per word, first frame
  | I2S_RCR5_W0W(BIT_DEPTH - 1)   // Word N Width        : Number of Bits per word, nth frame
  | I2S_RCR5_FBT(BIT_DEPTH - 1);  // First Bit Shifted   : Bit index for the first bit for each word in the frame minus one.

}
