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
#include "hal_jpeg_codec.h"

/* Private types -------------------------------------------------------------*/
typedef struct
{
 	/* video */
 	uint16_t frame_width;
 	uint16_t frame_height;
 	uint8_t  frame_bytedepth;
 	uint8_t  frame_rate;
 	uint8_t  frame_jpeg;
 	uint32_t frame_maxsize;

 	/* audio */
 	uint8_t  audio_numchannels;
 	uint8_t  audio_bytedepth;
 	uint16_t audio_samplerate;
	uint32_t audio_byterate;
	uint32_t audio_totalsize;

 	/* padding */
 	uint8_t  pad[489];
} __attribute__ ((packed)) jmv_lib_header_t;

typedef struct
{
 	/* video */
 	uint32_t frame_size;

 	/* padding */
 	uint8_t  pad[508];
} __attribute__ ((packed)) jmv_lib_frame_t;

typedef struct
{
	jmv_lib_header_t header;
	jmv_lib_frame_t  frame;	
} jmv_lib_metadata_t;

typedef struct
{
	uint8_t *  jpeg_buff_ptr;
	
	uint32_t   audio_pos;
	uint32_t   jpeg_buff_size;	

	JPEG_ConfTypeDef   	jpeginfo;
	jpeg_codec_handle_t hjpegcodec;
	
	jmv_lib_metadata_t metadata;
} jmv_lib_codec_t;

/* Private constants ---------------------------------------------------------*/
#define JMV_LIB_JPEG_IMG_TIMEOUT 	500

/* Private macro -------------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/
audio_lib_buffer_t jmv_buffer;
jmv_lib_codec_t jmv_codec;

/* Private function prototypes -----------------------------------------------*/
void jmv_lib_start(audio_lib_handle_t * hlib);
void jmv_lib_process(audio_lib_handle_t * hlib);
void jmv_lib_free(audio_lib_handle_t * hlib);
void jmv_lib_transfer_complete_cb(audio_lib_handle_t * hlib);
void jmv_lib_transfer_half_cb(audio_lib_handle_t * hlib);

const audio_lib_t jmv_lib = 
{
	jmv_lib_start,
	jmv_lib_process,
	jmv_lib_free,
	jmv_lib_transfer_complete_cb,
	jmv_lib_transfer_half_cb
};

void jmv_lib_start(audio_lib_handle_t * hlib)
{
	uint32_t bytesread;
	JPEG_HandleTypeDef * hjpeg = (JPEG_HandleTypeDef *) hlib->img.codec;
	
	/* Read file header */
	f_read (hlib->fp, &jmv_codec.metadata.header, sizeof(jmv_lib_header_t), (UINT *) &bytesread);
	if(bytesread != sizeof(jmv_lib_header_t))
	{
		hlib->err = AUDIO_LIB_READ_ERROR;
		return;
	}

	if(jmv_codec.metadata.header.frame_jpeg == 0)
	{
		hlib->err = AUDIO_LIB_UNSUPPORTED_FORMAT;
		return;		
	}

	/* Register variables */	
	hlib->prv_data = &jmv_codec;
	hlib->buffer = &jmv_buffer;
	hlib->buffer->size = 2 * jmv_codec.metadata.header.audio_byterate / jmv_codec.metadata.header.frame_rate;
	hlib->buffer->ptr = (uint8_t *) malloc(hlib->buffer->size);
	if(!hlib->buffer->ptr)
	{
		hlib->err = AUDIO_LIB_MEMORY_ERROR;
		return;
	}
	
	hlib->time.total_time = jmv_codec.metadata.header.audio_totalsize / jmv_codec.metadata.header.audio_byterate;

	/* Init hardware */
	if(BSP_AUDIO_OUT_Init(OUTPUT_DEVICE_BOTH, hlib->volume, jmv_codec.metadata.header.audio_samplerate) == 0)
	{
		BSP_AUDIO_OUT_SetAudioFrameSlot(CODEC_AUDIOFRAME_SLOT_02);
	}
	else
	{
		hlib->err = AUDIO_LIB_HARDWARE_ERROR;
		return;
	}	

	hlib->buffer->state = AUDIO_LIB_BUFFER_OFFSET_NONE;

	/* Allocate jpeg read buffer */
	jmv_codec.jpeg_buff_size = jmv_codec.metadata.header.frame_maxsize;
	jmv_codec.jpeg_buff_ptr = (uint8_t *) malloc(jmv_codec.jpeg_buff_size);
	if(!jmv_codec.jpeg_buff_ptr)
	{
		hlib->err = AUDIO_LIB_MEMORY_ERROR;
		return;
	}

	/* Fill audio buffer */
	f_read (hlib->fp, hlib->buffer->ptr, hlib->buffer->size / 2, (UINT *) &bytesread);
	if(bytesread != hlib->buffer->size / 2)
	{
		hlib->err = AUDIO_LIB_READ_ERROR;
		return;
	}
	
	memcpy(hlib->buffer->ptr + hlib->buffer->size / 2, hlib->buffer->ptr, hlib->buffer->size / 2);	
	jmv_codec.audio_pos = bytesread;
	
	/* Read jpeg frame */
	f_read (hlib->fp, &jmv_codec.metadata.frame, sizeof(jmv_lib_frame_t), (UINT *) &bytesread);
	if(bytesread != sizeof(jmv_lib_frame_t))
	{
		hlib->err = AUDIO_LIB_READ_ERROR;
		return;
	}

	if(jmv_codec.metadata.frame.frame_size > jmv_codec.jpeg_buff_size)
	{
		hlib->err = AUDIO_LIB_READ_ERROR;
		return;		
	}
	
	/* Init JPEG codec */
	jpeg_decoder_init(&jmv_codec.hjpegcodec,
	                  hjpeg,
					  hlib->fp,
					  jmv_codec.jpeg_buff_ptr,
					  hlib->img.ptr);

	/* JPEG decoding with DMA */
	jpeg_decoder_start(&jmv_codec.hjpegcodec, jmv_codec.metadata.frame.frame_size);

	/* Wait till end of JPEG decoding and perfom Input/Output Processing in BackGround */
	uint32_t timeout = HAL_GetTick();
	do
	{
		jpeg_decoder_io(&jmv_codec.hjpegcodec);
	}
	while(jmv_codec.hjpegcodec.state != JPEG_CODEC_STATE_IDLE && \
		  (HAL_GetTick() - timeout) < JMV_LIB_JPEG_IMG_TIMEOUT);

	if(jmv_codec.hjpegcodec.state != JPEG_CODEC_STATE_IDLE)
	{
		/* Call abort function to clean handle */
		HAL_JPEG_Abort(hjpeg);
	}
	
	/* Get JPEG Info */
	HAL_JPEG_GetInfo(hjpeg, &jmv_codec.jpeginfo);

	hlib->img.width = jmv_codec.jpeginfo.ImageWidth;
	hlib->img.height = jmv_codec.jpeginfo.ImageHeight;

	/* Start hardware */
	hlib->playback_state = AUDIO_LIB_STATE_PLAY;
	BSP_AUDIO_OUT_Play((uint16_t *) hlib->buffer->ptr, hlib->buffer->size);
	
	hlib->err = AUDIO_LIB_NO_ERROR;
	return;
}

void jmv_lib_process(audio_lib_handle_t * hlib)
{
	uint32_t bytesread;
	jmv_lib_codec_t * codec = hlib->prv_data;
	JPEG_HandleTypeDef * hjpeg = (JPEG_HandleTypeDef *) hlib->img.codec;

	switch(hlib->playback_state)
  	{
		case AUDIO_LIB_STATE_PLAY:
			if(hlib->buffer->state == AUDIO_LIB_BUFFER_OFFSET_HALF)
			{
				/* Fill audio buffer */
                f_read (hlib->fp, hlib->buffer->ptr, hlib->buffer->size / 2, (UINT *) &bytesread);
                if(bytesread != hlib->buffer->size / 2)
				{
					if(f_eof(hlib->fp) == 0)
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
				
				codec->audio_pos += bytesread;
				
				/* Read jpeg frame */
				f_read (hlib->fp, &codec->metadata.frame, sizeof(jmv_lib_frame_t), (UINT *) &bytesread);
				if(bytesread != sizeof(jmv_lib_frame_t))
				{
					if(f_eof(hlib->fp) == 0)
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

				if(codec->metadata.frame.frame_size > codec->jpeg_buff_size)
				{
                    BSP_AUDIO_OUT_Stop(CODEC_PDWN_HW);
					hlib->err = AUDIO_LIB_READ_ERROR;
					return;
				}
				
				/* JPEG decoding with DMA */
				jpeg_decoder_start(&codec->hjpegcodec, codec->metadata.frame.frame_size);

				/* Wait till end of JPEG decoding and perfom Input/Output Processing in BackGround */
				uint32_t timeout = HAL_GetTick();
				do
				{
					jpeg_decoder_io(&codec->hjpegcodec);
				}
				while(codec->hjpegcodec.state != JPEG_CODEC_STATE_IDLE && \
					  (HAL_GetTick() - timeout) < JMV_LIB_JPEG_IMG_TIMEOUT);

				if(codec->hjpegcodec.state != JPEG_CODEC_STATE_IDLE)
				{
					/* Call abort function to clean handle */
					HAL_JPEG_Abort(hjpeg);
				}
				
				hlib->buffer->state = AUDIO_LIB_BUFFER_OFFSET_NONE;
			}

			if(hlib->buffer->state == AUDIO_LIB_BUFFER_OFFSET_FULL)
			{
				/* Fill audio buffer */
                f_read (hlib->fp, hlib->buffer->ptr + hlib->buffer->size / 2, hlib->buffer->size / 2, (UINT *) &bytesread);
                if(bytesread != hlib->buffer->size / 2)
				{
					if(f_eof(hlib->fp) == 0)
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
				
				codec->audio_pos += bytesread;
				
				/* Read jpeg frame */
                f_read (hlib->fp, &codec->metadata.frame, sizeof(jmv_lib_frame_t), (UINT *) &bytesread);
                if(bytesread != sizeof(jmv_lib_frame_t))
				{
					if(f_eof(hlib->fp) == 0)
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

				if(codec->metadata.frame.frame_size > codec->jpeg_buff_size)
				{
                    BSP_AUDIO_OUT_Stop(CODEC_PDWN_HW);
					hlib->err = AUDIO_LIB_READ_ERROR;
					return;
				}
				
				/* JPEG decoding with DMA */
				jpeg_decoder_start(&codec->hjpegcodec, codec->metadata.frame.frame_size);

				/* Wait till end of JPEG decoding and perfom Input/Output Processing in BackGround */
				uint32_t timeout = HAL_GetTick();
				do
				{
					jpeg_decoder_io(&codec->hjpegcodec);
				}
				while(codec->hjpegcodec.state != JPEG_CODEC_STATE_IDLE && \
					  (HAL_GetTick() - timeout) < JMV_LIB_JPEG_IMG_TIMEOUT);

				if(codec->hjpegcodec.state != JPEG_CODEC_STATE_IDLE)
				{
					/* Call abort function to clean handle */
					HAL_JPEG_Abort(hjpeg);
				}
				
				hlib->buffer->state = AUDIO_LIB_BUFFER_OFFSET_NONE;
			}

			hlib->time.elapsed_time = codec->audio_pos / codec->metadata.header.audio_byterate;
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

void jmv_lib_free(audio_lib_handle_t * hlib)
{
	jmv_lib_codec_t * codec = hlib->prv_data;

	free(hlib->buffer->ptr);
	hlib->buffer->ptr = NULL;
	
	free(codec->jpeg_buff_ptr);
	codec->jpeg_buff_ptr = NULL;	
}

void jmv_lib_transfer_complete_cb(audio_lib_handle_t * hlib)
{
	if(hlib->playback_state == AUDIO_LIB_STATE_PLAY)
	{
		hlib->buffer->state = AUDIO_LIB_BUFFER_OFFSET_FULL;
	}	
}

void jmv_lib_transfer_half_cb(audio_lib_handle_t * hlib)
{
	if(hlib->playback_state == AUDIO_LIB_STATE_PLAY)
	{
		hlib->buffer->state = AUDIO_LIB_BUFFER_OFFSET_HALF;
	}	
}
