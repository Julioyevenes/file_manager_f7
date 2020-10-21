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

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __HAL_JPEG_CODEC_H
#define __HAL_JPEG_CODEC_H

#ifdef __cplusplus
 extern "C" {
#endif 

/* Includes ------------------------------------------------------------------*/
#include "jpeg_utils.h"
#include "ff.h"

/* Exported types ------------------------------------------------------------*/
typedef enum
{
    JPEG_CODEC_NO_ERROR = 0,
    JPEG_CODEC_READ_ERROR,
    JPEG_CODEC_WRITE_ERROR,
    JPEG_CODEC_MEMORY_ERROR
} jpeg_codec_err_t;

typedef enum
{
    JPEG_CODEC_STATE_IDLE = 0,
    JPEG_CODEC_STATE_INFO,
	JPEG_CODEC_STATE_IMG
} jpeg_codec_state_t;

typedef struct
{
    uint8_t 	full : 1;
    uint8_t *	ptr;
    uint32_t 	size;
} jpeg_codec_buffer_t;

typedef struct
{
    uint8_t								in_read_idx : 1;
    uint8_t								in_write_idx : 1;
    uint8_t								out_read_idx : 1;
    uint8_t								out_write_idx : 1;
    uint8_t 							in_pause : 1;
    uint8_t 							out_pause : 1;
    uint8_t *							frame_addr;
    uint32_t							mcu_total;
    uint32_t							mcu_value;

    FIL	*								fp;

    JPEG_HandleTypeDef * 				hjpeg;
    JPEG_YCbCrToRGB_Convert_Function 	color_fn;

    jpeg_codec_buffer_t 				in_buf[2];
    jpeg_codec_buffer_t 				out_buf[2];
    jpeg_codec_state_t 					state;
} jpeg_codec_handle_t;

/* Exported constants --------------------------------------------------------*/
/* Exported macro ------------------------------------------------------------*/
/* Exported variables --------------------------------------------------------*/
/* Exported functions --------------------------------------------------------*/
jpeg_codec_err_t 	jpeg_decoder_init(jpeg_codec_handle_t * hcodec, JPEG_HandleTypeDef * hjpeg, FIL * fp, uint32_t dst_addr);
jpeg_codec_err_t 	jpeg_decoder_start(jpeg_codec_handle_t * hcodec);
jpeg_codec_err_t 	jpeg_decoder_io(jpeg_codec_handle_t * hcodec);
void 				jpeg_decoder_free(jpeg_codec_handle_t * hcodec);

#ifdef __cplusplus
}
#endif

#endif /* __HAL_JPEG_CODEC_H */
