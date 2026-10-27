/********************************** (C) COPYRIGHT *******************************
 * File Name          : main.c
 * Author             : WCH & Gemini
 * Version            : V2.1 (Wired Mode)
 * Date               : 2025/07/09
 * Description        : 三模鼠标应用主函数，增加有线模式初始化
 *********************************************************************************
 * Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
 * Attention: This software (modified or not) and binary are used for 
 * microcontroller manufactured by Nanjing Qinheng Microelectronics.
 *******************************************************************************/

#include "CONFIG.h"
#include "HAL.h"
#include "hiddev.h"
#include "hidmouse.h"
#include "usb_handler.h" // <-- 新增: 包含USB处理头文件

__attribute__((aligned(4))) uint32_t MEM_BUF[BLE_MEMHEAP_SIZE / 4];

#if(defined(BLE_MAC)) && (BLE_MAC == TRUE)
const uint8_t MacAddr[6] = {0x84, 0xC2, 0xE4, 0x03, 0x02, 0x02};
#endif

__HIGH_CODE
__attribute__((noinline))
void Main_Circulation()
{
    while(1)
    {
        TMOS_SystemProcess();
    }
}

int main(void)
{
#if(defined(DCDC_ENABLE)) && (DCDC_ENABLE == TRUE)
    PWR_DCDCCfg(ENABLE);
#endif
    HSECFG_Capacitance(HSECap_18p);
    SetSysClock(CLK_SOURCE_HSE_PLL_62_4MHz);
#if(defined(HAL_SLEEP)) && (HAL_SLEEP == TRUE)
    GPIOA_ModeCfg(GPIO_Pin_All, GPIO_ModeIN_PU);
    GPIOB_ModeCfg(GPIO_Pin_All, GPIO_ModeIN_PU);
#endif
#ifdef DEBUG
     GPIOA_SetBits(GPIO_Pin_14);
     GPIOPinRemap(ENABLE, RB_PIN_UART0);
     GPIOA_ModeCfg(GPIO_Pin_15, GPIO_ModeIN_PU);
     GPIOA_ModeCfg(GPIO_Pin_14, GPIO_ModeOut_PP_5mA);
     UART0_DefInit();
#endif
    PRINT("%s\n", VER_LIB);

    // <-- 新增: 初始化USB设备功能 -->
    USB_Device_Init();
    PFIC_EnableIRQ(USB_IRQn);
    // <-- 结束新增部分 -->

    CH58x_BLEInit();
    HAL_Init();
    HidDev_Init();
    HidEmu_Init();
    Main_Circulation();
}

// <-- 新增: USB中断服务函数 -->
__attribute__((interrupt("WCH-Interrupt-fast")))
__attribute__((section(".highcode")))
void USB_IRQHandler(void)
{
    if(R8_USB_INT_FG & RB_UIF_TRANSFER)
    {
        USB_DevTransProcess();
    }
    else if(R8_USB_INT_FG & RB_UIF_BUS_RST)
    {
        R8_USB_DEV_AD = 0;
        R8_UEP0_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;
        R8_UEP1_CTRL = UEP_T_RES_NAK;
    }
    else if(R8_USB_INT_FG & RB_UIF_SUSPEND)
    {
        R8_USB_CTRL |= RB_UC_LOW_SPEED; // 进入睡眠
    }
    R8_USB_INT_FG = 0xFF; // 清除所有中断标志
}