/* Audio Library for Teensy 3.X
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

#include <Arduino.h>
#include "input_i2s_tdm.h"
#include "AudioStream32.h"
#include "output_i2s_tdm.h"

DMAMEM __attribute__((aligned(32))) static uint32_t i2s_rx_buffer[AUDIO_BLOCK_SAMPLES*4];
audio_block_t * AudioInputI2S::block_ch1 = NULL;
audio_block_t * AudioInputI2S::block_ch2 = NULL;
audio_block_t * AudioInputI2S::block_ch3 = NULL;
audio_block_t * AudioInputI2S::block_ch4 = NULL;
uint16_t AudioInputI2S::block_offset = 0;
bool AudioInputI2S::update_responsibility = false;
DMAChannel AudioInputI2S::dma(false);
int32_t* outBuffers[4];
BufferQueue AudioInputI2S::buffers;

#if defined(__MK20DX256__) || defined(__MK64FX512__) || defined(__MK66FX1M0__) || defined(__IMXRT1062__)

void AudioInputI2S::begin(void)
{
	dma.begin(true); // Allocate the DMA channel first

#if defined(KINETISK)
	// TODO: should we set & clear the I2S_RCSR_SR bit here?
	AudioOutputI2SQuad::config_i2s();

	CORE_PIN13_CONFIG = PORT_PCR_MUX(4); // pin 13, PTC5, I2S0_RXD0
#if defined(__MK20DX256__)
	CORE_PIN30_CONFIG = PORT_PCR_MUX(4); // pin 30, PTC11, I2S0_RXD1
#elif defined(__MK64FX512__) || defined(__MK66FX1M0__)
	CORE_PIN38_CONFIG = PORT_PCR_MUX(4); // pin 38, PTC11, I2S0_RXD1
#endif

#elif defined(__IMXRT1062__)
	//const int pinoffset = 0; // TODO: make this configurable...
	AudioOutputI2S::config_i2s();
	dma.begin(true); // Allocate the DMA channel first

	CORE_PIN8_CONFIG  = 3;  //1:RX_DATA0
	IOMUXC_SAI1_RX_DATA0_SELECT_INPUT = 2;

	dma.TCD->SADDR = (void *)((uint32_t)&I2S1_RDR0 + 0) ; // source address, read from 0 byte offset as we want the full 32 bits
	dma.TCD->SOFF = 0; // how many bytes to jump from current address on the next move. We're always reading the same register so no jump.
	dma.TCD->ATTR = DMA_TCD_ATTR_SSIZE(2) | DMA_TCD_ATTR_DSIZE(2); // 1=16bits, 2=32 bits. size of source, size of dest
	dma.TCD->NBYTES_MLNO = 4; // number of bytes to move, minor loop.
	dma.TCD->SLAST = 0; // how many bytes to jump when hitting the end of the major loop. In this case, no change to the source address.
	dma.TCD->DADDR = i2s_rx_buffer; // Destination address.
	dma.TCD->DOFF = 4; // how many bytes to move the destination at each minor loop. jump 4 bytes.
	dma.TCD->CITER_ELINKNO = sizeof(i2s_rx_buffer) / 4; // how many iterations are in the major loop
	dma.TCD->DLASTSGA = -sizeof(i2s_rx_buffer); // how many bytes to jump the destination address at the end of the major loop
	dma.TCD->BITER_ELINKNO = sizeof(i2s_rx_buffer) / 4; // beginning iteration count
	dma.TCD->CSR = DMA_TCD_CSR_INTHALF | DMA_TCD_CSR_INTMAJOR; // Tells the DMA mechanism to trigger interrupt at half and full population of the buffer
	dma.triggerAtHardwareEvent(DMAMUX_SOURCE_SAI1_RX); // run DMA at hardware event when new I2S data transmitted.

	// Enabled transmitting and receiving
	I2S1_RCSR = I2S_RCSR_RE | I2S_RCSR_BCE | I2S_RCSR_FRDE | I2S_RCSR_FR;

	update_responsibility = update_setup();
	dma.enable();
	dma.attachInterrupt(isr);
#endif
}

int32_t** AudioInputI2S::getData()
{
	outBuffers[0] = buffers.readPtr[0]; 
  outBuffers[1] = buffers.readPtr[1]; 
  if (CHANNELS > 2) {
    outBuffers[2] = buffers.readPtr[2]; 
    outBuffers[3] = buffers.readPtr[3]; 
  }
	buffers.consume();
	return outBuffers;
}


void AudioInputI2S::isr(void)
{
	uint32_t daddr, offset;
	const int32_t *src;
	int32_t *dest1, *dest2, *dest3, *dest4;

	//digitalWriteFast(3, HIGH);
	daddr = (uint32_t)(dma.TCD->DADDR);
	dma.clearInterrupt();

	if (daddr < (uint32_t)i2s_rx_buffer + sizeof(i2s_rx_buffer) / 2) {
		// DMA is receiving to the first half of the buffer
		// need to remove data from the second half
		src = (int32_t *)((uint32_t)i2s_rx_buffer + sizeof(i2s_rx_buffer) / 2);
		//src = (int32_t *)&i2s_rx_buffer[AUDIO_BLOCK_SAMPLES*2];
		if (update_responsibility) update_all();
	} else {
		// DMA is receiving to the second half of the buffer
		// need to remove data from the first half
		src = (int32_t *)&i2s_rx_buffer[0];
	}
	if (block_ch1) {
		offset = block_offset;
		if (offset <= AUDIO_BLOCK_SAMPLES/2) {
			arm_dcache_delete((void*)src, sizeof(i2s_rx_buffer) / 2);
			block_offset = offset + AUDIO_BLOCK_SAMPLES/2;
			dest1 = &(block_ch1->data[offset]);
			dest2 = &(block_ch2->data[offset]);
			dest3 = &(block_ch3->data[offset]);
			dest4 = &(block_ch4->data[offset]);
			//Serial.println((int32_t)dest1[0]);
			for (int i=0; i < AUDIO_BLOCK_SAMPLES/2; i++) {
				*dest1++ = *src++;
				*dest2++ = *src++;
				*dest3++ = *src++;
				*dest4++ = *src++;
			}
		}
	}
	//digitalWriteFast(3, LOW);
}


void AudioInputI2S::update(void)
{
	audio_block_t *new1, *new2, *new3, *new4;
	audio_block_t *out1, *out2, *out3, *out4;

	// allocate 4 new blocks
	new1 = allocate();
	new2 = allocate();
	new3 = allocate();
	new4 = allocate();
	// but if any fails, allocate none
	if (!new1 || !new2 || !new3 || !new4) {
		if (new1) {
			release(new1);
			new1 = NULL;
		}
		if (new2) {
			release(new2);
			new2 = NULL;
		}
		if (new3) {
			release(new3);
			new3 = NULL;
		}
		if (new4) {
			release(new4);
			new4 = NULL;
		}
	}
	__disable_irq();
	if (block_offset >= AUDIO_BLOCK_SAMPLES) {
		// the DMA filled 4 blocks, so grab them and get the
		// 4 new blocks to the DMA, as quickly as possible
		out1 = block_ch1;
		block_ch1 = new1;
		out2 = block_ch2;
		block_ch2 = new2;
		out3 = block_ch3;
		block_ch3 = new3;
		out4 = block_ch4;
		block_ch4 = new4;
		block_offset = 0;
		__enable_irq();
		// then transmit the DMA's former blocks
		transmit(out1, 0);
		release(out1);
		transmit(out2, 1);
		release(out2);
		transmit(out3, 2);
		release(out3);
		transmit(out4, 3);
		release(out4);
	} else if (new1 != NULL) {
		// the DMA didn't fill blocks, but we allocated blocks
		if (block_ch1 == NULL) {
			// the DMA doesn't have any blocks to fill, so
			// give it the ones we just allocated
			block_ch1 = new1;
			block_ch2 = new2;
			block_ch3 = new3;
			block_ch4 = new4;
			block_offset = 0;
			__enable_irq();
		} else {
			// the DMA already has blocks, doesn't need these
			__enable_irq();
			release(new1);
			release(new2);
			release(new3);
			release(new4);
		}
	} else {
		// The DMA didn't fill blocks, and we could not allocate
		// memory... the system is likely starving for memory!
		// Sadly, there's nothing we can do.
		__enable_irq();
	}
}

#else // not __MK20DX256__

void AudioInputI2S::begin(void)
{
}



#endif

