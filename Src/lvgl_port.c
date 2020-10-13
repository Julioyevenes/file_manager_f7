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
/* DMA Stream parameters definitions. You can modify these parameters to select
   a different DMA Stream and/or channel.
   But note that only DMA2 Streams are capable of Memory to Memory transfers. */
#define LVGL_DMA_STREAM               DMA2_Stream7
#define LVGL_DMA_CHANNEL              DMA_CHANNEL_0
#define LVGL_DMA_STREAM_IRQ           DMA2_Stream7_IRQn
#define LVGL_DMA_STREAM_IRQHANDLER    DMA2_Stream7_IRQHandler

/* Private macro ------------------------------------------------------------*/
/* Private variables --------------------------------------------------------*/
#if LV_COLOR_DEPTH == 16
static uint16_t * my_fb = (uint16_t *)LCD_FB_START_ADDRESS;
#else
static uint32_t * my_fb = (uint32_t *)LCD_FB_START_ADDRESS;
#endif

static DMA_HandleTypeDef     		lvgl_hdma;
static lv_disp_drv_t 				disp_drv;
static volatile int32_t 			x1_flush;
static volatile int32_t 			y1_flush;
static volatile int32_t 			x2_flush;
static volatile int32_t 			y2_flush;
static volatile int32_t 			y_flush_act;
static volatile const lv_color_t 	*buf_to_flush;

uint8_t husb_state;
USBH_HandleTypeDef hUSBH;
HID_MOUSE_Info_TypeDef * m_pinfo;

lv_indev_t * enc_indev;

/* Private function prototypes ----------------------------------------------*/
static void lvgl_disp_write_cb(lv_disp_drv_t* disp_drv, const lv_area_t* area, lv_color_t* color_p);
static bool lvgl_ts_read_cb(lv_indev_drv_t *indev, lv_indev_data_t *data);
static bool lvgl_mouse_read_cb(lv_indev_drv_t *indev, lv_indev_data_t *data);
static bool lvgl_encoder_read_cb(lv_indev_drv_t *indev, lv_indev_data_t *data);
static void DMA_Config(void);
static void DMA_TransferComplete(DMA_HandleTypeDef *han);
static void DMA_TransferError(DMA_HandleTypeDef *han);

void lvgl_disp_init(void)
{
    /** 
      * Initialize your display
      */
    BSP_LCD_Init();
    BSP_LCD_LayerDefaultInit(0, LCD_FB_START_ADDRESS);
    BSP_LCD_SelectLayer(0);

    /** 
      * Initialize dma
      */
	DMA_Config();
	  
    /** 
      * Create a buffer for drawing
      */
    static lv_disp_buf_t disp_buf;
    static lv_color_t buf00[LV_HOR_RES_MAX * 48];                      /* A buffer for 48 rows */
    static lv_color_t buf01[LV_HOR_RES_MAX * 48];                      /* A buffer for 48 rows */
    lv_disp_buf_init(&disp_buf, buf00, buf01, LV_HOR_RES_MAX * 48);   /* Initialize the display buffer */

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

static void lvgl_disp_write_cb(lv_disp_drv_t* disp_drv, const lv_area_t* area, lv_color_t* color_p)
{
	/*Truncate the area to the screen*/
	int32_t act_x1 = area->x1 < 0 ? 0 : area->x1;
	int32_t act_y1 = area->y1 < 0 ? 0 : area->y1;
	int32_t act_x2 = area->x2 > disp_drv->hor_res - 1 ? disp_drv->hor_res - 1 : area->x2;
	int32_t act_y2 = area->y2 > disp_drv->ver_res - 1 ? disp_drv->ver_res - 1 : area->y2;

	x1_flush = act_x1;
	y1_flush = act_y1;
	x2_flush = act_x2;
	y2_flush = act_y2;
	y_flush_act = act_y1;
	buf_to_flush = color_p;

	/*Use DMA instead of DMA2D to leave it free for GPU*/
	HAL_StatusTypeDef err;
	err = HAL_DMA_Start_IT(&lvgl_hdma,
						   (uint32_t)buf_to_flush,
						   (uint32_t)&my_fb[y_flush_act * disp_drv->hor_res + x1_flush],
			               (x2_flush - x1_flush + 1));
	if(err != HAL_OK)
	{
		while(1);	/*Halt on error*/
	}
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
    USBH_HandleTypeDef * phost = &hUSBH;
    HID_HandleTypeDef * HID_Handle;

    /* Fill mouse struct */
    if(husb_state == HOST_USER_CLASS_ACTIVE)
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

/**
  * @brief  Configure the DMA controller according to the Stream parameters
  *         defined in main.h file
  * @note  This function is used to :
  *        -1- Enable DMA2 clock
  *        -2- Select the DMA functional Parameters
  *        -3- Select the DMA instance to be used for the transfer
  *        -4- Select Callbacks functions called after Transfer complete and
               Transfer error interrupt detection
  *        -5- Initialize the DMA stream
  *        -6- Configure NVIC for DMA transfer complete/error interrupts
  * @param  None
  * @retval None
  */
static void DMA_Config(void)
{
  /* Enable DMA2 clock */
  __HAL_RCC_DMA2_CLK_ENABLE();

  /* Select the DMA functional Parameters */
  lvgl_hdma.Init.Channel = LVGL_DMA_CHANNEL;                     /* DMA_CHANNEL_0                    */
  lvgl_hdma.Init.Direction = DMA_MEMORY_TO_MEMORY;          /* M2M transfer mode                */
  lvgl_hdma.Init.PeriphInc = DMA_PINC_ENABLE;               /* Peripheral increment mode Enable */
  lvgl_hdma.Init.MemInc = DMA_MINC_ENABLE;                  /* Memory increment mode Enable     */
#if LV_COLOR_DEPTH == 16
  lvgl_hdma.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD; /* Peripheral data alignment : 16bit */
  lvgl_hdma.Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;    /* memory data alignment : 16bit     */
#else
  lvgl_hdma.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD; /* Peripheral data alignment : 16bit */
  lvgl_hdma.Init.MemDataAlignment = DMA_MDATAALIGN_WORD;    /* memory data alignment : 16bit     */
#endif
  lvgl_hdma.Init.Mode = DMA_NORMAL;                         /* Normal DMA mode                  */
  lvgl_hdma.Init.Priority = DMA_PRIORITY_HIGH;              /* priority level : high            */
  lvgl_hdma.Init.FIFOMode = DMA_FIFOMODE_ENABLE;            /* FIFO mode enabled                */
  lvgl_hdma.Init.FIFOThreshold = DMA_FIFO_THRESHOLD_FULL; /* FIFO threshold: 1/4 full   */
  lvgl_hdma.Init.MemBurst = DMA_MBURST_SINGLE;              /* Memory burst                     */
  lvgl_hdma.Init.PeriphBurst = DMA_PBURST_SINGLE;           /* Peripheral burst                 */

  /* Select the DMA instance to be used for the transfer : DMA2_Stream7 */
  lvgl_hdma.Instance = LVGL_DMA_STREAM;

  /* Initialize the DMA stream */
  if(HAL_DMA_Init(&lvgl_hdma) != HAL_OK)
  {
    while(1);
  }

  /* Select Callbacks functions called after Transfer complete and Transfer error */
  HAL_DMA_RegisterCallback(&lvgl_hdma, HAL_DMA_XFER_CPLT_CB_ID, DMA_TransferComplete);
  HAL_DMA_RegisterCallback(&lvgl_hdma, HAL_DMA_XFER_ERROR_CB_ID, DMA_TransferError);

  /* Configure NVIC for DMA transfer complete/error interrupts */
  HAL_NVIC_SetPriority(LVGL_DMA_STREAM_IRQ, 0, 0);
  HAL_NVIC_EnableIRQ(LVGL_DMA_STREAM_IRQ);
}

/**
  * @brief  DMA conversion complete callback
  * @note   This function is executed when the transfer complete interrupt
  *         is generated
  * @retval None
  */
static void DMA_TransferComplete(DMA_HandleTypeDef *hdma)
{
	y_flush_act ++;

	if(y_flush_act > y2_flush) {
	  lv_disp_flush_ready(&disp_drv);
	} else {
	  buf_to_flush += x2_flush - x1_flush + 1;
	  /* Start the DMA transfer using the interrupt mode */
	  /* Configure the source, destination and buffer size DMA fields and Start DMA Stream transfer */
	  /* Enable All the DMA interrupts */
	  if(HAL_DMA_Start_IT(hdma,
						  (uint32_t)buf_to_flush, 
						  (uint32_t)&my_fb[y_flush_act * disp_drv.hor_res + x1_flush],
						  (x2_flush - x1_flush + 1)) != HAL_OK)
	  {
	    while(1);	/*Halt on error*/
	  }
	}
}

/**
  * @brief  DMA conversion error callback
  * @note   This function is executed when the transfer error interrupt
  *         is generated during DMA transfer
  * @retval None
  */
static void DMA_TransferError(DMA_HandleTypeDef *hdma)
{
    while(1);
}

/**
  * @brief  This function handles DMA Stream interrupt request.
  * @param  None
  * @retval None
  */
void LVGL_DMA_STREAM_IRQHANDLER(void)
{
    /* Check the interrupt and clear flag */
    HAL_DMA_IRQHandler(&lvgl_hdma);
}
