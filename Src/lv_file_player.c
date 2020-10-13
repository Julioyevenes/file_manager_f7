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
#include "lv_file_player.h"

#include "wav_lib.h"
#include "mp3_lib.h"
#include "flac_lib.h"

/* Private types -------------------------------------------------------------*/
/* Private constants ---------------------------------------------------------*/
/* Private macro -------------------------------------------------------------*/
#define LV_FM_PLAYER_FREE(ptr)		if (ptr != NULL) \
									{ \
										free(ptr); ptr = NULL; \
									}

#define LV_FM_PLAYER_OBJ_DEL(ptr)	if (ptr != NULL) \
									{ \
										lv_obj_del(ptr); ptr = NULL; \
									}


#define LV_FM_PLAYER_TASK_DEL(ptr)	if (ptr != NULL) \
									{ \
										lv_task_del(ptr); ptr = NULL; \
									}

/* Private variables ---------------------------------------------------------*/
audio_lib_handle_t hlib;
audio_lib_t * audio_lib;

lv_task_t * player_process_task;
lv_task_t * player_bar_task;
lv_task_t * player_volume_task;

lv_obj_t * player_h;
lv_obj_t * player_bar;

uint8_t last_volume;

/* Private function prototypes -----------------------------------------------*/
static void lv_fm_player_create(lv_obj_t * parent);

static void lv_fm_player_delete(void);

static void lv_fm_player_btn_event_cb(lv_obj_t * btn, lv_event_t e);
static void lv_fm_player_slider_event_cb(lv_obj_t * slider, lv_event_t e);

void 		lv_fm_player_process_task(lv_task_t * task);
void 		lv_fm_player_bar_task(lv_task_t * task);
void 		lv_fm_player_volume_task(lv_task_t * task);

audio_lib_err_t lv_fm_player_start(lv_obj_t * parent, lv_fm_player_format_t format, FIL * fp)
{
	hlib.active = 1;
	hlib.volume = 50;
	hlib.fp = fp;
	hlib.err = AUDIO_LIB_NO_ERROR;

	switch(format)
	{
		case player_wav:
			audio_lib = &wav_lib;
			break;

		case player_mp3:
			audio_lib = &mp3_lib;
			break;

		case player_flac:
			audio_lib = &flac_lib;
			break;

		default:
			audio_lib = NULL;
			f_close (hlib.fp);
			hlib.err = AUDIO_LIB_UNSUPPORTED_FORMAT;
			break;
	}

	if(audio_lib != NULL)
	{
		audio_lib->lib_start(&hlib);
		if(hlib.err != AUDIO_LIB_NO_ERROR)
		{
			hlib.active = 0;
			f_close (hlib.fp);
			audio_lib->lib_free(&hlib);
		}
		else
		{
			lv_fm_player_create(parent);

			player_process_task = lv_task_create(lv_fm_player_process_task, 1, LV_TASK_PRIO_MID, &hlib);
			player_bar_task = lv_task_create(lv_fm_player_bar_task, 100, LV_TASK_PRIO_MID, &hlib);
			player_volume_task = lv_task_create(lv_fm_player_volume_task, 100, LV_TASK_PRIO_MID, &hlib);
		}
	}

	return hlib.err;
}

static void lv_fm_player_create(lv_obj_t * parent)
{
	lv_obj_t * btn;
	lv_obj_t * img;
	
	player_h = lv_cont_create(parent, NULL);
	lv_cont_set_layout(player_h, LV_LAYOUT_PRETTY_TOP);
	lv_cont_set_fit2(player_h, LV_FIT_NONE, LV_FIT_TIGHT);
	lv_obj_set_width(player_h, lv_page_get_width_grid(parent, 1, 1));

	btn = lv_btn_create(player_h, NULL);
	img = lv_img_create(btn, NULL);
	lv_img_set_src(img, LV_SYMBOL_STOP);
	lv_btn_set_fit2(btn, LV_FIT_TIGHT, LV_FIT_TIGHT);
	lv_obj_set_event_cb(btn, lv_fm_player_btn_event_cb);

	btn = lv_btn_create(player_h, NULL);
	img = lv_img_create(btn, NULL);
	lv_img_set_src(img, LV_SYMBOL_PAUSE);
	lv_btn_set_fit2(btn, LV_FIT_TIGHT, LV_FIT_TIGHT);
	lv_obj_set_event_cb(btn, lv_fm_player_btn_event_cb);

	lv_obj_t * slider = lv_slider_create(player_h, NULL);
	lv_slider_set_range(slider, 0, 80);
	lv_slider_set_value(slider, 50, LV_ANIM_OFF);
	lv_obj_set_event_cb(slider, lv_fm_player_slider_event_cb);
	lv_obj_set_width(slider, lv_obj_get_width_fit(player_h) / 3);

	player_bar = lv_bar_create(player_h, NULL);
	lv_obj_set_width(player_bar, lv_obj_get_width_fit(player_h));
}

static void lv_fm_player_delete(void)
{
	hlib.active = 0;
	f_close (hlib.fp);
	audio_lib->lib_free(&hlib);

	LV_FM_PLAYER_TASK_DEL(player_volume_task)
	LV_FM_PLAYER_TASK_DEL(player_bar_task)
	LV_FM_PLAYER_TASK_DEL(player_process_task)
	LV_FM_PLAYER_OBJ_DEL(player_h)
}

static void lv_fm_player_btn_event_cb(lv_obj_t * btn, lv_event_t e)
{
	char str[4];
	lv_obj_t * img;
	lv_img_ext_t * ext;
	
	if (e == LV_EVENT_CLICKED)
	{
		img = lv_obj_get_child(btn, NULL);
		ext = lv_obj_get_ext_attr(img);
		strcpy(str, ext->src);
		
		if (strcmp(str, LV_SYMBOL_PLAY) == 0)
		{
			hlib.playback_state = AUDIO_LIB_STATE_RESUME;			
			lv_img_set_src(img, LV_SYMBOL_PAUSE);
		}
		else if (strcmp(str, LV_SYMBOL_PAUSE) == 0)
		{
			hlib.playback_state = AUDIO_LIB_STATE_PAUSE;			
			lv_img_set_src(img, LV_SYMBOL_PLAY);
		}
		else if (strcmp(str, LV_SYMBOL_STOP) == 0)
		{
			hlib.playback_state = AUDIO_LIB_STATE_STOP;
		}		
	}
}

static void lv_fm_player_slider_event_cb(lv_obj_t * slider, lv_event_t e)
{
	if(e == LV_EVENT_VALUE_CHANGED)
	{
		hlib.volume = lv_slider_get_value(slider);
	}
}

void lv_fm_player_process_task(lv_task_t * task)
{
	audio_lib_handle_t * hlib = task->user_data;
	
	audio_lib->lib_process(hlib);
	if(hlib->err != AUDIO_LIB_NO_ERROR)
	{
		lv_fm_player_delete();
		
		return;
	}
	
	if(hlib->active == 0)
	{
		lv_fm_player_delete();
		
		return;		
	}
}

void lv_fm_player_bar_task(lv_task_t * task)
{
	static char buf[64];
	audio_lib_handle_t * hlib = task->user_data;
	uint32_t elapsed_time = (uint32_t) hlib->time.elapsed_time;
	uint32_t total_time = (uint32_t) hlib->time.total_time;
	
	lv_snprintf(buf, sizeof(buf), "%02d:%02d", elapsed_time / 60, elapsed_time % 60);
	lv_obj_set_style_local_value_str(player_bar, LV_BAR_PART_BG, LV_STATE_DEFAULT, buf);

	int16_t x = (int16_t) ((elapsed_time * 100) / total_time);
	lv_bar_set_value(player_bar, x, LV_ANIM_OFF);
}

void lv_fm_player_volume_task(lv_task_t * task)
{
	audio_lib_handle_t * hlib = task->user_data;

	if(hlib->volume != last_volume)
	{
		BSP_AUDIO_OUT_SetVolume(hlib->volume);

		last_volume = hlib->volume;
	}
}

void BSP_AUDIO_OUT_TransferComplete_CallBack(void)
{
	if(audio_lib->lib_transfer_complete_cb != NULL)
	{
		audio_lib->lib_transfer_complete_cb(&hlib);
	}
}

void BSP_AUDIO_OUT_HalfTransfer_CallBack(void)
{
	if(audio_lib->lib_transfer_half_cb != NULL)
	{
		audio_lib->lib_transfer_half_cb(&hlib);
	}
}
