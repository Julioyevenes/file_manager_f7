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

/* Private types -------------------------------------------------------------*/
typedef struct
{
	uint32_t ChunkID;       /* 0 */
	uint32_t FileSize;      /* 4 */
	uint32_t FileFormat;    /* 8 */
	uint32_t SubChunk1ID;   /* 12 */
	uint32_t SubChunk1Size; /* 16 */
	uint16_t AudioFormat;   /* 20 */
	uint16_t NbrChannels;   /* 22 */
	uint32_t SampleRate;    /* 24 */

	uint32_t ByteRate;      /* 28 */
	uint16_t BlockAlign;    /* 32 */
	uint16_t BitPerSample;  /* 34 */
	uint32_t SubChunk2ID;   /* 36 */
	uint32_t SubChunk2Size; /* 40 */
} wav_lib_info_t;

/* Private constants ---------------------------------------------------------*/
/* Private macro -------------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/
audio_lib_buffer_t wav_buffer;
wav_lib_info_t wav_info;

/* Private function prototypes -----------------------------------------------*/
void wav_lib_start(audio_lib_handle_t * hlib);
void wav_lib_process(audio_lib_handle_t * hlib);
void wav_lib_free(audio_lib_handle_t * hlib);
void wav_lib_transfer_complete_cb(audio_lib_handle_t * hlib);
void wav_lib_transfer_half_cb(audio_lib_handle_t * hlib);

const audio_lib_t wav_lib = 
{
	wav_lib_start,
	wav_lib_process,
	wav_lib_free,
	wav_lib_transfer_complete_cb,
	wav_lib_transfer_half_cb
};

void wav_lib_start(audio_lib_handle_t * hlib)
{
	uint32_t bytesread;

	hlib->prv_data = &wav_info;
	hlib->buffer = &wav_buffer;
	hlib->buffer->size = 8192;
	hlib->buffer->ptr = (uint8_t *) malloc(hlib->buffer->size);
	if(!hlib->buffer->ptr)
	{
		hlib->err = AUDIO_LIB_MEMORY_ERROR;
		return;
	}
	
	if( f_read (hlib->fp, &wav_info, sizeof(wav_info), (UINT *) &bytesread) != FR_OK )
	{
		hlib->err = AUDIO_LIB_READ_ERROR;
		return;
	}
	
	hlib->time.total_time = hlib->fp->fsize / wav_info.ByteRate;

	if(BSP_AUDIO_OUT_Init(OUTPUT_DEVICE_BOTH, hlib->volume, wav_info.SampleRate) == 0)
	{
		BSP_AUDIO_OUT_SetAudioFrameSlot(CODEC_AUDIOFRAME_SLOT_02);
	}
	else
	{
		hlib->err = AUDIO_LIB_HARDWARE_ERROR;
		return;
	}	

	hlib->buffer->state = AUDIO_LIB_BUFFER_OFFSET_NONE;

	f_read (hlib->fp, hlib->buffer->ptr, hlib->buffer->size, (UINT *) &bytesread);
	if(bytesread != hlib->buffer->size)
	{
		hlib->err = AUDIO_LIB_READ_ERROR;
		return;
	}

	hlib->playback_state = AUDIO_LIB_STATE_PLAY;
	BSP_AUDIO_OUT_Play((uint16_t *) hlib->buffer->ptr, hlib->buffer->size);
	
	hlib->err = AUDIO_LIB_NO_ERROR;
	return;
}

void wav_lib_process(audio_lib_handle_t * hlib)
{
	uint32_t bytesread;
	wav_lib_info_t * info = hlib->prv_data;

	switch(hlib->playback_state)
  	{
		case AUDIO_LIB_STATE_PLAY:
			if(hlib->buffer->state == AUDIO_LIB_BUFFER_OFFSET_HALF)
			{
				f_read (hlib->fp, hlib->buffer->ptr, hlib->buffer->size / 2, (UINT *) &bytesread);
				if(!bytesread)
				{ 
					if(hlib->fp->fptr != hlib->fp->fsize)
					{
						BSP_AUDIO_OUT_Stop(CODEC_PDWN_HW);
						hlib->err = AUDIO_LIB_READ_ERROR;
						return;
					}
					else
					{
						hlib->playback_state = AUDIO_LIB_STATE_STOP;
					}
				} 
				hlib->buffer->state = AUDIO_LIB_BUFFER_OFFSET_NONE;
			}

			if(hlib->buffer->state == AUDIO_LIB_BUFFER_OFFSET_FULL)
			{
				f_read (hlib->fp, hlib->buffer->ptr + (hlib->buffer->size / 2), hlib->buffer->size / 2, (UINT *) &bytesread);
				if(!bytesread)
				{ 
					if(hlib->fp->fptr != hlib->fp->fsize)
					{
						BSP_AUDIO_OUT_Stop(CODEC_PDWN_HW);
						hlib->err = AUDIO_LIB_READ_ERROR;
						return;
					}
					else
					{
						hlib->playback_state = AUDIO_LIB_STATE_STOP;
					}
				} 
				hlib->buffer->state = AUDIO_LIB_BUFFER_OFFSET_NONE;
			}

			hlib->time.elapsed_time = hlib->fp->fptr / info->ByteRate;
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

void wav_lib_free(audio_lib_handle_t * hlib)
{
	free(hlib->buffer->ptr);
	hlib->buffer->ptr = NULL;
}

void wav_lib_transfer_complete_cb(audio_lib_handle_t * hlib)
{
	if(hlib->playback_state == AUDIO_LIB_STATE_PLAY)
	{
		hlib->buffer->state = AUDIO_LIB_BUFFER_OFFSET_FULL;
	}	
}

void wav_lib_transfer_half_cb(audio_lib_handle_t * hlib)
{
	if(hlib->playback_state == AUDIO_LIB_STATE_PLAY)
	{
		hlib->buffer->state = AUDIO_LIB_BUFFER_OFFSET_HALF;
	}	
}
