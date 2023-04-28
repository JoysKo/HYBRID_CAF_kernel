/*
 * dbmdx-va-regmap.h  --  DBMDX VA register mapping
 *
 * Copyright (C) 2014 DSP Group
 * Copyright (C) 2020 XiaoMi, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _DBMDX_VA_REGMAP_H
#define _DBMDX_VA_REGMAP_H

/* DBMDX commands and values */
#define DBMDX_VA_SYNC_POLLING				0x80000000
#define DBMDX_VA_CMD_MASK				0x80000000

#define DBMDX_VA_SET_POWER_STATE_SLEEP			0x80170001

#define DBMDX_VA_GET_FW_VER				0x80000000
#define DBMDX_VA_OPR_MODE				0x80010000
#define DBMDX_VA_PRIMARY_AMODEL_SIZE			0x80020000
#define DBMDX_VA_SECONDARY_AMODEL_SIZE			0x80030000
#define DBMDX_VA_DIGITAL_GAIN				0x80040000
#define DBMDX_VA_IO_PORT_ADDR_LO			0x80050000
#define DBMDX_VA_IO_PORT_ADDR_HI			0x80060000
#define DBMDX_VA_IO_PORT_VALUE_LO			0x80070000
#define DBMDX_VA_IO_PORT_VALUE_HI			0x80080000
#define DBMDX_VA_AUDIO_BUFFER_SIZE			0x80090000
#define DBMDX_VA_NUM_OF_SMP_IN_BUF			0x800A0000
#define DBMDX_VA_LAST_MAX_SMP_VALUE			0x800B0000
#define DBMDX_VA_UART_SPEED				0x800C0000
#define DBMDX_VA_LOAD_NEW_ACUSTIC_MODEL			0x800F0000
#define DBMDX_VA_CLK_CFG				0x80100000
#define DBMDX_VA_AUDIO_PROC_ROUTING			0x80110000
#define DBMDX_VA_AUDIO_BUFFER_CONVERSION		0x80120000
#define DBMDX_VA_AUDIO_HISTORY				0x80120000
#define DBMDX_VA_UART_XOFF				0x80130000
#define DBMDX_VA_OKG_INTERFACE				0x80140000
#define DBMDX_VA_ANALOG_MIC_GAIN			0x80160000
#define DBMDX_VA_DEBUG_1				0x80180000
#define DBMDX_VA_SWITCH_TO_BOOT				0x80180015
#define DBMDX_VA_FW_ID					0x80190000
#define DBMDX_VA_HPF_ENABLE				0x801A0000
#define DBMDX_VA_MASTER_CLK_FREQ			0x801B0000
#define DBMDX_VA_TDM0_SCLK_FREQ				0x801D0000
#define DBMDX_VA_DSP_CLOCK_CONFIG_EXT			0x801E0000
#define DBMDX_VA_AUDIO_ROUTING_CONFIG			0x801F0000
#define DBMDX_VA_READ_AUDIO_BUFFER			0x80200000
#define DBMDX_VA_POST_DETECTION_CLK_CFG			0x80210000

#define DBMDX_VA_GENERAL_CONFIGURATION_1		0x80220000
#define DBMDX_VA_GENERAL_CONFIGURATION_2		0x80230000
#define DBMDX_VA_MICROPHONE1_CONFIGURATION		0x80240000
#define DBMDX_VA_MICROPHONE2_CONFIGURATION		0x80250000
#ifndef DBMDX_FW_BELOW_280
#define DBMDX_VA_MICROPHONE3_CONFIGURATION		0x80260000
#define DBMDX_VA_MICROPHONE4_CONFIGURATION		0x80270000
#endif
#define DBMDX_VA_HOST_INTERFACE_SUPPORT			0x80290000

#ifndef DBMDX_FW_BELOW_280
#define DBMDX_VA_SET_PARAM_ADDR				0x803D0000
#define DBMDX_VA_GET_PARAM				0x803F0000
#define DBMDX_VA_SET_PARAM				0x803E0000
#else
#define DBMDX_VA_SET_PARAM_ADDR				0x801C0000
#define DBMDX_VA_GET_PARAM				0x80270000
#define DBMDX_VA_SET_PARAM				0x80260000
#endif
#define DBMDX_VA_FEATURE_SUPPORT			0x80350000
#define DBMDX_VA_TDM_ACTIVATION_CTL			0x80310000
#define DBMDX_VA_ECHO_CANCELLER_CONFIG			0x80340000
#define DBMDX_VA_TDM_RX_CONFIG				0x80360000
#define DBMDX_VA_TDM_TX_CONFIG				0x80370000

#define DBMDX_VA_SENS_RECOGNITION_MODE			0x80400000
#define DBMDX_VA_SENS_INITIALIZED			0x80410000
#define DBMDX_VA_SENS_TG_THRESHOLD			0x80470000
#define DBMDX_VA_SENS_VERIF_THRESHOLD			0x80480000
#define DBMDX_VA_SENS_WORDID				0x805B0000
#define DBMDX_VA_SENS_ALTWORDID				0x805C0000
#define DBMDX_VA_SENS_FINAL_SCORE			0x805D0000
#define DBMDX_VA_SENS_SV_SCORE				0x805E0000



#define DBMDX_READ_CHECKSUM				0x805A0E00
#define DBMDX_FIRMWARE_BOOT				0x805A0B00
#define DBMDX_CLEAR_CHECKSUM				0x805A0F00

#define DBMDX_VA_USLEEP_FLAG				0x0aaa
#define DBMDX_VA_MSLEEP_FLAG				0x0aab

#define DBMDX_UNDEFINED_REGISTER			0xeeee

#define DBMDX_FIRMWARE_ID_DBMD2				0xdbd2
#define DBMDX_FIRMWARE_ID_DBMD4				0xdbd4
#define DBMDX_FIRMWARE_ID_DBMD6				0xdbd6
#define DBMDX_FIRMWARE_ID_DBMD8				0xdbd8

#define	DBMDX_MIC_DISABLE_VAL				0x0200

#define DBMDX_POST_PLL_DIV_MASK				0x0007
#define DBMDX_OKG_AMODEL_SUPPORT_MASK			0x0100
#define DBMDX_SV_AMODEL_SUPPORT_MASK			0x0001
#define DBMDX_SVT_AMODEL_SUPPORT_MASK			0x0004
#define	OKG_EVENT_ID					0x10
#define LOAD_AMODEL_OKG_FW_CMD				0x2
#endif
