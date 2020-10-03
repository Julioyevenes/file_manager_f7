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
#include <string.h>
#include "libdef.h"
#include "ff.h"
#include "stm32f769i_discovery_audio.h"

#include "mp3dec.h"

/* Private types -------------------------------------------------------------*/
typedef struct
{
	uint8_t			play_idx;
	uint8_t			deco_idx;	
	
	uint8_t *		read_buf;
	uint8_t *		read_ptr;

	uint32_t		bytes_left;
	uint32_t 		frame_size;
	uint32_t		no_data : 1;
	uint32_t		eof : 1;	
	
	HMP3Decoder 	h;
	MP3FrameInfo 	info;
} mp3_lib_codec_t;

/* Private constants ---------------------------------------------------------*/
#define MP3_LIB_READBUF_SIZE    (0x1000)  	/* 4096 byte */
#define MP3_LIB_AUDIOBUF_SIZE   (0x2400)	/* 9216 byte */
#define MP3_LIB_AUDIOBUF_NUM 	2

/* Private macro -------------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/
audio_lib_buffer_t mp3_buffer[MP3_LIB_AUDIOBUF_NUM];
mp3_lib_codec_t mp3_codec;

/* Private function prototypes -----------------------------------------------*/
void 	mp3_lib_start(audio_lib_handle_t * hlib);
void 	mp3_lib_process(audio_lib_handle_t * hlib);
void 	mp3_lib_free(audio_lib_handle_t * hlib);
void 	mp3_lib_transfer_complete_cb(audio_lib_handle_t * hlib);
void 	mp3_lib_transfer_half_cb(audio_lib_handle_t * hlib);

uint8_t MP3_DecodeFrame(audio_lib_handle_t * hlib);
int 	MP3_FillReadBuf(audio_lib_handle_t * hlib, uint32_t bytesAlign);
void 	MP3_AddAudioBuf(audio_lib_handle_t * hlib);

const audio_lib_t mp3_lib = 
{
	mp3_lib_start,
	mp3_lib_process,
	mp3_lib_free,
	mp3_lib_transfer_complete_cb,
	mp3_lib_transfer_half_cb
};

void mp3_lib_start(audio_lib_handle_t * hlib)
{
	uint8_t * ptr;
	
	/* Register variables */
	hlib->prv_data = &mp3_codec;
	hlib->buffer = &mp3_buffer[0];
	
	/* Init variables */
	mp3_codec.bytes_left = 0;
	mp3_codec.no_data = 0;
	mp3_codec.eof = 0;
	mp3_codec.frame_size = 0;
	
	mp3_codec.h = MP3InitDecoder();
	if(mp3_codec.h == 0)
	{
		hlib->err = AUDIO_LIB_MEMORY_ERROR;
		return;
	}

	mp3_codec.read_ptr = mp3_codec.read_buf = (uint8_t *) malloc(MP3_LIB_READBUF_SIZE);
	if(mp3_codec.read_ptr == 0)
	{
		hlib->err = AUDIO_LIB_MEMORY_ERROR;
		return;
	}

	ptr = (uint8_t *) malloc(MP3_LIB_AUDIOBUF_SIZE);
	if(ptr == 0)
	{
		hlib->err = AUDIO_LIB_MEMORY_ERROR;
		return;
	}

	mp3_buffer[0].ptr = ptr;
	mp3_buffer[1].ptr = ptr + (MP3_LIB_AUDIOBUF_SIZE / 2);
	
	/* Decode and playback from audio buf 0 */
	mp3_codec.deco_idx = mp3_codec.play_idx = 0;
	mp3_buffer[0].empty = mp3_buffer[1].empty = 1;
	
	/* Decode the first frame to get the frame format */
	if (MP3_DecodeFrame(hlib) == 0)
	{
		mp3_codec.frame_size = (mp3_codec.info.bitsPerSample / 8) * mp3_codec.info.outputSamps;
		MP3_AddAudioBuf (hlib);

		hlib->time.total_time = (hlib->fp->fsize * 8) / ((mp3_codec.info.bitrate / 1000) * 1024);
	}
	else
	{
		hlib->err = AUDIO_LIB_READ_ERROR;
		return;
	}

	/* Init hardware */
	if(BSP_AUDIO_OUT_Init(OUTPUT_DEVICE_BOTH, hlib->volume, mp3_codec.info.samprate) == 0)
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
	BSP_AUDIO_OUT_Play((uint16_t *) buf->ptr, buf->size * 2);
	
	hlib->err = AUDIO_LIB_NO_ERROR;
	return;	
}

void mp3_lib_process(audio_lib_handle_t * hlib)
{
	mp3_lib_codec_t * codec = hlib->prv_data;
	audio_lib_buffer_t * pbuf = &(hlib->buffer[codec->deco_idx]);
	
	switch(hlib->playback_state)
  	{
		case AUDIO_LIB_STATE_PLAY:
			/* wait for DMA transfer */
			if(pbuf->empty == 1)
			{
				/* Decoder one frame */
				if (MP3_DecodeFrame(hlib) == 0)
				{
					codec->frame_size = (codec->info.bitsPerSample / 8) * codec->info.outputSamps;
					MP3_AddAudioBuf (hlib);
				}
			}

			if(codec->no_data == 1 || codec->eof == 1)
			{
				hlib->playback_state = AUDIO_LIB_STATE_STOP;
			}

			hlib->time.elapsed_time = (hlib->fp->fptr * 8) / ((codec->info.bitrate / 1000) * 1024);
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

void mp3_lib_free(audio_lib_handle_t * hlib)
{
	mp3_lib_codec_t * codec = hlib->prv_data;
	
	MP3FreeDecoder(codec->h);
	codec->h = NULL;
	
	free(codec->read_buf);
	codec->read_ptr = codec->read_buf = NULL;
	
	free(hlib->buffer->ptr);
	hlib->buffer->ptr = NULL;	
}

void mp3_lib_transfer_complete_cb(audio_lib_handle_t * hlib)
{
	mp3_lib_codec_t * codec = hlib->prv_data;
	audio_lib_buffer_t * pbuf = &(hlib->buffer[codec->play_idx]);
		
	if(hlib->playback_state == AUDIO_LIB_STATE_PLAY)
	{
		/* Data in buffer[codec->play_idx] has been sent out */
		pbuf->empty = 1;
		pbuf->size = -1;

		/* Send the data in next audio buffer */
		codec->play_idx++;
		if (codec->play_idx == MP3_LIB_AUDIOBUF_NUM)
			codec->play_idx = 0;

		if (pbuf->empty == 1) {
			/* If empty==1, it means read file+decoder is slower than playback
		 	 (it will cause noise) or playback is over (it is ok). */;
		}
	}	
}

void mp3_lib_transfer_half_cb(audio_lib_handle_t * hlib)
{
	mp3_lib_codec_t * codec = hlib->prv_data;
	audio_lib_buffer_t * pbuf = &(hlib->buffer[codec->play_idx]);
		
	if(hlib->playback_state == AUDIO_LIB_STATE_PLAY)
	{
		/* Data in buffer[codec->play_idx] has been sent out */
		pbuf->empty = 1;
		pbuf->size = -1;

		/* Send the data in next audio buffer */
		codec->play_idx++;
		if (codec->play_idx == MP3_LIB_AUDIOBUF_NUM)
			codec->play_idx = 0;

		if (pbuf->empty == 1) {
			/* If empty==1, it means read file+decoder is slower than playback
		 	 (it will cause noise) or playback is over (it is ok). */;
		}
	}	
}

/**
 * @brief  Decode a frame.
 */
uint8_t MP3_DecodeFrame(audio_lib_handle_t * hlib)
{
	mp3_lib_codec_t * codec = hlib->prv_data;
	audio_lib_buffer_t * pbuf = &(hlib->buffer[codec->deco_idx]);
	uint8_t word_align, frame_decoded;
	int nRead, offset, err;

	frame_decoded = 0;
	word_align = 0;
	nRead = 0;
	offset = 0;

	do
	{
		/* somewhat arbitrary trigger to refill buffer - should always be enough for a full frame */
		if (codec->bytes_left < 2 * MAINBUF_SIZE && !codec->eof) {
			/* Align to 4 bytes */
			word_align = (4 - (codec->bytes_left & 3)) & 3;

			/* Fill read buffer */
			nRead = MP3_FillReadBuf(hlib, word_align);

			codec->bytes_left += nRead;
			codec->read_ptr = codec->read_buf + word_align;
			if (nRead == 0) {
				codec->eof = 1; /* end of file */
				codec->no_data = 1;
			}
		}

		/* find start of next MP3 frame - assume EOF if no sync found */
		offset = MP3FindSyncWord(codec->read_ptr, codec->bytes_left);
		if (offset < 0) {
			codec->read_ptr = codec->read_buf;
			codec->bytes_left = 0;
			continue;
		}
		codec->read_ptr += offset;
		codec->bytes_left -= offset;

		//simple check for valid header
		if (((*(codec->read_ptr + 1) & 24) == 8) || ((*(codec->read_ptr + 1) & 6) != 2) || ((*(codec->read_ptr + 2) & 240) == 240) || ((*(codec->read_ptr + 2) & 12) == 12)
				|| ((*(codec->read_ptr + 3) & 3) == 2)) {
			codec->read_ptr += 1; //header not valid, try next one
			codec->bytes_left -= 1;
			continue;
		}

		err = MP3Decode(codec->h, &codec->read_ptr, (int *) &codec->bytes_left, (short *) pbuf->ptr, 0);
		if (err == -6) {
			codec->read_ptr += 1;
			codec->bytes_left -= 1;
			continue;
		}

		if (err) {
			/* error occurred */
			switch (err) {
				case ERR_MP3_INDATA_UNDERFLOW:
					/* do nothing - next call to decode will provide more inData */
					break;
				case ERR_MP3_MAINDATA_UNDERFLOW:
					/* do nothing - next call to decode will provide more mainData */
					break;
				case ERR_MP3_INVALID_HUFFCODES:
					/* do nothing */
					break;
				case ERR_MP3_FREE_BITRATE_SYNC:
				default:
					codec->no_data = 1;
					break;
			}
		} else {
			/* no error */
			MP3GetLastFrameInfo(codec->h, &(codec->info));
			frame_decoded = 1;
		}

	} while (!frame_decoded && !codec->no_data);

	if (codec->no_data == 1)
		return 0x1; /* Decoder terminated */
	else return 0x0; /* Decoder success. */
}

/**
 * @brief  Read data from MP3 file and fill in the Read Buffer.
 */
int MP3_FillReadBuf(audio_lib_handle_t * hlib, uint32_t bytesAlign)
{
	mp3_lib_codec_t * codec = hlib->prv_data;
	uint32_t nRead;

	/* Move the left bytes from the end to the front */
	memmove(codec->read_buf + bytesAlign, codec->read_ptr, codec->bytes_left);

		f_read (hlib->fp, (void *) (codec->read_buf + codec->bytes_left + bytesAlign), MP3_LIB_READBUF_SIZE - codec->bytes_left - bytesAlign, (UINT *) &nRead);
		/* zero-pad to avoid finding false sync word after last frame (from old data in codec->read_buf) */
		if (nRead < MP3_LIB_READBUF_SIZE - codec->bytes_left - bytesAlign)
			memset(codec->read_buf + bytesAlign + codec->bytes_left + nRead, 0, MP3_LIB_READBUF_SIZE - bytesAlign - codec->bytes_left - nRead);

	return nRead;
}

/**
 * @brief  Add an PCM frame to audio buf after decoding.
 */
void MP3_AddAudioBuf(audio_lib_handle_t * hlib)
{
	mp3_lib_codec_t * codec = hlib->prv_data;
	audio_lib_buffer_t * pbuf = &(hlib->buffer[codec->deco_idx]);
	
	/* Mark the status to not-empty which means it is available to playback. */
	pbuf->empty = 0;
	pbuf->size = codec->frame_size;

	/* Point to the next buffer */
	codec->deco_idx ++;
	if (codec->deco_idx == MP3_LIB_AUDIOBUF_NUM)
		codec->deco_idx = 0;
}
