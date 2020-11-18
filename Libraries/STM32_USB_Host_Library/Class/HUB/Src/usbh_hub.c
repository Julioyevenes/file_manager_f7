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
#include "usbh_hub.h"

#include "usbh_hid.h"
#include "usbh_msc.h"

/* Private types -------------------------------------------------------------*/
typedef enum
{
	HUB_IDLE = 0,
	HUB_SYNC,
	HUB_BUSY,
	HUB_GET_DATA,
	HUB_POLL,
	HUB_LOOP_PORT_CHANGED,
	HUB_LOOP_PORT_WAIT,
	HUB_PORT_CHANGED,
	HUB_C_PORT_CONNECTION,
	HUB_C_PORT_RESET,
	HUB_RESET_DEVICE,
	HUB_DEV_ATTACHED,
	HUB_DEV_DETACHED,
	HUB_C_PORT_OVER_CURRENT,
	HUB_C_PORT_SUSPEND,
	HUB_C_PORT_ENABLE,
	HUB_ERROR
} HUB_StateTypeDef;

typedef enum
{
	HUB_REQ_IDLE = 0,
	HUB_REQ_GET_DESCRIPTOR,
	HUB_REQ_SET_POWER,
	HUB_WAIT_PWRGOOD,
	HUB_REQ_DONE
} HUB_CtlStateTypeDef;

typedef struct
{
	uint8_t  bLength;               /* Length of this descriptor. */
	uint8_t  bDescriptorType;       /* Descriptor Type, value: 29H for hub descriptor. */
	uint8_t  bNbrPorts;             /* Number of downstream facing ports that this hub supports. */
	uint16_t wHubCharacteristics;
    uint8_t  bPwrOn2PwrGood;        /* Time (in 2 ms intervals) from the time the power-on sequence begins on a port until power is good on that port. */
    uint8_t  bHubContrCurrent;      /* Maximum current requirements of the Hub Controller electronics in mA. */
    uint8_t  DeviceRemovable;       /* Indicates if a port has a removable device attached. */
    uint8_t  PortPwrCtrlMask;       /* This field exists for reasons of compatibility with software written for 1.0 compliant devices. */
} __attribute__ ((packed)) HUB_DescriptorTypeDef;

typedef struct
{
    union
    {
        struct
        {
        	uint8_t     connected 		: 1;
        	uint8_t     enable 			: 1;
        	uint8_t     suspend 		: 1;
        	uint8_t     over_current    : 1;
        	uint8_t     reset           : 1;
        	uint8_t     reserved_1		: 3;
        	uint8_t     power           : 1;
        	uint8_t     low_speed       : 1;
        	uint8_t     high_speed      : 1;
        	uint8_t     test            : 1;
        	uint8_t     indicator       : 1;
        	uint8_t     reserved_2		: 3;
        };

        uint16_t val;
    }   wPortStatus;

    union
    {
        struct
        {
        	uint8_t     c_connected		: 1;
        	uint8_t     c_enable        : 1;
        	uint8_t     c_suspend		: 1;
        	uint8_t     c_over_current	: 1;
        	uint8_t     c_reset			: 1;
        	uint16_t    reserved		: 11;
        };

        uint16_t val;
    }   wPortChange;
} __attribute__ ((packed)) HUB_PortStateTypeDef;

typedef struct
{
	HUB_StateTypeDef     	state;
	HUB_CtlStateTypeDef  	ctl_state;
	HUB_PortStateTypeDef *	cur_port_state;
	USBH_HandleTypeDef *	husb;
	
	uint8_t					cur_port;
	uint8_t					cur_port_speed;
	uint8_t					ports_change;
	uint8_t					total_port_num;
	uint8_t              	InPipe;
	uint8_t              	InEp;
	uint8_t              	buffer[20];
	uint16_t             	length;
	uint8_t              	ep_addr;
	uint16_t             	poll;
	uint32_t             	timer;
	uint8_t              	DataReady;
} HUB_HandleTypeDef;

/* Private constants ---------------------------------------------------------*/
#define USB_HUB_CLASS     						0x09
#define USB_DESC_HUB                   			((0x29 << 8) & 0xFF00)

#define HUB_MAX_PROTOCOL 						0x02
#define HUB_MIN_POLL 							200

#define HUB_ADDRESS_DEFAULT                     0
#define HUB_MPS_DEFAULT                         0x40
#define HUB_MPS_LOWSPEED                        8

#define HUB_FEAT_SEL_PORT_CONN					0x00
#define HUB_FEAT_SEL_PORT_ENABLE             	0x01
#define HUB_FEAT_SEL_PORT_SUSPEND            	0x02
#define HUB_FEAT_SEL_PORT_OVER_CURRENT       	0x03
#define HUB_FEAT_SEL_PORT_RESET             	0x04
#define HUB_FEAT_SEL_PORT_POWER              	0x08
#define HUB_FEAT_SEL_PORT_LOW_SPEED          	0x09
#define HUB_FEAT_SEL_C_PORT_CONNECTION       	0x10
#define HUB_FEAT_SEL_C_PORT_ENABLE           	0x11
#define HUB_FEAT_SEL_C_PORT_SUSPEND          	0x12
#define HUB_FEAT_SEL_C_PORT_OVER_CURRENT     	0x13
#define HUB_FEAT_SEL_C_PORT_RESET            	0x14
#define HUB_FEAT_SEL_PORT_INDICATOR          	0x16

/* Private macro -------------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/
/* Private function prototypes -----------------------------------------------*/
static USBH_StatusTypeDef 	USBH_HUB_InterfaceInit(USBH_HandleTypeDef * phost);
static USBH_StatusTypeDef 	USBH_HUB_InterfaceDeInit(USBH_HandleTypeDef * phost);
static USBH_StatusTypeDef 	USBH_HUB_Process(USBH_HandleTypeDef * phost);
static USBH_StatusTypeDef 	USBH_HUB_ClassRequest(USBH_HandleTypeDef * phost);
static USBH_StatusTypeDef 	USBH_HUB_SOFProcess(USBH_HandleTypeDef * phost);

static USBH_StatusTypeDef 	HUB_Request(USBH_HandleTypeDef * phost, 
										uint8_t request, 
										uint8_t dataDirection,
										uint16_t value, 
										uint16_t index, 
										uint8_t * buffer, 
										uint16_t size);
static void 				HUB_DeviceConnect(USBH_HandleTypeDef * phost, uint8_t port, uint8_t speed);
static void 				HUB_DeviceDisconnect(USBH_HandleTypeDef * phost, uint8_t port);
static uint8_t 				HUB_GetPort(uint8_t * byte);

USBH_ClassTypeDef  HUB_Class =
{
  "HUB",
  USB_HUB_CLASS,
  USBH_HUB_InterfaceInit,
  USBH_HUB_InterfaceDeInit,
  USBH_HUB_ClassRequest,
  USBH_HUB_Process,
  USBH_HUB_SOFProcess,
  NULL
};

static USBH_StatusTypeDef USBH_HUB_InterfaceInit(USBH_HandleTypeDef *phost)
{
	USBH_StatusTypeDef status;
	HUB_HandleTypeDef * HUB_Handle;
	uint8_t interface, proto_idx = 0;

	do
	{
		interface = USBH_FindInterface(phost, phost->pActiveClass->ClassCode, 0x00, proto_idx++);
	}
	while((interface == 0xFFU) && (proto_idx <= 2));

	if ((interface == 0xFFU) || (interface >= USBH_MAX_NUM_INTERFACES)) /* No Valid Interface */
	{
		USBH_DbgLog("Cannot Find the interface for %s class.", phost->pActiveClass->Name);
		return USBH_FAIL;
	}

	status = USBH_SelectInterface(phost, interface);

	if (status != USBH_OK)
	{
		return USBH_FAIL;
	}

	phost->pActiveClass->pData = (HUB_HandleTypeDef *) USBH_malloc(sizeof(HUB_HandleTypeDef));
	HUB_Handle = (HUB_HandleTypeDef *) phost->pActiveClass->pData;

	if (HUB_Handle == NULL)
	{
		USBH_DbgLog("Cannot allocate memory for HID Handle");
		return USBH_FAIL;
	}

	/* Initialize hid handler */
	USBH_memset(HUB_Handle, 0, sizeof(HUB_HandleTypeDef));

	HUB_Handle->state     = HUB_IDLE;
	HUB_Handle->ctl_state = HUB_REQ_IDLE;
	HUB_Handle->ep_addr   = phost->device.CfgDesc.Itf_Desc[interface].Ep_Desc[0].bEndpointAddress;
	HUB_Handle->length    = phost->device.CfgDesc.Itf_Desc[interface].Ep_Desc[0].wMaxPacketSize;
	HUB_Handle->poll      = phost->device.CfgDesc.Itf_Desc[interface].Ep_Desc[0].bInterval;

	if (HUB_Handle->poll  < HUB_MIN_POLL)
	{
		HUB_Handle->poll = HUB_MIN_POLL;
	}

	if (phost->device.CfgDesc.Itf_Desc[interface].Ep_Desc[0].bEndpointAddress & 0x80U)
	{
		HUB_Handle->InEp = (phost->device.CfgDesc.Itf_Desc[interface].Ep_Desc[0].bEndpointAddress);
		HUB_Handle->InPipe = USBH_AllocPipe(phost, HUB_Handle->InEp);

		/* Open pipe for IN endpoint */
		USBH_OpenPipe(	phost,
						HUB_Handle->InPipe,
						HUB_Handle->InEp,
						phost->device.address,
						phost->device.speed,
						USB_EP_TYPE_INTR,
						HUB_Handle->length);

		USBH_LL_SetToggle(phost, HUB_Handle->InPipe, 0U);
	}

	return USBH_OK;
}

static USBH_StatusTypeDef USBH_HUB_InterfaceDeInit(USBH_HandleTypeDef * phost)
{
	USBH_StatusTypeDef status = USBH_OK;
	
	HUB_HandleTypeDef * HUB_Handle = (HUB_HandleTypeDef *) phost->pActiveClass->pData;
	
	if(HUB_Handle->InPipe != 0x00)
	{
		USBH_ClosePipe(phost, HUB_Handle->InPipe);
		USBH_FreePipe(phost, HUB_Handle->InPipe);
		HUB_Handle->InPipe = 0; /* Reset the Channel as Free */
	}
	
	if(HUB_Handle->husb != NULL)
	{
		USBH_free(HUB_Handle->husb);
		HUB_Handle->husb = NULL;
	}

	if (phost->pActiveClass->pData != NULL)
	{
		USBH_free(phost->pActiveClass->pData);
		phost->pActiveClass->pData = NULL;
	}

	return status;	
}

static USBH_StatusTypeDef USBH_HUB_Process(USBH_HandleTypeDef * phost)
{
	USBH_StatusTypeDef status = USBH_OK;
	
	HUB_HandleTypeDef * HUB_Handle = (HUB_HandleTypeDef *) phost->pActiveClass->pData;

	switch (HUB_Handle->state)
	{
		case HUB_IDLE:
			HUB_Handle->cur_port = 0;
			HUB_Handle->state = HUB_SYNC;
			break;
			
		case HUB_SYNC:
			/* Sync with start of Even Frame */
			if(phost->Timer & 1)
			{
				HUB_Handle->state = HUB_GET_DATA;
			}		
			break;

		case HUB_GET_DATA:
			USBH_InterruptReceiveData(phost, HUB_Handle->buffer, HUB_Handle->length, HUB_Handle->InPipe);
    
			HUB_Handle->state = HUB_POLL;
			HUB_Handle->timer = phost->Timer;
			HUB_Handle->DataReady = 0;		
			break;	

		case HUB_POLL:
			if(USBH_LL_GetURBState(phost, HUB_Handle->InPipe) == USBH_URB_DONE)
			{
				if(HUB_Handle->DataReady == 0)
				{
					HUB_Handle->DataReady = 1;
					HUB_Handle->ports_change = HUB_Handle->buffer[0];

					if(HUB_Handle->ports_change)
					{
						HUB_Handle->state = HUB_LOOP_PORT_CHANGED;
					}
					else
					{
						HUB_Handle->state = HUB_GET_DATA;
					}
				}
			}
			else if(USBH_LL_GetURBState(phost , HUB_Handle->InPipe) == USBH_URB_STALL) /* IN Endpoint Stalled */
			{
    			/* Issue Clear Feature on interrupt IN endpoint */
    			if( (USBH_ClrFeature(phost, HUB_Handle->ep_addr)) == USBH_OK)
    			{
    				/* Change state to issue next IN token */
    				HUB_Handle->state = HUB_GET_DATA;
    			}				
			}
			break;

		case HUB_LOOP_PORT_CHANGED:
			HUB_Handle->cur_port = HUB_GetPort(&(HUB_Handle->ports_change));

			if(HUB_Handle->cur_port > 0)
			{
				USBH_Delay(200U);
				HUB_Handle->state = HUB_PORT_CHANGED;
			}
			else
			{
				HUB_Handle->state = HUB_IDLE;
			}
			break;

		case HUB_PORT_CHANGED:	
			if(HUB_Request(	phost, 
							USB_REQ_GET_STATUS, 
							USB_D2H,
							HUB_FEAT_SEL_PORT_CONN,
							HUB_Handle->cur_port,
							HUB_Handle->buffer, 
							sizeof(HUB_PortStateTypeDef)) == USBH_OK)
			{
				HUB_Handle->cur_port_state = (HUB_PortStateTypeDef *) HUB_Handle->buffer;
				
				if(HUB_Handle->cur_port_state->wPortStatus.power)
				{
					if(HUB_Handle->cur_port_state->wPortChange.c_suspend)
					{
						HUB_Handle->state = HUB_C_PORT_SUSPEND;
						break;						
					}

					if(HUB_Handle->cur_port_state->wPortChange.c_over_current)
					{
						HUB_Handle->state = HUB_C_PORT_OVER_CURRENT;
						break;						
					}

					if(HUB_Handle->cur_port_state->wPortChange.c_connected)
					{
						HUB_Handle->state = HUB_C_PORT_CONNECTION;
					}
					else
					{
						if(HUB_Handle->cur_port_state->wPortStatus.connected)
						{
							if(HUB_Handle->cur_port_state->wPortStatus.reset)
							{
								HUB_Handle->state = HUB_PORT_CHANGED;
							}
							else if(HUB_Handle->cur_port_state->wPortChange.c_reset)
							{
								HUB_Handle->state = HUB_C_PORT_RESET;
							}
							else if(HUB_Handle->cur_port_state->wPortStatus.enable)
							{
								HUB_Handle->state = HUB_DEV_ATTACHED;
							}
							else if(HUB_Handle->cur_port_state->wPortChange.c_enable)
							{
								HUB_Handle->state = HUB_C_PORT_ENABLE;
							}
							else
							{
								HUB_Handle->state = HUB_RESET_DEVICE;
							}
						}
						else
						{
							HUB_Handle->state = HUB_DEV_DETACHED;
						}
					}					
				}
			}
			break;

		case HUB_C_PORT_ENABLE:
			if(HUB_Request(	phost,
							USB_REQ_CLEAR_FEATURE,
							USB_H2D,
							HUB_FEAT_SEL_C_PORT_ENABLE,
							HUB_Handle->cur_port,
							0,
							0) == USBH_OK)
			{
				HUB_Handle->state = HUB_PORT_CHANGED;
			}
			break;

		case HUB_C_PORT_SUSPEND:
			if(HUB_Request(	phost, 
							USB_REQ_CLEAR_FEATURE, 
							USB_H2D,
							HUB_FEAT_SEL_C_PORT_SUSPEND,
							HUB_Handle->cur_port,
							0, 
							0) == USBH_OK)
			{
				HUB_Handle->state = HUB_PORT_CHANGED;
			}						
			break;

		case HUB_C_PORT_OVER_CURRENT:
			if(HUB_Request(	phost, 
							USB_REQ_CLEAR_FEATURE, 
							USB_H2D,
							HUB_FEAT_SEL_C_PORT_OVER_CURRENT,
							HUB_Handle->cur_port,
							0, 
							0) == USBH_OK)
			{
				HUB_Handle->state = HUB_PORT_CHANGED;
			}		
			break;

		case HUB_C_PORT_CONNECTION:
			if(HUB_Request(	phost, 
							USB_REQ_CLEAR_FEATURE, 
							USB_H2D,
							HUB_FEAT_SEL_C_PORT_CONNECTION,
							HUB_Handle->cur_port,
							0, 
							0) == USBH_OK)
			{
				HUB_Handle->state = HUB_PORT_CHANGED;
			}		
			break;

		case HUB_C_PORT_RESET:
			if(HUB_Request(	phost, 
							USB_REQ_CLEAR_FEATURE, 
							USB_H2D,
							HUB_FEAT_SEL_C_PORT_RESET,
							HUB_Handle->cur_port,
							0, 
							0) == USBH_OK)
			{
				HUB_Handle->state = HUB_PORT_CHANGED;
			}		
			break;		

		case HUB_RESET_DEVICE:
			if(HUB_Request(	phost, 
							USB_REQ_SET_FEATURE, 
							USB_H2D,
							HUB_FEAT_SEL_PORT_RESET,
							HUB_Handle->cur_port,
							0, 
							0) == USBH_OK)
			{
				HUB_Handle->state = HUB_PORT_CHANGED;
			}		
			break;

		case HUB_DEV_ATTACHED:
			HUB_Handle->state = HUB_LOOP_PORT_WAIT;
			
			if(HUB_Handle->cur_port_state->wPortStatus.high_speed)
			{
				HUB_Handle->cur_port_speed = 2;
			}
			else if(HUB_Handle->cur_port_state->wPortStatus.low_speed)
			{
				HUB_Handle->cur_port_speed = 0;
			}
			else
			{
				HUB_Handle->cur_port_speed = 1;
			}

			HUB_DeviceConnect(	phost, 
								HUB_Handle->cur_port,
								HUB_Handle->cur_port_speed);
			break;

		case HUB_DEV_DETACHED:
			HUB_Handle->state = HUB_LOOP_PORT_WAIT;
			
			HUB_DeviceDisconnect(	phost, 
									HUB_Handle->cur_port);
			break;	

		case HUB_LOOP_PORT_WAIT:
    		USBH_Delay(10);
    		HUB_Handle->state = HUB_LOOP_PORT_CHANGED;
			break;

		case HUB_ERROR:
		default:
			break;				
	}

	return status;	
}

static USBH_StatusTypeDef USBH_HUB_ClassRequest(USBH_HandleTypeDef * phost)
{
	USBH_StatusTypeDef status = USBH_BUSY;
	
	HUB_HandleTypeDef * HUB_Handle = (HUB_HandleTypeDef *) phost->pActiveClass->pData;
	HUB_DescriptorTypeDef * HUB_Desc = (HUB_DescriptorTypeDef *) HUB_Handle->buffer;
	
	static uint8_t port_idx;
	
	switch (HUB_Handle->ctl_state)
	{
		case HUB_REQ_IDLE:
		case HUB_REQ_GET_DESCRIPTOR:
			if(USBH_GetDescriptor(	phost,
									USB_REQ_RECIPIENT_DEVICE | USB_REQ_TYPE_CLASS,                                  
									USB_DESC_HUB,
									HUB_Handle->buffer,
									sizeof(HUB_DescriptorTypeDef)) == USBH_OK)
			{
				HUB_Handle->husb = (USBH_HandleTypeDef *) USBH_malloc(sizeof(USBH_HandleTypeDef) * HUB_Desc->bNbrPorts);
				memset(HUB_Handle->husb, 0, sizeof(USBH_HandleTypeDef) * HUB_Desc->bNbrPorts);
				
				HUB_Handle->total_port_num = HUB_Desc->bNbrPorts;
				HUB_Handle->ctl_state = HUB_REQ_SET_POWER;

				phost->HubNbrPorts = HUB_Desc->bNbrPorts;
				phost->childs = HUB_Handle->husb;

				port_idx = 1;
			}
			break;
			
		case HUB_REQ_SET_POWER:
			if(HUB_Request(	phost, 
							USB_REQ_SET_FEATURE, 
							USB_H2D,
							HUB_FEAT_SEL_PORT_POWER,
							port_idx,
							0, 
							0) == USBH_OK)
			{
				if(port_idx == HUB_Handle->total_port_num)
				{
					HUB_Handle->ctl_state = HUB_WAIT_PWRGOOD;
				}
				else
				{
					port_idx++;
				}
			}								
			break;
			
		case HUB_WAIT_PWRGOOD:
    		USBH_Delay(HUB_Desc->bPwrOn2PwrGood * 2);
    		HUB_Handle->ctl_state = HUB_REQ_DONE;		
			break;
			
		case HUB_REQ_DONE:
			status = USBH_OK;
			break;
	}

	return status;	
}

static USBH_StatusTypeDef USBH_HUB_SOFProcess(USBH_HandleTypeDef * phost)
{
	USBH_StatusTypeDef status = USBH_OK;
	
	HUB_HandleTypeDef * HUB_Handle = (HUB_HandleTypeDef *) phost->pActiveClass->pData;
	
	if(HUB_Handle->state == HUB_POLL)
	{
		if((phost->Timer - HUB_Handle->timer) >= HUB_Handle->poll)
		{
			HUB_Handle->state = HUB_GET_DATA;
		}
	}	

	return status;		
}

static USBH_StatusTypeDef HUB_Request(	USBH_HandleTypeDef * phost, 
										uint8_t request, 
										uint8_t dataDirection,
										uint16_t value, 
										uint16_t index, 
										uint8_t * buffer, 
										uint16_t size)
{
	phost->Control.setup.b.bmRequestType = dataDirection | USB_REQ_RECIPIENT_OTHER | USB_REQ_TYPE_CLASS;
	phost->Control.setup.b.bRequest  	 = request;
	phost->Control.setup.b.wValue.w		 = value;
	phost->Control.setup.b.wIndex.w		 = index;
	phost->Control.setup.b.wLength.w     = size;

	return USBH_CtlReq(phost, buffer, size);	
}

static void HUB_DeviceConnect(USBH_HandleTypeDef * phost, uint8_t port, uint8_t speed)
{
	HUB_HandleTypeDef * HUB_Handle = (HUB_HandleTypeDef *) phost->pActiveClass->pData;
	USBH_HandleTypeDef * husb = (USBH_HandleTypeDef *) &(HUB_Handle->husb[port - 1]);

	if(husb->valid == 0)
	{
		USBH_ClassTypeDef * hclass = (USBH_ClassTypeDef *) USBH_malloc(sizeof(USBH_ClassTypeDef) * 2);

		memcpy(&hclass[0], USBH_HID_CLASS, sizeof(USBH_ClassTypeDef));
		memcpy(&hclass[1], USBH_MSC_CLASS, sizeof(USBH_ClassTypeDef));

		husb->ClassNumber 					= 0;
		husb->pClass[husb->ClassNumber++] 	= &hclass[0];
		husb->pClass[husb->ClassNumber++] 	= &hclass[1];
	}

	husb->gState 						= HOST_ENUMERATION;
	husb->EnumState 					= ENUM_IDLE;
	husb->RequestState 					= CMD_SEND;

	husb->Control.errorcount 			= 0;
	husb->Control.state 				= CTRL_SETUP;
	husb->Control.pipe_size 			= (speed > 0) ? HUB_MPS_DEFAULT : HUB_MPS_LOWSPEED;
	husb->Control.pipe_out 				= phost->Control.pipe_out;
	husb->Control.pipe_in  				= phost->Control.pipe_in;

	husb->device.address 				= HUB_ADDRESS_DEFAULT;
	husb->device.speed   				= (speed > 1) ? USBH_SPEED_HIGH : ((speed > 0) ? USBH_SPEED_FULL : USBH_SPEED_LOW);
	husb->device.is_connected 			= 1;

	husb->pActiveClass 					= NULL;

	husb->Pipes 						= phost->Pipes;

	husb->Timer 						= 0;
	husb->id 							= phost->id;
	husb->address 						= port;
	husb->pUser 						= phost->pUser;

	husb->parent 						= phost;

	husb->valid 						= 1;

	USBH_Delay(200U);
}

static void HUB_DeviceDisconnect(USBH_HandleTypeDef * phost, uint8_t port)
{
	uint32_t i;
	HUB_HandleTypeDef * HUB_Handle = (HUB_HandleTypeDef *) phost->pActiveClass->pData;
	USBH_HandleTypeDef * husb = (USBH_HandleTypeDef *) &(HUB_Handle->husb[port - 1]);
	
	if(husb->valid)
	{
		if(husb->pUser != NULL)
		{
			husb->pUser(husb, HOST_USER_DISCONNECTION);
		}

	    if(husb->pActiveClass != NULL)
	    {
	    	husb->pActiveClass->DeInit(husb);
			husb->pActiveClass = NULL;
	    }

		if (husb->pClass[0] != NULL)
		{
			USBH_free(husb->pClass[0]);
			husb->pClass[0] = NULL;
		}

		for(i = 0; i < USBH_MAX_DATA_BUFFER; i++)
		{
			husb->device.Data[i] = 0;
		}
		
		memset(husb, 0, sizeof(USBH_HandleTypeDef));
	}
}

static uint8_t HUB_GetPort(uint8_t * byte)
{
	for(uint8_t port = 0; port < 255U; port++)
	{
		if ((*byte & (1U << port)) != 0U)
		{
			*byte &= ~(1U << port);
			return port;
		}
	}

	return 0;
}

USBH_HandleTypeDef * HUB_Process(USBH_HandleTypeDef * phost)
{
	static USBH_StatusTypeDef status = USBH_OK;
	static USBH_HandleTypeDef * pusb = NULL;
	static uint8_t usb_port = 0;

	if(pusb != NULL)
	{
		status = USBH_Process(pusb);
	}

	if(status == USBH_OK)
	{
		if(phost->gState == HOST_CLASS && \
		   USBH_GetActiveClass(phost) == USB_HUB_CLASS)
		{
			HUB_HandleTypeDef * HUB_Handle = (HUB_HandleTypeDef *) phost->pActiveClass->pData;

			if(usb_port < HUB_Handle->total_port_num)
			{
				USBH_HandleTypeDef * husb = (USBH_HandleTypeDef *) &(HUB_Handle->husb[usb_port]);

				if(husb->valid)
				{
					pusb = husb;
					USBH_LL_SetupEP0(pusb);
				}

				usb_port++;
			}
			else
			{
				pusb = phost;
				USBH_LL_SetupEP0(pusb);

				usb_port = 0;
			}
		}
		else
		{
			pusb = phost;
		}
	}

	return pusb;
}
