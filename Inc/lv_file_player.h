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
#ifndef __LV_FILE_PLAYER_H
#define __LV_FILE_PLAYER_H

#ifdef __cplusplus
 extern "C" {
#endif 

/* Includes ------------------------------------------------------------------*/
#include "lvgl/lvgl.h"
#include "libdef.h"

/* Exported types ------------------------------------------------------------*/
typedef enum
{
	player_wav = 4,	/* WAV audio */
	player_mp3,		/* MP3 audio */
	player_flac,	/* FLAC audio */
	player_jmv		/* JMV video */
} lv_fm_player_format_t;

/* Exported constants --------------------------------------------------------*/
/* Exported macro ------------------------------------------------------------*/
/* Exported variables --------------------------------------------------------*/
extern lv_obj_t * player_h;
extern lv_obj_t * player_bar;

/* Exported functions --------------------------------------------------------*/
audio_lib_err_t lv_fm_player_start(lv_obj_t * parent, lv_fm_player_format_t format, FIL * fp);

#ifdef __cplusplus
}
#endif

#endif /* __LV_FILE_PLAYER_H */
