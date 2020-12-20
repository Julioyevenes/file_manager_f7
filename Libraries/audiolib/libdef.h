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
#ifndef __LIBDEF_H
#define __LIBDEF_H

#ifdef __cplusplus
 extern "C" {
#endif 

/* Includes ------------------------------------------------------------------*/
#include "ff.h"

/* Exported types ------------------------------------------------------------*/
typedef enum
{
	AUDIO_LIB_NO_ERROR 				= 0,
	AUDIO_LIB_READ_ERROR 			= -1,
	AUDIO_LIB_MEMORY_ERROR 			= -5,
	AUDIO_LIB_UNSUPPORTED_FORMAT 	= -7,
	AUDIO_LIB_HARDWARE_ERROR 		= -8	
} audio_lib_err_t;

typedef enum
{
	AUDIO_LIB_STATE_IDLE = 0,
	AUDIO_LIB_STATE_WAIT,    
	AUDIO_LIB_STATE_INIT,    
	AUDIO_LIB_STATE_PLAY,
	AUDIO_LIB_STATE_PRERECORD,
	AUDIO_LIB_STATE_RECORD,  
	AUDIO_LIB_STATE_NEXT,  
	AUDIO_LIB_STATE_PREVIOUS,
	AUDIO_LIB_STATE_FORWARD,   
	AUDIO_LIB_STATE_BACKWARD,
	AUDIO_LIB_STATE_STOP,   
	AUDIO_LIB_STATE_PAUSE,
	AUDIO_LIB_STATE_RESUME,
	AUDIO_LIB_STATE_VOLUME_UP,
	AUDIO_LIB_STATE_VOLUME_DOWN,
	AUDIO_LIB_STATE_ERROR,  
} audio_lib_playback_state_t;

typedef enum
{
	AUDIO_LIB_BUFFER_OFFSET_NONE = 0,
	AUDIO_LIB_BUFFER_OFFSET_HALF,
	AUDIO_LIB_BUFFER_OFFSET_FULL,
} audio_lib_buffer_state_t;

typedef struct
{
	uint32_t 					elapsed_time;
	uint32_t 					total_time;
} audio_lib_time_t;

typedef struct
{
	uint8_t 					empty : 1;
	uint8_t *					ptr;

	uint32_t					size;
	
	audio_lib_buffer_state_t 	state;
} audio_lib_buffer_t;

typedef struct
{
    uint8_t *                   ptr;

    uint16_t                    width;
    uint16_t                    height;

    void *                      codec;
} audio_lib_img_t;

typedef struct
{
	uint8_t 					active : 1;
	uint8_t 					volume;
	
	void *						prv_data;
	
	FIL * 						fp;

	audio_lib_err_t				err;
	audio_lib_buffer_t *		buffer;
	audio_lib_playback_state_t 	playback_state;
	audio_lib_time_t			time;
	audio_lib_img_t             img;
} audio_lib_handle_t;

typedef struct
{
	void (* lib_start)					(audio_lib_handle_t *);
	void (* lib_process)				(audio_lib_handle_t *);
	void (* lib_free)					(audio_lib_handle_t *);
	void (* lib_transfer_complete_cb)	(audio_lib_handle_t *);
	void (* lib_transfer_half_cb)		(audio_lib_handle_t *);	
} audio_lib_t;

/* Exported constants --------------------------------------------------------*/
/* Exported macro ------------------------------------------------------------*/
/* Exported variables --------------------------------------------------------*/
/* Exported functions --------------------------------------------------------*/

#ifdef __cplusplus
}
#endif

#endif /* __LIBDEF_H */
