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
#include "lv_file_loader.h"

/* Private types -------------------------------------------------------------*/
typedef void (* lv_file_loader_app) (void);

typedef struct
{
	FRESULT 			fr;

	FIL * 				fp;

	uint32_t *			cpy_addr;
	uint32_t *			mem_addr;
	
	uint32_t			cpy_buf[512];
	uint32_t			cpy_size;
	uint32_t			mem_size;	

	lv_obj_t * 			parent;
	lv_file_loader_app	app;

	lv_fm_loader_err_t	err;
} lv_fm_loader_handle_t;

/* Private constants ---------------------------------------------------------*/
#define LV_FM_LOADER_MEMORY_ADDR 	0x08100000
#define LV_FM_LOADER_MEMORY_SIZE 	0x00100000

#define ADDR_FLASH_SECTOR_0     	((uint32_t)0x08000000) /* Base address of Sector 0, 32 Kbytes */
#define ADDR_FLASH_SECTOR_1     	((uint32_t)0x08008000) /* Base address of Sector 1, 32 Kbytes */
#define ADDR_FLASH_SECTOR_2     	((uint32_t)0x08010000) /* Base address of Sector 2, 32 Kbytes */
#define ADDR_FLASH_SECTOR_3     	((uint32_t)0x08018000) /* Base address of Sector 3, 32 Kbytes */
#define ADDR_FLASH_SECTOR_4     	((uint32_t)0x08020000) /* Base address of Sector 4, 128 Kbytes */
#define ADDR_FLASH_SECTOR_5     	((uint32_t)0x08040000) /* Base address of Sector 5, 256 Kbytes */
#define ADDR_FLASH_SECTOR_6     	((uint32_t)0x08080000) /* Base address of Sector 6, 256 Kbytes */
#define ADDR_FLASH_SECTOR_7     	((uint32_t)0x080C0000) /* Base address of Sector 7, 256 Kbytes */
#define ADDR_FLASH_SECTOR_8     	((uint32_t)0x08100000) /* Base address of Sector 8, 256 Kbytes */
#define ADDR_FLASH_SECTOR_9     	((uint32_t)0x08140000) /* Base address of Sector 9, 256 Kbytes */
#define ADDR_FLASH_SECTOR_10    	((uint32_t)0x08180000) /* Base address of Sector 10, 256 Kbytes */
#define ADDR_FLASH_SECTOR_11    	((uint32_t)0x081C0000) /* Base address of Sector 11, 256 Kbytes */

/* Private macro -------------------------------------------------------------*/
#define LV_FM_LOADER_OBJ_DEL(ptr)	if (ptr != NULL) \
									{ \
										lv_obj_del(ptr); ptr = NULL; \
									}

#define LV_FM_LOADER_TASK_DEL(ptr)	if (ptr != NULL) \
									{ \
										lv_task_del(ptr); ptr = NULL; \
									}

/* Private variables ---------------------------------------------------------*/
lv_fm_loader_handle_t hloader;

lv_task_t * loader_copy_task;
lv_task_t * loader_bar_task;

lv_obj_t * loader_h;
lv_obj_t * loader_bar;

/* Private function prototypes -----------------------------------------------*/
static 	void 				lv_fm_loader_copy(lv_fm_loader_handle_t * h);
static 	void 				lv_fm_loader_start(lv_fm_loader_handle_t * h);

static 	void 				lv_fm_loader_copying_btn_event_cb(lv_obj_t * btn, lv_event_t e);

static 	lv_fm_loader_err_t 	lv_fm_loader_mem_erase(uint32_t * addr, uint32_t size);
static 	lv_fm_loader_err_t	lv_fm_loader_mem_write(uint32_t ** addr, uint32_t * buf_addr, uint32_t size);
static 	uint32_t 			lv_fm_loader_get_sector(uint32_t addr);

static 	void 				lv_fm_loader_copy_task_kill(void);

		void 				lv_fm_loader_copy_task(lv_task_t * task);
		void 				lv_fm_loader_bar_task(lv_task_t * task);

lv_fm_loader_err_t lv_fm_loader_init(lv_obj_t * parent, FIL * fp)
{
	uint32_t data, bytesread, i = 0;

	hloader.fp = fp;
	hloader.cpy_addr = (uint32_t *) LV_FM_LOADER_MEMORY_ADDR;
	hloader.mem_addr = (uint32_t *) LV_FM_LOADER_MEMORY_ADDR;
	hloader.cpy_size = (uint32_t) 512 * sizeof(uint32_t);
	hloader.mem_size = (uint32_t) LV_FM_LOADER_MEMORY_SIZE;
	hloader.parent = parent;
	hloader.err = LV_FM_LOADER_NO_ERROR;
	
	if(hloader.fp->fsize > hloader.mem_size)
	{
		hloader.err = LV_FM_LOADER_SIZE_ERROR;
		return hloader.err;
	}

	do
	{
		hloader.fr = f_read(hloader.fp, &data, 4, (UINT *) &bytesread);
		if(bytesread)
		{
			if(hloader.mem_addr[i] != data)
			{
				break;
			}
		}
		else
		{
			lv_fm_loader_start(&hloader);
			return hloader.err;
		}
		
		i++;
	}
	while(hloader.fr == FR_OK);

	f_rewind(hloader.fp);
	
	hloader.err = lv_fm_loader_mem_erase(hloader.mem_addr, hloader.mem_size);
	if(hloader.err != LV_FM_LOADER_NO_ERROR)
	{
		return hloader.err;
	}
	
	HAL_FLASH_Unlock();
	
	lv_fm_loader_copy(&hloader);
	
	return hloader.err;
}

static void lv_fm_loader_copy(lv_fm_loader_handle_t * h)
{
	if(h->mem_addr == NULL)
	{
		h->err = LV_FM_LOADER_MEMORY_ERROR;
		return;
	}

	loader_h = lv_cont_create(h->parent, NULL);
	lv_cont_set_layout(loader_h, LV_LAYOUT_PRETTY_MID);
	lv_cont_set_fit2(loader_h, LV_FIT_NONE, LV_FIT_TIGHT);
	lv_obj_set_width(loader_h, lv_page_get_width_grid(h->parent, 1, 1));

	loader_bar = lv_bar_create(loader_h, NULL);
	lv_obj_set_width(loader_bar, lv_obj_get_width_fit(loader_h));

	lv_obj_t * btn = lv_btn_create(loader_h, NULL);
	lv_obj_t * label = lv_label_create(btn, NULL);
	lv_label_set_text(label ,"Cancel");
	lv_btn_set_fit2(btn, LV_FIT_TIGHT, LV_FIT_TIGHT);
	lv_obj_set_width(btn, lv_obj_get_width_fit(loader_h));
	lv_obj_set_event_cb(btn, lv_fm_loader_copying_btn_event_cb);

	loader_copy_task = lv_task_create(lv_fm_loader_copy_task, 1, LV_TASK_PRIO_MID, h);
	loader_bar_task = lv_task_create(lv_fm_loader_bar_task, 100, LV_TASK_PRIO_MID, h);
}

static void lv_fm_loader_start(lv_fm_loader_handle_t * h)
{
	h->app = (lv_file_loader_app) h->mem_addr[1];
	if(h->app == NULL)
	{
		h->err = LV_FM_LOADER_MEMORY_ERROR;
		return;
	}
	
	HAL_RCC_DeInit();
	HAL_DeInit();
	
    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL  = 0;	

	SCB->VTOR = (uint32_t) h->mem_addr;
	__set_MSP(h->mem_addr[0]);

	h->app();
}

static void lv_fm_loader_copying_btn_event_cb(lv_obj_t * btn, lv_event_t e)
{
	if (e == LV_EVENT_CLICKED)
	{
		LV_FM_LOADER_TASK_DEL(loader_bar_task)
		LV_FM_LOADER_TASK_DEL(loader_copy_task)

		f_close (hloader.fp);
		HAL_FLASH_Lock();

		LV_FM_LOADER_OBJ_DEL(loader_h)
	}
}

static lv_fm_loader_err_t lv_fm_loader_mem_erase(uint32_t * addr, uint32_t size)
{
	uint32_t FirstSector = 0, NbOfSectors = 0, SECTORError = 0;
	static FLASH_EraseInitTypeDef EraseInitStruct;
	HAL_StatusTypeDef status = HAL_OK;

	HAL_FLASH_Unlock();
	
	__HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS);

	/* Get the 1st sector to erase */
	FirstSector = lv_fm_loader_get_sector((uint32_t) addr);

	/* Get the number of sector to erase from 1st sector*/
	NbOfSectors = lv_fm_loader_get_sector((uint32_t) (addr + (size / 4))) - FirstSector + 1;

  	EraseInitStruct.TypeErase     = FLASH_TYPEERASE_SECTORS;
  	EraseInitStruct.VoltageRange  = FLASH_VOLTAGE_RANGE_3;
  	EraseInitStruct.Sector        = FirstSector;
  	EraseInitStruct.NbSectors     = NbOfSectors;

	status = HAL_FLASHEx_Erase(&EraseInitStruct, &SECTORError);

	HAL_FLASH_Lock();

	return (status == HAL_OK) ? LV_FM_LOADER_NO_ERROR : LV_FM_LOADER_ERASE_ERROR;
}

static lv_fm_loader_err_t lv_fm_loader_mem_write(uint32_t ** addr, uint32_t * buf_addr, uint32_t size)
{
	uint32_t i;

	for(i = 0; i < size; i++)
	{
		if(HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, (uint32_t) addr[0], buf_addr[i]) == HAL_OK)
		{
			/* Check the written value */
			if(*(uint32_t*)addr[0] != buf_addr[i])
			{
				/* Flash content doesn't match source content */
				HAL_FLASH_Lock();
				return LV_FM_LOADER_WRITE_ERROR;
			}
			/* Increment Flash destination address */
			addr[0] += 1;
		}
		else
		{
			/* Error occurred while writing buf_addr into Flash */
			HAL_FLASH_Lock();
			return LV_FM_LOADER_WRITE_ERROR;
		}
	}

	return LV_FM_LOADER_NO_ERROR;
}

static uint32_t lv_fm_loader_get_sector(uint32_t addr)
{
	uint32_t sector = 0;

	if((addr < ADDR_FLASH_SECTOR_1) && (addr >= ADDR_FLASH_SECTOR_0))
	{
		sector = FLASH_SECTOR_0;
	}
	else if((addr < ADDR_FLASH_SECTOR_2) && (addr >= ADDR_FLASH_SECTOR_1))
	{
		sector = FLASH_SECTOR_1;
	}
	else if((addr < ADDR_FLASH_SECTOR_3) && (addr >= ADDR_FLASH_SECTOR_2))
	{
		sector = FLASH_SECTOR_2;
	}
	else if((addr < ADDR_FLASH_SECTOR_4) && (addr >= ADDR_FLASH_SECTOR_3))
	{
		sector = FLASH_SECTOR_3;
	}
	else if((addr < ADDR_FLASH_SECTOR_5) && (addr >= ADDR_FLASH_SECTOR_4))
	{
		sector = FLASH_SECTOR_4;
	}
	else if((addr < ADDR_FLASH_SECTOR_6) && (addr >= ADDR_FLASH_SECTOR_5))
	{
		sector = FLASH_SECTOR_5;
	}
	else if((addr < ADDR_FLASH_SECTOR_7) && (addr >= ADDR_FLASH_SECTOR_6))
	{
		sector = FLASH_SECTOR_6;
	}
	else if((addr < ADDR_FLASH_SECTOR_8) && (addr >= ADDR_FLASH_SECTOR_7))
	{
		sector = FLASH_SECTOR_7;
	}
	else if((addr < ADDR_FLASH_SECTOR_9) && (addr >= ADDR_FLASH_SECTOR_8))
	{
		sector = FLASH_SECTOR_8;
	}
	else if((addr < ADDR_FLASH_SECTOR_10) && (addr >= ADDR_FLASH_SECTOR_9))
	{
		sector = FLASH_SECTOR_9;
	}
	else if((addr < ADDR_FLASH_SECTOR_11) && (addr >= ADDR_FLASH_SECTOR_10))
	{
		sector = FLASH_SECTOR_10;
	}
	else /* (addr < FLASH_END_ADDR) && (addr >= ADDR_FLASH_SECTOR_11) */
	{
		sector = FLASH_SECTOR_11;
	}

	return sector;
}

static void lv_fm_loader_copy_task_kill(void)
{
	HAL_FLASH_Lock();
	f_close (hloader.fp);
	LV_FM_LOADER_TASK_DEL(loader_bar_task)
	LV_FM_LOADER_TASK_DEL(loader_copy_task)
	LV_FM_LOADER_OBJ_DEL(loader_h)
}

void lv_fm_loader_copy_task(lv_task_t * task)
{
	uint32_t bytesread;
	lv_fm_loader_handle_t * h = task->user_data;

	h->fr = f_read (h->fp, &(h->cpy_buf), h->cpy_size, (UINT *) &bytesread);
	if(h->fr != FR_OK)
	{
		lv_fm_loader_copy_task_kill();
		h->err = LV_FM_LOADER_READ_ERROR;
		return;
	}
	else
	{
		h->err = lv_fm_loader_mem_write(&(h->cpy_addr), (uint32_t *) &(h->cpy_buf), bytesread / 4);
		if(h->err != LV_FM_LOADER_NO_ERROR)
		{
			lv_fm_loader_copy_task_kill();
			return;
		}
	}

	if (bytesread != h->cpy_size)
	{
		lv_fm_loader_copy_task_kill();
		h->err = LV_FM_LOADER_NO_ERROR;
		lv_fm_loader_start(&hloader);
	}
}

void lv_fm_loader_bar_task(lv_task_t * task)
{
	static char buf[64];
	lv_fm_loader_handle_t * h = task->user_data;
	uint32_t value = (uint32_t) h->fp->fptr / 1024;
	uint32_t total = (uint32_t) h->fp->fsize / 1024;

	lv_snprintf(buf, sizeof(buf), "Copying %d  /  %d  KB", value, total);
	lv_obj_set_style_local_value_str(loader_bar, LV_BAR_PART_BG, LV_STATE_DEFAULT, buf);

	int16_t x = (int16_t) ((value * 100) / total);
	lv_bar_set_value(loader_bar, x, LV_ANIM_OFF);
}
