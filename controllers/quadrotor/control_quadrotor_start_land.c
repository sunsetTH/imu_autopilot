/*
 * control_quadrotor_start_land.c
 *
 *  Created on: 13.07.2010
 *      Author: Laurens Mackay
 */

#include "control_quadrotor_start_land.h"
#include "global_data.h"
#include "control_quadrotor_attitude.h"
#include "control_quadrotor_position.h"
#include "remote_control.h"
#include "position_kalman.h"
#include "position_kalman2.h"
#include "position_kalman3.h"
#include "debug.h"

//20 Hz
void quadrotor_start_land_handler(uint64_t loop_start_time)
{
	/*STARTING AND LANDING FOR GUIDED MODE*/
	static uint32_t initial_wait_counter = 0;
	static uint32_t landing_wait_counter = 0;
	static uint32_t landing_counter = 0;

	//STARTING
	if ((global_data.state.status == MAV_STATE_ACTIVE || global_data.state.status
			== MAV_STATE_CRITICAL) && global_data.state.fly == FLY_WAIT_MOTORS)
	{
		initial_wait_counter += 50;

		//reset integrators otherwise we will get nick and roll on wire
		x_axis_controller.integral = 0;
		y_axis_controller.integral = 0;
		z_axis_controller.integral = 0;
		yaw_pos_controller.integral = 0;
		yaw_speed_controller.integral = 0;
		nick_controller.integral = 0;
		roll_controller.integral = 0;

		global_data.entry_critical = loop_start_time;
	}

	if (global_data.state.fly == FLY_WAIT_MOTORS && initial_wait_counter > 5000)//waited 5 seconds
	{
		global_data.state.fly = FLY_RAMP_UP;

		pid_set_parameters(&z_axis_controller,
				global_data.param[PARAM_PID_POS_Z_P],
				global_data.param[PARAM_PID_POS_Z_I], 0,
				global_data.param[PARAM_PID_POS_Z_AWU]);

		//reset integrators at start
		x_axis_controller.integral = 0;
		y_axis_controller.integral = 0;
		z_axis_controller.integral = 0;
		yaw_pos_controller.integral = 0;
		yaw_speed_controller.integral = 0;
		nick_controller.integral = 0;
		roll_controller.integral = 0;

		global_data.entry_critical = loop_start_time;

		debug_message_buffer("STARTING ramp up");
	}

	if (global_data.state.fly == FLY_STARTING && global_data.position.z
			< global_data.position_setpoint.z + 0.1)
	{
		//WE Reached almost the height of z-setpoint-> we are realy flying
		global_data.state.fly = FLY_FLYING;
		debug_message_buffer("STARTING finished now FLYING");
	}
	//LANDING
	// read out infraread just for landing
	//			if ((global_data.state.ground_distance_ok == 1)
	//					&& (global_data.state.fly == FLY_GROUNDED
	//							|| global_data.state.fly == FLY_LANDING
	//							|| global_data.state.fly == FLY_RAMP_DOWN))
	//			{
	//
	//				global_data.position.z = -global_data.ground_distance;
	//			}

	if (global_data.state.fly == FLY_SINKING)
	{
//		global_data.position_setpoint.z = -0.4;// is done already in communication.h
		if (global_data.position.z > global_data.position_setpoint.z - 0.1)
		{
			global_data.state.fly = FLY_WAIT_LANDING;
			debug_message_buffer("LANDING wait");
		}
		if(global_data.position.z>-0.3){
			global_data.state.fly = FLY_RAMP_DOWN;
			debug_message_buffer("LANDING ramp down DIRECT");
		}
	}
	if (global_data.state.fly == FLY_WAIT_LANDING)
	{
		landing_wait_counter += 50;
		if (landing_wait_counter > 5000)
		{
			global_data.state.fly = FLY_LANDING;
			global_data.thrust_landing = global_data.motor_thrust_actual * 0.97;
			debug_message_buffer("LANDING with 97%% thrust");
		}
		if(global_data.position.z>-0.3){
			global_data.state.fly = FLY_RAMP_DOWN;
			debug_message_buffer("LANDING ramp down DIRECT");
		}
	}
	if (global_data.state.fly == FLY_LANDING)
	{
		landing_counter += 50;
		if (landing_counter > 3000 || (global_data.ground_distance_unfiltered
				< 0.3 && global_data.state.ground_distance_ok == 1)
				|| global_data.position.z>-0.3)
		{
			global_data.state.fly = FLY_RAMP_DOWN;
			debug_message_buffer("LANDING ramp down");
		}
	}

	if (global_data.state.status == MAV_STATE_STANDBY)
	{
		global_data.state.fly = FLY_GROUNDED;
	}
	if (global_data.state.fly == FLY_GROUNDED)
	{
		initial_wait_counter = 0;
		landing_wait_counter = 0;
		landing_counter = 0;
		global_data.thrust_hover_offset = 0.0f;
	}
}
float quadrotor_start_land_motor_thrust()
{
	float motor_thrust = 0;
	if (global_data.state.fly == FLY_GROUNDED)
	{
		motor_thrust = 0;
	}
	else
	{
		if (global_data.state.fly == FLY_RAMP_UP)
		{
			global_data.thrust_hover_offset
					+= global_data.thrust_calibration_increment;
			//this was commented out for competition
//			if (global_data.ground_distance_unfiltered > 0.3)
//			{
//				//we took of and reached 0.3m. save this hover offset.
//				global_data.state.fly = FLY_STARTING;
//
//				debug_message_buffer_sprintf(
//						"STARTING ramp aborted. hoveroffset*1000=%i", 1000
//								* global_data.thrust_hover_offset);
//
//			}
			// check limit:
			if (global_data.thrust_hover_offset > global_data.param[PARAM_POSITION_HOVER_THRUST])
			{
				global_data.thrust_hover_offset = global_data.param[PARAM_POSITION_HOVER_THRUST];
				global_data.state.fly = FLY_STARTING;

				debug_message_buffer_sprintf(
						"STARTING ramp finished at %i%% of max thrust",global_data.param[PARAM_POSITION_HOVER_THRUST]*100);
			}
		}


		// Special Limitation for z Control in this mode don't allow negative output
		if (global_data.position_control_output.z
				< 0)
		{
			global_data.position_control_output.z
					= 0;

			//Inform Controller about saturation. TODO Check if this works and do it for all other controllers.
			z_axis_controller.saturated=1;

	//		debug_message_buffer_sprintf(
	//				"zcontrol output - limited would be*1000=%i", (uint32_t) 1000
	//						* global_data.position_control_output.z);
		}



		motor_thrust = (global_data.thrust_hover_offset
				+ global_data.position_control_output.z);
		if (global_data.state.fly == FLY_LANDING)
		{
			motor_thrust = global_data.thrust_landing;
		}
		if (global_data.state.fly == FLY_RAMP_DOWN)
		{
			global_data.thrust_landing -= 1.5
					* global_data.thrust_calibration_increment;
			motor_thrust = global_data.thrust_landing;
			// check if we are at 0
			if (global_data.thrust_landing <= 0)
			{
				global_data.thrust_landing = 0;
				global_data.state.fly = FLY_GROUNDED;

				debug_message_buffer("LANDING finished now GROUNDED");
			}
		}
		global_data.motor_thrust_actual = motor_thrust;

		//Security: thrust never higher than remote control
		if (motor_thrust > global_data.gas_remote)
		{
			motor_thrust = global_data.gas_remote;
		}
	}
	return motor_thrust;
}
