#pragma once

// Circular queue of buffers used to produce and consume new blocks of audio data coming from and going to the I2S bus.
class BufferQueue
{
#define BUFFER_QUEUE_SIZE 3
public:
	int32_t data1Left[AUDIO_BLOCK_SAMPLES * BUFFER_QUEUE_SIZE];
	int32_t data1Right[AUDIO_BLOCK_SAMPLES * BUFFER_QUEUE_SIZE];
	int32_t data2Left[AUDIO_BLOCK_SAMPLES * BUFFER_QUEUE_SIZE];
	int32_t data2Right[AUDIO_BLOCK_SAMPLES * BUFFER_QUEUE_SIZE];

	uint8_t readPos = 0;
	uint8_t writePos = 0;
	int available = 0;

	int32_t* readPtr[4];
	int32_t* writePtr[4];

	inline BufferQueue()
	{
		writePtr[0] = &data1Left[writePos * AUDIO_BLOCK_SAMPLES];
		writePtr[1] = &data1Right[writePos * AUDIO_BLOCK_SAMPLES];
    writePtr[2] = &data2Left[writePos * AUDIO_BLOCK_SAMPLES];
		writePtr[3] = &data2Right[writePos * AUDIO_BLOCK_SAMPLES];

		readPtr[0] = &data1Left[readPos * AUDIO_BLOCK_SAMPLES];
		readPtr[1] = &data1Right[readPos * AUDIO_BLOCK_SAMPLES];
    readPtr[2] = &data2Left[readPos * AUDIO_BLOCK_SAMPLES];
		readPtr[3] = &data2Right[readPos * AUDIO_BLOCK_SAMPLES];

        for (size_t i = 0; i < AUDIO_BLOCK_SAMPLES * BUFFER_QUEUE_SIZE; i++)
        {
            data1Left[i] = 0;
            data1Right[i] = 0;
            data2Left[i] = 0;
            data2Right[i] = 0;
        }
        
        for (size_t i = 0; i < BUFFER_QUEUE_SIZE - 1; i++)
        {
            publish();
        }
	}

	// Increases writePos by one and updates the write pointers
	// First, write to the write pointers, then call this function to mark the data as available for reading.
	inline void publish()
	{
		writePos = (writePos + 1) % BUFFER_QUEUE_SIZE;
		writePtr[0] = &data1Left[writePos * AUDIO_BLOCK_SAMPLES];
		writePtr[1] = &data1Right[writePos * AUDIO_BLOCK_SAMPLES];
    writePtr[2] = &data2Left[writePos * AUDIO_BLOCK_SAMPLES];
		writePtr[3] = &data2Right[writePos * AUDIO_BLOCK_SAMPLES];
		available++;

		// writing over the tail of the circular buffer. Should never happen as read and write should be synchronous!
		if (available > BUFFER_QUEUE_SIZE)
			consume(); // consume and discard one block
	}

	// increase ReadPos by one and updates the read pointers.
	// First read from the read pointers, then call this function to mark the data as dispensed.
	inline void consume()
	{
		if (available <= 0)
			return;

		readPos = (readPos + 1) % BUFFER_QUEUE_SIZE;
		readPtr[0] = &data1Left[readPos * AUDIO_BLOCK_SAMPLES];
		readPtr[1] = &data1Right[readPos * AUDIO_BLOCK_SAMPLES];
    readPtr[2] = &data1Left[readPos * AUDIO_BLOCK_SAMPLES];
		readPtr[3] = &data1Right[readPos * AUDIO_BLOCK_SAMPLES];
		available--;
	}
};
