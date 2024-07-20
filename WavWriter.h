/** Audio Recording Module
 ** 
 ** Record audio into a working buffer that is gradually written to a WAV file on an SD Card. 
 **
 ** Recordings are made with floating point input, and will be converted to the 
 ** specified bits per sample internally 
 **
 ** For now only 16-bit and 32-bit (signed int) formats are supported
 ** f32 and s24 formats will be added next
 **
 ** The transfer size determines the amount of internal memory used, and can have an
 ** effect on the performance of the streaming behavior of the WavWriter.
 ** Memory use can be calculated as: (2 * transfer_size) bytes
 ** Performance optimal with sizes: 16384, 32768
 ** 
 ** To use:
 ** 1. Create a WavWriter<size> object (e.g. WavWriter<32768> writer)
 ** 2. Configure the settings as desired by creating a WavWriter<32768>::Config struct and setting the settings.
 ** 3. Initialize the object with the configuration struct.
 ** 4. Open a new file for writing with: writer.OpenFile("FileName.wav")
 ** 5. Write to it within your audio callback using: writer.Sample(value)
 ** 6. Fill the Wav File on the SD Card with data from your main loop by running: writer.Write()
 ** 7. When finished with the recording finalize, and close the file with: writer.SaveFile();
 ** 
 ** */
#include "Arduino.h" 
#include "AudioConfig.h"
#include <SD.h>

#ifndef WavWriter_h
  #define WavWriter_h



// Taken from https://github.com/electro-smith/libDaisy/blob/f7727edb9a1febdd174b5310a7bc65340dae8700/src/daisy_core.h#L128
#define FBIPMAX 0.999985f             /**< close to 1.0f-LSB at 16 bit */
#define FBIPMIN (-FBIPMAX)            /**< - (1 - LSB) */
#define U82F_SCALE 0.0078740f         /**< 1 / 127 */
#define F2U8_SCALE 127.0f             /**< 128 - 1 */
#define S82F_SCALE 0.0078125f         /**< 1 / (2**7) */
#define F2S8_SCALE 127.0f             /**< (2 ** 7) - 1 */
#define S162F_SCALE 3.0517578125e-05f /**< 1 / (2** 15) */
#define F2S16_SCALE 32767.0f          /**< (2 ** 15) - 1 */
#define F2S24_SCALE 8388608.0f        /**< 2 ** 23 */
#define S242F_SCALE 1.192092896e-07f  /**< 1 / (2 ** 23) */
#define S24SIGN 0x800000              /**< 2 ** 23 */
#define S322F_SCALE 4.6566129e-10f    /**< 1 / (2** 31) */
#define F2S32_SCALE 2147483647.f      /**< (2 ** 31) - 1 */

/** Constants for In-Header IDs */
const uint32_t kWavFileChunkId     = 0x46464952; /**< "RIFF" = 5249 4646 */ 
const uint32_t kWavFileWaveId      = 0x45564157; /**< "WAVE" */
const uint32_t kWavFileSubChunk1Id = 0x20746d66; /**< "fmt " */
const uint32_t kWavFileSubChunk2Id = 0x61746164; /**< "data" */

/** Standard Format codes for the waveform data.
	 ** 
	 ** According to spec, extensible should be used whenever:
	 ** * PCM data has more than 16 bits/sample
	 ** * The number of channels is more than 2
	 ** * The actual number of bits/sample is not equal to the container size
	 ** * The mapping from channels to speakers needs to be specified.
	 ** */
	enum WavFileFormatCode
	{
	    WAVE_FORMAT_PCM        = 0x0001,
	    WAVE_FORMAT_IEEE_FLOAT = 0x0003,
	    WAVE_FORMAT_ALAW       = 0x0006,
	    WAVE_FORMAT_ULAW       = 0x0007,
	    WAVE_FORMAT_EXTENSIBLE = 0xFFFE,
	};

/** Helper struct for handling the WAV file format */
typedef struct
{
    uint32_t ChunkId;       /**< & */
    uint32_t FileSize;      /**< & */
    uint32_t FileFormat;    /**< & */
    uint32_t SubChunk1ID;   /**< & */
    uint32_t SubChunk1Size; /**< & */
    uint16_t AudioFormat;   /**< & */
    uint16_t NbrChannels;   /**< & */
    uint32_t SampleRate;    /**< & */
    uint32_t ByteRate;      /**< & */
    uint16_t BlockAlign;    /**< & */
    uint16_t BitPerSample;  /**< & */
    uint32_t SubChunk2ID;   /**< & */
    uint32_t SubCHunk2Size; /**< & */
} WAV_FormatTypeDef;


/** State of the internal Writing mechanism. 
** When the buffer is a certain amount full one section will write its contents
** while the other is still being written to. This is performed circularly
** so that audio will be uninterrupted during writing. */
enum class BufferState
{
	IDLE,
	FLUSH0,
	FLUSH1,
};

template <size_t transfer_size> 
class WavWriter
{
  public:
    WavWriter() {}
    ~WavWriter() {}

	/**  Initializes the WavFile header, and prepares the object for recording. */
	void WavInit() //const Config &cfg)
	{
	    // cfg_       = cfg;
	    num_samps_ = 0;
	    // Prep the wav header according to config.
	    // Certain things (i.e. Size, etc. will have to wait until the finalization of the file, or be updated while streaming).
	    wavheader_.ChunkId       = kWavFileChunkId;     /** "RIFF" */
	    wavheader_.FileFormat    = kWavFileWaveId;      /** "WAVE" */   //aac1 3600
	    wavheader_.SubChunk1ID   = kWavFileSubChunk1Id; /** "fmt " */
	    wavheader_.SubChunk1Size = 16;                  // for PCM
	    wavheader_.AudioFormat   = WAVE_FORMAT_PCM;
	    wavheader_.NbrChannels   = LOCAL_CHANNELS;
	    wavheader_.SampleRate    = static_cast<int>(SAMPLERATE);
	    wavheader_.ByteRate      = SAMPLERATE * LOCAL_CHANNELS * (BIT_DEPTH / 8);
	    wavheader_.BlockAlign    = LOCAL_CHANNELS * (BIT_DEPTH / 8);
	    wavheader_.BitPerSample  = BIT_DEPTH;
	    wavheader_.SubChunk2ID   = kWavFileSubChunk2Id; /** "data" */
	    /** Also calcs SubChunk2Size */
	    wavheader_.FileSize =  CalcFileSize();
	    // This is calculated as part of the subchunk size
	}

	/** Opens a file for writing. Writes the initial WAV Header, and gets ready for stream-based recording. */
	void OpenFile(const char *name)
	{   
	    // Prefill known WAV file information
	   
	    Serial.println("Open File");
	    if (SD.exists(name)) {
	        // The SD library writes new data to the end of the
	        // file, so to start a new recording, the old file
	        // must be deleted before new data is written.
	        SD.remove(name);
	    }

	    fp_=SD.open(name, FILE_WRITE);
	    // unsigned int bw = 0;
	    fp_.write(&wavheader_, sizeof(wavheader_));
	    
	    recording_ = true;
	    num_samps_ = 0;
	}

	/** Records the current sample into the working buffer,
	**  queues writes to media when necessary. 
	** 
	** \param in should be a pointer to an array of samples 
	*/
	// void Sample(const float *in)

	void Sample(const int32_t *in)
	{
	    for(size_t i = 0; i < 1; i++)
	    {
	        switch(BIT_DEPTH)
	        {
	            case 16:
	              {
	                int16_t *tp;
	                tp            = (int16_t *)transfer_buff;
	                //tp[wptr_ + i] = f2s16(in[i]);
	                tp[wptr_ + i] = in[i];
	              }
	              break;
	            case 32: 
	              //transfer_buff[wptr_ + i] = f2s32(in[i]); 
	              transfer_buff[wptr_ + i] = (int32_t)in; 
	              // Test Samples coming in:
	              //Serial.println(in[i]);
	              break;
	            default: 
	              break;
	        }
	    }
	    num_samps_++;
	    wptr_ += 1;
	    size_t cap_point
	        = BIT_DEPTH == 16 ? kTransferSamps * 2 :kTransferSamps;
	    if(wptr_ == cap_point)
	    {
	        bstate_ = BufferState::FLUSH0;
	    }
	    if(wptr_ >= cap_point * 2)
	    {
	        wptr_   = 0;
	        bstate_ = BufferState::FLUSH1;
	    }
	}

	/** Check buffer state and write */
	void Write()
	{

	   
	    if(bstate_ != BufferState::IDLE && recording_)
	    {
	        uint32_t     offset;
	        // unsigned int bw = 0; //for error messaging
	        //offset          = bstate_ == BufferState::FLUSH0 ? 0 : transfer_size;
	        offset  = bstate_ == BufferState::FLUSH0 ? 0 : kTransferSamps;
	        bstate_ = BufferState::IDLE;
	        //f_write(&fp_, &transfer_buff[offset], transfer_size, &bw); //STM32
	        fp_.write(&transfer_buff[offset],transfer_size); // SD Arduino library
			// Serial.print("File size: ");
			//Serial.println(fp_.size());
	    }
	}

	/** Finalizes the writing of the WAV file.
	 ** This overwrites the WAV Header with the correct
	 ** final size, and closes the fptr. */

	void SaveFile()
	{
	    Serial.println("Save File & overwrites WAV header");
	    // unsigned int bw = 0;
	    recording_      = false;
	    // We _should_ flush whatever's left in the transfer buff
	    // TODO: that.

	    wavheader_.FileSize = CalcFileSize();
	    fp_.seek(0);
	    fp_.write(&wavheader_, sizeof(wavheader_));
	    fp_.close();
	}

	/**
    ** Configuration structure for the wave writer. (not used atm)
    ** */
    struct Config
    {
        float   samplerate;
        int32_t channels;
        int32_t bitspersample;
    };

    // Float conversion not implemented atm.
    
	// /**
	//     Converts float to Signed 16-bit
	// */
	// int16_t f2s16(float x)
	// {
	//     x = x <= FBIPMIN ? FBIPMIN : x;
	//     x = x >= FBIPMAX ? FBIPMAX : x;
	//     return (int32_t)(x * F2S16_SCALE);
	// }

	// /**
	//     Converts Signed 24-bit to float
	//  */
	// float s242f(int32_t x)
	// {
	//     x = (x ^ S24SIGN) - S24SIGN; //sign extend aka ((x<<8)>>8)
	//     return (float)x * S242F_SCALE;
	// }
	// /**
	//     Converts float to Signed 24-bit
	//  */
	// int32_t f2s24(float x)
	// {
	//     x = x <= FBIPMIN ? FBIPMIN : x;
	//     x = x >= FBIPMAX ? FBIPMAX : x;
	//     return (int32_t)(x * F2S24_SCALE);
	// }

	// /**
	//     Converts Signed 32-bit to float
	//  */
	// float s322f(int32_t x)
	// {
	//     return (float)x * S322F_SCALE;
	// }
	// /**
	//     Converts float to Signed 24-bit
	//  */
	// int32_t f2s32(float x)
	// {
	//     x = x <= FBIPMIN ? FBIPMIN : x;
	//     x = x >= FBIPMAX ? FBIPMAX : x;
	//     return (int32_t)(x * F2S32_SCALE);
	// }




	private:
		static constexpr int kTransferSamps = transfer_size / sizeof(int32_t);
		int32_t           transfer_buff[kTransferSamps * 2];
		uint32_t          num_samps_, wptr_; 
		File              fp_;  // The file where data is recorded
		bool 			  recording_;
		BufferState       bstate_;
		size_t 			  LOCAL_CHANNELS = 1; // For testing one channel
		WAV_FormatTypeDef wavheader_;


		inline uint32_t CalcFileSize()
		{
		    wavheader_.SubCHunk2Size
		        = num_samps_ * CHANNELS * BIT_DEPTH / 8;
		    return 36 + wavheader_.SubCHunk2Size;
		}
};
#endif