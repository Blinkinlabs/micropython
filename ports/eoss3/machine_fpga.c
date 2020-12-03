/*==========================================================
 * Copyright 2020 QuickLogic Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *==========================================================*/

/*==========================================================
*                                                          
*    File   : fpga_loader.c
*    Purpose: Contains functionality to load FPGA
*                                                          
*=========================================================*/

#include "Fw_global_config.h"

#include "stdio.h"
#include "eoss3_dev.h"
#include "s3x_clock_hal.h"
#include "machine_fpga.h"

// PIF controller is the FPGA programming interface
// CFG_CTL_TOP (PIF Controller): 0x40014000
// So CFG_CTL has at least:
// 0x000: CFG_CTL
// 0xFFC: CFG_DATA

// CFG_CTL_TOP (PIF Controller) base: 0x40014000
#define CFG_CTL_CFG_DATA                (*(volatile uint32_t *)(0x40014FFC))

// CFG_CTL_TOP (PIF Controller) base: 0x40014000
#define CFG_CTL_CFG_CTL                 (*(volatile uint32_t *)(0x40014000))

/*************************************************************
 *
 *  Load FPGA from in memory description
 *
 *************************************************************/

/*To enable clock. Pass the clock ID as defined in the enum S3x_CLK_ID*/
int S3x_Clk_Enable(uint32_t clk_id) {
    // Clock gating on the EOS-S3 is somewhat convoluted, so just hardcode sequences for the clocks
    // that we care about.
    switch(clk_id) {
    case S3X_FB_16_CLK:  // FPGA Sys_Clk0
        CRU->CLK_DIVIDER_CLK_GATING |= (1<<0);
        //CRU->CLK_CTRL_F_0;                   // Clock divider setting
        //CRU->CLK_CTRL_F_1 = 0;               // Select high-speed clock
        CRU->C16_CLK_GATE |= (1<<0);           // Enable clock gate
        break;
    case S3X_FB_21_CLK:  // FPGA Sys_Clk1
        CRU->CLK_DIVIDER_CLK_GATING |= (1<<8);
        //CRU->CLK_CTRL_I_0;                   // Clock divider setting
        //CRU->CLK_CTRL_I_1 = 0;               // Select high-speed clock
        CRU->C21_CLK_GATE |= (1<<0);           // Enable clock gate
        break;
    case S3X_FB_02_CLK:  // FPGA Sys_Pclk
        CRU->CLK_DIVIDER_CLK_GATING |= (1<<1);
        //CRU->CLK_CTRL_B_0                    // Clock divider setting
        //CRU->CLK_SWITCH_FOR_B = 0;           // Select high-speed clock
        CRU->C02_CLK_GATE |= (1<<1);	       // Enable clock gate
        break;
    case S3X_A0_08_CLK:
        CRU->CLK_DIVIDER_CLK_GATING |= (1<<2);
        // Clock divider setting
        //CRU->CLK_SWITCH_FOR_C = 0;           // Select high-speed clock
        CRU->C08_X1_CLK_GATE |= (1<<3);        // Enable clock gate
        break;
    case S3X_CLKGATE_FB:
        CRU->CLK_DIVIDER_CLK_GATING |= (1<<0);
        // Clock divider setting
        // Select high-speed clock
        CRU->C09_CLK_GATE |= (1<<2);	       // Enable clock gate
        break;
    case S3X_CLKGATE_PIF:
        CRU->CLK_DIVIDER_CLK_GATING |= (1<<0);
        // Clock divider setting
        // Select high-speed clock
        CRU->C09_CLK_GATE |= (1<<1);	       // Enable clock gate
        break;
    default:
        return 0;
    }

    return 1;
}

/*To disable clock. Pass the clock ID as defined in the enum S3x_CLK_ID*/
int S3x_Clk_Disable(uint32_t clk_id) {
    // Clock gating on the EOS-S3 is somewhat convoluted, so just hardcode sequences for the clocks
    // that we care about.
    switch(clk_id) {
    case S3X_FB_16_CLK:
        CRU->C16_CLK_GATE &= ~(1<<0);
        break;
    case S3X_FB_21_CLK:
        CRU->C21_CLK_GATE &= ~(1<<0);
        break;
    case S3X_FB_02_CLK:
        CRU->C02_CLK_GATE &= ~(1<<1);	       // Enable clock gate
        break;
    default:
        return 0;
    }

    return 1;
}

/*To set clock rate. Pass the cloack ID and the desired rate*/
int S3x_Clk_Set_Rate(uint32_t clk_id, uint32_t rate) {
    // TODO
    return 0;
}

static void low_rent_delay(uint32_t counts) {
    for (uint32_t i=0;i<counts; i++) {
        PMU->GEN_PURPOSE_1  = i << 4;
    }
}

// Start the FPGA load process
void fpga_load_begin() {
    // From the main function of the example
    // It doesn't seem like the clocks actually /have/ to be disabled, but might cause side effects.
    S3x_Clk_Disable(S3X_FB_21_CLK);     // FPGA general purpose clocks                        
    S3x_Clk_Disable(S3X_FB_16_CLK);     
    S3x_Clk_Disable(S3X_FB_02_CLK);
    // TODO: Why not Sys_Pclk?

    // Abbreviated and translated from the STK load_fpga() function
    // This doesn't appear to be documented, but does appear to work
    
    S3x_Clk_Enable(S3X_A0_08_CLK);
    S3x_Clk_Enable(S3X_CLKGATE_FB);
    S3x_Clk_Enable(S3X_CLKGATE_PIF);
    
    // Configuration of CFG_CTRL for writes
    CFG_CTL_CFG_CTL = 0x0000bdff ;
    
    // wait some time for fpga to get reset pulse
    low_rent_delay(58);
}

// Program data to the FPGA. Note that the iamge size is fixed, but it is the responsibility
// of the caller to keep track of sending the correct amount. This function can be called
// repeatably until all data is loaded.
//
// @param img_size Size of the chunk to program. Must be a multiple of 4
// @param image_ptr Pointer to the chunk data
void fpga_load_add_data(uint32_t img_size, uint32_t* image_ptr) {
    volatile uint32_t   *gFPGAPtr = (volatile uint32_t*)image_ptr;
    for(uint32_t chunk_cnt=0;chunk_cnt<(img_size/4);chunk_cnt++)
    	CFG_CTL_CFG_DATA = gFPGAPtr[chunk_cnt];
}

// Finish the FPGA routine. Note that the correct amount of data needs to be programmed
// using fpga_load_add_data() before this is called.
void fpga_load_end() {
    // wait some time for fpga to get reset pulse
    low_rent_delay(50);
    
    CFG_CTL_CFG_CTL = 0; // exit config mode
    
    PMU->GEN_PURPOSE_0 = 0; //set APB_FB_EN = 0 for normal mode
    
    PMU->FB_ISOLATION = 0;
    
    CRU->FB_SW_RESET = 0;
    CRU->FB_MISC_SW_RST_CTL = 0;
    
    // required wait time before releasing LTH_ENB
    low_rent_delay(500);
    
    //release isolation - LTH_ENB
    PMU->FB_ISOLATION = 0;

    // TODO: Expose clock speed config
    S3x_Clk_Set_Rate(S3X_FB_21_CLK, 12000*1000);
    S3x_Clk_Set_Rate(S3X_FB_16_CLK, 12000*1000);
    S3x_Clk_Enable(S3X_FB_02_CLK);
    S3x_Clk_Enable(S3X_FB_21_CLK);
    S3x_Clk_Enable(S3X_FB_16_CLK);
}

void load_fpga_sane(uint32_t img_size, uint32_t* image_ptr) {
    fpga_load_begin();

    fpga_load_add_data(img_size, image_ptr);

    fpga_load_end();
}
