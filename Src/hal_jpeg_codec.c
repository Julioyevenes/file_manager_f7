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
#include "hal_jpeg_codec.h"

/* Private types -------------------------------------------------------------*/
/* Private constants ---------------------------------------------------------*/
#define JPEG_CODEC_SIZE_IN  4096
#define JPEG_CODEC_SIZE_OUT 768

/* Private macro -------------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/
static jpeg_codec_handle_t * hjpegcodec;

/* Private function prototypes -----------------------------------------------*/

jpeg_codec_err_t jpeg_decoder_init(jpeg_codec_handle_t * hcodec, JPEG_HandleTypeDef * hjpeg, FIL * fp, uint32_t dst_addr)
{
    uint8_t * ptr;

    hjpegcodec = hcodec;
    memset(hcodec, 0, sizeof(jpeg_codec_handle_t));

    hcodec->frame_addr = (uint8_t *) dst_addr;
    hcodec->fp = fp;
    hcodec->hjpeg = hjpeg;

    ptr = (uint8_t *) malloc((JPEG_CODEC_SIZE_IN + JPEG_CODEC_SIZE_OUT) * 2);
    if(ptr == NULL)
    {
        return JPEG_CODEC_MEMORY_ERROR;
    }
    hcodec->in_buf[0].ptr = ptr;
    hcodec->in_buf[1].ptr = ptr + JPEG_CODEC_SIZE_IN;
    hcodec->out_buf[0].ptr = ptr + (JPEG_CODEC_SIZE_IN * 2);
    hcodec->out_buf[1].ptr = ptr + (JPEG_CODEC_SIZE_IN * 2) + JPEG_CODEC_SIZE_OUT;

    return JPEG_CODEC_NO_ERROR;
}

jpeg_codec_err_t jpeg_decoder_start(jpeg_codec_handle_t * hcodec)
{
    uint32_t i;

    /* Read from JPG file and fill input buffers */
    for(i = 0; i < 2; i++)
    {
        if(f_read (hcodec->fp, 
                   hcodec->in_buf[i].ptr, 
                   JPEG_CODEC_SIZE_IN, 
                   (UINT *)(&hcodec->in_buf[i].size)) == FR_OK)
        {
            hcodec->in_buf[i].full = 1;
        }
        else
        {
            return JPEG_CODEC_READ_ERROR;
        }
    }

    /* Start JPEG decoding with DMA method */
    HAL_JPEG_Decode_DMA(hcodec->hjpeg,
                        hcodec->in_buf[0].ptr,
                        hcodec->in_buf[0].size, 
                        hcodec->out_buf[0].ptr,
                        JPEG_CODEC_SIZE_OUT);

    hcodec->state = JPEG_CODEC_STATE_INFO;
    return JPEG_CODEC_NO_ERROR;
}

jpeg_codec_err_t jpeg_decoder_io(jpeg_codec_handle_t * hcodec)
{
    uint32_t data_converted;

    if(hcodec->in_buf[hcodec->in_write_idx].full == 0)
    {
        if(f_read (hcodec->fp, 
                   hcodec->in_buf[hcodec->in_write_idx].ptr, 
                   JPEG_CODEC_SIZE_IN, 
                   (UINT *)(&hcodec->in_buf[hcodec->in_write_idx].size)) == FR_OK)
        {
            hcodec->in_buf[hcodec->in_write_idx].full = 1;
        }
        else
        {
            return JPEG_CODEC_READ_ERROR;
        } 

        if((hcodec->in_pause == 1) && (hcodec->in_write_idx == hcodec->in_read_idx))
        {
            hcodec->in_pause = 0;
            HAL_JPEG_ConfigInputBuffer(hcodec->hjpeg,
                                       hcodec->in_buf[hcodec->in_read_idx].ptr,
                                       hcodec->in_buf[hcodec->in_read_idx].size);
            HAL_JPEG_Resume(hcodec->hjpeg, JPEG_PAUSE_RESUME_INPUT);
        } 

        hcodec->in_write_idx++;
        if(hcodec->in_write_idx >= 2)
        {
            hcodec->in_write_idx = 0;
        }      
    }

    if(hcodec->out_buf[hcodec->out_read_idx].full == 1)
    {
        hcodec->mcu_value += hcodec->color_fn(hcodec->out_buf[hcodec->out_read_idx].ptr, 
                                              hcodec->frame_addr, 
                                              hcodec->mcu_value, 
                                              hcodec->out_buf[hcodec->out_read_idx].size, 
                                              &data_converted);

        hcodec->out_buf[hcodec->out_read_idx].full = 0;
        hcodec->out_buf[hcodec->out_read_idx].size = 0;

        hcodec->out_read_idx++;
        if(hcodec->out_read_idx >= 2)
        {
            hcodec->out_read_idx = 0;
        }

        if(hcodec->mcu_value == hcodec->mcu_total)
        {
            hcodec->state = JPEG_CODEC_STATE_IDLE;
        }
    }
    else if((hcodec->out_pause == 1) && \
            (hcodec->out_buf[hcodec->out_read_idx].full == 0) &&\
            (hcodec->out_buf[hcodec->out_write_idx].full == 0))
    {
        hcodec->out_pause = 0;
        HAL_JPEG_Resume(hcodec->hjpeg, JPEG_PAUSE_RESUME_OUTPUT);
    }

    return JPEG_CODEC_NO_ERROR;
}

void jpeg_decoder_free(jpeg_codec_handle_t * hcodec)
{
    uint8_t * ptr = hcodec->in_buf[0].ptr;

    free(ptr);
    hcodec->in_buf[0].ptr = hcodec->in_buf[1].ptr = NULL;
    hcodec->out_buf[0].ptr = hcodec->out_buf[1].ptr = NULL;   
}

void HAL_JPEG_InfoReadyCallback(JPEG_HandleTypeDef * hjpeg, JPEG_ConfTypeDef * pInfo)
{
	if(pInfo->ChromaSubsampling == JPEG_420_SUBSAMPLING)
	{
		if((pInfo->ImageWidth % 16) != 0)
		pInfo->ImageWidth += (16 - (pInfo->ImageWidth % 16));

		if((pInfo->ImageHeight % 16) != 0)
		pInfo->ImageHeight += (16 - (pInfo->ImageHeight % 16));
	}

	if(pInfo->ChromaSubsampling == JPEG_422_SUBSAMPLING)
	{
		if((pInfo->ImageWidth % 16) != 0)
		pInfo->ImageWidth += (16 - (pInfo->ImageWidth % 16));

		if((pInfo->ImageHeight % 8) != 0)
		pInfo->ImageHeight += (8 - (pInfo->ImageHeight % 8));
	}

	if(pInfo->ChromaSubsampling == JPEG_444_SUBSAMPLING)
	{
		if((pInfo->ImageWidth % 8) != 0)
		pInfo->ImageWidth += (8 - (pInfo->ImageWidth % 8));

		if((pInfo->ImageHeight % 8) != 0)
		pInfo->ImageHeight += (8 - (pInfo->ImageHeight % 8));
	}

	JPEG_GetDecodeColorConvertFunc(pInfo, &(hjpegcodec->color_fn), &(hjpegcodec->mcu_total));

	hjpegcodec->state = JPEG_CODEC_STATE_IMG;
}

void HAL_JPEG_GetDataCallback(JPEG_HandleTypeDef * hjpeg, uint32_t NbDecodedData)
{
	if(NbDecodedData == hjpegcodec->in_buf[hjpegcodec->in_read_idx].size)
	{
		hjpegcodec->in_buf[hjpegcodec->in_read_idx].full = 0;
		hjpegcodec->in_buf[hjpegcodec->in_read_idx].size = 0;

		hjpegcodec->in_read_idx++;
		if(hjpegcodec->in_read_idx >= 2)
		{
			hjpegcodec->in_read_idx = 0;
		}

		if(hjpegcodec->in_buf[hjpegcodec->in_read_idx].full == 0)
		{
			HAL_JPEG_Pause(hjpegcodec->hjpeg, JPEG_PAUSE_RESUME_INPUT);
			hjpegcodec->in_pause = 1;
		}
		else
		{
			HAL_JPEG_ConfigInputBuffer(hjpegcodec->hjpeg,
									   hjpegcodec->in_buf[hjpegcodec->in_read_idx].ptr,
									   hjpegcodec->in_buf[hjpegcodec->in_read_idx].size);
		}
	}
	else
	{
		HAL_JPEG_ConfigInputBuffer(hjpegcodec->hjpeg,
								   hjpegcodec->in_buf[hjpegcodec->in_read_idx].ptr + NbDecodedData,
								   hjpegcodec->in_buf[hjpegcodec->in_read_idx].size - NbDecodedData);
	}
}

void HAL_JPEG_DataReadyCallback (JPEG_HandleTypeDef * hjpeg, uint8_t * pDataOut, uint32_t OutDataLength)
{
	hjpegcodec->out_buf[hjpegcodec->out_write_idx].full = 1;
	hjpegcodec->out_buf[hjpegcodec->out_write_idx].size = OutDataLength;

	hjpegcodec->out_write_idx++;
	if(hjpegcodec->out_write_idx >= 2)
	{
		hjpegcodec->out_write_idx = 0;
	}

	if(hjpegcodec->out_buf[hjpegcodec->out_write_idx].full != 0)
	{
		HAL_JPEG_Pause(hjpegcodec->hjpeg, JPEG_PAUSE_RESUME_OUTPUT);
		hjpegcodec->out_pause = 1;
	}
	HAL_JPEG_ConfigOutputBuffer(hjpegcodec->hjpeg,
							    hjpegcodec->out_buf[hjpegcodec->out_write_idx].ptr,
							    JPEG_CODEC_SIZE_OUT);
}

void HAL_JPEG_ErrorCallback(JPEG_HandleTypeDef * hjpeg)
{
}

void HAL_JPEG_DecodeCpltCallback(JPEG_HandleTypeDef * hjpeg)
{
}

void HAL_JPEG_MspInit(JPEG_HandleTypeDef *hjpeg)
{
	static DMA_HandleTypeDef   hdmaIn;
	static DMA_HandleTypeDef   hdmaOut;

	/* Enable JPEG clock */
	__HAL_RCC_JPEG_CLK_ENABLE();

	/* Enable DMA clock */
	__HAL_RCC_DMA2_CLK_ENABLE();

	HAL_NVIC_SetPriority(JPEG_IRQn, 0x06, 0x0F);
	HAL_NVIC_EnableIRQ(JPEG_IRQn);

	/* Input DMA */
	/* Set the parameters to be configured */
	hdmaIn.Init.Channel = DMA_CHANNEL_9;
	hdmaIn.Init.Direction = DMA_MEMORY_TO_PERIPH;
	hdmaIn.Init.PeriphInc = DMA_PINC_DISABLE;
	hdmaIn.Init.MemInc = DMA_MINC_ENABLE;
	hdmaIn.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
	hdmaIn.Init.MemDataAlignment = DMA_MDATAALIGN_WORD;
	hdmaIn.Init.Mode = DMA_NORMAL;
	hdmaIn.Init.Priority = DMA_PRIORITY_HIGH;
	hdmaIn.Init.FIFOMode = DMA_FIFOMODE_ENABLE;
	hdmaIn.Init.FIFOThreshold = DMA_FIFO_THRESHOLD_FULL;
	hdmaIn.Init.MemBurst = DMA_MBURST_INC4;
	hdmaIn.Init.PeriphBurst = DMA_PBURST_INC4;
	hdmaIn.Instance = DMA2_Stream3;

	/* Associate the DMA handle */
	__HAL_LINKDMA(hjpeg, hdmain, hdmaIn);

	HAL_NVIC_SetPriority(DMA2_Stream3_IRQn, 0x07, 0x0F);
	HAL_NVIC_EnableIRQ(DMA2_Stream3_IRQn);

	/* DeInitialize the DMA Stream */
	HAL_DMA_DeInit(&hdmaIn);
	/* Initialize the DMA stream */
	HAL_DMA_Init(&hdmaIn);

	/* Output DMA */
	/* Set the parameters to be configured */
	hdmaOut.Init.Channel = DMA_CHANNEL_9;
	hdmaOut.Init.Direction = DMA_PERIPH_TO_MEMORY;
	hdmaOut.Init.PeriphInc = DMA_PINC_DISABLE;
	hdmaOut.Init.MemInc = DMA_MINC_ENABLE;
	hdmaOut.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
	hdmaOut.Init.MemDataAlignment = DMA_MDATAALIGN_WORD;
	hdmaOut.Init.Mode = DMA_NORMAL;
	hdmaOut.Init.Priority = DMA_PRIORITY_VERY_HIGH;
	hdmaOut.Init.FIFOMode = DMA_FIFOMODE_ENABLE;
	hdmaOut.Init.FIFOThreshold = DMA_FIFO_THRESHOLD_FULL;
	hdmaOut.Init.MemBurst = DMA_MBURST_INC4;
	hdmaOut.Init.PeriphBurst = DMA_PBURST_INC4;
	hdmaOut.Instance = DMA2_Stream4;

	/* Associate the DMA handle */
	__HAL_LINKDMA(hjpeg, hdmaout, hdmaOut);

	HAL_NVIC_SetPriority(DMA2_Stream4_IRQn, 0x07, 0x0F);
	HAL_NVIC_EnableIRQ(DMA2_Stream4_IRQn);

	/* DeInitialize the DMA Stream */
	HAL_DMA_DeInit(&hdmaOut);
	/* Initialize the DMA stream */
	HAL_DMA_Init(&hdmaOut);  
}

void JPEG_IRQHandler(void)
{
	HAL_JPEG_IRQHandler(hjpegcodec->hjpeg);
}

void DMA2_Stream3_IRQHandler(void)
{
    HAL_DMA_IRQHandler(hjpegcodec->hjpeg->hdmain);
}

void DMA2_Stream4_IRQHandler(void)
{
    HAL_DMA_IRQHandler(hjpegcodec->hjpeg->hdmaout);
}
