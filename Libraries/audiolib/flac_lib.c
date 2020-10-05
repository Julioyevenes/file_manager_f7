/**
  ******************************************************************************
  * @file    ${file_name} 
  * @author  ${user}
  * @version 
  * @date    ${date}
  * @brief   
  ******************************************************************************
  * @attention
  *
  * This program is free software: you can redistribute it and/or modify
  * it under the terms of the GNU General Public License as published by
  * the Free Software Foundation, either version 3 of the License, or
  * (at your option) any later version.
  *
  * This program is distributed in the hope that it will be useful,
  * but WITHOUT ANY WARRANTY; without even the implied warranty of
  * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  * GNU General Public License for more details.
  *
  * You should have received a copy of the GNU General Public License
  * along with this program.  If not, see <http://www.gnu.org/licenses/>.
  *
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "libdef.h"
#include "ff.h"
#include "stm32f769i_discovery_audio.h"

#include "FLAC/stream_decoder.h"

/* Private types -------------------------------------------------------------*/
typedef struct
{
	uint32_t 				blocksize;
	uint32_t 				channels;
	uint32_t 				bps;
} flac_lib_frame_t;

typedef struct
{
	FLAC__uint64 			total_samples;
	uint32_t 				sample_rate;
	uint32_t 				channels;
	uint32_t 				bps;
} flac_lib_metadata_t;

typedef struct
{
	uint8_t					play_idx;
	uint8_t					deco_idx;
	
	uint32_t				bitrate;
	uint32_t 				frame_size;

	FLAC__StreamDecoder *	h;
	
	flac_lib_frame_t		frame;
	flac_lib_metadata_t		metadata;
} flac_lib_codec_t;

/* Private constants ---------------------------------------------------------*/
#define FLAC_LIB_AUDIOBUF_SIZE	(0x9000)	/* 36864 byte */
#define FLAC_LIB_AUDIOBUF_NUM	2

/* Private macro -------------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/
audio_lib_buffer_t flac_buffer[FLAC_LIB_AUDIOBUF_NUM];
flac_lib_codec_t flac_codec;

/* Private function prototypes -----------------------------------------------*/
void 									flac_lib_start(audio_lib_handle_t * hlib);
void 									flac_lib_process(audio_lib_handle_t * hlib);
void 									flac_lib_free(audio_lib_handle_t * hlib);
void 									flac_lib_transfer_complete_cb(audio_lib_handle_t * hlib);
void 									flac_lib_transfer_half_cb(audio_lib_handle_t * hlib);

void 									FLAC_AddAudioBuf(audio_lib_handle_t * hlib);

static FLAC__StreamDecoderReadStatus 	flac_lib_read_cb(const FLAC__StreamDecoder *decoder, FLAC__byte buffer[], size_t *bytes, void *client_data);
static FLAC__StreamDecoderWriteStatus 	flac_lib_write_cb(const FLAC__StreamDecoder *decoder, const FLAC__Frame *frame, const FLAC__int32 * const buffer[], void *client_data);
static FLAC__StreamDecoderSeekStatus 	flac_lib_seek_cb(const FLAC__StreamDecoder *decoder, FLAC__uint64 absolute_byte_offset, void *client_data);
static FLAC__StreamDecoderTellStatus 	flac_lib_tell_cb(const FLAC__StreamDecoder *decoder, FLAC__uint64 *absolute_byte_offset, void *client_data);
static FLAC__StreamDecoderLengthStatus 	flac_lib_length_cb(const FLAC__StreamDecoder *decoder, FLAC__uint64 *stream_length, void *client_data);
static FLAC__bool 						flac_lib_eof_cb(const FLAC__StreamDecoder *decoder, void *client_data);
static void 							flac_lib_metadata_cb(const FLAC__StreamDecoder *decoder, const FLAC__StreamMetadata *metadata, void *client_data);
static void 							flac_lib_error_cb(const FLAC__StreamDecoder *decoder, FLAC__StreamDecoderErrorStatus status, void *client_data);

const audio_lib_t flac_lib = 
{
	flac_lib_start,
	flac_lib_process,
	flac_lib_free,
	flac_lib_transfer_complete_cb,
	flac_lib_transfer_half_cb
};

void flac_lib_start(audio_lib_handle_t * hlib)
{
	uint8_t * ptr;
	
	/* Register variables */
	hlib->prv_data = &flac_codec;
	hlib->buffer = &flac_buffer[0];

	flac_codec.h = FLAC__stream_decoder_new();
	if(flac_codec.h == 0)
	{
		hlib->err = AUDIO_LIB_MEMORY_ERROR;
		return;
	}
	
	FLAC__stream_decoder_init_stream(flac_codec.h, 
									 flac_lib_read_cb,
									 flac_lib_seek_cb,
									 flac_lib_tell_cb,
									 flac_lib_length_cb,
									 flac_lib_eof_cb,
									 flac_lib_write_cb, 
									 flac_lib_metadata_cb,
									 flac_lib_error_cb,
									 hlib);

	ptr = (uint8_t *) malloc(FLAC_LIB_AUDIOBUF_SIZE);
	if(ptr == 0)
	{
		hlib->err = AUDIO_LIB_MEMORY_ERROR;
		return;
	}

	/* Init first half pcm buffer */
	flac_buffer[0].ptr = ptr;
	
	/* Decode and playback from audio buf 0 */
	flac_codec.deco_idx = flac_codec.play_idx = 0;
	flac_buffer[0].empty = flac_buffer[1].empty = 1;
	
	/* Decode the first metadata block */
	if (FLAC__stream_decoder_process_until_end_of_metadata(flac_codec.h) == true)
	{
		/* Then decode the first audio frame */
		FLAC__stream_decoder_process_single(flac_codec.h);

		flac_codec.bitrate = (uint32_t) (((float) hlib->fp->fsize / flac_codec.metadata.total_samples) * flac_codec.metadata.sample_rate * 8);
		flac_codec.frame_size = flac_codec.frame.blocksize * flac_codec.frame.channels * (flac_codec.frame.bps / 8);

		/* Init second half pcm buffer */
		flac_buffer[1].ptr = ptr + flac_codec.frame_size;

		/* Then decode the second audio frame */
		FLAC__stream_decoder_process_single(flac_codec.h);

		hlib->time.total_time = (hlib->fp->fsize * 8) / flac_codec.bitrate;
	}
	else
	{
		hlib->err = AUDIO_LIB_READ_ERROR;
		return;
	}

	/* Init hardware */
	if(BSP_AUDIO_OUT_Init(OUTPUT_DEVICE_BOTH, hlib->volume, flac_codec.metadata.sample_rate) == 0)
	{
		BSP_AUDIO_OUT_SetAudioFrameSlot(CODEC_AUDIOFRAME_SLOT_02);
	}
	else
	{
		hlib->err = AUDIO_LIB_HARDWARE_ERROR;
		return;
	}

	/* Start hardware */
	audio_lib_buffer_t * buf = &(hlib->buffer[0]);
	hlib->playback_state = AUDIO_LIB_STATE_PLAY;
	BSP_AUDIO_OUT_Play((uint16_t *) buf->ptr, flac_codec.frame_size * 2);
	
	hlib->err = AUDIO_LIB_NO_ERROR;
	return;		
}

void flac_lib_process(audio_lib_handle_t * hlib)
{
	flac_lib_codec_t * codec = hlib->prv_data;
	audio_lib_buffer_t * pbuf = &(hlib->buffer[codec->deco_idx]);
	
	switch(hlib->playback_state)
  	{
		case AUDIO_LIB_STATE_PLAY:
			/* wait for DMA transfer */
			if(pbuf->empty == 1)
			{
				/* Decoder one frame */
				if (FLAC__stream_decoder_process_single(codec->h) != true)
				{
					BSP_AUDIO_OUT_Stop(CODEC_PDWN_HW);
					hlib->err = AUDIO_LIB_READ_ERROR;
					return;
				}
			}

			if(f_eof(hlib->fp) == 1)
			{
				hlib->playback_state = AUDIO_LIB_STATE_STOP;
			}

			hlib->time.elapsed_time = (hlib->fp->fptr * 8) / codec->bitrate;
			break;

		case AUDIO_LIB_STATE_STOP:
			BSP_AUDIO_OUT_Stop(CODEC_PDWN_HW);
			hlib->active = 0;
			break;

		case AUDIO_LIB_STATE_PAUSE:
			BSP_AUDIO_OUT_Pause();
			hlib->playback_state = AUDIO_LIB_STATE_WAIT;
			break;

		case AUDIO_LIB_STATE_RESUME:
			BSP_AUDIO_OUT_Resume();
			hlib->playback_state = AUDIO_LIB_STATE_PLAY;
			break;
		
		case AUDIO_LIB_STATE_WAIT:
  		case AUDIO_LIB_STATE_IDLE:
  		case AUDIO_LIB_STATE_INIT:
		default:
			break;
	}

	hlib->err = AUDIO_LIB_NO_ERROR;
	return;		
}

void flac_lib_free(audio_lib_handle_t * hlib)
{
	flac_lib_codec_t * codec = hlib->prv_data;
	
	FLAC__stream_decoder_delete(codec->h);
	codec->h = NULL;

	free(hlib->buffer->ptr);
	hlib->buffer->ptr = NULL;	
}

void flac_lib_transfer_complete_cb(audio_lib_handle_t * hlib)
{
	flac_lib_codec_t * codec = hlib->prv_data;
	audio_lib_buffer_t * pbuf = &(hlib->buffer[codec->play_idx]);
		
	if(hlib->playback_state == AUDIO_LIB_STATE_PLAY)
	{
		/* Data in buffer[codec->play_idx] has been sent out */
		pbuf->empty = 1;
		pbuf->size = -1;

		/* Send the data in next audio buffer */
		codec->play_idx++;
		if (codec->play_idx == FLAC_LIB_AUDIOBUF_NUM)
			codec->play_idx = 0;

		if (pbuf->empty == 1) {
			/* If empty==1, it means read file+decoder is slower than playback
		 	 (it will cause noise) or playback is over (it is ok). */;
		}
	}	
}

void flac_lib_transfer_half_cb(audio_lib_handle_t * hlib)
{
	flac_lib_codec_t * codec = hlib->prv_data;
	audio_lib_buffer_t * pbuf = &(hlib->buffer[codec->play_idx]);
		
	if(hlib->playback_state == AUDIO_LIB_STATE_PLAY)
	{
		/* Data in buffer[codec->play_idx] has been sent out */
		pbuf->empty = 1;
		pbuf->size = -1;

		/* Send the data in next audio buffer */
		codec->play_idx++;
		if (codec->play_idx == FLAC_LIB_AUDIOBUF_NUM)
			codec->play_idx = 0;

		if (pbuf->empty == 1) {
			/* If empty==1, it means read file+decoder is slower than playback
		 	 (it will cause noise) or playback is over (it is ok). */;
		}
	}	
}

/**
 * @brief  Add an PCM frame to audio buf after decoding.
 */
void FLAC_AddAudioBuf(audio_lib_handle_t * hlib)
{
	flac_lib_codec_t * codec = hlib->prv_data;
	audio_lib_buffer_t * pbuf = &(hlib->buffer[codec->deco_idx]);
	
	/* Mark the status to not-empty which means it is available to playback. */
	pbuf->empty = 0;
	pbuf->size = codec->frame_size;

	/* Point to the next buffer */
	codec->deco_idx ++;
	if (codec->deco_idx == FLAC_LIB_AUDIOBUF_NUM)
		codec->deco_idx = 0;
}

/**
 * @brief  Read data callback. Called when decoder needs more input data.
 */
FLAC__StreamDecoderReadStatus flac_lib_read_cb(const FLAC__StreamDecoder* decoder, FLAC__byte buffer[], size_t* bytes, void* client_data)
{
	audio_lib_handle_t * hlib = client_data;
	uint32_t bytesread;
	
    if (*bytes > 0) 
    {
        // read data directly into buffer
		f_read (hlib->fp, buffer, *bytes * sizeof(FLAC__byte), (UINT *) &bytesread);
		*bytes = bytesread / sizeof(FLAC__byte);
        if (f_error(hlib->fp)) {
            // read error -> abort
            return FLAC__STREAM_DECODER_READ_STATUS_ABORT;
        }
        else if (*bytes == 0) {
            // EOF
            return FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM;
        }
        else {
            // OK, continue decoding
            return FLAC__STREAM_DECODER_READ_STATUS_CONTINUE;
        }
    }
    else {
        // decoder called but didn't want ay bytes -> abort
        return FLAC__STREAM_DECODER_READ_STATUS_ABORT;
    }	
}

/**
 * @brief  Write callback. Called when decoder has decoded a single audio frame.
 */
FLAC__StreamDecoderWriteStatus flac_lib_write_cb(const FLAC__StreamDecoder* decoder, const FLAC__Frame* frame, 
                                              const FLAC__int32* const buffer[], void* client_data)
{
	audio_lib_handle_t * hlib = client_data;
	flac_lib_codec_t * codec = hlib->prv_data;
	uint32_t * ptr = (uint32_t *) hlib->buffer[codec->deco_idx].ptr;
	uint32_t sample, size;

	if(codec->metadata.total_samples == 0) {
		return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
	}
	if(codec->metadata.channels != 2 || codec->metadata.bps != 16) {
		return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
	}

	codec->frame.blocksize = frame->header.blocksize;
	codec->frame.channels = frame->header.channels;
	codec->frame.bps = frame->header.bits_per_sample;
    size = codec->frame.blocksize * codec->frame.channels * (codec->frame.bps / 8);

    for (sample = 0; sample < codec->frame.blocksize; sample++) {
    	ptr[sample] = (buffer[0][sample] << 16) | (buffer[1][sample] & 0xFFFF);
    }
	FLAC_AddAudioBuf (hlib);

	return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
}

/**
 * @brief  Seek callback. Called when decoder needs to seek the stream.
 */
FLAC__StreamDecoderSeekStatus flac_lib_seek_cb(const FLAC__StreamDecoder* decoder, FLAC__uint64 absolute_byte_offset, void* client_data)
{
	audio_lib_handle_t * hlib = client_data;
	
	if (f_lseek(hlib->fp, (off_t)absolute_byte_offset) < 0) {
        // seek failed
        return FLAC__STREAM_DECODER_SEEK_STATUS_ERROR;
    }
    else {
        return FLAC__STREAM_DECODER_SEEK_STATUS_OK;
    }	
}

/**
 * @brief  Tell callback. Called when decoder wants to know current position of stream.
 */
FLAC__StreamDecoderTellStatus flac_lib_tell_cb(const FLAC__StreamDecoder* decoder, FLAC__uint64* absolute_byte_offset, void* client_data)
{
	audio_lib_handle_t * hlib = client_data;
	
    if (f_tell(hlib->fp) < 0) {
        // seek failed
        return FLAC__STREAM_DECODER_TELL_STATUS_ERROR;
    }
    else {
        // update offset
        *absolute_byte_offset = (FLAC__uint64) f_tell(hlib->fp);
        return FLAC__STREAM_DECODER_TELL_STATUS_OK;
    }	
}

/**
 * @brief  Length callback. Called when decoder wants total length of stream.
 */
FLAC__StreamDecoderLengthStatus flac_lib_length_cb(const FLAC__StreamDecoder* decoder, FLAC__uint64* stream_length, void* client_data)
{
	audio_lib_handle_t * hlib = client_data;
	
    if (f_size(hlib->fp) == 0) {
        // failed
        return FLAC__STREAM_DECODER_LENGTH_STATUS_ERROR;
    }
    else {
        // pass on length
        *stream_length = (FLAC__uint64) f_size(hlib->fp);
        return FLAC__STREAM_DECODER_LENGTH_STATUS_OK;
    }	
}

/**
 * @brief  EOF callback. Called when decoder wants to know if end of stream is reached.
 */
FLAC__bool flac_lib_eof_cb(const FLAC__StreamDecoder* decoder, void* client_data)
{
	audio_lib_handle_t * hlib = client_data;
	
	return f_eof(hlib->fp);
}

/**
 * @brief  Metadata callback. Called when decoder has decoded metadata.
 */
void flac_lib_metadata_cb(const FLAC__StreamDecoder* decoder, const FLAC__StreamMetadata* metadata, void* client_data)
{
	audio_lib_handle_t * hlib = client_data;
	flac_lib_codec_t * codec = hlib->prv_data;
	
	if(metadata->type == FLAC__METADATA_TYPE_STREAMINFO)
	{
		/* save for later */
		codec->metadata.total_samples = metadata->data.stream_info.total_samples;
		codec->metadata.sample_rate = metadata->data.stream_info.sample_rate;
		codec->metadata.channels = metadata->data.stream_info.channels;
		codec->metadata.bps = metadata->data.stream_info.bits_per_sample;		
	}
}

/**
 * @brief  Error callback. Called when error occured during decoding.
 */
void flac_lib_error_cb(const FLAC__StreamDecoder* decoder, FLAC__StreamDecoderErrorStatus status, void* client_data)
{
}	
