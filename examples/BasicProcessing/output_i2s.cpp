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
#include "AudioConfig.h"
#include "output_i2s.h"
#include "input_i2s.h"
#include <cmath>

// high-level explanation of how this I2S & DMA code works:
// https://forum.pjrc.com/threads/65229?p=263104&viewfull=1#post263104

BufferQueue AudioOutputI2S::buffers;
DMAChannel AudioOutputI2S::dma(false);

void audioCallbackPassthrough(int32_t** inputs, int32_t** outputs)
{
	for (size_t i = 0; i < AUDIO_BLOCK_SAMPLES; i++)
	{
		outputs[0][i] = inputs[0][i];
		outputs[1][i] = inputs[1][i];
    outputs[2][i] = inputs[2][i];
		outputs[3][i] = inputs[3][i];
	}
}

void (*i2sAudioCallback)(int32_t** inputs, int32_t** outputs) = audioCallbackPassthrough;

DMAMEM __attribute__((aligned(32))) static uint64_t i2s_tx_buffer[AUDIO_BLOCK_SAMPLES];
#include "utility/imxrt_hw.h"
#include "imxrt.h"
#include "i2s_timers.h"

void AudioOutputI2S::begin()
{
	dma.begin(true); // Allocate the DMA channel first
	config_i2s();

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
	dma.enable();

	// Enabled transmitting and receiving
	I2S1_RCSR |= I2S_RCSR_RE | I2S_RCSR_BCE;
	I2S1_TCSR = I2S_TCSR_TE | I2S_TCSR_BCE | I2S_TCSR_FRDE;
	dma.attachInterrupt(isr);
}

// This gets called twice per block, when buffer is half full and completely full
// Every other call, after we've pushed the second half of the current block onto the tx_buffer, we trigger the
// process() call again, computing a new block of data
void AudioOutputI2S::isr(void)
{
	int32_t* dest;
	int32_t* block1L;
	int32_t* block1R;
  int32_t* block2L;
	int32_t* block2R;
	
	uint32_t saddr, offset;
	bool callUpdate;

	saddr = (uint32_t)(dma.TCD->SADDR);
	dma.clearInterrupt();
	if (saddr < (uint32_t)i2s_tx_buffer + sizeof(i2s_tx_buffer) / 2) 
	{
		// DMA is transmitting the first half of the buffer
		// so we must fill the second half
		dest = (int32_t *)&i2s_tx_buffer[AUDIO_BLOCK_SAMPLES/2];
    // dest = (int32_t *)((uint32_t)i2s_tx_buffer + sizeof(i2s_tx_buffer) / 2);
		callUpdate = true;
		offset = AUDIO_BLOCK_SAMPLES / 2;
	}
	else
	{
		// DMA is transmitting the second half of the buffer
		// so we must fill the first half
		dest = (int32_t *)i2s_tx_buffer;
		callUpdate = false;
		offset = 0;
	}

	block1L = buffers.readPtr[0];
	block1R = buffers.readPtr[1];
  block2L = buffers.readPtr[2];
	block2R = buffers.readPtr[3];

	for (size_t i = 0; i < AUDIO_BLOCK_SAMPLES/2; i++)
	{
		dest[4*i/2] = block1L[i+offset];
		dest[4*i/2+1] = block1R[i+offset];
    dest[4*i/2+2] = block2L[i+offset];
		dest[4*i/2+3] = block2R[i+offset];
	}
	
	arm_dcache_flush_delete(dest, sizeof(i2s_tx_buffer) / 2 );

	if (callUpdate)
	{
		Timers::ResetFrame();

		// We've finished reading all the data from the current read block
		buffers.consume();

		// Fetch the input samples
		int32_t** dataInPtr = AudioInputI2S::getData();

		// populate the next block
		i2sAudioCallback(dataInPtr, buffers.writePtr);
		// publish the block
		buffers.publish();

		Timers::LapInner(Timers::TIMER_TOTAL);
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
	int fs = SAMPLERATE;
	// PLL between 27*24 = 648MHz und 54*24=1296MHz
	int n1 = 16; //SAI prescaler 4 => (n1*n2) = multiple of 4
	int n2 = 1 + (24000000 * 27) / (fs * 256 * n1);

	double C = ((double)fs * 256 * n1 * n2) / 24000000;
	int c0 = C;
	int c2 = 10000;
	int c1 = C * c2 - (c0 * c2);
	set_audioClock(c0, c1, c2);

	// clear SAI1_CLK register locations
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

	int rsync = 0;
	int tsync = 1;
  int channels = 4;
  int bitdepth = 32;
	I2S1_TMR = 0;
	//I2S1_TCSR = (1<<25); //Reset
	I2S1_TCR1 = I2S_TCR1_RFW(channels -1); // 4 FIFI words
	I2S1_TCR2 = I2S_TCR2_SYNC(tsync) | I2S_TCR2_BCP // sync=0; tx is async;
		    | (I2S_TCR2_BCD | I2S_TCR2_DIV((1)) | I2S_TCR2_MSEL(1));
	I2S1_TCR3 = I2S_TCR3_TCE;
	I2S1_TCR4 = I2S_TCR4_FRSZ(channels -1) | I2S_TCR4_SYWD((bitdepth-1)) | I2S_TCR4_MF //2 Frames?
		    | I2S_TCR4_FSD | I2S_TCR4_FSE | I2S_TCR4_FSP;
	I2S1_TCR5 = I2S_TCR5_WNW((bitdepth-1)) | I2S_TCR5_W0W((bitdepth-1)) | I2S_TCR5_FBT((bitdepth-1));

	I2S1_RMR = 0;
	//I2S1_RCSR = (1<<25); //Reset
	I2S1_RCR1 = I2S_RCR1_RFW(channels -1);
	I2S1_RCR2 = I2S_RCR2_SYNC(rsync) | I2S_RCR2_BCP  // sync=0; rx is async;
		    | (I2S_RCR2_BCD | I2S_RCR2_DIV((1)) | I2S_RCR2_MSEL(1));
	I2S1_RCR3 = I2S_RCR3_RCE;
	I2S1_RCR4 = I2S_RCR4_FRSZ(channels -1) | I2S_RCR4_SYWD((bitdepth-1)) | I2S_RCR4_MF
		    | I2S_RCR4_FSE | I2S_RCR4_FSP | I2S_RCR4_FSD;
	I2S1_RCR5 = I2S_RCR5_WNW((bitdepth-1)) | I2S_RCR5_W0W((bitdepth-1)) | I2S_RCR5_FBT((bitdepth-1));
}
