/**
 ******************************************************************************
 * @addtogroup PIOS PIOS Core hardware abstraction layer
 * @{
 * @addtogroup PIOS_ADS1299 ADS1299 Functions
 * @brief Deals with the hardware interface to the EEG ADC chip
 * @{
 *
 * @file       pios_ads1299.c
 * @author     Tau Labs, http://taulabs.org, Copyright (C) 2014
 * @brief      ADS1299 Interface library
 * @see        The GNU Public License (GPL) Version 3
 *
 ******************************************************************************
 */
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

/* Project Includes */
#include "pios.h"

#if defined(PIOS_INCLUDE_ADS1299)
#include "pios_ads1299.h"

/* Global Variables */

enum pios_ads1299_dev_magic {
    PIOS_ADS1299_DEV_MAGIC = 0xaba9b3ed,
};

#define PIOS_ADS1299_MAX_QUEUESIZE 2

struct ads1299_dev {
	uint32_t spi_id;
	uint32_t slave_num;
	xQueueHandle queue;
	const struct pios_ads1299_cfg *cfg;
	volatile bool configured;
	enum pios_ads1299_dev_magic magic;
};

struct pios_eeg_data {
	uint32_t data;
};

//! Global structure for this device device
static struct ads1299_dev *pios_ads1299_dev;

//! Private functions
static struct ads1299_dev *PIOS_ADS1299_alloc(void);
static int32_t PIOS_ADS1299_Validate(struct ads1299_dev *dev);
static int32_t PIOS_ADS1299_ClaimBus();
static int32_t PIOS_ADS1299_ReleaseBus();
static int32_t PIOS_ADS1299_SetReg(uint8_t address, uint8_t buffer);
static int32_t PIOS_ADS1299_GetReg(uint8_t address);
static int32_t PIOS_ADS1299_ReadID();

/**
 * @brief Allocate a new device
 */
static struct ads1299_dev *PIOS_ADS1299_alloc(void)
{
	struct ads1299_dev *ads1299_dev;

	ads1299_dev = (struct ads1299_dev *)PIOS_malloc(sizeof(*ads1299_dev));

	if (!ads1299_dev) return (NULL);

	ads1299_dev->magic = PIOS_ADS1299_DEV_MAGIC;

	ads1299_dev->configured = false;

	ads1299_dev->queue = xQueueCreate(PIOS_ADS1299_MAX_QUEUESIZE, sizeof(struct pios_eeg_data));

	if (ads1299_dev->queue == NULL) {
		vPortFree(ads1299_dev);
		return NULL;
	}

	return ads1299_dev;
}

/**
 * @brief Validate the handle to the spi device
 * @returns 0 for valid device or -1 otherwise
 */
static int32_t PIOS_ADS1299_Validate(struct ads1299_dev *dev)
{
	if (dev == NULL)
		return -1;

	if (dev->magic != PIOS_ADS1299_DEV_MAGIC)
		return -2;

	if (dev->spi_id == 0)
		return -3;

	return 0;
}

/**
 * @brief Initialize the ADS1299 EEG
 * @return 0 for success, -1 for failure
 */
int32_t PIOS_ADS1299_Init(uint32_t spi_id, uint32_t slave_num, const struct pios_ads1299_cfg *cfg)
{
	pios_ads1299_dev = PIOS_ADS1299_alloc();

	if (pios_ads1299_dev == NULL)
		return -1;

	pios_ads1299_dev->spi_id = spi_id;
	pios_ads1299_dev->slave_num = slave_num;
	pios_ads1299_dev->cfg = cfg;

	// TODO:
	// 1. Enable the power line
	// 2. Trigger a reset

	if (false) {
		/* Set up EXTI line */
		PIOS_EXTI_Init(cfg->exti_cfg);

		PIOS_ADS1299_SetReg(0,0);
		PIOS_ADS1299_GetReg(0);
	}

	if ((PIOS_ADS1299_ReadID() & 0x1F) == 0b00011110)
		return 0;
	else
		return -1;

	//PIOS_SENSORS_Register(PIOS_SENSOR_GYRO, pios_mpu6000_dev->gyro_queue);

	return 0;
}

/**
 * @brief Claim the SPI bus for the accel communications and select this chip
 * @return 0 if successful, -1 for invalid device, -2 if unable to claim bus
 */
static int32_t PIOS_ADS1299_ClaimBus()
{
	if (PIOS_ADS1299_Validate(pios_ads1299_dev) != 0)
		return -1;

	if (PIOS_SPI_ClaimBus(pios_ads1299_dev->spi_id) != 0)
		return -2;

	PIOS_SPI_RC_PinSet(pios_ads1299_dev->spi_id, pios_ads1299_dev->slave_num, 0);
	return 0;
}

/**
 * @brief Claim the SPI bus for the accel communications and select this chip
 * \param[in] pointer which receives if a task has been woken
 * @return 0 if successful, -1 for invalid device, -2 if unable to claim bus
 */
static int32_t PIOS_ADS1299_ClaimBusISR(bool *woken)
{
	if (PIOS_ADS1299_Validate(pios_ads1299_dev) != 0)
		return -1;

	if (PIOS_SPI_ClaimBusISR(pios_ads1299_dev->spi_id, woken) != 0)
		return -2;

	PIOS_SPI_RC_PinSet(pios_ads1299_dev->spi_id, pios_ads1299_dev->slave_num, 0);
	return 0;
}

/**
 * @brief Release the SPI bus for the accel communications and end the transaction
 * @return 0 if successful
 */
static int32_t PIOS_ADS1299_ReleaseBus()
{
	if (PIOS_ADS1299_Validate(pios_ads1299_dev) != 0)
		return -1;

	PIOS_SPI_RC_PinSet(pios_ads1299_dev->spi_id, pios_ads1299_dev->slave_num, 1);

	return PIOS_SPI_ReleaseBus(pios_ads1299_dev->spi_id);
}

/**
 * @brief Release the SPI bus for the accel communications and end the transaction
 * \param[in] pointer which receives if a task has been woken
 * @return 0 if successful
 */
static int32_t PIOS_ADS1299_ReleaseBusISR(bool *woken)
{
	if (PIOS_ADS1299_Validate(pios_ads1299_dev) != 0)
		return -1;

	PIOS_SPI_RC_PinSet(pios_ads1299_dev->spi_id, pios_ads1299_dev->slave_num, 1);

	return PIOS_SPI_ReleaseBusISR(pios_ads1299_dev->spi_id, woken);
}

/**
 * @brief Read a register from MPU6000
 * @returns The register value or -1 if failure to get bus
 * @param reg[in] Register address to be read
 */
static int32_t PIOS_ADS1299_GetReg(uint8_t reg)
{
	uint8_t data;

	if (PIOS_ADS1299_ClaimBus() != 0)
		return -1;

	PIOS_SPI_TransferByte(pios_ads1299_dev->spi_id, (0x20 | reg)); // set the register address
	PIOS_SPI_TransferByte(pios_ads1299_dev->spi_id, 0);            // request one byte
	data = PIOS_SPI_TransferByte(pios_ads1299_dev->spi_id, 0);     // receive response

	PIOS_ADS1299_ReleaseBus();
	return data;
}

/**
 * @brief Writes one byte to the MPU6000
 * \param[in] reg Register address
 * \param[in] data Byte to write
 * \return 0 if operation was successful
 * \return -1 if unable to claim SPI bus
 * \return -2 if unable to claim i2c device
 */
static int32_t PIOS_ADS1299_SetReg(uint8_t reg, uint8_t data)
{
	if (PIOS_ADS1299_ClaimBus() != 0)
		return -1;

	PIOS_SPI_TransferByte(pios_ads1299_dev->spi_id, 0x4 | reg);   // set the register address and write mode
	PIOS_SPI_TransferByte(pios_ads1299_dev->spi_id, 0);           // write one byte
	PIOS_SPI_TransferByte(pios_ads1299_dev->spi_id, data);

	PIOS_ADS1299_ReleaseBus();

	return 0;
}

/**
 * Read the ADS1299 device ID
 * @return id or < 0 if unsuccessful
 */
static int32_t PIOS_ADS1299_ReadID()
{
	uint32_t id = PIOS_ADS1299_GetReg(0);

	return id;
}

/**
* @brief IRQ Handler.  Read all the data from onboard buffer
*/
bool PIOS_ADS1299_IRQHandler(void)
{
	if (PIOS_ADS1299_Validate(pios_ads1299_dev) != 0 || pios_ads1299_dev->configured == false)
		return false;

	bool woken = false;

	// TODO: Fetch data from sensor

	if (PIOS_ADS1299_ClaimBusISR(&woken) != 0)
		return false;

	PIOS_ADS1299_ReleaseBusISR(&woken);

	return woken;
}

#endif /* PIOS_INCLUDE_ADS1299 */

/**
 * @}
 * @}
 */
