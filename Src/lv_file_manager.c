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
#include "lv_file_manager.h"
#include "lv_file_player.h"
#include "lv_file_loader.h"

#include "ff_gen_drv.h"
#include "sd_diskio_dma.h"
#include "usbh_diskio.h"

#include "usbh_core.h"
#include "usbh_msc.h"
#include "usbh_hid.h"

#include <stdlib.h>

/* Private types ------------------------------------------------------------*/
typedef enum
{
	other = 0,
	bmp,			/* Bitmap image */
	jpeg,			/* JPEG image */
 	gif,			/* GIF image */	
	wav,			/* WAV audio */
	mp3,			/* MP3 audio */
	flac,			/* FLAC audio */
 	jmv,			/* JMV video */
 	bin             /* BIN executable */
} lv_fm_format_t;

typedef enum
{
	LV_FM_NO_ERROR 				= 0,
	LV_FM_READ_ERROR 			= -1,
	LV_FM_WRITE_ERROR 			= -2,
	LV_FM_DELETE_ERROR 			= -3,
	LV_FM_FORMAT_ERROR 			= -4,
	LV_FM_MEMORY_ERROR 			= -5,
	LV_FM_FILE_ALREADY_EXISTS 	= -6,
	LV_FM_UNSUPPORTED_FORMAT 	= -7
} lv_fm_err_t;

typedef enum
{
	LV_FM_IDLE_STAGE = 0,
	LV_FM_COPY_STAGE,
	LV_FM_PASTE_STAGE,
	LV_FM_DELETE_STAGE,
	LV_FM_FORMAT_STAGE
} lv_fm_stages_t;

typedef enum
{
	LV_FM_MEDIA_SD = 0,
	LV_FM_MEDIA_USB
} lv_fm_media_hardware_t;

typedef struct
{
	char 			name[255];

	uint8_t 		folder : 1;
	uint8_t 		volume : 1;

	uint32_t		size;

	lv_fm_format_t 	format;
} lv_fm_obj_t;

typedef struct
{
	lv_fm_media_hardware_t	media_hard;

	Diskio_drvTypeDef *		drv;

	uint8_t 				media_present : 1;
	uint8_t 				valid : 1;
	uint8_t					lun;
	
	FATFS 					fat_fs;
	
	char					path[4];
} lv_fm_media_t;

typedef struct
{
	char 		name[255];
	char		src_path[255];
	char		dst_path[255];
} lv_fm_queue_data_t;

typedef struct _lv_fm_queue_t
{
	lv_fm_queue_data_t data;
	struct _lv_fm_queue_t * next;	
} lv_fm_queue_t;

typedef struct
{
	FRESULT 		fr;

	FIL 			src;
	FIL 			dst;

	uint8_t 		flag_cut : 1;

	uint8_t * 		buffer;

	uint32_t 		buffer_size;
	uint32_t 		count;
	uint32_t 		total;

	lv_fm_obj_t *	obj;
	lv_fm_stages_t	stage;
	lv_fm_err_t		err;
	
	lv_fm_queue_t * head;
	lv_fm_queue_t * tail;	
	
	char 			src_vol[4];
	char 			dst_vol[4];	
	char			src_path[255];
	char			dst_path[255];
} lv_fm_task_data_t;

/* Private constants --------------------------------------------------------*/
#define LV_FM_MAX_ELEMENTS 	100
#define LV_FM_MAX_VOLUMES 	2

const char * str_vol_opt[] = {	LV_SYMBOL_DRIVE, "Format",
								LV_SYMBOL_CLOSE, "Cancel",
								NULL  };

const char * str_file_opt[] = {	LV_SYMBOL_COPY, "Copy",
								LV_SYMBOL_CUT, "Cut",
								LV_SYMBOL_TRASH, "Delete",
								LV_SYMBOL_EDIT, "Rename",
								LV_SYMBOL_DIRECTORY, "New folder",
								LV_SYMBOL_CLOSE, "Cancel",
								NULL };

const char * btns00[] = {"Ok", ""};
const char * btns01[] = {"Cancel", "Ok", ""};

/* Private macro ------------------------------------------------------------*/
#define LV_FM_FREE(ptr)		if (ptr != NULL) \
							{ \
								free(ptr); ptr = NULL; \
							}

#define LV_FM_OBJ_DEL(ptr)	if (ptr != NULL) \
							{ \
								lv_obj_del(ptr); ptr = NULL; \
							}


#define LV_FM_TASK_DEL(ptr)	if (ptr != NULL) \
							{ \
								lv_task_del(ptr); ptr = NULL; \
							}

/* Private variables --------------------------------------------------------*/
char lfn_buffer[255];

extern uint8_t husb_state;
extern USBH_HandleTypeDef hUSBH;

lv_fm_obj_t fm_obj_save;
lv_fm_obj_t fm_obj[LV_FM_MAX_ELEMENTS];
lv_fm_media_t fm_media[LV_FM_MAX_VOLUMES];
lv_fm_task_data_t fm_task_data;

extern lv_indev_t * enc_indev;

lv_task_t * media_task;
lv_task_t * file_task;
lv_task_t * bar_task;
lv_task_t * mbox_task;

static lv_group_t * g;
static lv_obj_t * tv;
static lv_obj_t * t1;
static lv_obj_t * list_local;
static lv_obj_t * list_options;
static lv_obj_t * h;
static lv_obj_t * bar;
static lv_obj_t * mbox_question;
static lv_obj_t * mbox_err;
static lv_obj_t * ta;
static lv_obj_t * kb;
static lv_obj_t * img;
static lv_event_t e_last = _LV_EVENT_LAST;

/* Private function prototypes ----------------------------------------------*/
static void 			lv_fm_local_tab_create(lv_obj_t * parent);
static lv_obj_t * 		lv_fm_list_create(lv_obj_t * parent, lv_coord_t w, lv_coord_t h, lv_align_t align, const char ** str, lv_event_cb_t event_cb);
static lv_obj_t * 		lv_fm_mbox_create(lv_obj_t * parent, const char * txt, const char ** str, lv_event_cb_t event_cb);

static void 			lv_fm_list_local_btn_event_cb(lv_obj_t * btn, lv_event_t e);
static void 			lv_fm_list_options_btn_event_cb(lv_obj_t * btn, lv_event_t e);
static void 			lv_fm_mbox_question_btn_event_cb(lv_obj_t * btn, lv_event_t e);
static void 			lv_fm_copying_btn_event_cb(lv_obj_t * btn, lv_event_t e);
static void 			lv_fm_mbox_err_btn_event_cb(lv_obj_t * btn, lv_event_t e);
static void 			lv_fm_img_event_cb(lv_obj_t * obj, lv_event_t e);
static void 			lv_fm_kb_event_cb(lv_obj_t * _kb, lv_event_t e);

static void 			lv_fm_usb_cb(USBH_HandleTypeDef * phost, uint8_t id);

static void 			lv_fm_err(lv_fm_err_t err);
static uint8_t 			lv_fm_media_detect(lv_fm_media_t * m);
static void 			lv_fm_media_check(lv_fm_media_t * m);
static void 			lv_fm_list_fill(lv_fm_media_t * m, lv_fm_obj_t * obj, lv_obj_t * list, lv_event_cb_t event_cb, uint8_t svol);
static lv_fm_format_t 	lv_fm_get_ext(FILINFO *finfo);
static void 			lv_fm_copy(lv_fm_task_data_t * data);
static void 			lv_fm_delete(lv_fm_task_data_t * data);
static void 			lv_fm_format(lv_fm_task_data_t * data);
static void 			lv_fm_rename(lv_fm_task_data_t * data);
static void 			lv_fm_create(lv_fm_task_data_t * data);

static lv_fm_err_t 		lv_fm_file_copy(lv_fm_task_data_t * data);
static lv_fm_err_t 		lv_fm_folder_scan(lv_fm_task_data_t * data, const char * src_path, const char * dst_path, const char * name);
static lv_fm_err_t 		lv_fm_folder_delete(const char * path);

static void 			lv_fm_queue_put(lv_fm_queue_t ** head, lv_fm_queue_t ** tail, lv_fm_queue_data_t * data);
static lv_fm_queue_t * 	lv_fm_queue_get(lv_fm_queue_t ** head, lv_fm_queue_t ** tail);

static void 			lv_fm_file_task_kill(lv_fm_task_data_t * data, lv_fm_err_t err);

void 					lv_fm_media_task(lv_task_t * task);
void 					lv_fm_file_task(lv_task_t * task);
void 					lv_fm_bar_task(lv_task_t * task);
void 					lv_fm_mbox_task(lv_task_t * task);

void lv_fm_init(void)
{
    int32_t i;

	fm_media[0].media_hard = LV_FM_MEDIA_SD;
	fm_media[0].drv = (Diskio_drvTypeDef *) &SD_Driver;

	USBH_Init(&hUSBH, lv_fm_usb_cb, 0);
	USBH_RegisterClass(&hUSBH, USBH_MSC_CLASS);
	USBH_RegisterClass(&hUSBH, USBH_HID_CLASS);
	USBH_Start(&hUSBH);

	fm_media[1].media_hard = LV_FM_MEDIA_USB;
	fm_media[1].drv = (Diskio_drvTypeDef *) &USBH_Driver;

    for (i = 0; i < LV_FM_MAX_VOLUMES; i++)
    {
        FATFS_LinkDriver(fm_media[i].drv, &(fm_media[i].path[0]));
    }

    g = lv_group_create();
    lv_indev_set_group(enc_indev, g);
	
	tv = lv_tabview_create(lv_scr_act(), NULL);
    t1 = lv_tabview_add_tab(tv, "Local");

    lv_fm_local_tab_create(t1);
}

void lv_fm_non_task_process(void)
{
	USBH_Process(&hUSBH);
}

static void lv_fm_local_tab_create(lv_obj_t * parent)
{
    lv_coord_t grid_h = lv_page_get_height_grid(parent, 1, 1);
    lv_coord_t grid_w = lv_page_get_width_grid(parent, 1, 1);

    lv_page_set_scrl_layout(parent, LV_LAYOUT_GRID);

	fm_task_data.buffer_size = 1024 * 32;
	fm_task_data.obj = &fm_obj_save;
	
    list_local = lv_fm_list_create(parent, grid_w, grid_h, LV_ALIGN_CENTER, NULL, NULL);
    lv_group_add_obj(g, list_local);
    lv_group_set_editing(g, true);

    media_task = lv_task_create(lv_fm_media_task, 500, LV_TASK_PRIO_MID, &fm_media[0]);
    mbox_task = lv_task_create(lv_fm_mbox_task, 500, LV_TASK_PRIO_MID, &fm_task_data);
}

static lv_obj_t * lv_fm_list_create(lv_obj_t * parent, lv_coord_t w, lv_coord_t h, lv_align_t align, const char ** str, lv_event_cb_t event_cb)
{
	int32_t i;
	lv_obj_t * btn;
	lv_obj_t * list = lv_list_create(parent, NULL);

	lv_list_set_scroll_propagation(list, true);
	lv_obj_set_size(list, w, h);
	lv_obj_align(list, parent, align, 0, 0);
	lv_group_add_obj(g, list);

	for(i = 0; str != NULL && str[i] != NULL; i += 2)
	{
		btn = lv_list_add_btn(list, str[i], str[i + 1]);
		lv_obj_set_event_cb(btn, event_cb);
	}

	return list;
}

static lv_obj_t * lv_fm_mbox_create(lv_obj_t * parent, const char * txt, const char ** str, lv_event_cb_t event_cb)
{
	lv_obj_t * m = lv_msgbox_create(parent, NULL);
	lv_msgbox_set_text(m, txt);
	lv_msgbox_add_btns(m, str);
	lv_obj_set_event_cb(m, event_cb);
	lv_obj_align(m, NULL, LV_ALIGN_CENTER, 0, 0);

	return m;
}

static void lv_fm_list_local_btn_event_cb(lv_obj_t * btn, lv_event_t e)
{
	int32_t i;
	lv_fm_obj_t * tobj;
	lv_coord_t grid_h = lv_page_get_height_grid(t1, 1, 1);
	lv_coord_t grid_w = lv_page_get_width_grid(t1, 3, 1);
	
	lv_group_focus_obj(list_local);
	lv_group_set_editing(g, true);

	if (e == LV_EVENT_CLICKED)
	{
		if (e_last != LV_EVENT_LONG_PRESSED)
		{
			i = lv_list_get_btn_index(list_local, btn);

			tobj = &fm_obj[i];
			if(tobj->volume == 1 && \
			   tobj->folder == 1)
			{
				lv_fm_list_fill(&fm_media[0], &fm_obj[0], list_local, lv_fm_list_local_btn_event_cb, 1);
			}
			else if(tobj->volume == 1 && \
					tobj->folder == 0)
			{
				if (f_chdrive (tobj->name) == FR_OK)
				{
					lv_fm_list_fill(&fm_media[0], &fm_obj[0], list_local, lv_fm_list_local_btn_event_cb, 0);
				}
			}
			else if(tobj->volume == 0 && \
					tobj->folder == 1)
			{
				if (f_chdir (tobj->name) == FR_OK)
				{
					lv_fm_list_fill(&fm_media[0], &fm_obj[0], list_local, lv_fm_list_local_btn_event_cb, 0);
				}
			}
			else if(tobj->format == wav || \
					tobj->format == mp3 || \
					tobj->format == flac)
			{
				if (list_options == NULL && \
					player_h == NULL && \
					loader_h == NULL && \
					h == NULL && \
					img == NULL)
				{
					fm_task_data.fr = f_open (&(fm_task_data.src), tobj->name, FA_READ);
					if (fm_task_data.fr == FR_OK)
					{
						fm_task_data.err = (lv_fm_err_t) lv_fm_player_start(t1,
																			(lv_fm_player_format_t) tobj->format,
																			&(fm_task_data.src));
					}
				}
			}
			else if(tobj->format == jpeg)
			{
				if (list_options == NULL && \
					player_h == NULL && \
					loader_h == NULL && \
					h == NULL && \
					img == NULL)
				{
					img = lv_img_create(lv_scr_act(), NULL);
					lv_img_set_src(img, tobj->name);
					lv_obj_set_drag(img, true);
					lv_obj_set_event_cb(img, lv_fm_img_event_cb);

		    		if(lv_obj_get_width(img) == 0 || lv_obj_get_height(img) == 0)
		    		{
		    		    LV_FM_OBJ_DEL(img)
		    		}
				}
			}
			else if(tobj->format == bin)
			{
				if (list_options == NULL && \
					player_h == NULL && \
					loader_h == NULL && \
					h == NULL && \
					img == NULL)
				{
					fm_task_data.fr = f_open (&(fm_task_data.src), tobj->name, FA_READ);
					if (fm_task_data.fr == FR_OK)
					{
						fm_task_data.err = (lv_fm_err_t) lv_fm_loader_init(t1, &(fm_task_data.src));
					}
				}
			}
		}

		e_last = _LV_EVENT_LAST;
	}

	if (e == LV_EVENT_LONG_PRESSED || \
	    e == LV_EVENT_RIGHT_CLICKED)
	{
		e_last = e;
		i = lv_list_get_btn_index(list_local, btn);

		tobj = &fm_obj[i];
		if (list_options == NULL && \
			player_h == NULL && \
			loader_h == NULL && \
			h == NULL && \
			img == NULL)
		{
			fm_obj_save = fm_obj[i];
			if(tobj->volume == 1 && \
			   tobj->folder == 0)
			{
				lv_obj_set_width(list_local, grid_w * 2);
				lv_obj_align(list_local, t1, LV_ALIGN_IN_LEFT_MID, 0, 0);
				list_options = lv_fm_list_create(t1, grid_w, grid_h, LV_ALIGN_IN_RIGHT_MID, str_vol_opt, lv_fm_list_options_btn_event_cb);
			}
			else if(tobj->volume == 0 && \
					tobj->folder == 1)
			{
				lv_obj_set_width(list_local, grid_w * 2);
				lv_obj_align(list_local, t1, LV_ALIGN_IN_LEFT_MID, 0, 0);
				list_options = lv_fm_list_create(t1, grid_w, grid_h, LV_ALIGN_IN_RIGHT_MID, str_file_opt, lv_fm_list_options_btn_event_cb);
			}
			else if(tobj->volume == 0 && \
					tobj->folder == 0)
			{
				lv_obj_set_width(list_local, grid_w * 2);
				lv_obj_align(list_local, t1, LV_ALIGN_IN_LEFT_MID, 0, 0);
				list_options = lv_fm_list_create(t1, grid_w, grid_h, LV_ALIGN_IN_RIGHT_MID, str_file_opt, lv_fm_list_options_btn_event_cb);
			}
		}
	}
}

static void lv_fm_list_options_btn_event_cb(lv_obj_t * btn, lv_event_t e)
{
	char * pstr;
	lv_obj_t * label;
	lv_coord_t grid_h = lv_page_get_height_grid(t1, 1, 1);
	lv_coord_t grid_w = lv_page_get_width_grid(t1, 1, 1);

	lv_group_focus_obj(list_options);
	lv_group_set_editing(g, true);

	if (e == LV_EVENT_CLICKED)
	{
		pstr = (char *) lv_list_get_btn_text(btn);

		if (strcmp(pstr, "Copy") == 0)
		{
			fm_task_data.flag_cut = 0;
			fm_task_data.stage = LV_FM_COPY_STAGE;
			lv_fm_copy(&fm_task_data);

			label = lv_obj_get_child(btn, NULL);
			lv_label_set_text(label, "Paste");
		}
		else if (strcmp(pstr, "Cut") == 0)
		{
			fm_task_data.flag_cut = 1;
			fm_task_data.stage = LV_FM_COPY_STAGE;
			lv_fm_copy(&fm_task_data);

			label = lv_obj_get_child(btn, NULL);
			lv_label_set_text(label, "Paste");
		}
		else if (strcmp(pstr, "Paste") == 0)
		{
			fm_task_data.stage = LV_FM_PASTE_STAGE;
			lv_fm_copy(&fm_task_data);
		}
		else if (strcmp(pstr, "Delete") == 0)
		{
			fm_task_data.stage = LV_FM_DELETE_STAGE;
			mbox_question = lv_fm_mbox_create(lv_scr_act(),
									 	 	  "Confirm Delete.",
											  btns01,
											  lv_fm_mbox_question_btn_event_cb);
		}
		else if (strcmp(pstr, "Rename") == 0)
		{
			lv_fm_rename(&fm_task_data);
		}
		else if (strcmp(pstr, "Format") == 0)
		{
			fm_task_data.stage = LV_FM_FORMAT_STAGE;
			mbox_question = lv_fm_mbox_create(lv_scr_act(),
									 	 	  "Confirm Format.",
											  btns01,
											  lv_fm_mbox_question_btn_event_cb);
		}
		else if (strcmp(pstr, "New folder") == 0)
		{
			lv_fm_create(&fm_task_data);
		}
		else if (strcmp(pstr, "Cancel") == 0)
		{
			fm_task_data.stage = LV_FM_IDLE_STAGE;

			lv_obj_set_width(list_local, grid_w);
			lv_obj_align(list_local, t1, LV_ALIGN_CENTER, 0, 0);
			LV_FM_OBJ_DEL(list_options)
		}
	}
}

static void lv_fm_mbox_question_btn_event_cb(lv_obj_t * btn, lv_event_t e)
{
	char * pstr;

	if (e == LV_EVENT_CLICKED)
	{
		pstr = (char *) lv_msgbox_get_active_btn_text(mbox_question);

		if (strcmp(pstr, "Cancel") == 0)
		{
			LV_FM_OBJ_DEL(mbox_question)
		}
		else if (strcmp(pstr, "Ok") == 0)
		{
			if (fm_task_data.stage == LV_FM_DELETE_STAGE)
			{
				lv_fm_delete(&fm_task_data);
			}
			if (fm_task_data.stage == LV_FM_FORMAT_STAGE)
			{
				lv_fm_format(&fm_task_data);
			}
			LV_FM_OBJ_DEL(mbox_question)
		}
	}
}

static void lv_fm_copying_btn_event_cb(lv_obj_t * btn, lv_event_t e)
{
	if (e == LV_EVENT_CLICKED)
	{
		LV_FM_TASK_DEL(bar_task)
		LV_FM_TASK_DEL(file_task)

		f_close (&(fm_task_data.src));
		f_close (&(fm_task_data.dst));

		while(fm_task_data.head != NULL)
		{
			free(lv_fm_queue_get(&(fm_task_data.head), &(fm_task_data.tail)));
		}

		LV_FM_FREE(fm_task_data.buffer)

		fm_task_data.flag_cut = 0;
		fm_task_data.total = fm_task_data.count = 0;

		LV_FM_OBJ_DEL(h)

		lv_fm_list_fill(&fm_media[0], &fm_obj[0], list_local, lv_fm_list_local_btn_event_cb, 0);
	}
}

static void lv_fm_mbox_err_btn_event_cb(lv_obj_t * btn, lv_event_t e)
{
	char * pstr;

	if (e == LV_EVENT_CLICKED)
	{
		pstr = (char *) lv_msgbox_get_active_btn_text(mbox_err);

		if (strcmp(pstr, "Ok") == 0)
		{
			LV_FM_OBJ_DEL(mbox_err)
		}
	}
}

static void lv_fm_img_event_cb(lv_obj_t * obj, lv_event_t e)
{
	if (e == LV_EVENT_CLICKED || \
		e == LV_EVENT_DEFOCUSED)
	{
		LV_FM_OBJ_DEL(img)
	}
}

static void lv_fm_kb_event_cb(lv_obj_t * _kb, lv_event_t e)
{
    char * pstr;

    lv_keyboard_def_event_cb(kb, e);

    if(e == LV_EVENT_APPLY)
    {
		pstr = lv_textarea_get_text(ta);

		if( f_rename ((TCHAR *) fm_task_data.obj->name, (TCHAR*) pstr) != FR_OK)
		{
			fm_task_data.err = LV_FM_WRITE_ERROR;
		}

		lv_fm_list_fill(&fm_media[0], &fm_obj[0], list_local, lv_fm_list_local_btn_event_cb, 0);

		lv_obj_set_height(tv, LV_VER_RES);
		LV_FM_OBJ_DEL(kb)
		LV_FM_OBJ_DEL(h)
    }

    if(e == LV_EVENT_CANCEL)
    {
		lv_obj_set_height(tv, LV_VER_RES);
		LV_FM_OBJ_DEL(kb)
		LV_FM_OBJ_DEL(h)
    }
}

static void lv_fm_usb_cb(USBH_HandleTypeDef * phost, uint8_t id)
{
	husb_state = id;
}

static void lv_fm_err(lv_fm_err_t err)
{
	mbox_err = lv_fm_mbox_create(lv_scr_act(),
							 	 "Message",
								 btns00,
								 lv_fm_mbox_err_btn_event_cb);

	switch(err)
	{
		case LV_FM_READ_ERROR:
			lv_msgbox_set_text(mbox_err, "Read error.");
			break;

		case LV_FM_WRITE_ERROR:
			lv_msgbox_set_text(mbox_err, "Write error.");
			break;

		case LV_FM_DELETE_ERROR:
			lv_msgbox_set_text(mbox_err, "Delete error.");
			break;

		case LV_FM_FORMAT_ERROR:
			lv_msgbox_set_text(mbox_err, "Format error.");
			break;

		case LV_FM_MEMORY_ERROR:
			lv_msgbox_set_text(mbox_err, "Memory error.");
			break;

		case LV_FM_FILE_ALREADY_EXISTS:
			lv_msgbox_set_text(mbox_err, "File already exists.");
			break;

		case LV_FM_UNSUPPORTED_FORMAT:
			lv_msgbox_set_text(mbox_err, "Unsupported format.");
			break;
	}
}

static uint8_t lv_fm_media_detect(lv_fm_media_t * m)
{
	switch(m->media_hard)
	{
		case LV_FM_MEDIA_SD:
			return BSP_SD_IsDetected();

		case LV_FM_MEDIA_USB:
			if(USBH_GetActiveClass(&hUSBH) == USB_MSC_CLASS)
			{
				return !m->drv->disk_status(m->lun);
			}
			else
			{
				return 0;
			}
	}

	return 0;
}

static void lv_fm_media_check(lv_fm_media_t * m)
{
	int32_t i;
	uint8_t media_present_now;
	lv_fm_media_t * tm;
	
	for (i = 0; i < LV_FM_MAX_VOLUMES; i++)
	{
		tm = &m[i];
		media_present_now = lv_fm_media_detect(tm);
		
		if(media_present_now != tm->media_present)
		{
			if(media_present_now)
			{
				if( tm->drv->disk_initialize(tm->lun) != STA_NOINIT )
				{
					if( f_mount(&(tm->fat_fs), (TCHAR const*)tm->path, 1) == FR_OK )
					{
						tm->media_present = 1;
						tm->valid = 1;
					}
					else
					{
						tm->media_present = 0;
					}
				}
				else
				{
					tm->media_present = 0;
				}
			}
			else
			{
				tm->media_present = 0;
				tm->valid = 0;

				f_mount(NULL, (TCHAR const*)tm->path, 1);
			}

			lv_fm_list_fill(&fm_media[0], &fm_obj[0], list_local, lv_fm_list_local_btn_event_cb, 1);
		}
	}	
}

static void lv_fm_list_fill(lv_fm_media_t * m, lv_fm_obj_t * obj, lv_obj_t * list, lv_event_cb_t event_cb, uint8_t svol)
{
	uint32_t nobj = 0;
	uint32_t i;
	lv_obj_t * btn;
	lv_fm_obj_t * tobj;
	lv_fm_media_t * tm;
	char path[255];
	char str[255];
    FRESULT fr;
    DIR dir;
    FILINFO finfo;
	char * pimg;

	finfo.lfname = (TCHAR *) &lfn_buffer[0];
	finfo.lfsize = 255;

	if (svol)
	{
		for (i = 0; i < LV_FM_MAX_VOLUMES; i++)
		{
			tm = &m[i];
			if(tm->valid)
			{
				tobj = &obj[nobj];
				tobj->volume = 1;
				tobj->folder = 0;
				strcpy(tobj->name, tm->path);
				
				nobj++;
			}
		}
	}
	else
	{
		f_getcwd ((TCHAR*) &path, sizeof(path));
		for (i = 0; i < LV_FM_MAX_VOLUMES; i++)
		{
			tm = &m[i];
			if (strcmp(path, tm->path) == 0)
			{
				tobj = &obj[nobj];
				tobj->volume = 1;
				tobj->folder = 1;
				strcpy(tobj->name, "..");
				
				nobj++;
			}
		}
		
		fr = f_findfirst (&dir, &finfo, "", "*");
		for(i = 0; i < LV_FM_MAX_ELEMENTS; i++)
		{
			if(fr == FR_OK)
			{
				if(finfo.fattrib == AM_DIR)
    			{
                	if( (finfo.fname[0] == '.' && finfo.fname[1] == '\0') || \
                	     finfo.fname[0] == '\0' )
                	{
                    	fr = f_findnext(&dir, &finfo);
                    	continue;
                	}
					
					tobj = &obj[nobj];
					tobj->volume = 0;
					tobj->folder = 1;
					if(dir.lfn_idx != 0xFFFF)
					{
						strcpy(tobj->name, finfo.lfname);
					}
					else
					{
						strcpy(tobj->name, finfo.fname);
					}

                	nobj++;
    			}
				else if( (finfo.fattrib == AM_ARC) || (finfo.fattrib == (AM_ARC | AM_RDO)) )
				{
					if( (finfo.fname[0] == '.' || finfo.fname[0] == '\0') )
					{
						fr = f_findnext(&dir, &finfo);
						continue;
					}
					
					tobj = &obj[nobj];
					tobj->volume = 0;
					tobj->folder = 0;
					tobj->size = finfo.fsize;
					tobj->format = lv_fm_get_ext(&finfo);
					if(dir.lfn_idx != 0xFFFF)
					{
						strcpy(tobj->name, finfo.lfname);
					}
					else
					{
						strcpy(tobj->name, finfo.fname);
					}

					nobj++;
				}
			}
            else
            {
				break;
            }

			fr = f_findnext(&dir, &finfo);
		}
	}
	
	lv_list_clean(list);
	
	for (i = 0; i < nobj; i++)
	{
		tobj = &obj[i];
		
    	if(tobj->volume == 1 && \
    	   tobj->folder == 0)
    	{
    		pimg = LV_SYMBOL_DRIVE;
    	}
    	else if(tobj->folder == 1)
        {
            pimg = LV_SYMBOL_DIRECTORY;
        }
        else if(tobj->format == jpeg)
        {
            pimg = LV_SYMBOL_IMAGE;
        }
        else if(tobj->format == bmp)
        {
            pimg = LV_SYMBOL_IMAGE;
        }
        else if(tobj->format == flac)
        {
            pimg = LV_SYMBOL_AUDIO;
        }
        else if(tobj->format == mp3)
        {
            pimg = LV_SYMBOL_AUDIO;
        }
		else if(tobj->format == wav)
        {
            pimg = LV_SYMBOL_AUDIO;
        }
		else if(tobj->format == jmv)
		{
			pimg = LV_SYMBOL_VIDEO;
		}
        else
        {
            pimg = LV_SYMBOL_FILE;
        }	

    	if(tobj->folder == 0 && tobj->volume == 0)
    	{
    		lv_snprintf(str, sizeof(str), "%s\t-\t%d KB", tobj->name, tobj->size / 1024);
    		btn = lv_list_add_btn(list, pimg, str);
    	}
    	else
    	{
    		btn = lv_list_add_btn(list, pimg, tobj->name);
    	}
		lv_obj_set_event_cb(btn, event_cb);
	}
}

static lv_fm_format_t lv_fm_get_ext(FILINFO *finfo)
{
	lv_fm_format_t Ext;

	if     (strstr(finfo->fname, ".bmp") || strstr(finfo->fname, ".BMP"))
		Ext = bmp;
	else if(strstr(finfo->fname, ".jpg") || strstr(finfo->fname, ".JPG"))
		Ext = jpeg;
	else if(strstr(finfo->fname, ".gif") || strstr(finfo->fname, ".GIF"))
		Ext = gif;
	else if(strstr(finfo->fname, ".wav") || strstr(finfo->fname, ".WAV"))
		Ext = wav;
	else if(strstr(finfo->fname, ".mp3") || strstr(finfo->fname, ".MP3"))
		Ext = mp3;
	else if(strstr(finfo->fname, ".jmv") || strstr(finfo->fname, ".JMV"))
		Ext = jmv;
	else if(strstr(finfo->fname, ".fla") || strstr(finfo->fname, ".FLA"))
		Ext = flac;
	else if(strstr(finfo->fname, ".bin") || strstr(finfo->fname, ".BIN"))
		Ext = bin;
	else
		Ext = other;

	return(Ext);
}

static void lv_fm_copy(lv_fm_task_data_t * data)
{
	char * str;
    lv_coord_t grid_h = lv_page_get_height_grid(t1, 1, 1);
    lv_coord_t grid_w = lv_page_get_width_grid(t1, 1, 1);
    lv_fm_queue_data_t q_data;

	switch(data->stage)
	{
		case LV_FM_COPY_STAGE:
			str = (char *) &(data->src_path);
			f_getcwd ((TCHAR*) &(data->src_path), sizeof(data->src_path));
			memcpy(&(data->src_vol), &(data->src_path), 3);
			data->src_vol[3] = 0;

			if (data->obj->folder == 1)
			{
				if(data->src_path[3] == 0)
				{
				   str += strlen(data->src_path);
				   strcpy(str, data->obj->name);
				}
				else
				{
				   str += strlen(data->src_path);
				   strcpy(str, "/");
				   str += strlen("/");
				   strcpy(str, data->obj->name);
				}
			}
			break;

		case LV_FM_PASTE_STAGE:
			f_getcwd ((TCHAR*) &(data->dst_path), sizeof(data->dst_path));
			memcpy(&(data->dst_vol), &(data->dst_path), 3);
			data->dst_vol[3] = 0;

			if (data->obj->folder == 1)
			{
				data->err = lv_fm_folder_scan(data, data->src_path, data->dst_path, data->obj->name);
				data->count = 0;
				data->err = lv_fm_file_copy(data);
			}
			else
			{
				strcpy(q_data.name, data->obj->name);
				strcpy(q_data.src_path, data->src_path);
				strcpy(q_data.dst_path, data->dst_path);

				lv_fm_queue_put(&(data->head), &(data->tail), &q_data);

				data->count = 0;
				data->total = 1;
				data->err = lv_fm_file_copy(data);
			}
			
			data->stage = LV_FM_IDLE_STAGE;

			lv_obj_set_width(list_local, grid_w);
			lv_obj_align(list_local, t1, LV_ALIGN_CENTER, 0, 0);
			LV_FM_OBJ_DEL(list_options)
			break;
	}
}

static void lv_fm_delete(lv_fm_task_data_t * data)
{
    lv_coord_t grid_h = lv_page_get_height_grid(t1, 1, 1);
    lv_coord_t grid_w = lv_page_get_width_grid(t1, 1, 1);

	if (data->obj->folder == 1)
	{
		data->err = lv_fm_folder_delete(data->obj->name);
	}
	else
	{
		if( f_unlink (data->obj->name) != FR_OK)
		{
			data->err = LV_FM_DELETE_ERROR;
		}
	}

	lv_fm_list_fill(&fm_media[0], &fm_obj[0], list_local, lv_fm_list_local_btn_event_cb, 0);

	data->stage = LV_FM_IDLE_STAGE;

	lv_obj_set_width(list_local, grid_w);
	lv_obj_align(list_local, t1, LV_ALIGN_CENTER, 0, 0);
	LV_FM_OBJ_DEL(list_options)
}

static void lv_fm_format(lv_fm_task_data_t * data)
{
    lv_coord_t grid_h = lv_page_get_height_grid(t1, 1, 1);
    lv_coord_t grid_w = lv_page_get_width_grid(t1, 1, 1);

	if( f_mkfs (data->obj->name, 0, 0) != FR_OK)
	{
		data->err = LV_FM_FORMAT_ERROR;
	}

	data->stage = LV_FM_IDLE_STAGE;

	lv_obj_set_width(list_local, grid_w);
	lv_obj_align(list_local, t1, LV_ALIGN_CENTER, 0, 0);
	LV_FM_OBJ_DEL(list_options)
}

static void lv_fm_rename(lv_fm_task_data_t * data)
{
	lv_coord_t grid_h = lv_page_get_height_grid(t1, 1, 1);
	lv_coord_t grid_w = lv_page_get_width_grid(t1, 1, 1);

	h = lv_cont_create(t1, NULL);
	lv_cont_set_layout(h, LV_LAYOUT_PRETTY_MID);
    lv_cont_set_fit2(h, LV_FIT_NONE, LV_FIT_TIGHT);
	lv_obj_set_width(h, lv_page_get_width_grid(t1, 1, 1));

	ta = lv_textarea_create(h, NULL);
	lv_cont_set_fit2(ta, LV_FIT_PARENT, LV_FIT_NONE);
	lv_textarea_set_text(ta, "");
	lv_textarea_set_placeholder_text(ta, "Enter new name");
	lv_textarea_set_one_line(ta, true);
	lv_textarea_set_cursor_hidden(ta, false);

	lv_obj_set_height(tv, LV_VER_RES / 2);
	kb = lv_keyboard_create(lv_scr_act(), NULL);
	lv_obj_set_event_cb(kb, lv_fm_kb_event_cb);
	lv_page_focus(t1, lv_textarea_get_label(ta), LV_ANIM_ON);
	lv_keyboard_set_textarea(kb, ta);

	data->err = LV_FM_NO_ERROR;
	data->stage = LV_FM_IDLE_STAGE;

	lv_obj_set_width(list_local, grid_w);
	lv_obj_align(list_local, t1, LV_ALIGN_CENTER, 0, 0);
	LV_FM_OBJ_DEL(list_options)
}

static void lv_fm_create(lv_fm_task_data_t * data)
{
	lv_coord_t grid_h = lv_page_get_height_grid(t1, 1, 1);
	lv_coord_t grid_w = lv_page_get_width_grid(t1, 1, 1);

	if( f_mkdir ("New folder") != FR_OK)
	{
		data->err = LV_FM_WRITE_ERROR;
	}

	lv_fm_list_fill(&fm_media[0], &fm_obj[0], list_local, lv_fm_list_local_btn_event_cb, 0);

	data->stage = LV_FM_IDLE_STAGE;

	lv_obj_set_width(list_local, grid_w);
	lv_obj_align(list_local, t1, LV_ALIGN_CENTER, 0, 0);
	LV_FM_OBJ_DEL(list_options)
}

static lv_fm_err_t lv_fm_file_copy(lv_fm_task_data_t * data)
{
	data->buffer = (uint8_t *) malloc(data->buffer_size);
	if(!data->buffer)
	{
		return LV_FM_MEMORY_ERROR;
	}

	h = lv_cont_create(t1, NULL);
	lv_cont_set_layout(h, LV_LAYOUT_PRETTY_MID);
	lv_cont_set_fit2(h, LV_FIT_NONE, LV_FIT_TIGHT);
	lv_obj_set_width(h, lv_page_get_width_grid(t1, 1, 1));

	bar = lv_bar_create(h, NULL);
	lv_obj_set_width(bar, lv_obj_get_width_fit(h));

	lv_obj_t * btn = lv_btn_create(h, NULL);
	lv_obj_t * label = lv_label_create(btn, NULL);
	lv_label_set_text(label ,"Cancel");
	lv_btn_set_fit2(btn, LV_FIT_TIGHT, LV_FIT_TIGHT);
	lv_obj_set_width(btn, lv_obj_get_width_fit(h));
	lv_obj_set_event_cb(btn, lv_fm_copying_btn_event_cb);

	lv_obj_align(h, NULL, LV_ALIGN_CENTER, 0, 0);

	file_task = lv_task_create(lv_fm_file_task, 1, LV_TASK_PRIO_LOW, data);
	bar_task = lv_task_create(lv_fm_bar_task, 100, LV_TASK_PRIO_LOW, data);

	return LV_FM_NO_ERROR;
}

static lv_fm_err_t lv_fm_folder_scan(lv_fm_task_data_t * data, const char * src_path, const char * dst_path, const char * name)
{
	FRESULT 	fr;
	DIR 		dir;
	FILINFO 	finfo;
	char src_str[255], dst_str[255];
	char * str_ptr = (char *) &dst_str;
	lv_fm_queue_data_t q_data;
	
	finfo.lfname = (TCHAR *) &lfn_buffer[0];
	finfo.lfsize = 255;

	f_chdrive (data->dst_vol);
	if( f_chdir (dst_path) != FR_OK )
		return LV_FM_READ_ERROR;
	
	if( f_mkdir (name) != FR_OK )
		return LV_FM_WRITE_ERROR;
	
	if(dst_path[3] == 0)
	{
		strcpy(dst_str, dst_path);
		str_ptr += strlen(dst_path);
		strcpy(str_ptr, name);
	}
	else
	{
		strcpy(dst_str, dst_path);
		str_ptr += strlen(dst_path);
		strcpy(str_ptr, "/");
		str_ptr += strlen("/");
		strcpy(str_ptr, name);
	}

	f_chdrive (data->src_vol);
	if( f_chdir (src_path) != FR_OK )
		return LV_FM_READ_ERROR;

	fr = f_findfirst (&dir, &finfo, "", "*.*");
	while(fr == FR_OK)
	{
		if(finfo.fname[0] == 0)
            		break;

		if( (finfo.fattrib == AM_ARC) || (finfo.fattrib == (AM_ARC | AM_RDO)) )
		{
			if( finfo.fname[0] == '.' )
			{
				fr = f_findnext(&dir, &finfo);
				continue;
			}

			if(dir.lfn_idx != 0xFFFF)
			{
				strcpy(q_data.name, finfo.lfname);
			}
			else
			{
				strcpy(q_data.name, finfo.fname);
			}
			strcpy(q_data.src_path, src_path);
			strcpy(q_data.dst_path, dst_str);

			lv_fm_queue_put(&(data->head), &(data->tail), &q_data);

			data->total++;
		}

		fr = f_findnext(&dir, &finfo);
	}

	fr = f_findfirst (&dir, &finfo, "", "*");
	while(fr == FR_OK)
	{
		if(finfo.fname[0] == 0)
            		break;

		if(finfo.fattrib == AM_DIR)
		{
			if( finfo.fname[0] == '.' || (finfo.fname[0] == '.' && finfo.fname[1] == '.') )
			{
				fr = f_findnext(&dir, &finfo);
				continue;
			}

			if( f_chdir (finfo.fname) != FR_OK )
				return LV_FM_READ_ERROR;

			if( f_getcwd ((TCHAR*) &src_str, sizeof(src_str)) != FR_OK )
				return LV_FM_READ_ERROR;

			if(dir.lfn_idx != 0xFFFF)
			{
				data->err = lv_fm_folder_scan(data, src_str, dst_str, finfo.lfname);
			}
			else
			{
				data->err = lv_fm_folder_scan(data, src_str, dst_str, finfo.fname);
			}
			if(data->err != 0)
				return(data->err);

			f_chdrive (data->src_vol);
			if( f_chdir (src_path) != FR_OK )
				return LV_FM_READ_ERROR;
		}

		fr = f_findnext(&dir, &finfo);
	}

	return LV_FM_NO_ERROR;	
}

static lv_fm_err_t lv_fm_folder_delete(const char * path)
{
	FRESULT 	fr;
	DIR 		dir;
	FILINFO 	finfo;
	lv_fm_err_t err;
	
	finfo.lfname = (TCHAR *) &lfn_buffer[0];
	finfo.lfsize = 255;

	if( f_chdir (path) != FR_OK )
		return LV_FM_READ_ERROR;

	fr = f_findfirst (&dir, &finfo, "", "*.*");
	while(fr == FR_OK)
	{
		if(finfo.fname[0] == 0)
            		break;

		if( (finfo.fattrib == AM_ARC) || (finfo.fattrib == (AM_ARC | AM_RDO)) )
		{
			if( finfo.fname[0] == '.' )
			{
				fr = f_findnext(&dir, &finfo);
				continue;
			}

			if( f_unlink (finfo.fname) != FR_OK)
				return LV_FM_DELETE_ERROR;
		}

		fr = f_findnext(&dir, &finfo);
	}

	fr = f_findfirst (&dir, &finfo, "", "*");
	while(fr == FR_OK)
	{
		if(finfo.fname[0] == 0)
            		break;

		if(finfo.fattrib == AM_DIR)
		{
			if( finfo.fname[0] == '.' || (finfo.fname[0] == '.' && finfo.fname[1] == '.') )
			{
				fr = f_findnext(&dir, &finfo);
				continue;
			}

			err = lv_fm_folder_delete(finfo.fname);
			if(err != 0)
				return(err);
		}

		fr = f_findnext(&dir, &finfo);
	}

	if( f_chdir ("..") != FR_OK )
		return LV_FM_READ_ERROR;

	if( f_unlink (path) != FR_OK)
		return LV_FM_DELETE_ERROR;

	return LV_FM_NO_ERROR;	
}

static void lv_fm_queue_put(lv_fm_queue_t ** head, lv_fm_queue_t ** tail, lv_fm_queue_data_t * data)
{
	lv_fm_queue_t * q = (lv_fm_queue_t *) malloc(sizeof(lv_fm_queue_t));
	q->data = *data;
	q->next = NULL;

	if(*head == NULL)
	{
		*head = *tail = q;
	}
	else
	{
		(*tail)->next = q;
		*tail = q;
	}
}

static lv_fm_queue_t * lv_fm_queue_get(lv_fm_queue_t ** head, lv_fm_queue_t ** tail)
{
	if (*head == NULL)
	{
		return NULL;
	}

	lv_fm_queue_t * q = *head;
	*head = (*head)->next;
	q->next = NULL;

	if (*head == NULL)
	{
		*tail = NULL;
	}

	return q;
}

static void lv_fm_file_task_kill(lv_fm_task_data_t * data, lv_fm_err_t err)
{
	data->err = err;
	data->flag_cut = 0;
	data->total = data->count = 0;

	while(data->head != NULL)
	{
		free(lv_fm_queue_get(&(data->head), &(data->tail)));
	}

	lv_fm_list_fill(&fm_media[0], &fm_obj[0], list_local, lv_fm_list_local_btn_event_cb, 0);

	LV_FM_FREE(data->buffer)

	LV_FM_OBJ_DEL(h)

	LV_FM_TASK_DEL(bar_task)
	LV_FM_TASK_DEL(file_task)
}

void lv_fm_media_task(lv_task_t * task)
{
	lv_fm_media_t * m = task->user_data;

	if(hUSBH.device.is_connected == 1 && \
	   husb_state != HOST_USER_CLASS_ACTIVE)
	{
		return;
	}

	lv_fm_media_check(m);
}

void lv_fm_file_task(lv_task_t * task)
{
	uint32_t bytesread, byteswrite;
	lv_fm_task_data_t * data = task->user_data;
	lv_fm_queue_t * q = NULL;
	char * str;

	if(data->src.fs == NULL)
	{
		data->count++;
		q = lv_fm_queue_get(&(data->head), &(data->tail));
		if (q != NULL)
		{
			f_chdrive (data->src_vol);
			data->fr = f_chdir (q->data.src_path);
			if(data->fr != FR_OK)
			{
				lv_fm_file_task_kill(data, LV_FM_READ_ERROR);
				return;
			}

			data->fr = f_open (&(data->src), q->data.name, FA_READ);
			if(data->fr != FR_OK)
			{
				lv_fm_file_task_kill(data, LV_FM_READ_ERROR);
				return;
			}

			f_chdrive (data->dst_vol);
			data->fr = f_chdir (q->data.dst_path);
			if(data->fr != FR_OK)
			{
				f_close (&(data->src));
				lv_fm_file_task_kill(data, LV_FM_READ_ERROR);
				return;
			}

			data->fr = f_open (&(data->dst), q->data.name, FA_CREATE_NEW | FA_WRITE);
			if(data->fr != FR_OK)
			{
				f_close (&(data->src));
				lv_fm_file_task_kill(data, LV_FM_WRITE_ERROR);
				return;
			}

			LV_FM_FREE(q)
		}
	}

	data->fr = f_read (&(data->src), data->buffer, data->buffer_size, (UINT *) &bytesread);
	if(data->fr != FR_OK)
	{
		f_close (&(data->src));
		f_close (&(data->dst));
		lv_fm_file_task_kill(data, LV_FM_READ_ERROR);
		return;
	}

	data->fr = f_write (&(data->dst), data->buffer, bytesread, (UINT *) &byteswrite);
	if(data->fr != FR_OK)
	{
		f_close (&(data->src));
		f_close (&(data->dst));
		lv_fm_file_task_kill(data, LV_FM_WRITE_ERROR);
		return;
	}

	if (bytesread != data->buffer_size)
	{
		f_close (&(data->src));
		f_close (&(data->dst));

		if (data->count == data->total)
		{
			if (data->flag_cut == 1)
			{
				f_chdrive (data->src_vol);

				if (data->obj->folder == 1)
				{
					str = (char *) &(data->src_path);
					str += strlen(data->src_path);
					str -= strlen(data->obj->name) + 1;
					*str = 0;

					if (strlen(data->src_path) < 3)
					{
						*str = '/'; str++; *str = 0;
					}

					data->fr = f_chdir (data->src_path);
					if(data->fr != FR_OK)
					{
						lv_fm_file_task_kill(data, LV_FM_READ_ERROR);
						return;
					}

					data->err = lv_fm_folder_delete(data->obj->name);
				}
				else
				{
					data->fr = f_chdir (data->src_path);
					if(data->fr != FR_OK)
					{
						lv_fm_file_task_kill(data, LV_FM_READ_ERROR);
						return;
					}

					if( f_unlink (data->obj->name) != FR_OK)
					{
						data->err = LV_FM_DELETE_ERROR;
					}
				}
			}

			lv_fm_file_task_kill(data, LV_FM_NO_ERROR);
			return;
		}
	}
}

void lv_fm_bar_task(lv_task_t * task)
{
	static char buf[64];
	lv_fm_task_data_t * data = task->user_data;
	uint32_t value = (uint32_t) data->src.fptr / 1024;
	uint32_t total = (uint32_t) data->src.fsize / 1024;
	uint32_t n = (uint32_t) data->count;
	uint32_t t = (uint32_t) data->total;

	lv_snprintf(buf, sizeof(buf), "Copying %d  /  %d  KB     %d  /  %d", value, total, n, t);
	lv_obj_set_style_local_value_str(bar, LV_BAR_PART_BG, LV_STATE_DEFAULT, buf);

	int16_t x = (int16_t) ((value * 100) / total);
	lv_bar_set_value(bar, x, LV_ANIM_OFF);
}

void lv_fm_mbox_task(lv_task_t * task)
{
	lv_fm_task_data_t * data = task->user_data;

	if (data->err)
	{
		lv_fm_err(data->err);

		data->err = LV_FM_NO_ERROR;
	}
}
