/********************************** (C) COPYRIGHT *******************************
 * File Name          : hidmouse.c
 * Author             : zhengdt
 * Version            : V3.0 (Tri-Mode, Complete)
 * Date               : 2025/07/09
 * Description        : 三模鼠标应用程序，包含完整的传感器驱动和模式切换逻辑
 *******************************************************************************/

#include "CONFIG.h"
#include "battservice.h"
#include "devinfoservice.h"
#include "hiddev.h"
#include "hidmouse.h"
#include "hidmouseservice.h"
#include "PAW3395.h"
#include "CH58x_common.h"
#include "usb_handler.h"
#include "usb_descriptors.h"
#include "CH58x_spi.h"
#include "CH58x_rf.h"

/* 鼠标报告结构体定义 */
__packed struct mouseHID_t
{
	uint8_t buttons;    // Bit0:左键, Bit1:右键, Bit2:中键
	int16_t x;          // x轴相对位移
	int16_t y;          // y轴相对位移
	int8_t  wheel;      // 滚轮位移
};

/* 工作模式定义 */
typedef enum {
    MODE_BLUETOOTH,
    MODE_2_4G_RF,
    MODE_WIRED_USB,
    MODE_COUNT // 用于模式切换计数
} work_mode_t;

/* 任务事件定义 */
#define MOUSE_POLL_EVT              0x0002

/* 2.4G RF 通信参数 */
#define RF_ACCESS_ADDRESS    0x17263544
#define RF_CHANNEL           10
#define RF_TX_POWER          LL_TX_POWEER_0_DBM

/* BLE 相关常量 */
#define DEFAULT_DESIRED_MIN_CONN_INTERVAL    8
#define DEFAULT_DESIRED_MAX_CONN_INTERVAL    8
#define DEFAULT_HID_IDLE_TIMEOUT             0

/* 全局变量 */
static uint8_t hidEmuTaskId = INVALID_TASK_ID;
static work_mode_t current_mode = MODE_WIRED_USB;
struct mouseHID_t myMouseReport;
uint8_t motion_burst_data[12];
extern uint8_t Ep1Buffer[];
static uint8_t switch_btn_last_state = 1;

/* BLE 相关变量 */
static uint8_t scanRspData[] = {
    0x0A, GAP_ADTYPE_LOCAL_NAME_COMPLETE, 'H', 'I', 'D', ' ', 'M', 'o', 'u', 's', 'e',
    0x05, GAP_ADTYPE_SLAVE_CONN_INTERVAL_RANGE, LO_UINT16(DEFAULT_DESIRED_MIN_CONN_INTERVAL), HI_UINT16(DEFAULT_DESIRED_MIN_CONN_INTERVAL), LO_UINT16(DEFAULT_DESIRED_MAX_CONN_INTERVAL), HI_UINT16(DEFAULT_DESIRED_MAX_CONN_INTERVAL),
    0x05, GAP_ADTYPE_16BIT_MORE, LO_UINT16(HID_SERV_UUID), HI_UINT16(HID_SERV_UUID), LO_UINT16(BATT_SERV_UUID), HI_UINT16(BATT_SERV_UUID),
    0x02, GAP_ADTYPE_POWER_LEVEL, 0
};
static uint8_t advertData[] = {
    0x02, GAP_ADTYPE_FLAGS, GAP_ADTYPE_FLAGS_LIMITED | GAP_ADTYPE_FLAGS_BREDR_NOT_SUPPORTED,
    0x03, GAP_ADTYPE_APPEARANCE, LO_UINT16(GAP_APPEARE_HID_MOUSE), HI_UINT16(GAP_APPEARE_HID_MOUSE)
};
static CONST uint8_t attDeviceName[GAP_DEVICE_NAME_LEN] = "HID Mouse";
static hidDevCfg_t hidEmuCfg = {
    DEFAULT_HID_IDLE_TIMEOUT,
    HID_FEATURE_FLAGS
};

/* 本地函数声明 */
static void hidEmu_ProcessTMOSMsg(tmos_event_hdr_t *pMsg);
static void hidEmuSendMouseReport(struct mouseHID_t* report);
static void PAW3395_PowerUp(void);
void PAW3395_Write_Register(uint8_t adress, uint8_t value);
uint8_t PAW3395_Read_Register(uint8_t adress);
static void Power_Up_Initializaton_Register_Setting(void);
void PAW3395_Motion_Burst(uint8_t *buffer);
static void hidEmuStateCB(gapRole_States_t newState, gapRoleEvent_t *pEvent);
static uint8_t hidEmuRptCB(uint8_t id, uint8_t type, uint16_t uuid, uint8_t oper, uint16_t *pLen, uint8_t *pData);
static void hidEmuEvtCB(uint8_t evt);

//================================================================================
// 传感器驱动部分 (完整版)
//================================================================================

void PAW3395_Write_Register(uint8_t adress, uint8_t value)
{
    GPIOA_ResetBits(GPIO_Pin_12);
    SPI0_SendRecvByte(adress | 0x80);
    SPI0_SendRecvByte(value);
    GPIOA_SetBits(GPIO_Pin_12);
    DelayUs(20);
}

uint8_t PAW3395_Read_Register(uint8_t adress)
{
    uint8_t temp;
    GPIOA_ResetBits(GPIO_Pin_12);
    SPI0_SendRecvByte(adress & 0x7F);
    DelayUs(100);
    temp = SPI0_SendRecvByte(0xFF);
    GPIOA_SetBits(GPIO_Pin_12);
    DelayUs(1);
    return temp;
}

static void Power_Up_Initializaton_Register_Setting(void)
{
	PAW3395_Write_Register(0x7F ,0x07);
	PAW3395_Write_Register(0x40 ,0x41);
	PAW3395_Write_Register(0x7F ,0x00);
	PAW3395_Write_Register(0x40 ,0x80);
	PAW3395_Write_Register(0x7F ,0x0E);
	PAW3395_Write_Register(0x55 ,0x0D);
	PAW3395_Write_Register(0x56 ,0x1B);
	PAW3395_Write_Register(0x57 ,0xE8);
	PAW3395_Write_Register(0x58 ,0xD5);
	PAW3395_Write_Register(0x7F ,0x14);
	PAW3395_Write_Register(0x42 ,0xBC);
	PAW3395_Write_Register(0x43 ,0x74);
	PAW3395_Write_Register(0x4B ,0x20);
	PAW3395_Write_Register(0x4D ,0x00);
	PAW3395_Write_Register(0x53 ,0x0E);
	PAW3395_Write_Register(0x7F ,0x05);
	PAW3395_Write_Register(0x44 ,0x04);
	PAW3395_Write_Register(0x4D ,0x06);
	PAW3395_Write_Register(0x51 ,0x40);
	PAW3395_Write_Register(0x53 ,0x40);
	PAW3395_Write_Register(0x55 ,0xCA);
	PAW3395_Write_Register(0x5A ,0xE8);
	PAW3395_Write_Register(0x5B ,0xEA);
	PAW3395_Write_Register(0x61 ,0x31);
	PAW3395_Write_Register(0x62 ,0x64);
	PAW3395_Write_Register(0x6D ,0xB8);
	PAW3395_Write_Register(0x6E ,0x0F);
	PAW3395_Write_Register(0x70 ,0x02);
	PAW3395_Write_Register(0x4A ,0x2A);
	PAW3395_Write_Register(0x60 ,0x26);
	PAW3395_Write_Register(0x7F ,0x06);
	PAW3395_Write_Register(0x6D ,0x70);
	PAW3395_Write_Register(0x6E ,0x60);
	PAW3395_Write_Register(0x6F ,0x04);
	PAW3395_Write_Register(0x53 ,0x02);
	PAW3395_Write_Register(0x55 ,0x11);
	PAW3395_Write_Register(0x7A ,0x01);
	PAW3395_Write_Register(0x7D ,0x51);
	PAW3395_Write_Register(0x7F ,0x07);
	PAW3395_Write_Register(0x41 ,0x10);
	PAW3395_Write_Register(0x42 ,0x32);
	PAW3395_Write_Register(0x43 ,0x00);
	PAW3395_Write_Register(0x7F ,0x08);
	PAW3395_Write_Register(0x71 ,0x4F);
	PAW3395_Write_Register(0x7F ,0x09);
	PAW3395_Write_Register(0x62 ,0x1F);
	PAW3395_Write_Register(0x63 ,0x1F);
	PAW3395_Write_Register(0x65 ,0x03);
	PAW3395_Write_Register(0x66 ,0x03);
	PAW3395_Write_Register(0x67 ,0x1F);
	PAW3395_Write_Register(0x68 ,0x1F);
	PAW3395_Write_Register(0x69 ,0x03);
	PAW3395_Write_Register(0x6A ,0x03);
	PAW3395_Write_Register(0x6C ,0x1F);
	PAW3395_Write_Register(0x6D ,0x1F);
	PAW3395_Write_Register(0x51 ,0x04);
	PAW3395_Write_Register(0x53 ,0x20);
	PAW3395_Write_Register(0x54 ,0x20);
	PAW3395_Write_Register(0x71 ,0x0C);
	PAW3395_Write_Register(0x72 ,0x07);
	PAW3395_Write_Register(0x73 ,0x07);
	PAW3395_Write_Register(0x7F ,0x0A);
	PAW3395_Write_Register(0x4A ,0x14);
	PAW3395_Write_Register(0x4C ,0x14);
	PAW3395_Write_Register(0x55 ,0x19);
	PAW3395_Write_Register(0x7F ,0x14);
	PAW3395_Write_Register(0x4B ,0x30);
	PAW3395_Write_Register(0x4C ,0x03);
	PAW3395_Write_Register(0x61 ,0x0B);
	PAW3395_Write_Register(0x62 ,0x0A);
	PAW3395_Write_Register(0x63 ,0x02);
	PAW3395_Write_Register(0x7F ,0x15);
	PAW3395_Write_Register(0x4C ,0x02);
	PAW3395_Write_Register(0x56 ,0x02);
	PAW3395_Write_Register(0x41 ,0x91);
	PAW3395_Write_Register(0x4D ,0x0A);
	PAW3395_Write_Register(0x7F ,0x0C);
	PAW3395_Write_Register(0x4A ,0x10);
	PAW3395_Write_Register(0x4B ,0x0C);
	PAW3395_Write_Register(0x4C ,0x40);
	PAW3395_Write_Register(0x41 ,0x25);
	PAW3395_Write_Register(0x55 ,0x18);
	PAW3395_Write_Register(0x56 ,0x14);
	PAW3395_Write_Register(0x49 ,0x0A);
	PAW3395_Write_Register(0x42 ,0x00);
	PAW3395_Write_Register(0x43 ,0x2D);
	PAW3395_Write_Register(0x44 ,0x0C);
	PAW3395_Write_Register(0x54 ,0x1A);
	PAW3395_Write_Register(0x5A ,0x0D);
	PAW3395_Write_Register(0x5F ,0x1E);
	PAW3395_Write_Register(0x5B ,0x05);
	PAW3395_Write_Register(0x5E ,0x0F);
	PAW3395_Write_Register(0x7F ,0x0D);
	PAW3395_Write_Register(0x48 ,0xDD);
	PAW3395_Write_Register(0x4F ,0x03);
	PAW3395_Write_Register(0x52 ,0x49);
	PAW3395_Write_Register(0x51 ,0x00);
	PAW3395_Write_Register(0x54 ,0x5B);
	PAW3395_Write_Register(0x53 ,0x00);
	PAW3395_Write_Register(0x56 ,0x64);
	PAW3395_Write_Register(0x55 ,0x00);
	PAW3395_Write_Register(0x58 ,0xA5);
	PAW3395_Write_Register(0x57 ,0x02);
	PAW3395_Write_Register(0x5A ,0x29);
	PAW3395_Write_Register(0x5B ,0x47);
	PAW3395_Write_Register(0x5C ,0x81);
	PAW3395_Write_Register(0x5D ,0x40);
	PAW3395_Write_Register(0x71 ,0xDC);
	PAW3395_Write_Register(0x70 ,0x07);
	PAW3395_Write_Register(0x73 ,0x00);
	PAW3395_Write_Register(0x72 ,0x08);
	PAW3395_Write_Register(0x75 ,0xDC);
	PAW3395_Write_Register(0x74 ,0x07);
	PAW3395_Write_Register(0x77 ,0x00);
	PAW3395_Write_Register(0x76 ,0x08);
	PAW3395_Write_Register(0x7F ,0x10);
	PAW3395_Write_Register(0x4C ,0xD0);
	PAW3395_Write_Register(0x7F ,0x00);
	PAW3395_Write_Register(0x4F ,0x63);
	PAW3395_Write_Register(0x4E ,0x00);
	PAW3395_Write_Register(0x52 ,0x63);
	PAW3395_Write_Register(0x51 ,0x00);
	PAW3395_Write_Register(0x54 ,0x54);
	PAW3395_Write_Register(0x5A ,0x10);
	PAW3395_Write_Register(0x77 ,0x4F);
	PAW3395_Write_Register(0x47 ,0x01);
	PAW3395_Write_Register(0x5B ,0x40);
	PAW3395_Write_Register(0x64 ,0x60);
	PAW3395_Write_Register(0x65 ,0x06);
	PAW3395_Write_Register(0x66 ,0x13);
	PAW3395_Write_Register(0x67 ,0x0F);
	PAW3395_Write_Register(0x78 ,0x01);
	PAW3395_Write_Register(0x79 ,0x9C);
	PAW3395_Write_Register(0x40 ,0x00);
	PAW3395_Write_Register(0x55 ,0x02);
	PAW3395_Write_Register(0x23 ,0x70);
	PAW3395_Write_Register(0x22 ,0x01);

	DelayMs(1);

	for( int i = 0 ;i < 60 ;i++)
	{
		if(PAW3395_Read_Register(0x6C) == 0x80 ) break;
		DelayMs(1);
	}

	PAW3395_Write_Register(0x22 ,0x00);
	PAW3395_Write_Register(0x55 ,0x00);
	PAW3395_Write_Register(0x7F ,0x07);
	PAW3395_Write_Register(0x40 ,0x40);
	PAW3395_Write_Register(0x7F ,0x00);
}

void PAW3395_PowerUp(void)
{
    uint8_t reg_it;
    DelayMs(50);
    GPIOA_SetBits(GPIO_Pin_12);
    DelayUs(1);
    GPIOA_ResetBits(GPIO_Pin_12);
    DelayUs(1);
    PAW3395_Write_Register(PAW3395_SPIREGISTER_POWERUPRESET, PAW3395_POWERUPRESET_POWERON);
    DelayMs(5);
    Power_Up_Initializaton_Register_Setting();
    for(reg_it = 0x02; reg_it <= 0x06; reg_it++)
    {
        PAW3395_Read_Register(reg_it);
    }
}

void PAW3395_Motion_Burst(uint8_t *buffer)
{
    GPIOA_ResetBits(GPIO_Pin_12);
    SPI0_SendRecvByte(PAW3395_SPIREGISTER_MotionBurst);
    DelayUs(35);
    for(uint8_t i = 0; i < 12; i++)
    {
        buffer[i] = SPI0_SendRecvByte(0xFF);
    }
    GPIOA_SetBits(GPIO_Pin_12);
}

//================================================================================
// RF 和 模式切换函数
//================================================================================
void RF_2G4StatusCallBack(uint8_t sta, uint8_t crc, uint8_t *rxBuf) { }

void switchTo24GMode()
{
    rfConfig_t rfConfig;
    RF_RoleInit();
    rfConfig.accessAddress = RF_ACCESS_ADDRESS;
    rfConfig.CRCInit = 0x555555;
    rfConfig.Channel = RF_CHANNEL;
    rfConfig.LLEMode = LLE_MODE_AUTO;
    rfConfig.rfStatusCB = RF_2G4StatusCallBack;
    RF_Config(&rfConfig);
    RF_Shut();
    GAPRole_SetParameter(GAPROLE_ADVERT_ENABLED, sizeof(uint8_t), &(uint8_t){FALSE});
}

void apply_current_mode() {
    switch(current_mode) {
        case MODE_WIRED_USB:
            PRINT("Switched to Wired Mode.\n");
            RF_Shut();
            GAPRole_SetParameter(GAPROLE_ADVERT_ENABLED, sizeof(uint8_t), &(uint8_t){FALSE});
            break;
        case MODE_BLUETOOTH:
            PRINT("Switched to Bluetooth Mode.\n");
            RF_Shut();
            GAPRole_SetParameter(GAPROLE_ADVERT_ENABLED, sizeof(uint8_t), &(uint8_t){TRUE});
            break;
        case MODE_2_4G_RF:
            PRINT("Switched to 2.4G Mode.\n");
            switchTo24GMode();
            break;
        default: break;
    }
}

void check_mode_switch() {
    uint8_t current_state = GPIOB_ReadPortPin(GPIO_Pin_23);
    if (switch_btn_last_state == 1 && current_state == 0) {
        DelayMs(10);
        if (GPIOB_ReadPortPin(GPIO_Pin_23) == 0) {
            current_mode = (work_mode_t)(((int)current_mode + 1) % MODE_COUNT);
            apply_current_mode();
        }
    }
    switch_btn_last_state = current_state;
}


//================================================================================
// 主应用任务部分
//================================================================================
void HidEmu_Init()
{
    hidEmuTaskId = TMOS_ProcessEventRegister(HidEmu_ProcessEvent);
    
    GPIOB_ModeCfg(GPIO_Pin_1 | GPIO_Pin_2 | GPIO_Pin_3, GPIO_ModeIN_PU);
    GPIOA_ModeCfg(GPIO_Pin_7 | GPIO_Pin_8, GPIO_ModeIN_PU);
    GPIOB_ModeCfg(GPIO_Pin_23, GPIO_ModeIN_PU);
    
    GPIOA_SetBits(GPIO_Pin_12);
    GPIOA_ModeCfg(GPIO_Pin_12, GPIO_ModeOut_PP_5mA);
    SPI0_MasterInit(SPI_Mode_0, 24000000, FALSE, NULL);
    SPI0_MasterMode(ENABLE);

    PAW3395_PowerUp();

    GGS_SetParameter(GGS_DEVICE_NAME_ATT, GAP_DEVICE_NAME_LEN, (void *)attDeviceName);
    GAPRole_SetParameter(GAPROLE_SCAN_RSP_DATA, sizeof(scanRspData), scanRspData);
    GAPRole_SetParameter(GAPROLE_ADVERT_DATA, sizeof(advertData), advertData);
    Hid_AddService();
    HidDev_Register(&hidEmuCfg, &hidEmuHidCBs);
    Batt_AddService();

    if (R8_USB_CTRL & RB_UC_DEV_PU_EN) {
        current_mode = MODE_WIRED_USB;
    } else {
        current_mode = MODE_2_4G_RF;
    }
    PRINT("Default mode: %d\n", current_mode);
    apply_current_mode();

    tmos_start_task(hidEmuTaskId, MOUSE_POLL_EVT, 10);
}

uint16_t HidEmu_ProcessEvent(uint8_t task_id, uint16_t events)
{
    if(events & SYS_EVENT_MSG)
    {
        uint8_t *pMsg;
        if((pMsg = tmos_msg_receive(hidEmuTaskId)) != NULL) {
            hidEmu_ProcessTMOSMsg((tmos_event_hdr_t *)pMsg);
            tmos_msg_deallocate(pMsg);
        }
        return (events ^ SYS_EVENT_MSG);
    }
    
    if(events & MOUSE_POLL_EVT)
    {
        check_mode_switch();
        
        PAW3395_Motion_Burst(motion_burst_data);
        
        myMouseReport.buttons = 0;
        if (GPIOB_ReadPortPin(GPIO_Pin_1) == 0) myMouseReport.buttons |= (1 << 0);
        if (GPIOB_ReadPortPin(GPIO_Pin_2) == 0) myMouseReport.buttons |= (1 << 2);
        if (GPIOB_ReadPortPin(GPIO_Pin_3) == 0) myMouseReport.buttons |= (1 << 1);
        
        myMouseReport.x = (int16_t)((motion_burst_data[3] << 8) | motion_burst_data[2]);
        myMouseReport.y = (int16_t)((motion_burst_data[5] << 8) | motion_burst_data[4]);
        
        // 此处应添加滚轮处理逻辑，更新 myMouseReport.wheel
        myMouseReport.wheel = 0;
        
        hidEmuSendMouseReport(&myMouseReport);

        Batt_MeasLevel();
        tmos_start_task(hidEmuTaskId, MOUSE_POLL_EVT, 8);
        
        return (events ^ MOUSE_POLL_EVT);
    }
    
    return 0;
}

static void hidEmu_ProcessTMOSMsg(tmos_event_hdr_t *pMsg) { }

static void hidEmuSendMouseReport(struct mouseHID_t* report)
{
    switch(current_mode)
    {
        case MODE_WIRED_USB:
            if(DevConfig) {
                memcpy(Ep1Buffer, (uint8_t*)report, sizeof(struct mouseHID_t));
                R8_UEP1_T_LEN = sizeof(struct mouseHID_t);
                R8_UEP1_CTRL = (R8_UEP1_CTRL & ~MASK_UEP_T_RES) | UEP_T_RES_ACK;
            }
            break;
        case MODE_BLUETOOTH:
            HidDev_Report(HID_RPT_ID_MOUSE_IN, HID_REPORT_TYPE_INPUT, sizeof(struct mouseHID_t), (uint8_t*)report);
            break;
        case MODE_2_4G_RF:
            RF_Tx((uint8_t*)report, sizeof(struct mouseHID_t), 0, 0);
            break;
    }
}

static void hidEmuStateCB(gapRole_States_t newState, gapRoleEvent_t *pEvent)
{
    if (current_mode != MODE_BLUETOOTH) return;
    switch(newState & GAPROLE_STATE_ADV_MASK)
    {
        case GAPROLE_STARTED:
            PRINT("BLE Initialized..\n");
            break;
        case GAPROLE_ADVERTISING:
            if(pEvent->gap.opcode == GAP_MAKE_DISCOVERABLE_DONE_EVENT) {
                PRINT("BLE Advertising..\n");
            }
            break;
        case GAPROLE_CONNECTED:
            if(pEvent->gap.opcode == GAP_LINK_ESTABLISHED_EVENT) {
                PRINT("BLE Connected..\n");
            }
            break;
        case GAPROLE_WAITING:
             if(pEvent->gap.opcode == GAP_LINK_TERMINATED_EVENT) {
                PRINT("BLE Disconnected.. Reason:%x\n", pEvent->linkTerminate.reason);
                uint8_t advertising_enable = TRUE;
                GAPRole_SetParameter(GAPROLE_ADVERT_ENABLED, sizeof(uint8_t), &advertising_enable);
            }
            break;
        case GAPROLE_ERROR:
            PRINT("BLE Error %x ..\n", pEvent->gap.opcode);
            break;
        default:
            break;
    }
}

static uint8_t hidEmuRptCB(uint8_t id, uint8_t type, uint16_t uuid, uint8_t oper, uint16_t *pLen, uint8_t *pData)
{
    return SUCCESS;
}

static void hidEmuEvtCB(uint8_t evt) { }