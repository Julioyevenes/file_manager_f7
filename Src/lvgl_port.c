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
#include "lvgl_port.h"
#include "lvgl/lvgl.h"
#include "stm32f769i_discovery_lcd.h"
#include "stm32f769i_discovery_ts.h"

#include "usbh_core.h"
#include "usbh_hid.h"

/* Private types ------------------------------------------------------------*/
/* Private constants --------------------------------------------------------*/
#define LVGL_JPEG_INFO_TIMEOUT 	500
#define LVGL_JPEG_IMG_TIMEOUT 	5000

/* Private macro ------------------------------------------------------------*/
/* Private variables --------------------------------------------------------*/
static lv_disp_drv_t 				disp_drv;

static uint8_t * 					lvgl_img_addr;
static FIL 							lvgl_img_fh;
static JPEG_ConfTypeDef   			lvgl_img_jpeginfo;
static jpeg_codec_handle_t 			lvgl_img_hjpegcodec;

JPEG_HandleTypeDef          lvgl_img_hjpeg;
USBH_HandleTypeDef 			hUSBH;
USBH_HandleTypeDef * 		pusb_hid = NULL;
HID_MOUSE_Info_TypeDef * 	m_pinfo;

lv_indev_t * enc_indev;

/* Private function prototypes ----------------------------------------------*/
static void 	lvgl_disp_write_cb(lv_disp_drv_t* disp_drv, const lv_area_t* area, lv_color_t* color_p);
static bool 	lvgl_ts_read_cb(lv_indev_drv_t *indev, lv_indev_data_t *data);
static bool 	lvgl_mouse_read_cb(lv_indev_drv_t *indev, lv_indev_data_t *data);
static bool 	lvgl_encoder_read_cb(lv_indev_drv_t *indev, lv_indev_data_t *data);
static lv_res_t lvgl_img_decoder_info_cb(struct _lv_img_decoder * decoder, const void * src, lv_img_header_t * header);
static lv_res_t lvgl_img_decoder_open_cb(lv_img_decoder_t * decoder, lv_img_decoder_dsc_t * dsc);
static void 	lvgl_img_decoder_close_cb(lv_img_decoder_t * decoder, lv_img_decoder_dsc_t * dsc);

void lvgl_disp_init(void)
{
	uint32_t offset;
    /** 
      * Initialize your display
      */
    BSP_LCD_Init();
    BSP_LCD_LayerDefaultInit(0, LCD_FB_START_ADDRESS);
    BSP_LCD_SelectLayer(0);

    /** 
      * Create a buffer for drawing
      */
    static lv_disp_buf_t disp_buf;

    offset = BSP_LCD_GetXSize() * BSP_LCD_GetYSize() * sizeof(lv_color_t);
    lv_disp_buf_init(&disp_buf,
                     (lv_color_t *) LCD_FB_START_ADDRESS,
                     (lv_color_t *) (LCD_FB_START_ADDRESS + offset),
                     BSP_LCD_GetXSize() * BSP_LCD_GetYSize());   /* Initialize the display buffer */

    /** 
      * Register the display in LittlevGL
      */
    lv_disp_drv_init(&disp_drv);                    /* Basic initialization */

    /* Set the resolution of the display */
    disp_drv.hor_res = (lv_coord_t) BSP_LCD_GetXSize();
    disp_drv.ver_res = (lv_coord_t) BSP_LCD_GetYSize();

    /* Used to copy the buffer's content to the display */
    disp_drv.flush_cb = lvgl_disp_write_cb;

    /* Set a display buffer */
    disp_drv.buffer = &disp_buf;

    /* Finally register the driver */
    lv_disp_drv_register(&disp_drv);	
}

void lvgl_indev_init(void)
{
    lv_indev_drv_t indev_drv;
    lv_indev_t * indev_ptr;

    /** 
      * Touchpad
      */
    /* Initialize your touchpad if you have */
    if(BSP_TS_Init(BSP_LCD_GetXSize(), BSP_LCD_GetYSize()) == TS_OK)
    {
        /* Register a touchpad input device */
        lv_indev_drv_init(&indev_drv);
        indev_drv.type = LV_INDEV_TYPE_POINTER;
        indev_drv.read_cb = lvgl_ts_read_cb;
        lv_indev_drv_register(&indev_drv);
    }

    /**
      * Mouse
      */
    /* Initialize your mouse if you have */
    /* Nothing to do here */

    /* Register a mouse input device */
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = lvgl_mouse_read_cb;
    indev_ptr = lv_indev_drv_register(&indev_drv);

    /* Set cursor */
    lv_obj_t * mouse_cursor = lv_img_create(lv_disp_get_scr_act(NULL), NULL);
    lv_img_set_src(mouse_cursor, LV_SYMBOL_UP);
    lv_indev_set_cursor(indev_ptr, mouse_cursor);

    /**
      * Encoder
      */
    /* Initialize your mouse if you have */
    /* Nothing to do here */

    /* Register a mouse input device */
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_ENCODER;
    indev_drv.read_cb = lvgl_encoder_read_cb;
    enc_indev = lv_indev_drv_register(&indev_drv);
}

void lvgl_img_decoder_init(void)
{
    uint32_t offset;

    /* Init The JPEG Look Up Tables used for YCbCr to RGB conversion */
    JPEG_InitColorTables();

    /* Init the HAL JPEG driver */
    lvgl_img_hjpeg.Instance = JPEG;
    HAL_JPEG_Init(&lvgl_img_hjpeg);

    /* Set image raw data address */
	offset = BSP_LCD_GetXSize() * BSP_LCD_GetYSize() * sizeof(lv_color_t) * 2;
    lvgl_img_addr = (uint8_t *) (LCD_FB_START_ADDRESS + offset);

    lv_img_decoder_t * dec = lv_img_decoder_create();
    lv_img_decoder_set_info_cb(dec, lvgl_img_decoder_info_cb);
    lv_img_decoder_set_open_cb(dec, lvgl_img_decoder_open_cb);
    lv_img_decoder_set_close_cb(dec, lvgl_img_decoder_close_cb);
}

static void lvgl_disp_write_cb(lv_disp_drv_t* disp_drv, const lv_area_t* area, lv_color_t* color_p)
{
	/* Change display buffer address */
	BSP_LCD_SetLayerAddress(0, (uint32_t) disp_drv->buffer->buf_act);

	/* Inform the graphics library that you are ready with the flushing */
	lv_disp_flush_ready(disp_drv);
}

static bool lvgl_ts_read_cb(lv_indev_drv_t *indev, lv_indev_data_t *data)
{
    static lv_coord_t last_x = 0;
    static lv_coord_t last_y = 0;
    static uint8_t last_ts_detected = 0;
	TS_StateTypeDef TS_State;
	
	/* Fill touchscreen struct */
	BSP_TS_GetState(&TS_State);

    /*Save the pressed coordinates and the state*/
    if(TS_State.touchDetected) {
        last_x = TS_State.touchX[0];
        last_y = TS_State.touchY[0];
        data->state = LV_INDEV_STATE_PR;
    } else if(last_ts_detected) {
        last_ts_detected = 0;
        data->state = LV_INDEV_STATE_REL;
    }
    last_ts_detected = TS_State.touchDetected;

    /*Set the last pressed coordinates*/
    data->point.x = last_x;
    data->point.y = last_y;

    /*Return `false` because we are not buffering and no more data to read*/
    return false;
}

static bool lvgl_mouse_read_cb(lv_indev_drv_t *indev, lv_indev_data_t *data)
{
    static lv_coord_t last_x = 0;
    static lv_coord_t last_y = 0;
    static HID_MOUSE_Info_TypeDef last_m_pinfo;
    USBH_HandleTypeDef * phost = pusb_hid;
    HID_HandleTypeDef * HID_Handle;

    /* Fill mouse struct */
    if(phost->gState == HOST_CLASS)
    {
        if(USBH_GetActiveClass(phost) == USB_HID_CLASS)
        {
        	HID_Handle = (HID_HandleTypeDef *) phost->pActiveClass->pData;
        	if(HID_Handle->state != HID_INIT)
        	{
        		if(USBH_HID_GetDeviceType(phost) == HID_MOUSE)
        		{
        			m_pinfo = USBH_HID_GetMouseInfo(phost);
        		}
        	}
        }
    }

    /*Save the pressed coordinates and the state*/
    if(m_pinfo != NULL) {
        last_x += (int8_t) m_pinfo->x;
        last_y += (int8_t) m_pinfo->y;

    	if(last_x > indev->disp->driver.hor_res) {
    	    last_x = indev->disp->driver.hor_res;
    	}
    	if(last_x < 0) {
    	    last_x = 0;
    	}

    	if(last_y > indev->disp->driver.ver_res) {
    	    last_y = indev->disp->driver.ver_res;
    	}
    	if(last_y < 0) {
    	    last_y = 0;
    	}

        if(m_pinfo->buttons[0]) {
            data->state = LV_INDEV_STATE_PR;
        } else if(m_pinfo->buttons[1]) {
            data->state = LV_INDEV_STATE_RIGHT_PR;
        } else if(last_m_pinfo.buttons[0]) {
            last_m_pinfo.buttons[0] = 0;
            data->state = LV_INDEV_STATE_REL;
        } else if(last_m_pinfo.buttons[1]) {
            last_m_pinfo.buttons[1] = 0;
            data->state = LV_INDEV_STATE_RIGHT_REL;
        }

        last_m_pinfo = (HID_MOUSE_Info_TypeDef) (* m_pinfo);
    }

    /*Set the last pressed coordinates*/
    data->point.x = last_x;
    data->point.y = last_y;

    /*Return `false` because we are not buffering and no more data to read*/
    return false;
}

static bool lvgl_encoder_read_cb(lv_indev_drv_t *indev, lv_indev_data_t *data)
{
    int8_t dir;

    /* Fill encoder struct */
    /* Nothing to do here */

    /*Assing new value*/
    if(m_pinfo != NULL) {
		dir = (int8_t) m_pinfo->wheel_dir;
		if(dir > 0) {
			data->enc_diff++;
		}
		else if(dir < 0) {
			data->enc_diff--;
		}
		data->state = LV_INDEV_STATE_REL;
    }

    /*Return `false` because we are not buffering and no more data to read*/
    return false;
}

static lv_res_t lvgl_img_decoder_info_cb(struct _lv_img_decoder * decoder, const void * src, lv_img_header_t * header)
{
    lv_img_src_t src_type = lv_img_src_get_type(src);          /*Get the source type*/

    if(src_type == LV_IMG_SRC_FILE)
    {
        const char * fn = src;

        if(!strcmp(&fn[strlen(fn) - 3], "JPG") || \
           !strcmp(&fn[strlen(fn) - 3], "jpg"))
        {
            /* Open the JPG file with read access */
            if(f_open(&lvgl_img_fh, fn, FA_READ) == FR_OK)
            {
                /* Init JPEG codec */
                jpeg_decoder_init(&lvgl_img_hjpegcodec,
                                  &lvgl_img_hjpeg,
                                  &lvgl_img_fh,
								  0,
                                  lvgl_img_addr);

                /* JPEG decoding with DMA */
                jpeg_decoder_start(&lvgl_img_hjpegcodec, 0);

                /* Wait till end of JPEG decoding and perfom Input/Output Processing in BackGround */
                uint32_t timeout = HAL_GetTick();
                do
                {
                    jpeg_decoder_io(&lvgl_img_hjpegcodec);
                }
                while(lvgl_img_hjpegcodec.state != JPEG_CODEC_STATE_IMG && \
                	  (HAL_GetTick() - timeout) < LVGL_JPEG_INFO_TIMEOUT);

                /* Call abort function to clean handle */
                HAL_JPEG_Abort(&lvgl_img_hjpeg);

                /* Get JPEG Info */
                HAL_JPEG_GetInfo(&lvgl_img_hjpeg, &lvgl_img_jpeginfo);

                header->cf = LV_IMG_CF_TRUE_COLOR;
                header->w = lvgl_img_jpeginfo.ImageWidth;
                header->h = lvgl_img_jpeginfo.ImageHeight;

                /* Close the JPEG file */
                f_close(&lvgl_img_fh);

                /* Discard image if is bigger than res limit */
                if(lvgl_img_jpeginfo.ImageWidth > 2048 || \
                   lvgl_img_jpeginfo.ImageHeight > 2048)
                {
                	header->cf = LV_IMG_CF_UNKNOWN;
                	header->w = header->h = 0;

                	return LV_RES_INV;
                }

                return LV_RES_OK;
            }
        }
    }

    return LV_RES_INV;         /*If didn't succeeded earlier then it's an error*/
}

static lv_res_t lvgl_img_decoder_open_cb(lv_img_decoder_t * decoder, lv_img_decoder_dsc_t * dsc)
{
    if(dsc->src_type == LV_IMG_SRC_FILE)
    {
        const char * fn = dsc->src;

        if(!strcmp(&fn[strlen(fn) - 3], "JPG") || \
           !strcmp(&fn[strlen(fn) - 3], "jpg"))
        {
            /* Open the JPG file with read access */
            if(f_open(&lvgl_img_fh, fn, FA_READ) == FR_OK)
            {
                /* Init JPEG codec */
                jpeg_decoder_init(&lvgl_img_hjpegcodec,
                                  &lvgl_img_hjpeg,
                                  &lvgl_img_fh,
								  0,
                                  lvgl_img_addr);

                /* JPEG decoding with DMA */
                jpeg_decoder_start(&lvgl_img_hjpegcodec, 0);

                /* Wait till end of JPEG decoding and perfom Input/Output Processing in BackGround */
                uint32_t timeout = HAL_GetTick();
                do
                {
                    jpeg_decoder_io(&lvgl_img_hjpegcodec);
                }
                while(lvgl_img_hjpegcodec.state != JPEG_CODEC_STATE_IDLE && \
                	  (HAL_GetTick() - timeout) < LVGL_JPEG_IMG_TIMEOUT);

                if(lvgl_img_hjpegcodec.state != JPEG_CODEC_STATE_IDLE)
                {
                	/* Call abort function to clean handle */
                	HAL_JPEG_Abort(&lvgl_img_hjpeg);
                }

				dsc->img_data = (uint8_t *) lvgl_img_addr;

                /* Close the JPEG file */
                f_close(&lvgl_img_fh);

				return LV_RES_OK;
            }
        }
    }

    return LV_RES_INV;         /*If didn't succeeded earlier then it's an error*/
}

static void lvgl_img_decoder_close_cb(lv_img_decoder_t * decoder, lv_img_decoder_dsc_t * dsc)
{
}

/**
  * @brief  Initialize the BSP LCD Msp.
  */
void BSP_LCD_MspInit(void)
{
  /** @brief Enable the LTDC clock */
  __HAL_RCC_LTDC_CLK_ENABLE();

  /** @brief Toggle Sw reset of LTDC IP */
  __HAL_RCC_LTDC_FORCE_RESET();
  __HAL_RCC_LTDC_RELEASE_RESET();

  /** @brief Enable DSI Host and wrapper clocks */
  __HAL_RCC_DSI_CLK_ENABLE();

  /** @brief Soft Reset the DSI Host and wrapper */
  __HAL_RCC_DSI_FORCE_RESET();
  __HAL_RCC_DSI_RELEASE_RESET();

  /** @brief NVIC configuration for LTDC interrupt that is now enabled */
  HAL_NVIC_SetPriority(LTDC_IRQn, 3, 0);
  HAL_NVIC_EnableIRQ(LTDC_IRQn);

  /** @brief NVIC configuration for DSI interrupt that is now enabled */
  HAL_NVIC_SetPriority(DSI_IRQn, 3, 0);
  HAL_NVIC_EnableIRQ(DSI_IRQn);
}
