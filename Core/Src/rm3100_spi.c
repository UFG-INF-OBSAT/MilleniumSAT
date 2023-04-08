/*
 * Author: Lucas de Jesus B. Gonçalves
 *
 * Last modified: 02/04/2023
 * Description: SPI library for the RM3100 sensor using stm32.
 */

#include "stm32l0xx_hal.h"
#include <limits.h>
#include <stdio.h>
#include <rm3100_spi.h>

#define SPI_MAX_SEND 32

uint8_t revid;
uint16_t cycle_count;
float gain = 1.0;

enum {x2 = 0, x1 = 1, x0 = 2, y2 = 3, y1 = 4, y0 = 5, z2 = 6, z1 = 7, z0 = 8};
char msg[35] = {'\0'};

/*-------------------------------FUNÇÕES INTERNAS-----------------------*/

/*Exibe mensagem via uart*/
void uart_print(char *msg, int dbg_enabled)
{
	if (!dbg_enabled) return;

	HAL_UART_Transmit(uart_handle, (uint8_t *) msg, sizeof(msg), 100);
	HAL_Delay(1000);
}

/*Aguarda pelo pino de Data Ready*/
void wait_dr()
{
	if(USE_DR_PIN)
	{
		while(HAL_GPIO_ReadPin(GPIOB, DR_PIN) == GPIO_PIN_RESET); /* checa o pino de data ready*/
		return;
	}

	uint8_t status;
	do
	{
		RM3100_SPI_READ(STATUS_REG, &status, 1);
	}
	while((status & 0x80) != 0x80); /* lê o status interno do registrador */
}

/*Formata os dados , pois não é um signed int de 24 bits*/
void data_format(RM3100_DATA *dados, uint8_t *readings)
{
	/* Manipulação de dados - não é um signed int de 24 bits */
	if (readings[x2] & 0x80) dados->x = 0xFF;
	if (readings[y2] & 0x80) dados->y = 0xFF;
	if (readings[z2] & 0x80) dados->z = 0xFF;


	dados->x = (dados->x*256*256*256)|(int32_t) readings[x2]*256*256|(uint16_t) readings[x1]*256|readings[x0];
	dados->y = (dados->y*256*256*256)|(int32_t) readings[y2]*256*256|(uint16_t) readings[y1]*256|readings[y0];
	dados->z = (dados->z*256*256*256)|(int32_t) readings[z2]*256*256|(uint16_t) readings[z1]*256|readings[z0];
}
/*---------------------------------------------------------------------*/

/*
 * RM3100_SPI_WRITE: Envia dados para via SPI
 * ---------------------------------------------------------------------
 * addr: valor de 7 bits do endereço do registrador, data é o valor de 8
 * bits referente ao dado a ser escrito
 * data: vetor de dados a serem enviados
 * size: numero de bytes a serem enviados
 */
void RM3100_SPI_WRITE(uint8_t addr, uint8_t *data, uint16_t size)
{
	uint8_t buffer_saida[SPI_MAX_SEND];

	/* Converte o endereço para o formato de 7 bits*/
	buffer_saida[0] = (addr << 1);

	/*copia dos dados para o buffer*/
	for (int i = 1; i <= size; i++)
	{
		buffer_saida[i] = data[i-1];
	}

	/* Começa deixando o pino de CS em LOW */
	HAL_GPIO_WritePin (CS_GPIO, CS_PIN, GPIO_PIN_RESET);

	/* Envia o endereço e depois captura o dado*/
	HAL_SPI_Transmit(spi_handle, buffer_saida, size+1, HAL_MAX_DELAY);

	/* Termina com o chip de CS em HIGH */
	HAL_GPIO_WritePin (CS_GPIO, CS_PIN, GPIO_PIN_SET);
}

/*
 * addr é o valor de 7 bits do endereço do registrador, data é o valor de 8
 * bits referente ao dado a ser lido
 */
void RM3100_SPI_READ(uint8_t addr, uint8_t *data, uint16_t size)
{
	/* Converte o endereço para o formato de 7 bits*/
	uint8_t addr_7bit = (addr << 1);

	/* Começa deixando o pino de CS em LOW */
	HAL_GPIO_WritePin (CS_GPIO, CS_PIN, GPIO_PIN_RESET);

	/* Envia o endereço e depois captura o dado*/
	HAL_SPI_Transmit(spi_handle, &addr_7bit, 1, HAL_MAX_DELAY);
	HAL_SPI_Receive(spi_handle, data, size, HAL_MAX_DELAY);  // receive 6 bytes data

	/* Termina com o chip de CS em HIGH */
	HAL_GPIO_WritePin (CS_GPIO, CS_PIN, GPIO_PIN_SET);
}

/*
 * RM3100_SPI_CHANGE_CC: Faz a mudança do Cycle Count
 * ----------------------------------------------------------------------
 * new_cc: Novo valor de Cycle Count do rm3100
 */
void RM3100_SPI_CHANGE_CC(uint16_t new_cc)
{
	uint8_t CCMSB = (new_cc & 0xFF00) >> 8; /* pega o byte mais significativo */
	uint8_t CCLSB = new_cc & 0xFF; 			/* pega o byte menos significativo */

	uint8_t buffer[]={
			CCMSB,  /* write new cycle count to ccx1 */
			CCLSB,  /* write new cycle count to ccx0 */
			CCMSB,  /* write new cycle count to ccy1 */
			CCLSB,  /* write new cycle count to ccy0 */
			CCMSB,  /* write new cycle count to ccz1 */
			CCLSB  	/* write new cycle count to ccz0 */
	};

	RM3100_SPI_WRITE(CCX1_REG, buffer, 6);
}

/* Faz a configuração da conexão com o stm32*/
void RM3100_SPI_SETUP(GPIO_InitTypeDef *GPIO_InitStruct)
{
	/*Definição de pino Data Ready*/
	GPIO_InitStruct->Pin = DR_PIN;
	GPIO_InitStruct->Mode = GPIO_MODE_INPUT;
	GPIO_InitStruct->Pull = GPIO_NOPULL;
	HAL_GPIO_Init(DR_GPIO, GPIO_InitStruct);

	/*faz a leitura do Revid e em seguida printa no uart*/
	RM3100_SPI_READ(REVID_REG, &revid, 1);

	/*deve ser 0x22*/
	sprintf(msg, "REVID ID = 0x%d", revid);
	uart_print(msg, UART_DBG);

	/*Altera o cycle count*/
	RM3100_SPI_CHANGE_CC((uint16_t) INITIAL_CC);

	uint8_t cc_tmp1, cc_tmp2;
	RM3100_SPI_READ(CCX1_REG, &cc_tmp1, 1);
	RM3100_SPI_READ(CCX0_REG, &cc_tmp2, 1);

	cycle_count = (((uint16_t) cc_tmp1) << 8) | ((uint16_t) cc_tmp2);

	sprintf(msg, "Cycles count = %d", cycle_count);
	uart_print(msg, UART_DBG);

	/* Equação linear para calcular o ganho por meio de cycle_count */
	gain = (0.3671 * (float)cycle_count) + 1.5;

	sprintf(msg, "Gain = %u.%u", (unsigned int) gain*100 / 100, (unsigned int) gain*100%100);
	uart_print(msg, UART_DBG);

	uint8_t tmp;
	if (SINGLE_MODE)
	{
		/*Ativa o single mode*/
		tmp  = 0;
		RM3100_SPI_WRITE(CMM_REG, &tmp, 1);

		tmp  = 0x70;
		RM3100_SPI_WRITE(POLL_REG, &tmp, 1);
	}
	else
	{
		/*Permite a transmissão de medidas contínuas com as funções de alarme desligadas */
		tmp = 0x79;
		RM3100_SPI_WRITE(CMM_REG, &tmp, 1);
	}
}

/* Função que deve ficar em loop, retorna uma struct contendo os dados de leitura*/
RM3100_DATA RM3100_SPI_DATA()
{
	RM3100_DATA dados;
	dados.x = 0;
	dados.y = 0;
	dados.z = 0;
	dados.gain = gain;

	uint8_t readings[9]; /* valores de leitura : x2,x1,x0,y2,y1,y0,z2,z1,z0 */
	uint8_t tmp;

	/* Aguarda até que um dos 2 métodos esteja pronto para receber os dados*/
	wait_dr();

	/* Começa deixando o pino de CS em LOW */
	HAL_GPIO_WritePin (CS_GPIO, CS_PIN, GPIO_PIN_RESET);

	/* Envia o endereço e depois captura o dado*/
	tmp = 0xA4;
	uint8_t hex_val[] = {0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xAB, 0xAC, 0};

	HAL_SPI_Transmit(spi_handle, &tmp, 1, HAL_MAX_DELAY);
	HAL_SPI_TransmitReceive(spi_handle, hex_val,  readings, 9, HAL_MAX_DELAY);

	/* Termina com o chip de CS em HIGH */
	HAL_GPIO_WritePin (CS_GPIO, CS_PIN, GPIO_PIN_SET);


	/* formata os resultados em valores de 32 bits (com sinal) */
	data_format(&dados, readings);

	return dados;
}
