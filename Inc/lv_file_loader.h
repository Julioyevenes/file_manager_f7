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
#ifndef __TEMPLATE_H
#define __TEMPLATE_H

#ifdef __cplusplus
 extern "C" {
#endif 

/* Includes ------------------------------------------------------------------*/
#include "lvgl/lvgl.h"
#include "ff.h"

/* Exported types ------------------------------------------------------------*/
typedef enum
{
	LV_FM_LOADER_NO_ERROR 			= 0,
	LV_FM_LOADER_READ_ERROR 		= -1,
	LV_FM_LOADER_WRITE_ERROR 		= -2,	
	LV_FM_LOADER_MEMORY_ERROR 		= -5,
	LV_FM_LOADER_SIZE_ERROR 		= -9,
	LV_FM_LOADER_ERASE_ERROR 		= -10	
} lv_fm_loader_err_t;

/* Exported constants --------------------------------------------------------*/
/* Exported macro ------------------------------------------------------------*/
/* Exported variables --------------------------------------------------------*/
extern lv_obj_t * loader_h;

/* Exported functions --------------------------------------------------------*/
lv_fm_loader_err_t lv_fm_loader_init(lv_obj_t * parent, FIL * fp);

#ifdef __cplusplus
}
#endif

#endif /* __TEMPLATE_H */
