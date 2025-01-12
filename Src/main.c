/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2020 STMicroelectronics.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by ST under BSD 3-Clause license,
  * the "License"; You may not use this file except in compliance with the
  * License. You may obtain a copy of the License at:
  *                        opensource.org/licenses/BSD-3-Clause
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "MadgwickAHRS.h"
#include <math.h>
#include "string.h"
#include "MY_NRF24.h"
#include "string.h"
#include "stdio.h"

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

typedef struct{
	float p;
	float i;
	float d;
  float tpa_const;
	float i_range;
	float error; //private
	float last_error; //private
	uint64_t current_micros; //private
	uint64_t last_micros; //private
	float elapsed_seconds; //private
}PID_TypeDef;

typedef enum{
	MPU_Slave_Number_0 = 0,
	MPU_Slave_Number_1 = 1,
	MPU_Slave_Number_2 = 2,
	MPU_Slave_Number_3 = 3,
	MPU_Slave_RW_Read = 0x1U, 
	MPU_Slave_RW_Write = 0x0U,
	MPU_Slave_RW_Default = 0x0U,
	MPU_Slave_Enable = 0x1U,
	MPU_Slave_Disable = 0x0U,
	MPU_Slave_Byte_Swapping_EN = 0x1U,
	MPU_Slave_Byte_Swapping_DIS = 0x0U,
	MPU_Slave_Byte_Swapping_Default = 0x0U,
	MPU_Slave_REG_DIS_Default = 0x0U,
	MPU_Slave_Byte_Grouping_Even_Odd = 0x0U,
	MPU_Slave_Byte_Grouping_Odd_Even = 0x1U,
	MPU_Slave_Byte_Grouping_Default = 0x0U
}MPU_Slave_StatusTypeDef;

typedef struct{
	uint8_t Slave_Number;//Specifies which slave is used. This struct is for Slave 0 to Slave 3. Slave 4 not yet supported.
	bool RW; //slave read/write select
	uint8_t Periph_Address; //peripheral bus address
	uint8_t Register_Address; //Internal register addess in peripheral to start read from
	bool Slave_Enable; //enables Slave 0 for I2C data transaction
	bool Byte_Swapping; //configures byte swapping of word pairs. When byte swapping is enabled, the high and low bytes of a word pair are swapped.
	bool REG_DIS; //should equal 0 when register address set
	bool Byte_Grouping;//specifies the grouping order of word pairs received from registers
	uint8_t Transfer_Length; //Specifies the number of bytes transferred to and from slave
}MPU_Slave_TypeDef;

typedef enum
{
  Sensor_Read_OK = 0x00U, //everything ok
  Sensor_Read_Error = 0x01U, //fatal error, cant reach sensor
	Sensor_Read_Busy = 0x02U, //fatal error, cant reach sensor
	Sensor_Offset_Measuring = 0x03U, //sensor is offsetting
} Sensor_StatusTypedef;

typedef struct
{
  Sensor_StatusTypedef MPU_Status;
  HAL_StatusTypeDef MPU_Setup_Status;
	Sensor_StatusTypedef BMP_Status;
	HAL_StatusTypeDef BMP_Setup_Status;
	Sensor_StatusTypedef HMC_Status;
	HAL_StatusTypeDef HMC_Setup_Status;
} Sensor_ReturnTypedef;
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
//i2c addresses
#define mpu_i2c_address 0x68
#define bmp_i2c_address 0x76
#define hmc_i2c_address 0x1E

//reciever channel allocations
#define rollIn_ch ch[3]
#define pitchIn_ch ch[1]
#define throttleIn_ch ch[2]
#define yawIn_ch ch[0]

//control endpoints
#define roll_pitch_bankAng_max 30
#define yaw_max_degPsec 200

//hardware peripheral labeling
#define MOTOR_TIMER htim3
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
I2C_HandleTypeDef hi2c1;
DMA_HandleTypeDef hdma_i2c1_rx;
DMA_HandleTypeDef hdma_i2c1_tx;

SPI_HandleTypeDef hspi2;

TIM_HandleTypeDef htim1;
TIM_HandleTypeDef htim2;
TIM_HandleTypeDef htim3;

UART_HandleTypeDef huart1;
UART_HandleTypeDef huart2;
UART_HandleTypeDef huart3;

/* USER CODE BEGIN PV */
/* Private variables ---------------------------------------------------------*/

//reciever pins connections (top to bottom)
//ppm reciever
//TL motor
//BL motor
//BR motor
//TR motor


_Bool new_data = 0;
_Bool dma_new_receive = 0;
//I2C Variables
uint8_t txBuffer[2];
uint8_t rxBuffer[30];
uint8_t mpu_rx_buffer[26];
int16_t rawData[7];

//Mpu data storage variables
Sensor_ReturnTypedef sensor_status;
float accX, accY, accZ, temp, gyroX, gyroY, gyroZ, gyroX_rad, gyroY_rad, gyroZ_rad; //raw usable data
float accX_raw, accY_raw, accZ_raw, gyroX_raw, gyroY_raw, gyroZ_raw;
float roll, pitch, yaw;
float x_degrees,y_degrees,z_degrees;
float w,x,y,z;

//Mpu settings variables
/*float gyro_sensitivity_scale_factor = 131.0f; //�250deg/s
float gyro_sensitivity_scale_factor = 65.5f; //�500deg/s
float gyro_sensitivity_scale_factor = 32.8f; //�1000deg/s*/
float gyro_sensitivity_scale_factor = 16.4f; //�2000deg/s

/*float acc_sensitivity_scale_factor = 16384.0f; //�2g
float acc_sensitivity_scale_factor = 8192.0f; //�4g
float acc_sensitivity_scale_factor = 4096.0f; //�8g*/
float acc_sensitivity_scale_factor = 2048.0f; //�16g

//Mpu tweaking variables
int32_t gyroX_Offset = 0;
int32_t gyroY_Offset = 0;
int32_t gyroZ_Offset = 0;
float accX_Offset = -45.83873227210463;
float accY_Offset = 315.6486942077451;
float accZ_Offset = -70.77163484576726;
float accX_Scale = 0.997142840160418212890625;
float accY_Scale = 1.0071514293393857421875;
float accZ_Scale = 1.009796179997785986328125;

//Mpu timing variables
unsigned long reading_elapsed_time = 0;
unsigned long reading_last_time = 0;
uint32_t samples = 0;
uint32_t reading_count = 0;
uint32_t this_time = 0;

//Timekeeping Variables
uint16_t loop_freq = 250; //250 hz (desired)
uint32_t loop1_last = 0;
uint32_t loop2_last = 0;
uint32_t loop3_last = 0;
uint32_t loop4_last = 0;
uint32_t loop5_last = 0;
uint32_t time_stamps[8];
//uint32_t loop3_last = 0;
uint32_t system_clock = 0;
uint32_t loop_count = 500;

//Maths variables
const float rad_to_deg = 57.2957795f;
const float deg_to_rad = 1.0f / rad_to_deg;

//nrf24 variables(imported from "NRF24 Tutorial RX" with no changes!)
uint64_t RXpipe_address = 0x11223344AA;
char rx_data[40];
char ack_data[32] = "I have recieved your data";
_Bool nrf24_available = 0;

//ppm reciever input variables
uint32_t ch[8];
uint32_t ch_count = 9;
const uint16_t reciever_low = 996, reciever_mid = 1502, reciever_high = 2016;

//general reciever variables
float receiver_beta = 0.05;//WIP
float rollIn, pitchIn, yawIn, throttleIn;
uint32_t last_signal_micros;
_Bool receiver_signal_loss = 0;

//Pid variables (imported from "Drone Test")
//uint32_t pitch_current_time,pitch_last_time;
//uint32_t roll_current_time,roll_last_time;
//uint32_t yaw_current_time,yaw_last_time;
//double pitch_elapsed_time, roll_elapsed_time, yaw_elapsed_time;
//double pitch_pid_error, pitch_error, pitch_last_error = 0, pitch_p_error, pitch_i_error, pitch_d_error;
//double roll_pid_error, roll_error, roll_last_error = 0, roll_p_error, roll_i_error, roll_d_error;
//double yaw_pid_error, yaw_error, yaw_last_error = 0, yaw_p_error, yaw_i_error, yaw_d_error;
//roll_pid, yaw_pid;
PID_TypeDef pitch_pid, roll_pid, yaw_pid;
#define pM 0.5 //0.5
float pKp =1.5*pM, pKi = 1.5*pM, pKd = 0.5*pM;//1.5 1.5  0.5 (tweak i) (throttle and sticks a bit twitchy)
float rKp = 1.5*pM, rKi = 1.5*pM, rKd = 0.5*pM;
float yKp = 1*pM, yKi = 0.5*pM, yKd = 0*pM;//1 0.5 0 (tweak i)
float tpa_pid_p = 0.14; //reduces pid_p value as throttle increases (0.14)
float pitch_pid_output, roll_pid_output, yaw_pid_output;
#define pid_i_range 10 //10

//Motor variables (ported from "Drone Test")
float motorTR, motorBR, motorBL, motorTL;
const float servo_low = 18000;
const float servo_high = 36000;
bool failsafe = 0;


//Bmp calibration values and constants
uint16_t dig_T1 = 0;
int16_t dig_T2 = 0;
int16_t dig_T3 = 0;
uint16_t dig_P1 = 0;
int16_t dig_P2 = 0;
int16_t dig_P3 = 0;
int16_t dig_P4 = 0;
int16_t dig_P5 = 0;
int16_t dig_P6 = 0;
int16_t dig_P7 = 0;
int16_t dig_P8 = 0;
int16_t dig_P9 = 0;
const float sea_level_press = 101325.0;

//Bmp data
MPU_Slave_TypeDef bmp_slv;
PID_TypeDef altitude_pid;
int32_t temp_raw = 0;
int32_t press_raw = 0;
float pressure, temperature, altitude;

//hmc data
MPU_Slave_TypeDef hmc_slv;
int16_t mag_x_raw = 0, mag_y_raw = 0, mag_z_raw = 0;
float mag_x = 0, mag_y = 0, mag_z = 0;
float hmc_output_scale = 1024.0; //1024 counts/Gauss 
const float mag_offset_x = -0.04833999999999994;
const float mag_offset_y = -0.00048800000000004395;
const float mag_offset_z = -0.0449215;
const float mag_scale_x = 0.9557852619889624;
const float mag_scale_y = 1.0583403581326762;
const float mag_scale_z = 0.9912136198171252;



//usb buffer variables (unused)
uint8_t usb_tx_buffer[256];
uint16_t usb_tx_len;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_I2C1_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_SPI2_Init(void);
static void MX_TIM1_Init(void);
static void MX_TIM2_Init(void);
static void MX_USART3_UART_Init(void);
static void MX_TIM3_Init(void);
static void MX_USART2_UART_Init(void);
/* USER CODE BEGIN PFP */
/* Private function prototypes -----------------------------------------------*/

#ifdef __GNUC__
#define PUTCHAR_PROTOTYPE int __io_putchar(int ch)
#else
	#define PUTCHAR_PROTOTYPE int fputc(int ch, FILE *f)
#endif
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
PUTCHAR_PROTOTYPE{
  HAL_UART_Transmit(&huart1, (uint8_t *)&ch, 1, 0xFFFF);
	return ch;
}
//PUTCHAR_PROTOTYPE  {
//	/*for(int i = 0; i<400; i++){
//		if(CDC_Transmit_FS((uint8_t*)&ch, 1) == USBD_OK)
//		break;
//	}*/
//	while(!(CDC_Transmit_FS((uint8_t*)&ch, 1) == USBD_OK));
//	//while(!(CDC_Transmit_FS((uint8_t*)&ch, 1) == USBD_BUSY));
//	//CDC_Transmit_FS((uint8_t*)&ch, 1);
//	return ch;
//}
float map_v1(float input, float input_low, float input_high, float output_low, float output_high){
	/*if(input<input_low){
		input = input_low;
	}
	else if(input>input_high){
		input = input_high;
	}*/
	return (((input-input_low)/(input_high-input_low))*(output_high-output_low))+output_low;
}
float map_with_midpoint(float input, float input_low, float input_mid, float input_high, float output_low, float output_high){
	float output_mid = (output_high + output_low)/2;
	if(input < input_mid)
		return map_v1(input, input_low, input_mid, output_low, output_mid);
	else if(input_mid <= input )
		return map_v1(input, input_mid, input_high, output_mid, output_high);
}
HAL_StatusTypeDef i2c_device_register_set_verifiy(uint8_t i2c_address, uint8_t register_address, uint8_t data, uint8_t tries){
	uint8_t data_read[1];
	for(int i = 0; i<tries; i++){
		if(HAL_I2C_Mem_Write(&hi2c1, i2c_address<<1, register_address, 1, &data, 1, 100) != HAL_OK){
			printf("i2c tx error\r\n");
		}
		HAL_StatusTypeDef status_rw = HAL_I2C_Mem_Read(&hi2c1, i2c_address<<1, register_address, 1, data_read, 1, 100);
		printf("Status: %i, address: %i, data at the address: %i\r\n", status_rw, register_address, data_read[0]);
		if(data_read[0] == data){
			return HAL_OK;
		}
	}
	if(data_read[0] != data){
			return HAL_ERROR;
	}
}
int32_t compensate_temp(int32_t adc_temp,int32_t *fine_temp) {
	int32_t var1, var2;

	var1 = ((((adc_temp >> 3) - ((int32_t) dig_T1 << 1)))
			* (int32_t) dig_T2) >> 11;
	var2 = (((((adc_temp >> 4) - (int32_t) dig_T1)
			* ((adc_temp >> 4) - (int32_t) dig_T1)) >> 12)
			* (int32_t) dig_T3) >> 14;

	*fine_temp = var1 + var2;
	return (*fine_temp * 5 + 128) >> 8;
}

uint32_t compensate_press(int32_t adc_press,int32_t fine_temp) {
	int64_t var1, var2, p;

	var1 = (int64_t) fine_temp - 128000;
	var2 = var1 * var1 * (int64_t) dig_P6;
	var2 = var2 + ((var1 * (int64_t) dig_P5) << 17);
	var2 = var2 + (((int64_t) dig_P4) << 35);
	var1 = ((var1 * var1 * (int64_t) dig_P3) >> 8)
			+ ((var1 * (int64_t) dig_P2) << 12);
	var1 = (((int64_t) 1 << 47) + var1) * ((int64_t) dig_P1) >> 33;

	if (var1 == 0) {
		return 0;  // avoid exception caused by division by zero
	}

	p = 1048576 - adc_press;
	p = (((p << 31) - var2) * 3125) / var1;
	var1 = ((int64_t) dig_P9 * (p >> 13) * (p >> 13)) >> 25;
	var2 = ((int64_t) dig_P8 * p) >> 19;

	p = ((p + var1 + var2) >> 8) + ((int64_t) dig_P7 << 4);
	return p;
}
void mpu_read(void){
//	if(sensor_status.MPU_Setup_Status == HAL_OK){
//		txBuffer[0] = 0x3B;
//		HAL_StatusTypeDef status = HAL_I2C_Mem_Read(&hi2c1, mpu_i2c_address<<1, 0x3B, 1, mpu_rx_buffer, 26, 100);
//		if(status == HAL_TIMEOUT)
//			status = HAL_ERROR;
//		if(status){
//			sensor_status.MPU_Status = (Sensor_StatusTypedef)status;
//		}
//	}
	//do timekeeping
	for(int i = 0; i<7; i++){
	rawData[i] = mpu_rx_buffer[i*2]<<8 | mpu_rx_buffer[(i*2)+1]; //combine bytes into raw measurements
	}
	//calculate readable raw data
	accX_raw = rawData[0];
	accY_raw = rawData[1];
	accZ_raw = rawData[2];
	gyroX_raw = rawData[4];
	gyroY_raw = rawData[5];
	gyroZ_raw = rawData[6];
	mag_x_raw = mpu_rx_buffer[14] << 8 | mpu_rx_buffer[15];
	mag_y_raw = mpu_rx_buffer[16] << 8 | mpu_rx_buffer[17];
	mag_z_raw = mpu_rx_buffer[18] << 8 | mpu_rx_buffer[19];
	press_raw = mpu_rx_buffer[20]<<12/*msb*/ | mpu_rx_buffer[21]<<4 /*lsb*/ | mpu_rx_buffer[22]>>4; /*xlsb*/
	temp_raw = mpu_rx_buffer[23]<<12/*msb*/ | mpu_rx_buffer[24]<<4 /*lsb*/ | mpu_rx_buffer[25]>>4; /*xlsb*/
	
	//offset measurement
	if(samples < 500) {
			samples++;
			sensor_status.MPU_Status = Sensor_Offset_Measuring;
			return;
		} 
		else if(samples < 1500) {
			gyroX_Offset += (int32_t)gyroX_raw;
			gyroY_Offset += (int32_t)gyroY_raw;
			gyroZ_Offset += (int32_t)gyroZ_raw;
			samples++;
			sensor_status.MPU_Status = Sensor_Offset_Measuring;
			return;
		}
		/*else{
			gyroX_Offset /= 1000;
			gyroY_Offset /= 1000;
			gyroZ_Offset /= 1000;
		}*/
				
		//calculate raw acc and gyro values
		gyroX_raw -= gyroX_Offset/1000;
		gyroY_raw -= gyroY_Offset/1000;
		gyroZ_raw -= gyroZ_Offset/1000;
		/*gyroX_raw -= gyroX_Offset;
		gyroY_raw -= gyroY_Offset;
		gyroZ_raw -= gyroZ_Offset;*/
		accX_raw = (accX_raw + accX_Offset)*accX_Scale;
		accY_raw = (accY_raw + accY_Offset)*accY_Scale;
		accZ_raw = (accZ_raw + accZ_Offset)*accZ_Scale;
		
		//calculate real acc and gyro values		
		accX = ((float)accX_raw)/acc_sensitivity_scale_factor;
		accY = ((float)accY_raw)/acc_sensitivity_scale_factor;
		accZ = ((float)accZ_raw)/acc_sensitivity_scale_factor;
		temp = (((float)rawData[3])/340) + 36.53;
		gyroX = (((float)(gyroX_raw))/gyro_sensitivity_scale_factor);
		gyroY = (((float)(gyroY_raw))/gyro_sensitivity_scale_factor);
		gyroZ = (((float)(gyroZ_raw))/gyro_sensitivity_scale_factor);
		
		gyroX_rad = gyroX * deg_to_rad;
		gyroY_rad = gyroY * deg_to_rad;
		gyroZ_rad = gyroZ * deg_to_rad;
		sensor_status.MPU_Status = Sensor_Read_OK;
		
		//declare temporary bmp variables 
		int32_t fine_temp = 0;
		int32_t fixed_temperature = 0;
		uint32_t fixed_pressure= 0;
		
		//calculate temp and pressure and altitude (bmp)
		fine_temp = 0;
		fixed_temperature = compensate_temp(temp_raw, &fine_temp);
		fixed_pressure = compensate_press(press_raw, fine_temp);
		
		temperature = (float) fixed_temperature / 100;
		pressure = (float) fixed_pressure / 256;
		
		if(pressure > 150000 || pressure < 20000 || temperature < -50 || temperature > 165){
			sensor_status.BMP_Status = Sensor_Read_Error;
		}

		altitude = ((powf((sea_level_press/pressure), 0.1902225603956629)-1)*(temperature+273.15))*153.8461538461538;
		
		//magnetometer calculations
		mag_x = ((mag_x_raw/hmc_output_scale) - mag_offset_x)*mag_scale_x;
		mag_y = ((mag_y_raw/hmc_output_scale) - mag_offset_y)*mag_scale_y;
		mag_z = ((mag_z_raw/hmc_output_scale) - mag_offset_z)*mag_scale_z;
		//note: upon disconection from i2c bus, both bmp and hmc keep returning last updated values and do not hinder the working of the other sensor.
		//Bmp does not get effected from vcc disconnect, however gnd disconnects and vcc disconnect to hmc require a restart to recover and hmc power fault hinders working of bmp as well
		if(mag_x == 0 && mag_y == 0 && mag_z == 0)
			sensor_status.HMC_Status = Sensor_Read_Error;
		else
			sensor_status.HMC_Status = Sensor_Read_OK;
}	
uint32_t microseconds(void){
	return system_clock*1000 + ((SysTick->LOAD - SysTick->VAL)/72); //SYSTICK IS A DOWN COUNTER; CHANGE!!!!
}
void QuatToEuler(void){
	float sqw;
	float sqx;
	float sqy;
	float sqz;

	float rotxrad;
	float rotyrad;
	float rotzrad;

	sqw = q0* q0;
	sqx = q1* q1;
	sqy = q2* q2;
	sqz = q3* q3;
	
	rotxrad = (float) atan2(2.0 * (q2* q3 + q1* q0 ) , ( -sqx - sqy + sqz + sqw ));
	rotyrad = (float) asin(-2.0 * (q1* q3 - q2* q0 ));
	rotzrad = (float) atan2(2.0 * (q1* q2 + q3* q0 ) , (sqx - sqy - sqz + sqw ));

	x_degrees = rotxrad * rad_to_deg;
	y_degrees = rotyrad * rad_to_deg;
	z_degrees = rotzrad * rad_to_deg;
	roll = -x_degrees;
	pitch = y_degrees;
	yaw = -z_degrees;
	return;
}
void nrf24_setup(void){
  NRF24_begin(CE_GPIO_Port,CSN_Pin,CE_Pin,hspi2);
	nrf24_DebugUART_Init(huart1);
	
	NRF24_openReadingPipe(1,RXpipe_address);
	NRF24_setAutoAck(true);
	NRF24_enableDynamicPayloads();
	NRF24_enableAckPayload();
	NRF24_setChannel(42);
	NRF24_setPayloadSize(32);
	NRF24_setDataRate(RF24_250KBPS);
	NRF24_startListening();
	printRadioSettings();
}
void nrf24_test(void){
	if(nrf24_available){
		nrf24_available = 0;
		NRF24_read(rx_data,32);
		NRF24_writeAckPayload(1,ack_data,32);
		
		
		rx_data[32] = '\r'; rx_data[33] = '\n';
		HAL_UART_Transmit(&huart1, (uint8_t*)rx_data,34,10);
		if(rx_data[0] == '0')
			HAL_GPIO_WritePin(GPIOC,GPIO_PIN_13, GPIO_PIN_SET);
		else if(rx_data[0] == '1')
			HAL_GPIO_WritePin(GPIOC,GPIO_PIN_13, GPIO_PIN_RESET);
	}
}
void reciever_print(void){
	for(int i = 0; i<8; i++){
		  printf("CHANNEL %i: %i ", i+1,ch[i]);
		}
  printf("\n");
}
float Pid_calculate_tpa(PID_TypeDef* pid, float current_state, float desired_state, float tpa_input, bool i_ignore){
	float p_error = 0, i_error = 0, d_error = 0;
  pid->last_error = pid->error;
  pid->error = current_state-desired_state;
  pid->last_micros = pid->current_micros;
  pid->current_micros = microseconds();
  pid->elapsed_seconds = (pid->current_micros-pid->last_micros)/ 1000000.0f; //!! eliminate the division by the microseconds period if problems arise !!
  p_error = (pid->p) * (pid->error) * map_v1(tpa_input, 0, 180, 1+pid->tpa_const, 1-pid->tpa_const);
	if(pid->i_range != -1){
	  if((pid->i_range>pid->error)&&(pid->error>-pid->i_range)){
    i_error += pid->i*(pid->error * pid->elapsed_seconds);
		}
		else{
			i_error = 0;
		}
	}
	
	if(i_ignore){
		i_error = 0;
	}
  d_error = (pid->d*((pid->error-pid->last_error)/pid->elapsed_seconds)); 
	printf("%f\n", pid->elapsed_seconds);
  return p_error + i_error + d_error;
	
  /*if(pitch_pid_output > 30)
		pitch_pid_output = 30;
	else if(pitch_pid_output < -30)
		pitch_pid_output = -30;*/
}
void motor_signal_set(bool failsf){
	motorTR = throttleIn;
	motorBR = throttleIn;
	motorBL = throttleIn; 
	motorTL = throttleIn;
	
	
	//pitch(fix clip protect)
	motorTR -= pitch_pid_output;
	if(motorTR<0){
		motorBR += 0 - motorTR;
		motorTR = 0;
	}
	else if(180<motorTR){
		motorBR += 180 - motorTR;
		motorTR = 180;
	}
	
	motorBR += pitch_pid_output;
	if(motorBR<0){
		motorTR += 0 - motorBR;
		motorBR = 0;
	}
	else if(180<motorBR){
		motorTR += 180 - motorBR;
		motorBR = 180;
	}
	
	motorBL += pitch_pid_output; 
	if(motorBL<0){
		motorTL += 0 - motorBL;
		motorBL = 0;
	}
	else if(180<motorBL){
		motorTL += 180 - motorBL;
		motorBL = 180;
	}
	
	motorTL -= pitch_pid_output;
	if(motorTL<0){
		motorBL += 0 - motorTL;
		motorTL = 0;
	}
	else if(180<motorTL){
		motorBL += 180 - motorTL;
		motorTL = 180;
	}
	
	
	
	
	//roll(fix clip protect)
	motorTR += roll_pid_output;
	if(motorTR<0){
		motorTL += 0 - motorTR;
		motorTR = 0;
	}
	else if(180<motorTR){
		motorTL += 180 - motorTR;
		motorTR = 180;
	}
	
	motorTL -= roll_pid_output;
	if(motorTL<0){
		motorTR += 0 - motorTL;
		motorTL = 0;
	}
	else if(180<motorTL){
		motorTR += 180 - motorTL;
		motorTL = 180;
	}
	
	motorBR += roll_pid_output;
	if(motorBR<0){
		motorBL += 0 - motorBR;
		motorBR = 0;
	}
	else if(180<motorBR){
		motorBL += 180 - motorBR;
		motorBR = 180;
	}
	
	motorBL -= roll_pid_output; 
	if(motorBL<0){
		motorBR += 0 - motorBL;
		motorBL = 0;
	}
	else if(180<motorBL){
		motorBR += 180 - motorBL;
		motorBL = 180;
	}
	
	
	
	/*motorTR += yawIn;
	motorBR -= yawIn;
	motorBL += yawIn; 
	motorTL -= yawIn;*/

	motorTR += yaw_pid_output;
	motorBR -= yaw_pid_output;
	motorBL += yaw_pid_output;
	motorTL -= yaw_pid_output;
	
	motorTL = map_v1(motorTL,0,180,servo_low,servo_high);
	motorTR = map_v1(motorTR,0,180,servo_low,servo_high);
	motorBL = map_v1(motorBL,0,180,servo_low,servo_high);
	motorBR = map_v1(motorBR,0,180,servo_low,servo_high);
	//clipping
  if(motorTL<servo_low){
		motorTL = servo_low;
	}
	else if(motorTL>servo_high){
		motorTL = servo_high;
	}
	if(motorTR<servo_low){
		motorTR = servo_low;
	}
	else if(motorTR>servo_high){
		motorTR = servo_high;
	}	
	if(motorBL<servo_low){
		motorBL = servo_low;
	}
	else if(motorBL>servo_high){
		motorBL = servo_high;
	}	
	if(motorBR<servo_low){
		motorBR = servo_low;
	}
	else if(motorBR>servo_high){
		motorBR = servo_high;
	}
	/*motorTR = servo_low;
	motorBR = servo_high;
	motorBL = servo_low; 
	motorTL = servo_high;*/
	
	//pwm signal set
	if(failsf || (throttleIn < 10)){
		motorTR = servo_low;
		motorBR = servo_low;
		motorBL = servo_low; 
		motorTL = servo_low;
	}
	__HAL_TIM_SET_COMPARE(&MOTOR_TIMER, TIM_CHANNEL_1, motorTR); //PA6
	__HAL_TIM_SET_COMPARE(&MOTOR_TIMER, TIM_CHANNEL_2, motorBR); //PA7
	__HAL_TIM_SET_COMPARE(&MOTOR_TIMER, TIM_CHANNEL_3, motorBL); //PB0
	__HAL_TIM_SET_COMPARE(&MOTOR_TIMER, TIM_CHANNEL_4, motorTL); //PB1
	
	
//	HAL_TIM_PWM_Start(&MOTOR_TIMER,TIM_CHANNEL_1);
//	HAL_TIM_PWM_Start(&MOTOR_TIMER,TIM_CHANNEL_2);
//	HAL_TIM_PWM_Start(&MOTOR_TIMER,TIM_CHANNEL_3);
//	HAL_TIM_PWM_Start(&MOTOR_TIMER,TIM_CHANNEL_4);
//	HAL_TIM_OnePulse_Start(&MOTOR_TIMER,TIM_CHANNEL_1);
//	HAL_TIM_OnePulse_Start(&MOTOR_TIMER,TIM_CHANNEL_2);
//	HAL_TIM_OnePulse_Start(&MOTOR_TIMER,TIM_CHANNEL_3);
//	HAL_TIM_OnePulse_Start(&MOTOR_TIMER,TIM_CHANNEL_4);
}
void map_reciever_inputs(){	
	
	//WIP Low pass filter
	float rollIn_raw     = map_with_midpoint((float)rollIn_ch, reciever_low, reciever_mid, reciever_high, -roll_pitch_bankAng_max,  roll_pitch_bankAng_max);
	float pitchIn_raw    = -map_with_midpoint((float)pitchIn_ch, reciever_low, reciever_mid, reciever_high, -roll_pitch_bankAng_max,  roll_pitch_bankAng_max);
	float yawIn_raw      = -map_with_midpoint((float)yawIn_ch, reciever_low, reciever_mid, reciever_high, -yaw_max_degPsec,  yaw_max_degPsec);
	float throttleIn_raw = map_with_midpoint((float)throttleIn_ch, reciever_low, reciever_mid, reciever_high,   0.0f, 180.0f);
	
	rollIn = rollIn_raw;
	pitchIn = pitchIn_raw;
	yawIn = yawIn_raw;
	throttleIn = throttleIn_raw;
	
	/*rollIn = rollIn - (reciever_beta * (rollIn - rollIn_raw));
	pitchIn = pitchIn - (reciever_beta * (pitchIn - pitchIn_raw));
	yawIn = yawIn - (reciever_beta * (yawIn - yawIn_raw));
	throttleIn = throttleIn - (reciever_beta * (throttleIn - throttleIn_raw));*/
	
	
	 /*rollIn     = map_with_midpoint((double)rollIn_ch, reciever_low, reciever_mid, reciever_high, -roll_pitch_bankAng_max,  roll_pitch_bankAng_max);
	 pitchIn    = map_with_midpoint((double)pitchIn_ch, reciever_low, reciever_mid, reciever_high, -roll_pitch_bankAng_max,  roll_pitch_bankAng_max);
	 yawIn      = map_with_midpoint((double)yawIn_ch, reciever_low, reciever_mid, reciever_high, -yaw_max_degPsec,  yaw_max_degPsec);
	 throttleIn = map_with_midpoint((double)throttleIn_ch, reciever_low, reciever_mid, reciever_high,   0.0f, 180.0f);*/
}
void time_stamps_print(uint16_t last_timestamp){
	uint32_t total_time = (time_stamps[last_timestamp] - time_stamps[0]);
	printf("TOTAL PROCESS TAKEN: %i\n", total_time);
	printf("TOTAL TIME: %u\n", reading_elapsed_time);
	printf("CPU USAGE: %f\n", ((float)total_time/(float)reading_elapsed_time)*100.0);
	for(int i = 1; i<last_timestamp; i++){
		printf("TIMESTAMP %i:, %i\n", i, (time_stamps[i] - time_stamps[i-1]));
	}
	printf("\n");
}
HAL_StatusTypeDef mpu_setup(void){
	uint8_t mpu_status = HAL_I2C_IsDeviceReady(&hi2c1, mpu_i2c_address<<1, 10, 100);
	HAL_StatusTypeDef error = 0;
	if(mpu_status == HAL_OK){
		error = i2c_device_register_set_verifiy(mpu_i2c_address,0x6B,0x0, 5); //Power Management 1
		error |= i2c_device_register_set_verifiy(mpu_i2c_address,0x19,15, 5); //Sample rate divider
		error |= i2c_device_register_set_verifiy(mpu_i2c_address,0x1A,0x0, 5); //DLPF Config
  	error |= i2c_device_register_set_verifiy(mpu_i2c_address,0x1B,3<<3, 5); //Gyro Config (2000 d)
  	error |= i2c_device_register_set_verifiy(mpu_i2c_address,0x1C,3<<3, 5); //Acc Config	(16 g)
    error |= i2c_device_register_set_verifiy(mpu_i2c_address,56,1, 5); //Interrupt Config
		error |= i2c_device_register_set_verifiy(mpu_i2c_address,106,0, 5); //Aux i2c master off
		error |= i2c_device_register_set_verifiy(mpu_i2c_address,55,2, 5); //Aux i2c passthrough enable
	}
	else{
		printf("\nMPU Setup: Error at mpu connection: %i\n", mpu_status);
		return mpu_status;
	}
	if(error != HAL_OK){
		printf("\nMPU Setup: Error at mpu setup: %i\n", error);
		return error;
	}
	else{
		printf("MPU Setup successful!\n");
		return HAL_OK;
	}

}
HAL_StatusTypeDef bmp_setup(void){
	HAL_StatusTypeDef error = HAL_OK;
	HAL_StatusTypeDef bmp_status = HAL_I2C_IsDeviceReady(&hi2c1, bmp_i2c_address<<1, 10, 100);
	if(bmp_status == HAL_OK){
		error |= i2c_device_register_set_verifiy(bmp_i2c_address, 0xF4,87, 5); /*Pressure (x16), temperature(x2) oversampling rates and mode(normal) setup*/
		//uint8_t payload = 2<<5 | 5<<2 | 3;
		//error |= bmp_register_set_new(0xF4,payload, 2); /*Pressure (x16), temperature(x2) oversampling rates and mode(normal) setup*/
		error |= i2c_device_register_set_verifiy(bmp_i2c_address, 0xF5,4<<2, 5); //iir filter coefficent 16
		
	}
	else{
		printf("\nBMP setup: Error at bmp connection: %i\n", bmp_status);
		return bmp_status;
	}
	if(error != HAL_OK){
		printf("\nBMP setup: Error at bmp setup: %i\n", error);
		return error;
	}
	else{
		printf("BMP setup successful!\n");
		return HAL_OK;
	}

}

HAL_StatusTypeDef hmc_setup(void){
	HAL_StatusTypeDef error = HAL_OK;
	HAL_StatusTypeDef hmc_status = HAL_I2C_IsDeviceReady(&hi2c1, hmc_i2c_address<<1, 10, 100);
	if(hmc_status == HAL_OK){
		error |= i2c_device_register_set_verifiy(hmc_i2c_address, 0, 0x6<<2, 5); /*75 hz data output rate*/
		error |= i2c_device_register_set_verifiy(hmc_i2c_address, 1, 0x1<<2, 5); /*1024 counts/Gauss +- 1.2 Gauss accuracy*/
		error |= i2c_device_register_set_verifiy(hmc_i2c_address, 2, 0, 5); /*continous measurement mode*/
	}
	else{
		printf("\nHMC setup: Error at hmc connection: %i\n", hmc_status);
		return hmc_status;
	}
	if(error != HAL_OK){
		printf("\nHMC setup: Error at hmc setup: %i\n", error);
		return error;
	}
	else{
		printf("HMC setup successful!\n");
		return HAL_OK;
	}
}
HAL_StatusTypeDef MPU_Slave_Setup(I2C_HandleTypeDef *hi2c, MPU_Slave_TypeDef *slave){
	uint8_t error = HAL_OK;
	uint8_t mpu_status = HAL_I2C_IsDeviceReady(hi2c, mpu_i2c_address<<1, 10, 100);
	uint8_t Config_Data[3];

	Config_Data[0] = slave->RW<<7 | slave->Periph_Address;
	Config_Data[1] = slave->Register_Address;
	Config_Data[2] = slave->Slave_Enable<<7 | slave->Byte_Swapping<<6 | slave->REG_DIS<<5 | slave->Byte_Grouping<<4 | slave->Transfer_Length;
	
	if(mpu_status == HAL_OK){
		error |= i2c_device_register_set_verifiy(mpu_i2c_address, 37 + 3*slave->Slave_Number, Config_Data[0], 5);
		error |= i2c_device_register_set_verifiy(mpu_i2c_address, 38 + 3*slave->Slave_Number, Config_Data[1], 5); 
		error |= i2c_device_register_set_verifiy(mpu_i2c_address, 39 + 3*slave->Slave_Number, Config_Data[2], 5);
	}
	else{
		printf("\nSlave %i setup: Error at mpu connection: %i\n", slave->Slave_Number, mpu_status);
		return mpu_status;
	}
	if(error != HAL_OK){
		printf("\nSlave %i setup: Error at slave %i setup: %i\n", slave->Slave_Number, slave->Slave_Number, error);
		return error;
	}
	else{
		printf("Slave %i setup successful!\n", slave->Slave_Number);
		return HAL_OK;
	}
}
HAL_StatusTypeDef mpu_master_setup(void){
	HAL_StatusTypeDef error = HAL_OK;
	uint8_t mpu_status = HAL_I2C_IsDeviceReady(&hi2c1, mpu_i2c_address<<1, 10, 100);
	if(mpu_status == HAL_OK){
    //error |= mpu_register_set_new(56,1, 2); //Interrupt Config (maybe add slave interrupts)
		error |= i2c_device_register_set_verifiy(mpu_i2c_address,52,5, 5); //reduces slave access rate to 1/(1+5) samples (normal sample rate/6 = 83.33 hz)
		error |= i2c_device_register_set_verifiy(mpu_i2c_address,103,1<<1 | 1, 5); //slave 0 and 1 reduced access speed
		error |= i2c_device_register_set_verifiy(mpu_i2c_address,36,13, 5); //Aux i2c master setup
	
		error |= i2c_device_register_set_verifiy(mpu_i2c_address,55,0, 5); //Aux i2c passthrough disable
		error |= i2c_device_register_set_verifiy(mpu_i2c_address,106,32, 5); //Aux i2c master on
	}
	else{
		printf("\nError at mpu master setup: %i\n", mpu_status);
		return mpu_status;
	}
	if(error != HAL_OK){
		printf("\nError at mpu master setup: %i\n", error);
		return error;
	}
	else{
		printf("Mpu master setup successful!\n");
		return HAL_OK;
	}
}
void bmp_read_calib(void){
	txBuffer[0] = 0x88;
	uint8_t status = HAL_I2C_Master_Transmit(&hi2c1, bmp_i2c_address<<1, txBuffer, 1, 100);//set pointer
	status |= HAL_I2C_Master_Receive(&hi2c1, bmp_i2c_address<<1, rxBuffer, 24, 100);//read all data registers
	dig_T1 = rxBuffer[1]<<8 | rxBuffer[0];
	dig_T2 = rxBuffer[3]<<8 | rxBuffer[2];
	dig_T3 = rxBuffer[5]<<8 | rxBuffer[4];
  dig_P1 = rxBuffer[7]<<8 | rxBuffer[6];
	dig_P2 = rxBuffer[9]<<8 | rxBuffer[8];
	dig_P3 = rxBuffer[11]<<8 | rxBuffer[10];
	dig_P4 = rxBuffer[13]<<8 | rxBuffer[12];
	dig_P5 = rxBuffer[15]<<8 | rxBuffer[14];
	dig_P6 = rxBuffer[17]<<8 | rxBuffer[16];
	dig_P7 = rxBuffer[19]<<8 | rxBuffer[18];
	dig_P8 = rxBuffer[21]<<8 | rxBuffer[20];
	dig_P9 = rxBuffer[23]<<8 | rxBuffer[22];
	printf("Status: %i\r\n", status);
}
void mpu_read_dma(void){
	//printf("R");
	if(sensor_status.MPU_Setup_Status == HAL_OK){
		txBuffer[0] = 0x3B;
		HAL_StatusTypeDef status = HAL_I2C_Mem_Read_DMA(&hi2c1, mpu_i2c_address<<1, 0x3B, 1, mpu_rx_buffer, 26);
		if(status == HAL_TIMEOUT)
			status = HAL_ERROR;
		if(status){
			sensor_status.MPU_Status = (Sensor_StatusTypedef)status;
		}
	}
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */
  

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_I2C1_Init();
  MX_USART1_UART_Init();
  MX_SPI2_Init();
  MX_TIM1_Init();
  MX_TIM2_Init();
  MX_USART3_UART_Init();
  MX_TIM3_Init();
  MX_USART2_UART_Init();
  /* USER CODE BEGIN 2 */
	//set sensor setup typedefs
	sensor_status.MPU_Setup_Status = HAL_ERROR;
	sensor_status.HMC_Setup_Status = HAL_ERROR;
	sensor_status.BMP_Setup_Status = HAL_ERROR;
	
	//set_pid_constants
	pitch_pid.p = pKp;
	pitch_pid.i = pKi;
	pitch_pid.d = pKd;
	pitch_pid.tpa_const = tpa_pid_p;
	pitch_pid.i_range = pid_i_range;
	
	roll_pid.p = rKp;
	roll_pid.i = rKi;
	roll_pid.d = rKd;
	roll_pid.tpa_const = tpa_pid_p;
	roll_pid.i_range = pid_i_range;

	
	yaw_pid.p = yKp;
	yaw_pid.i = yKi;
	yaw_pid.d = yKd;
	yaw_pid.tpa_const = tpa_pid_p;
	yaw_pid.i_range = pid_i_range;



	//hmc slave typedef setup
	hmc_slv.Slave_Number = MPU_Slave_Number_0;
	hmc_slv.Byte_Grouping = MPU_Slave_Byte_Grouping_Default;
	hmc_slv.Byte_Swapping = MPU_Slave_Byte_Swapping_Default;
	hmc_slv.Periph_Address = hmc_i2c_address;
	hmc_slv.Register_Address = 0x3;
	hmc_slv.REG_DIS = MPU_Slave_REG_DIS_Default;
	hmc_slv.RW = MPU_Slave_RW_Read;
	hmc_slv.Slave_Enable = MPU_Slave_Enable;
	hmc_slv.Transfer_Length = 6;
	
	
	//bmp slave typedef setup
	bmp_slv.Slave_Number = MPU_Slave_Number_1;
	bmp_slv.Byte_Grouping = MPU_Slave_Byte_Grouping_Default;
	bmp_slv.Byte_Swapping = MPU_Slave_Byte_Swapping_Default;
	bmp_slv.Periph_Address = bmp_i2c_address;
	bmp_slv.Register_Address = 0xF7;
	bmp_slv.REG_DIS = MPU_Slave_REG_DIS_Default;
	bmp_slv.RW = MPU_Slave_RW_Read;
	bmp_slv.Slave_Enable = MPU_Slave_Enable;
	bmp_slv.Transfer_Length = 6;

	
	  SysTick_Config(SystemCoreClock/1000);//set systick up
//		mpu_register_set_new(0x19,15); //Sample rate divider
//		while(true)
//			printf("%i ", mpu_register_read(117));		

		//timer start
		HAL_TIM_PWM_Start(&MOTOR_TIMER,TIM_CHANNEL_1);
		HAL_TIM_PWM_Start(&MOTOR_TIMER,TIM_CHANNEL_2);
		HAL_TIM_PWM_Start(&MOTOR_TIMER,TIM_CHANNEL_3);
		HAL_TIM_PWM_Start(&MOTOR_TIMER,TIM_CHANNEL_4);
		
		//nrf24_setup();
		HAL_TIM_IC_Start_IT(&htim1,TIM_CHANNEL_1); //ppm recieve start
		loop1_last = microseconds();
		loop2_last = microseconds();
		
		
		//sensor setups
		sensor_status.MPU_Setup_Status = mpu_setup();
		if(sensor_status.MPU_Setup_Status != HAL_OK){
			HAL_GPIO_WritePin(GPIOC,GPIO_PIN_13,GPIO_PIN_RESET);
			//HAL_NVIC_SystemReset();
			while(1);
		}
		sensor_status.HMC_Setup_Status = hmc_setup();
		sensor_status.BMP_Setup_Status = bmp_setup();
		bmp_read_calib();
		
		
		//set bmp280 as slave 0
		if(sensor_status.HMC_Setup_Status == HAL_OK || sensor_status.MPU_Setup_Status == HAL_OK){
			sensor_status.HMC_Setup_Status = MPU_Slave_Setup(&hi2c1, &hmc_slv);
		}
		//set hmc5883 as slave 1
		if(sensor_status.BMP_Setup_Status == HAL_OK || sensor_status.MPU_Setup_Status == HAL_OK){
			sensor_status.BMP_Setup_Status = MPU_Slave_Setup(&hi2c1, &bmp_slv);
		}
		//mpu master setup
		HAL_StatusTypeDef mpu_master_setup_status = mpu_master_setup();
		if(mpu_master_setup_status != HAL_OK){
			sensor_status.HMC_Setup_Status = HAL_ERROR;
			sensor_status.BMP_Setup_Status = HAL_ERROR;
		}
  /* USER CODE END 2 */
 
 

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {

    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
		//if(new_data){
		if(dma_new_receive){
			time_stamps[0] = microseconds();
			
			HAL_GPIO_WritePin(CE_GPIO_Port,CE_Pin,GPIO_PIN_SET);
			new_data = 0;
			dma_new_receive = 0;
			mpu_read(); //optimisable
			
			time_stamps[1] = microseconds();
			
			if(sensor_status.MPU_Status == Sensor_Read_OK){
				MadgwickAHRSupdateIMU(gyroX_rad, gyroY_rad, gyroZ_rad, accX, accY, accZ);//unoptimisable
			}
//			this_time = microseconds();
//			reading_elapsed_time = this_time - reading_last_time;
//			reading_last_time = this_time;
			QuatToEuler();//partly optimisable
			
			time_stamps[2] = microseconds();
			
			map_reciever_inputs();
			
			
//			rollPidCompute();
//			pitchPidCompute();
//		  yawPidCompute();
			
			bool i_ignore = (failsafe == 1 || throttleIn < 10);
			roll_pid_output = Pid_calculate_tpa(&roll_pid, roll, rollIn,  throttleIn, i_ignore);
			pitch_pid_output = Pid_calculate_tpa(&pitch_pid, pitch, pitchIn,  throttleIn, i_ignore);
			yaw_pid_output  = Pid_calculate_tpa(&yaw_pid, gyroZ, yawIn, throttleIn, i_ignore);

			
			time_stamps[3] = microseconds();
			
			loop3_last = microseconds();
			
			if(loop_count>450 && loop_count<510 && (microseconds()-last_signal_micros)<100000){
				failsafe = 0;
				motor_signal_set(failsafe);
				HAL_GPIO_WritePin(GPIOC,GPIO_PIN_13,GPIO_PIN_SET);
			}
			else{
				failsafe = 1;
				motor_signal_set(failsafe);
				HAL_GPIO_WritePin(GPIOC,GPIO_PIN_13,GPIO_PIN_RESET);
				//HAL_NVIC_SystemReset();
			}
			reading_count++;
			//printf("%f\n", gyroY_rad*rad_to_deg);
			time_stamps[4] = microseconds();
			HAL_GPIO_WritePin(CE_GPIO_Port,CE_Pin,GPIO_PIN_RESET);
		}
		if(system_clock - loop1_last> 1000){
			loop_count = reading_count;
		  loop1_last = system_clock;
			reading_count = 0;
		}
		
		/*if(1/((microseconds() - loop3_last)/1000000)> 200){
		  //Add motor set to here igf you want to run slower than loop freq
		}*/
		
		if(system_clock - loop2_last> 250){
			loop2_last = system_clock;
			uint32_t start_a = microseconds();
		
			//CDC_Transmit_FS(pack, strlen((char *)pack));
//			printf("ROLL : %f\n",roll);
//			printf("PITCH : %f\n",pitch);
//			printf("YAW : %f\n",yaw);
//			printf("MAGX : %f\n",mag_x);
//			printf("MAGY : %f\n",mag_y);
//			printf("MAGZ : %f\n",mag_z);
//			printf("TEMP : %f\n",temperature);
//			printf("PRES : %f\n",pressure);
//			printf("ALT : %f\n",altitude);
//			printf("TL%f\n", motorTL);
//			printf("TR%f\n", motorTR);
//			printf("BL%f\n", motorBL);
//			printf("%f\n", motorBR);
//			printf("%f\n", ((motorBR + motorBL + motorTL + motorTR)/4)/(map_with_midpoint(throttleIn_ch, reciever_low, reciever_mid, reciever_high, 18000, 36000)));
//			printf("ROLL IN  : %f\n",rollIn);
//			printf("PITCH IN : %f\n",pitchIn);
//			printf("YAW IN   : %f\n",yawIn);
//			printf("THRO IN   : %f\n",throttleIn);
//			printf("X%f\n",accX_raw);
//			printf("Y%f\n",accY_raw);
//			printf("Z%f\n",accZ_raw);
//			printf("GYX : %f\n",gyroX);
//			printf("GYY : %f\n",gyroY);
//			printf("GYZ : %f\n",gyroZ);
//			reciever_print();
//			time_stamps_print(4);
//			printf("L count a: %i\n",loop_count);
//			printf("STATUS:: MPU: %i BMP: %i HMC: %i\n",sensor_status.MPU_Setup_Status, sensor_status.BMP_Setup_Status, sensor_status.HMC_Setup_Status);
				//printf("Print time: %i\n",(microseconds()-start_a));
		}
		//nrf24_test();
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Initializes the CPU, AHB and APB busses clocks 
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }
  /** Initializes the CPU, AHB and APB busses clocks 
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief I2C1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C1_Init(void)
{

  /* USER CODE BEGIN I2C1_Init 0 */

  /* USER CODE END I2C1_Init 0 */

  /* USER CODE BEGIN I2C1_Init 1 */

  /* USER CODE END I2C1_Init 1 */
  hi2c1.Instance = I2C1;
  hi2c1.Init.ClockSpeed = 400000;
  hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C1_Init 2 */

  /* USER CODE END I2C1_Init 2 */

}

/**
  * @brief SPI2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI2_Init(void)
{

  /* USER CODE BEGIN SPI2_Init 0 */

  /* USER CODE END SPI2_Init 0 */

  /* USER CODE BEGIN SPI2_Init 1 */

  /* USER CODE END SPI2_Init 1 */
  /* SPI2 parameter configuration*/
  hspi2.Instance = SPI2;
  hspi2.Init.Mode = SPI_MODE_MASTER;
  hspi2.Init.Direction = SPI_DIRECTION_2LINES;
  hspi2.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi2.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi2.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi2.Init.NSS = SPI_NSS_SOFT;
  hspi2.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_4;
  hspi2.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi2.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi2.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi2.Init.CRCPolynomial = 10;
  if (HAL_SPI_Init(&hspi2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI2_Init 2 */

  /* USER CODE END SPI2_Init 2 */

}

/**
  * @brief TIM1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM1_Init(void)
{

  /* USER CODE BEGIN TIM1_Init 0 */

  /* USER CODE END TIM1_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_IC_InitTypeDef sConfigIC = {0};

  /* USER CODE BEGIN TIM1_Init 1 */

  /* USER CODE END TIM1_Init 1 */
  htim1.Instance = TIM1;
  htim1.Init.Prescaler = 71;
  htim1.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim1.Init.Period = 65535;
  htim1.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim1.Init.RepetitionCounter = 0;
  htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim1) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim1, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_IC_Init(&htim1) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim1, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigIC.ICPolarity = TIM_INPUTCHANNELPOLARITY_RISING;
  sConfigIC.ICSelection = TIM_ICSELECTION_DIRECTTI;
  sConfigIC.ICPrescaler = TIM_ICPSC_DIV1;
  sConfigIC.ICFilter = 15;
  if (HAL_TIM_IC_ConfigChannel(&htim1, &sConfigIC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM1_Init 2 */

  /* USER CODE END TIM1_Init 2 */

}

/**
  * @brief TIM2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM2_Init(void)
{

  /* USER CODE BEGIN TIM2_Init 0 */

  /* USER CODE END TIM2_Init 0 */

  /* USER CODE BEGIN TIM2_Init 1 */

  /* USER CODE END TIM2_Init 1 */
  htim2.Instance = TIM2;
  htim2.Init.Prescaler = 0;
  htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim2.Init.Period = 18100;
  htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_OnePulse_Init(&htim2, TIM_OPMODE_SINGLE) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM2_Init 2 */

  /* USER CODE END TIM2_Init 2 */

}

/**
  * @brief TIM3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM3_Init(void)
{

  /* USER CODE BEGIN TIM3_Init 0 */

  /* USER CODE END TIM3_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};

  /* USER CODE BEGIN TIM3_Init 1 */

  /* USER CODE END TIM3_Init 1 */
  htim3.Instance = TIM3;
  htim3.Init.Prescaler = 3;
  htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim3.Init.Period = 36100;
  htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim3) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim3, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_Init(&htim3) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim3, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_PWM_ConfigChannel(&htim3, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_ConfigChannel(&htim3, &sConfigOC, TIM_CHANNEL_2) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_ConfigChannel(&htim3, &sConfigOC, TIM_CHANNEL_3) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_ConfigChannel(&htim3, &sConfigOC, TIM_CHANNEL_4) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM3_Init 2 */

  /* USER CODE END TIM3_Init 2 */
  HAL_TIM_MspPostInit(&htim3);

}

/**
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 2000000;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

}

/**
  * @brief USART3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART3_UART_Init(void)
{

  /* USER CODE BEGIN USART3_Init 0 */

  /* USER CODE END USART3_Init 0 */

  /* USER CODE BEGIN USART3_Init 1 */

  /* USER CODE END USART3_Init 1 */
  huart3.Instance = USART3;
  huart3.Init.BaudRate = 115200;
  huart3.Init.WordLength = UART_WORDLENGTH_8B;
  huart3.Init.StopBits = UART_STOPBITS_1;
  huart3.Init.Parity = UART_PARITY_NONE;
  huart3.Init.Mode = UART_MODE_TX_RX;
  huart3.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart3.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart3) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART3_Init 2 */

  /* USER CODE END USART3_Init 2 */

}

/** 
  * Enable DMA controller clock
  */
static void MX_DMA_Init(void) 
{

  /* DMA controller clock enable */
  __HAL_RCC_DMA1_CLK_ENABLE();

  /* DMA interrupt init */
  /* DMA1_Channel6_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Channel6_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Channel6_IRQn);
  /* DMA1_Channel7_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Channel7_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Channel7_IRQn);

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, CSN_Pin|CE_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : PC13 */
  GPIO_InitStruct.Pin = GPIO_PIN_13;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pins : NRF_Interrupt_Pin MPU_Interrupt_Pin */
  GPIO_InitStruct.Pin = NRF_Interrupt_Pin|MPU_Interrupt_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pins : CSN_Pin CE_Pin */
  GPIO_InitStruct.Pin = CSN_Pin|CE_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* EXTI interrupt init*/
  HAL_NVIC_SetPriority(EXTI3_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI3_IRQn);

  HAL_NVIC_SetPriority(EXTI9_5_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);

}

/* USER CODE BEGIN 4 */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
	receiver_signal_loss = 1;
}

void HAL_I2C_MemTxCpltCallback(I2C_HandleTypeDef *hi2c)
{
	//printf("w");//remove
}

void HAL_I2C_MemRxCpltCallback(I2C_HandleTypeDef *hi2c)
{
	//printf("r");//remove
	dma_new_receive = 1;
}
/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  while(1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{ 
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     tex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
