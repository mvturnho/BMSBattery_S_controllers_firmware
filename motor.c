/*
 * EGG OpenSource EBike firmware
 *
 * Copyright (C) Casainho, 2015, 2106, 2017.
 *
 * Released under the GPL License, Version 3
 */

#include <stdint.h>
#include <stdio.h>
#include "stm8s_iwdg.h"
#include "stm8s_gpio.h"
#include "stm8s_tim1.h"
#include "motor.h"
#include "gpio.h"
#include "pwm.h"
#include "config.h"
#include "adc.h"
//#include "update_setpoint.h" FIXME, not needed anymore?
#include "ACAcontrollerState.h"
#include "ACAcommons.h"

uint8_t ui8_counter = 0;
uint8_t ui8_half_rotation_flag = 0;
uint8_t ui8_foc_enable_flag = 0;

uint16_t ui16_PWM_cycles_counter = 0;
uint16_t ui16_PWM_cycles_counter_6 = 0;
uint16_t ui16_PWM_cycles_counter_total = 0;

uint8_t ui8_assumed_motor_position = 0;
uint8_t ui8_sinetable_position = 0; // in 360/256 degrees
uint8_t ui8_motor_rotor_hall_position = 0; // in 360/256 degrees

uint16_t ui16_PWM_cycles_counter_total_div_4 = 0;
uint8_t ui8_interpolation_angle = 0;

uint16_t ui16_adc_current_phase_B = 0;
uint16_t ui16_adc_current_phase_B_accumulated = 0;
uint16_t ui16_adc_current_phase_B_filtered = 0;

int8_t hall_sensors;
int8_t hall_sensors_last = 0;

uint16_t ui16_ADC_iq_current_accumulated = 4096;
uint16_t ui16_iq_current_ma = 0;

void TIM1_UPD_OVF_TRG_BRK_IRQHandler(void) __interrupt(TIM1_UPD_OVF_TRG_BRK_IRQHANDLER) {
	adc_trigger();
	hall_sensors_read_and_action();

	motor_fast_loop();

	// clear the interrupt pending bit for TIM1
	TIM1_ClearITPendingBit(TIM1_IT_UPDATE);
}

void hall_sensor_init(void) {

	GPIO_Init(HALL_SENSORS__PORT,
			(GPIO_Pin_TypeDef) (HALL_SENSOR_A__PIN | HALL_SENSOR_B__PIN | HALL_SENSOR_C__PIN),
			GPIO_MODE_IN_FL_NO_IT);
}

void hall_sensors_read_and_action(void) {
	// read hall sensors signal pins and mask other pins
	hall_sensors = (GPIO_ReadInputData(HALL_SENSORS__PORT) & (HALL_SENSORS_MASK));
	if ((hall_sensors != hall_sensors_last) || (ui8_motor_state == MOTOR_STATE_COAST)) // let's run the code when motor is stopped/coast so it can pick right motor position for correct startup
	{
		updateHallOrder(hall_sensors);

		//printf("hall change! %d, %d \n", hall_sensors, hall_sensors_last );
		hall_sensors_last = hall_sensors;

		if (ui8_motor_state == MOTOR_STATE_COAST) {
			ui8_motor_state = MOTOR_STATE_RUNNING_NO_INTERPOLATION_60_DEGREES;
		}


		switch (hall_sensors) {
			case 3://rotor position 180 degree
				// full electric revolution recognized, update counters
				uint8_t_hall_case[3] = ui8_adc_read_phase_B_current();
				debug_pin_set();

				if (ui8_half_rotation_flag) {
					ui8_half_rotation_flag = 0;
					if (ui16_PWM_cycles_counter > 20) ui16_PWM_cycles_counter_total = ui16_PWM_cycles_counter;

					ui16_PWM_cycles_counter = 0;
					ui16_PWM_cycles_counter_total_div_4 = ui16_PWM_cycles_counter_total >> 2;
					ui16_motor_speed_erps = ((uint16_t) PWM_CYCLES_SECOND) / ui16_PWM_cycles_counter_total; // this division takes ~4.2us

				}

				if (ui16_motor_speed_erps == -1) {
					ui16_motor_speed_erps = 0;
				}
				// update motor state based on motor speed
#if MOTOR_TYPE == MOTOR_TYPE_Q85
				if (ui16_motor_speed_erps > 1000) {
					ui8_motor_state = MOTOR_STATE_RUNNING_INTERPOLATION_360_DEGREES;
				} else if (ui16_motor_speed_erps > 3
						) {
					ui8_motor_state = MOTOR_STATE_RUNNING_INTERPOLATION_60_DEGREES;
				} else {
					ui8_motor_state = MOTOR_STATE_RUNNING_NO_INTERPOLATION_60_DEGREES;
				}
#elif MOTOR_TYPE == MOTOR_TYPE_EUC2
				if (ui16_motor_speed_erps > 10) {
					ui8_motor_state = MOTOR_STATE_RUNNING_INTERPOLATION_60_DEGREES;
				} else {
					ui8_motor_state = MOTOR_STATE_RUNNING_NO_INTERPOLATION_60_DEGREES;
				}
#endif


				ui8_motor_rotor_hall_position = ANGLE_180;
				break;

			case 1:
				uint8_t_hall_case[4] = ui8_adc_read_phase_B_current();

				if (ui8_motor_state != MOTOR_STATE_RUNNING_INTERPOLATION_360_DEGREES) {

					ui8_motor_rotor_hall_position = ANGLE_240;
				}
				break;

			case 5: //rotor position 300 degree

				uint8_t_hall_case[5] = ui8_adc_read_phase_B_current();


				if (ui8_motor_state != MOTOR_STATE_RUNNING_INTERPOLATION_360_DEGREES) {


					ui8_motor_rotor_hall_position = ANGLE_300;
				}
				break;

			case 4: //rotor position 0 degree
				ui8_half_rotation_flag = 1;
				ui8_foc_enable_flag = 1;

				uint8_t_hall_case[0] = ui8_adc_read_phase_B_current();

				debug_pin_reset();
				if (ui8_motor_state != MOTOR_STATE_RUNNING_INTERPOLATION_360_DEGREES) {

					ui8_motor_rotor_hall_position = ANGLE_1;
				}
				break;

			case 6://rotor position 60 degree
				uint8_t_hall_case[1] = ui8_adc_read_phase_B_current();


				if (ui8_motor_state != MOTOR_STATE_RUNNING_INTERPOLATION_360_DEGREES) {

					ui8_motor_rotor_hall_position = ANGLE_60;
				}
				break;

			case 2://rotor position 120 degree
				uint8_t_hall_case[2] = ui8_adc_read_phase_B_current();


				if (ui8_motor_state != MOTOR_STATE_RUNNING_INTERPOLATION_360_DEGREES) {

					ui8_motor_rotor_hall_position = ANGLE_120;
				}
				break;

			default:
				return;
				break;
		}



		// FIXME, this basically disables this mode?
		ui16_PWM_cycles_counter_6 = 0;
	}
}

void updateCorrection() {
	
	if (ui8_duty_cycle_target > 5) {
		ui16_ADC_iq_current_accumulated -= ui16_ADC_iq_current_accumulated >> 3;
		ui16_ADC_iq_current_accumulated += ui16_adc_read_phase_B_current();
		ui16_ADC_iq_current = ui16_ADC_iq_current_accumulated >> 3; // this value is regualted to be zero by FOC 
	}
	
	if ((ui16_aca_flags & ANGLE_CORRECTION_ENABLED) != ANGLE_CORRECTION_ENABLED) {
		ui8_position_correction_value = 127; //set advance angle to neutral value
		return;
	}

	if (ui16_motor_speed_erps > 3 && ui16_BatteryCurrent > ui16_current_cal_b + 3) { //normal riding,
		if (ui16_ADC_iq_current >> 2 > 128 && ui8_position_correction_value < 143) {
			ui8_position_correction_value++;
		} else if (ui16_ADC_iq_current >> 2 < 126 && ui8_position_correction_value > 111) {
			ui8_position_correction_value--;
		}
	} else if (ui16_motor_speed_erps > 3 && ui16_BatteryCurrent < ui16_current_cal_b - 3) {//regen
		ui8_position_correction_value = 127; //set advance angle to neutral value
	} else if (ui16_motor_speed_erps < 3) {
		ui8_position_correction_value = 127; //reset advance angle at very low speed)
	}
	
}

// runs every 64us (PWM frequency)

void motor_fast_loop(void) {
	if (ui16_time_ticks_for_uart_timeout < 65530) {
		ui16_time_ticks_for_uart_timeout++;
	}
	if (ui16_time_ticks_for_speed_calculation < 65530) {
		ui16_time_ticks_for_speed_calculation++;
	} //increase SPEED Counter but avoid overflow
	if (ui16_time_ticks_for_pas_calculation < 65530) {
		ui16_time_ticks_for_pas_calculation++;
	} //increase PAS Counter but avoid overflow
	if (GPIO_ReadInputPin(PAS__PORT, PAS__PIN) && ui16_PAS_High_Counter < 65530) {
		ui16_PAS_High_Counter++;
	}


	// count number of fast loops / PWM cycles


	if (ui16_PWM_cycles_counter < PWM_CYCLES_COUNTER_MAX) {
		ui16_PWM_cycles_counter++;
		ui16_PWM_cycles_counter_6++;
	} else {


		//ui16_PWM_cycles_counter = 0;
		//ui16_PWM_cycles_counter_6 = 0;
		ui16_PWM_cycles_counter_total = 0xffff; //(SVM_TABLE_LEN_x1024) / PWM_CYCLES_COUNTER_MAX;
		//ui8_position_correction_value = 127;
		hall_sensors_last = 0;
		ui16_motor_speed_erps = 0;


		// next code is need for motor startup correctly
		ui8_motor_state = MOTOR_STATE_COAST;
		hall_sensors_read_and_action();
	}

#define DO_INTERPOLATION 1 // may be usefull when debugging
#if DO_INTERPOLATION == 1
	//  // calculate the interpolation angle
	//  // interpolation seems a problem when motor starts, so avoid to do it at very low speed
	if (ui8_motor_state == MOTOR_STATE_RUNNING_INTERPOLATION_60_DEGREES) {
		ui8_interpolation_angle = (ui16_PWM_cycles_counter_6 << 8) / ui16_PWM_cycles_counter_total;
#if MOTOR_TYPE == MOTOR_TYPE_Q85
		ui8_sinetable_position = ui8_motor_rotor_hall_position + ui8_s_motor_angle + ui8_position_correction_value + ui8_interpolation_angle;
#elif MOTOR_TYPE == MOTOR_TYPE_EUC2
		ui8_sinetable_position = ui8_motor_rotor_hall_position + ui8_s_motor_angle + ui8_position_correction_value - ui8_interpolation_angle;
#endif
	} else if (ui8_motor_state == MOTOR_STATE_RUNNING_INTERPOLATION_360_DEGREES) {
		ui8_interpolation_angle = (ui16_PWM_cycles_counter << 8) / ui16_PWM_cycles_counter_total;
#if MOTOR_TYPE == MOTOR_TYPE_Q85
		ui8_sinetable_position = ui8_motor_rotor_hall_position + ui8_s_motor_angle + ui8_position_correction_value + ui8_interpolation_angle;
#elif MOTOR_TYPE == MOTOR_TYPE_EUC2
		ui8_sinetable_position = ui8_motor_rotor_hall_position + ui8_s_motor_angle + ui8_position_correction_value - ui8_interpolation_angle;
#endif
	} else // MOTOR_STATE_COAST || MOTOR_STATE_RUNNING_NO_INTERPOLATION_60_DEGREES
#endif
	{
		ui8_interpolation_angle = 0;
		ui8_sinetable_position = ui8_motor_rotor_hall_position + ui8_s_motor_angle + ui8_position_correction_value;
	}

	//ui8_assumed_motor_position = ui8_motor_rotor_hall_position + ui8_interpolation_angle + ui8_s_motor_angle + ui8_position_correction_value - 127;
	ui8_assumed_motor_position = ui8_motor_rotor_hall_position + ui8_interpolation_angle + ui8_s_motor_angle;


	// check if FOC control is needed
	if ((ui8_foc_enable_flag) && ((ui8_assumed_motor_position) >= (ui8_correction_at_angle)) && ((ui8_assumed_motor_position) < (ui8_correction_at_angle + 4))) {
		// make sure we just execute one time per ERPS, so reset the flag
		ui8_foc_enable_flag = 0;

		ui8_variableDebugA = ui8_assumed_motor_position;
		ui8_variableDebugB = ui8_assumed_motor_position + ui8_position_correction_value - 127;
		
		updateCorrection();
	}


	//reset watchdog
	IWDG->KR = IWDG_KEY_REFRESH;
	pwm_duty_cycle_controller();
}

void watchdog_init(void) {
	IWDG_Enable();
	IWDG_WriteAccessCmd(IWDG_WriteAccess_Enable);
	IWDG_SetPrescaler(IWDG_Prescaler_4);

	//  Timeout period
	//  The timeout period can be configured through the IWDG_PR and IWDG_RLR registers. It
	//  is determined by the following equation:
	//  T = 2 * T LSI * P * R
	//  where:
	//  T = Timeout period
	//  T LSI = 1/f LSI
	//  P = 2 (PR[2:0] + 2)
	//  R = RLR[7:0]+1
	//
	//  0.0001 = 2 * (1 / 128000) * 4 * R
	//  R = 1.6 ; rounding to R = 2
	//  R = 2 means a value of reload register = 1
	IWDG_SetReload(2); // 187.5us; for some reason, a value of 1 don't work, only 2
	IWDG_ReloadCounter();
}
