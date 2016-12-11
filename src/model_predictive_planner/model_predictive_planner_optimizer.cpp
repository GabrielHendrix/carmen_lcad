/*
 * model_predictive_planner_optimizer.cpp
 *
 *  Created on: Jun 23, 2016
 *      Author: lcad
 */


#include <stdio.h>
#include <iostream>
#include <math.h>
#include <gsl/gsl_errno.h>
#include <gsl/gsl_spline.h>
#include <gsl/gsl_multimin.h>

#include <carmen/collision_detection.h>

#include "model/robot_state.h"
#include "model/global_state.h"
#include "util.h"
#include "model_predictive_planner_optimizer.h"

bool use_obstacles = true;


void
compute_a_and_t_from_s(double s, double target_v,
		TrajectoryLookupTable::TrajectoryDimensions target_td,
		TrajectoryLookupTable::TrajectoryControlParameters &tcp_seed,
		ObjectiveFunctionParams *params)
{
	// https://www.wolframalpha.com/input/?i=solve+s%3Dv*x%2B0.5*a*x%5E2
	tcp_seed.tt = (2.0 * s) / (target_v + target_td.v_i);
	double a = (target_v - target_td.v_i) / tcp_seed.tt;
	if (a > GlobalState::robot_config.maximum_acceleration_forward)
	{
		a = GlobalState::robot_config.maximum_acceleration_forward;
		double v = target_td.v_i;
		tcp_seed.tt = (sqrt(2.0 * a * s + v * v) - v) / a;
	}
	else if (a < -GlobalState::robot_config.maximum_deceleration_forward)
	{
		a = -GlobalState::robot_config.maximum_deceleration_forward;
		double v = target_td.v_i;
		tcp_seed.tt = -(sqrt(2.0 * a * s + v * v) + v) / a;
	}
//	printf("s %.1lf, a %.3lf, t %.1lf, tv %.1lf, vi %.1lf\n", s, a, tcp_seed.tt, target_v, target_td.v_i);
	params->suitable_tt = tcp_seed.tt;
	params->suitable_acceleration = tcp_seed.a = a;
}


TrajectoryLookupTable::TrajectoryControlParameters
fill_in_tcp(const gsl_vector *x, ObjectiveFunctionParams *params)
{
	TrajectoryLookupTable::TrajectoryControlParameters tcp;

	if (x->size == 4)
	{
		tcp.has_k1 = true;

		tcp.k1 = gsl_vector_get(x, 0);
		tcp.k2 = gsl_vector_get(x, 1);
		tcp.k3 = gsl_vector_get(x, 2);
		if (params->optimize_time == OPTIMIZE_TIME)
		{
			tcp.tt = gsl_vector_get(x, 3);
			tcp.a = params->suitable_acceleration;
		}
		if (params->optimize_time == OPTIMIZE_DISTANCE)
		{
			tcp.s = gsl_vector_get(x, 3);
			compute_a_and_t_from_s(tcp.s, params->target_v, *params->target_td, tcp, params);
			tcp.a = params->suitable_acceleration;
			tcp.tt = params->suitable_tt;
		}
		if (params->optimize_time == OPTIMIZE_ACCELERATION)
		{
			tcp.a = gsl_vector_get(x, 3);
			tcp.tt = params->suitable_tt;
		}
	}
	else
	{
		tcp.has_k1 = false;

		tcp.k2 = gsl_vector_get(x, 0);
		tcp.k3 = gsl_vector_get(x, 1);
		if (params->optimize_time == OPTIMIZE_TIME)
		{
			tcp.tt = gsl_vector_get(x, 2);
			tcp.a = params->suitable_acceleration;
		}
		if (params->optimize_time == OPTIMIZE_DISTANCE)
		{
			tcp.s = gsl_vector_get(x, 2);
			compute_a_and_t_from_s(tcp.s, params->target_v, *params->target_td, tcp, params);
			tcp.a = params->suitable_acceleration;
			tcp.tt = params->suitable_tt;
		}
		if (params->optimize_time == OPTIMIZE_ACCELERATION)
		{
			tcp.a = gsl_vector_get(x, 2);
			tcp.tt = params->suitable_tt;
		}
	}
	tcp.vf = params->tcp_seed->vf;
	tcp.sf = params->tcp_seed->sf;

	if (tcp.tt < 0.2) // o tempo nao pode ser pequeno demais
		tcp.tt = 0.2;
	if (tcp.a < -GlobalState::robot_config.maximum_deceleration_forward) // a aceleracao nao pode ser negativa demais
		tcp.a = -GlobalState::robot_config.maximum_deceleration_forward;
//	if (tcp.a > GlobalState::robot_config.maximum_acceleration_forward) // a aceleracao nao pode ser negativa demais
//		tcp.a = GlobalState::robot_config.maximum_acceleration_forward;

//	double max_phi_during_planning = 1.8 * GlobalState::robot_config.max_phi;
//	if (tcp.has_k1)
//	{
//		if (tcp.k1 > max_phi_during_planning)
//			tcp.k1 = max_phi_during_planning;
//		else if (tcp.k1 < -max_phi_during_planning)
//			tcp.k1 = -max_phi_during_planning;
//	}
//
//	if (tcp.k2 > max_phi_during_planning)
//		tcp.k2 = max_phi_during_planning;
//	else if (tcp.k2 < -max_phi_during_planning)
//		tcp.k2 = -max_phi_during_planning;
//
//	if (tcp.k3 > max_phi_during_planning)
//		tcp.k3 = max_phi_during_planning;
//	else if (tcp.k3 < -max_phi_during_planning)
//		tcp.k3 = -max_phi_during_planning;

	tcp.valid = true;

	return (tcp);
}


void
move_path_to_current_robot_pose(vector<carmen_ackerman_path_point_t> &path, Pose *localizer_pose)
{
	for (std::vector<carmen_ackerman_path_point_t>::iterator it = path.begin(); it != path.end(); ++it)
	{
		double x = localizer_pose->x + it->x * cos(localizer_pose->theta) - it->y * sin(localizer_pose->theta);
		double y = localizer_pose->y + it->x * sin(localizer_pose->theta) + it->y * cos(localizer_pose->theta);
		it->x = x;
		it->y = y;
		it->theta = carmen_normalize_theta(it->theta + localizer_pose->theta);
	}
}


inline
double
dist(carmen_ackerman_path_point_t v, carmen_ackerman_path_point_t w)
{
	return sqrt((carmen_square(v.x - w.x) + carmen_square(v.y - w.y)));
}


inline
double
ortho_dist(carmen_ackerman_path_point_t v, carmen_ackerman_path_point_t w)
{
	double d_x = v.x - w.x;
	double d_y = v.y - w.y;
	double theta = atan2(d_y, d_x);
	double d = sqrt(d_x * d_x + d_y * d_y) * sin(fabs(v.theta - theta));
	return (d);
}


inline
double
dist2(carmen_ackerman_path_point_t v, carmen_ackerman_path_point_t w)
{
	return (carmen_square(v.x - w.x) + carmen_square(v.y - w.y));
}


carmen_ackerman_path_point_t
move_to_front_axle(carmen_ackerman_path_point_t pose)
{
	double L = GlobalState::robot_config.distance_between_front_and_rear_axles;
	carmen_ackerman_path_point_t pose_moved = pose;
	pose_moved.x += L * cos(pose.theta);
	pose_moved.y += L * sin(pose.theta);

	return (pose_moved);
}


double
compute_path_to_lane_distance(ObjectiveFunctionParams *my_params, vector<carmen_ackerman_path_point_t> &path)
{
	double distance = 0.0;
	double total_distance = 0.0;
	double total_points = 0.0;

	for (unsigned int i = 0; i < my_params->detailed_lane.size(); i += 3)
	{
		if (my_params->path_point_nearest_to_lane.at(i) < path.size())
		{
//			distance = ortho_dist(move_to_front_axle(path.at(my_params->path_point_nearest_to_lane.at(i))),
//										 my_params->detailed_lane.at(i));
			distance = dist(path.at(my_params->path_point_nearest_to_lane.at(i)),
										 my_params->detailed_lane.at(i));
			total_points += 1.0;
		}
		else
			distance = 0.0;

		total_distance += distance * distance;
	}
	return (total_distance / total_points);
}


void
compute_path_points_nearest_to_lane(ObjectiveFunctionParams *param, vector<carmen_ackerman_path_point_t> &path)
{
	double distance = 0.0;
	double min_dist;
	int index = 0;
	param->path_point_nearest_to_lane.clear();
	param->path_size = path.size();
	for (unsigned int i = 0; i < param->detailed_lane.size(); i++)
	{
		// consider the first point as the nearest one
		min_dist = dist(path.at(index), param->detailed_lane.at(i));

		for (unsigned int j = index; j < path.size(); j++)
		{
//			distance = dist(move_to_front_axle(path.at(j)), param->detailed_lane.at(i));
			distance = dist(path.at(j), param->detailed_lane.at(i));

			if (distance < min_dist)
			{
				min_dist = distance;
				index = j;
			}
		}
		param->path_point_nearest_to_lane.push_back(index);
	}
}


inline carmen_ackerman_path_point_t
move_path_point_to_map_coordinates(const carmen_ackerman_path_point_t point, double displacement)
{
	carmen_ackerman_path_point_t path_point_in_map_coords;
	double coss, sine;

	sincos(point.theta, &sine, &coss);
	double x_disp = point.x + displacement * coss;
	double y_disp = point.y + displacement * sine;

	sincos(GlobalState::localizer_pose->theta, &sine, &coss);
	path_point_in_map_coords.x = (GlobalState::localizer_pose->x - GlobalState::distance_map->config.x_origin + x_disp * coss - y_disp * sine) / GlobalState::distance_map->config.resolution;
	path_point_in_map_coords.y = (GlobalState::localizer_pose->y - GlobalState::distance_map->config.y_origin + x_disp * sine + y_disp * coss) / GlobalState::distance_map->config.resolution;

	return (path_point_in_map_coords);
}


//double
//distance_from_traj_point_to_obstacle(carmen_ackerman_path_point_t point, double displacement, double min_dist)
//{
//	// Move path point to map coordinates
//	carmen_ackerman_path_point_t path_point_in_map_coords =	move_path_point_to_map_coordinates(point, displacement);
//	int x_map_cell = (int) round(path_point_in_map_coords.x);
//	int y_map_cell = (int) round(path_point_in_map_coords.y);
//
//	// Os mapas de carmen sao orientados a colunas, logo a equacao eh como abaixo
//	int index = y_map_cell + GlobalState::distance_map->config.y_size * x_map_cell;
//	if (index < 0 || index >= GlobalState::distance_map->size)
//		return (min_dist);
//
//	double dx = (double) GlobalState::distance_map->complete_x_offset[index] + (double) x_map_cell - path_point_in_map_coords.x;
//	double dy = (double) GlobalState::distance_map->complete_y_offset[index] + (double) y_map_cell - path_point_in_map_coords.y;
//
//	double distance_in_map_coordinates = sqrt(dx * dx + dy * dy);
//	double distance = distance_in_map_coordinates * GlobalState::distance_map->config.resolution;
//
//	return (distance);
//}


//double
//compute_distance_to_closest_obstacles(carmen_ackerman_path_point_t path_pose, double circle_radius)
//{
//	int number_of_point = 4;
//	double displacement_inc = GlobalState::robot_config.distance_between_front_and_rear_axles / (number_of_point - 2);
//	double displacement = 0.0;
//	double proximity_to_obstacles = 0.0;
//
//	for (int i = -1; i < number_of_point; i++)
//	{
//		displacement = displacement_inc * i;
//
//		if (i < 0)
//			displacement = -GlobalState::robot_config.distance_between_rear_car_and_rear_wheels;
//
//		if (i == number_of_point - 1)
//			displacement = GlobalState::robot_config.distance_between_front_and_rear_axles + GlobalState::robot_config.distance_between_front_car_and_front_wheels;
//
//		double distance = distance_from_traj_point_to_obstacle(path_pose, displacement, circle_radius);
//		double delta = distance - circle_radius;
//		if (delta < 0.0)
//			proximity_to_obstacles += delta * delta;
//	}
//
//	return (proximity_to_obstacles);
//}


double
compute_proximity_to_obstacles_using_distance_map(vector<carmen_ackerman_path_point_t> path)
{
	double proximity_to_obstacles_for_path = 0.0;
	double circle_radius = (GlobalState::robot_config.width + 1.6) / 2.0; // metade da largura do carro + um espacco de guarda
	carmen_point_t localizer = {GlobalState::localizer_pose->x, GlobalState::localizer_pose->y, GlobalState::localizer_pose->theta};
	for (unsigned int i = 0; i < path.size(); i += 1)
	{
		carmen_point_t point_to_check = {path[i].x, path[i].y, path[i].theta};
		proximity_to_obstacles_for_path += carmen_obstacle_avoider_compute_car_distance_to_closest_obstacles(&localizer,
				point_to_check, GlobalState::robot_config, GlobalState::distance_map, circle_radius);
	}
	return (proximity_to_obstacles_for_path);
}


double
my_f(const gsl_vector *x, void *params)
{
	ObjectiveFunctionParams *my_params = (ObjectiveFunctionParams *) params;

	TrajectoryLookupTable::TrajectoryControlParameters tcp = fill_in_tcp(x, my_params);
	TrajectoryLookupTable::TrajectoryDimensions td;

	vector<carmen_ackerman_path_point_t> path = simulate_car_from_parameters(td, tcp, my_params->target_td->v_i, my_params->target_td->phi_i, false);
	my_params->tcp_seed->vf = tcp.vf;
	my_params->tcp_seed->sf = tcp.sf;

	double result = ((td.dist - my_params->target_td->dist) * (td.dist - my_params->target_td->dist) / my_params->distance_by_index +
			(carmen_normalize_theta(td.theta) - my_params->target_td->theta) * (carmen_normalize_theta(td.theta) - my_params->target_td->theta) / (my_params->theta_by_index * 0.2) +
			(carmen_normalize_theta(td.d_yaw) - my_params->target_td->d_yaw) * (carmen_normalize_theta(td.d_yaw) - my_params->target_td->d_yaw) / (my_params->d_yaw_by_index * 0.2));
	my_params->plan_cost = result;

	return (result);
}


void
my_df(const gsl_vector *v, void *params, gsl_vector *df)
{
	double f_x = my_f(v, params);

	double h = 0.00005;

	gsl_vector *x_h;
	x_h = gsl_vector_alloc(3);

	gsl_vector_set(x_h, 0, gsl_vector_get(v, 0) + h);
	gsl_vector_set(x_h, 1, gsl_vector_get(v, 1));
	gsl_vector_set(x_h, 2, gsl_vector_get(v, 2));
	double f_x_h = my_f(x_h, params);
	double d_f_x_h = (f_x_h - f_x) / h;

	gsl_vector_set(x_h, 0, gsl_vector_get(v, 0));
	gsl_vector_set(x_h, 1, gsl_vector_get(v, 1) + h);
	gsl_vector_set(x_h, 2, gsl_vector_get(v, 2));
	double f_y_h = my_f(x_h, params);
	double d_f_y_h = (f_y_h - f_x) / h;

	gsl_vector_set(x_h, 0, gsl_vector_get(v, 0));
	gsl_vector_set(x_h, 1, gsl_vector_get(v, 1));
	gsl_vector_set(x_h, 2, gsl_vector_get(v, 2) + h);
	double f_z_h = my_f(x_h, params);
	double d_f_z_h = 150.0 * (f_z_h - f_x) / h;

	gsl_vector_set(df, 0, d_f_x_h);
	gsl_vector_set(df, 1, d_f_y_h);
	gsl_vector_set(df, 2, d_f_z_h);

	gsl_vector_free(x_h);
}


void
my_fdf(const gsl_vector *x, void *params, double *f, gsl_vector *df)
{
	*f = my_f(x, params);
	my_df(x, params, df);
}


double
my_g(const gsl_vector *x, void *params)
{
	ObjectiveFunctionParams *my_params = (ObjectiveFunctionParams *) params;

	TrajectoryLookupTable::TrajectoryControlParameters tcp = fill_in_tcp(x, my_params);
	TrajectoryLookupTable::TrajectoryDimensions td;

	vector<carmen_ackerman_path_point_t> path = simulate_car_from_parameters(td, tcp, my_params->target_td->v_i, my_params->target_td->phi_i, false);

	double path_to_lane_distance = 0.0;
	if (my_params->use_lane && (my_params->detailed_lane.size() > 0))
	{
		if (path.size() != my_params->path_size)
		{
			compute_path_points_nearest_to_lane(my_params, path);
			path_to_lane_distance = compute_path_to_lane_distance(my_params, path);
		}
		else
			path_to_lane_distance = compute_path_to_lane_distance(my_params, path);
	}

	double proximity_to_obstacles = 0.0;

	if (use_obstacles && GlobalState::distance_map != NULL)
		proximity_to_obstacles = compute_proximity_to_obstacles_using_distance_map(path);

	my_params->tcp_seed->vf = tcp.vf;
	my_params->tcp_seed->sf = tcp.sf;

	my_params->plan_cost = sqrt((td.dist - my_params->target_td->dist) * (td.dist - my_params->target_td->dist) / my_params->distance_by_index +
			(carmen_normalize_theta(td.theta) - my_params->target_td->theta) * (carmen_normalize_theta(td.theta) - my_params->target_td->theta) / (my_params->theta_by_index * 0.2) +
			(carmen_normalize_theta(td.d_yaw) - my_params->target_td->d_yaw) * (carmen_normalize_theta(td.d_yaw) - my_params->target_td->d_yaw) / (my_params->d_yaw_by_index * 0.2));

	double w1, w2, w3, w4, w5;
	w1 = 10.0; w2 = 15.0; w3 = 15.0; w4 = 3.0; w5 = 10.0;

	double result = (
			w1 * (td.dist - my_params->target_td->dist) * (td.dist - my_params->target_td->dist) / my_params->distance_by_index +
			w2 * (carmen_normalize_theta(td.theta - my_params->target_td->theta) * carmen_normalize_theta(td.theta - my_params->target_td->theta)) / my_params->theta_by_index +
			w3 * (carmen_normalize_theta(td.d_yaw - my_params->target_td->d_yaw) * carmen_normalize_theta(td.d_yaw - my_params->target_td->d_yaw)) / my_params->d_yaw_by_index +
			w4 * path_to_lane_distance + // já é quandrática
			w5 * proximity_to_obstacles); // já é quandrática

	//printf("s %lf, tdc %.2lf, tdd %.2f, a %.2lf\n", tcp.s, td.dist, my_params->target_td->dist, tcp.a);
	return (result);
}


void
my_dg(const gsl_vector *v, void *params, gsl_vector *df)
{
	double g_x = my_g(v, params);

	double h = 0.00005;//<<< 0.00001

	gsl_vector *x_h;
	x_h = gsl_vector_alloc(4);

	gsl_vector_set(x_h, 0, gsl_vector_get(v, 0) + h);
	gsl_vector_set(x_h, 1, gsl_vector_get(v, 1));
	gsl_vector_set(x_h, 2, gsl_vector_get(v, 2));
	gsl_vector_set(x_h, 3, gsl_vector_get(v, 3));
	double g_x_h = my_g(x_h, params);
	double d_g_x_h = (g_x_h - g_x) / h;

	gsl_vector_set(x_h, 0, gsl_vector_get(v, 0));
	gsl_vector_set(x_h, 1, gsl_vector_get(v, 1) + h);
	gsl_vector_set(x_h, 2, gsl_vector_get(v, 2));
	gsl_vector_set(x_h, 3, gsl_vector_get(v, 3));
	double g_y_h = my_g(x_h, params);
	double d_g_y_h = (g_y_h - g_x) / h;

	gsl_vector_set(x_h, 0, gsl_vector_get(v, 0));
	gsl_vector_set(x_h, 1, gsl_vector_get(v, 1));
	gsl_vector_set(x_h, 2, gsl_vector_get(v, 2) + h);
	gsl_vector_set(x_h, 3, gsl_vector_get(v, 3));
	double g_z_h = my_g(x_h, params);
	double d_g_z_h = (g_z_h - g_x) / h;

	gsl_vector_set(x_h, 0, gsl_vector_get(v, 0));
	gsl_vector_set(x_h, 1, gsl_vector_get(v, 1));
	gsl_vector_set(x_h, 2, gsl_vector_get(v, 2));
	gsl_vector_set(x_h, 3, gsl_vector_get(v, 3) + h);
	double g_w_h = my_g(x_h, params);
	double d_g_w_h = 200.0 * (g_w_h - g_x) / h;

	gsl_vector_set(df, 0, d_g_x_h);
	gsl_vector_set(df, 1, d_g_y_h);
	gsl_vector_set(df, 2, d_g_z_h);
	gsl_vector_set(df, 3, d_g_w_h);

	gsl_vector_free(x_h);
}


/* Compute both g and df together. for while df equal to dg */
void
my_gdf(const gsl_vector *x, void *params, double *g, gsl_vector *dg)
{
	*g = my_g(x, params);
	my_dg(x, params, dg);
}


double
compute_suitable_acceleration(double tt, TrajectoryLookupTable::TrajectoryDimensions target_td, double target_v)
{
	// (i) S = Vo*t + 1/2*a*t^2
	// (ii) dS/dt = Vo + a*t
	// dS/dt = 0 => máximo ou mínimo de S => 0 = Vo + a*t; a*t = -Vo; (iii) a = -Vo/t; (iv) t = -Vo/a
	// Se "a" é negativa, dS/dt = 0 é um máximo de S
	// Logo, como S = target_td.dist, "a" e "t" tem que ser tais em (iii) e (iv) que permitam que
	// target_td.dist seja alcançada.
	//
	// O valor de maxS pode ser computado substituindo (iv) em (i):
	// maxS = Vo*-Vo/a + 1/2*a*(-Vo/a)^2 = -Vo^2/a + 1/2*Vo^2/a = -1/2*Vo^2/a

	if (target_v < 0.0)
		target_v = 0.0;

	double a = (target_v - target_td.v_i) / tt;

	if (a > 0.0)
	{
		if (a > GlobalState::robot_config.maximum_acceleration_forward)
			a = GlobalState::robot_config.maximum_acceleration_forward;
		return (a);
	}

	if ((-0.5 * (target_td.v_i * target_td.v_i) / a) > target_td.dist * 1.1)
	{
		return (a);
	}
	else
	{
		while ((-0.5 * (target_td.v_i * target_td.v_i) / a) <= target_td.dist * 1.1)
			a *= 0.95;

		return (a);
	}
}


//void
//estimate_piramidal_profile()
//{
//TODO modo para fazer profile piramide INCOMPLETO
//		double estimate_vf = sqrt((target_td.dist / 2.0) * GlobalState::robot_config.maximum_acceleration_forward);
//		a = (estimate_vf - target_td.v_i) / tcp_seed.tt;
//		params.optimize_time = false;
//}


void
compute_suitable_acceleration_and_tt(ObjectiveFunctionParams &params,
		TrajectoryLookupTable::TrajectoryControlParameters &tcp_seed,
		TrajectoryLookupTable::TrajectoryDimensions target_td, double target_v)
{
	// (i) S = Vo*t + 1/2*a*t^2
	// (ii) dS/dt = Vo + a*t
	// dS/dt = 0 => máximo ou mínimo de S => 0 = Vo + a*t; a*t = -Vo; (iii) a = -Vo/t; (iv) t = -Vo/a
	// Se "a" é negativa, dS/dt = 0 é um máximo de S
	// Logo, como S = target_td.dist, "a" e "t" tem que ser tais em (iii) e (iv) que permitam que
	// target_td.dist seja alcançada.
	//
	// O valor de maxS pode ser computado substituindo (iv) em (i):
	// maxS = Vo*-Vo/a + 1/2*a*(-Vo/a)^2 = -Vo^2/a + 1/2*Vo^2/a = -1/2*Vo^2/a
	//
	// Se estou co velocidade vi e quero chagar a vt, sendo que vt < vi, a eh negativo. O tempo, tt, para
	// ir de vi a vt pode ser derivado de dS/dt = Vo + a*t -> vt = vi + a*tt; a*tt = vt - vi; tt = (vt - vi) / a

	if (target_v < 0.0)
		target_v = 0.0;
	params.optimize_time = OPTIMIZE_DISTANCE;
//	params.optimize_time = OPTIMIZE_TIME;

	if (params.optimize_time == OPTIMIZE_DISTANCE)
	{
		compute_a_and_t_from_s(tcp_seed.s, target_v, target_td, tcp_seed, &params);
	}
	else
	{
		double a = (target_v - target_td.v_i) / tcp_seed.tt;
		double tt;


		if (a == 0) //avoid div by zero and plan v = 0 e vi = 0
		{
			tt = tcp_seed.tt;

			if (target_td.v_i == 0.0)
				params.optimize_time = OPTIMIZE_ACCELERATION;
			else
				params.optimize_time = OPTIMIZE_TIME;
		}
		else
			tt = (target_v - target_td.v_i) / a;


		if (a > 0.0)
		{
			params.optimize_time = OPTIMIZE_TIME;
			if (a > GlobalState::robot_config.maximum_acceleration_forward)
				a = GlobalState::robot_config.maximum_acceleration_forward;
		}

		if (a < 0.0)
		{
			params.optimize_time = OPTIMIZE_ACCELERATION;
			if (a < -GlobalState::robot_config.maximum_deceleration_forward)
			{
				a = -GlobalState::robot_config.maximum_deceleration_forward;
				tt = (target_v - target_td.v_i) / a;
			}
		}

		if (tt > 10.0)
			tt = 10.0;
		else if (tt < 2.0)
			tt = 2.0;

		params.suitable_tt = tcp_seed.tt = tt;
		params.suitable_acceleration = tcp_seed.a = a;
	//	printf("a %lf, tt %lf\n", a, tt);
	}
}


void
get_optimization_params(double target_v,
		TrajectoryLookupTable::TrajectoryControlParameters &tcp_seed,
		TrajectoryLookupTable::TrajectoryDimensions &target_td,
		ObjectiveFunctionParams &params)
{
	params.distance_by_index = fabs(get_distance_by_index(N_DIST - 1));
	params.theta_by_index = fabs(get_theta_by_index(N_THETA - 1));
	params.d_yaw_by_index = fabs(get_d_yaw_by_index(N_D_YAW - 1));
	params.target_td = &target_td;
	params.tcp_seed = &tcp_seed;
	params.target_v = target_v;
	params.path_size = 0;
}


void
get_missing_k1(const TrajectoryLookupTable::TrajectoryDimensions& target_td,
		TrajectoryLookupTable::TrajectoryControlParameters& tcp_seed)
{
	double knots_x[3] = { 0.0, tcp_seed.tt / 2.0, tcp_seed.tt };
	double knots_y[3];
	knots_y[0] = target_td.phi_i;
	knots_y[1] = tcp_seed.k2;
	knots_y[2] = tcp_seed.k3;
	gsl_interp_accel* acc = gsl_interp_accel_alloc();
	const gsl_interp_type* type = gsl_interp_cspline;
	gsl_spline* phi_spline = gsl_spline_alloc(type, 3);
	gsl_spline_init(phi_spline, knots_x, knots_y, 3);
	tcp_seed.k1 = gsl_spline_eval(phi_spline, tcp_seed.tt / 4.0, acc);
	tcp_seed.has_k1 = true;
	gsl_spline_free(phi_spline);
	gsl_interp_accel_free(acc);
}


void
print_tcp(TrajectoryLookupTable::TrajectoryControlParameters tcp)
{
	printf("v %d, tt %1.2lf, a %1.2lf, h_k1 %d, k1 %1.2lf, k2 %1.2lf, k3 %1.2lf, vf %2.2lf\n",
			tcp.valid, tcp.tt, tcp.a, tcp.has_k1, tcp.k1, tcp.k2, tcp.k3, tcp.vf);
}


void
print_td(TrajectoryLookupTable::TrajectoryDimensions td)
{
	printf("dist %2.2lf, theta %1.2lf, d_yaw %1.2lf, phi_i %1.2lf, v_i %2.2lf\n",
			td.dist, td.theta, td.d_yaw, td.phi_i, td.v_i);
}


TrajectoryLookupTable::TrajectoryControlParameters
optimized_lane_trajectory_control_parameters(TrajectoryLookupTable::TrajectoryControlParameters &tcp_seed,
		TrajectoryLookupTable::TrajectoryDimensions target_td, double target_v, ObjectiveFunctionParams params)
{
	//	vector<carmen_ackerman_path_point_t> path = simulate_car_from_parameters(target_td, tcp_seed, target_td.v_i, target_td.phi_i, false);

	//	FILE *lane_file = fopen("gnu_tests/gnuplot_lane.txt", "w");
	//	print_lane(params.detailed_lane, lane_file);
	//	fclose(lane_file);
	//	char path_name[20];
	//	sprintf(path_name, "path/%d.txt", 0);
	//	FILE *path_file = fopen("gnu_tests/gnuplot_traj.txt", "w");
	//	print_lane(path,path_file);
	//	fclose(path_file);
	//	getchar();

	get_optimization_params(target_v, tcp_seed, target_td, params);
	//print_tcp(tcp_seed);
	//print_td(target_td);
	//printf("tv = %lf\n", target_v);

	gsl_multimin_function_fdf my_func;

	my_func.n = 4;
	my_func.f = my_g;
	my_func.df = my_dg;
	my_func.fdf = my_gdf;
	my_func.params = &params;

	if (!tcp_seed.has_k1)
		get_missing_k1(target_td, tcp_seed);

	/* Starting point, x */
	gsl_vector *x = gsl_vector_alloc(4);
	gsl_vector_set(x, 0, tcp_seed.k1);
	gsl_vector_set(x, 1, tcp_seed.k2);
	gsl_vector_set(x, 2, tcp_seed.k3);
	if (params.optimize_time == OPTIMIZE_TIME)
		gsl_vector_set(x, 3, tcp_seed.tt);
	if (params.optimize_time == OPTIMIZE_DISTANCE)
		gsl_vector_set(x, 3, tcp_seed.s);
	if (params.optimize_time == OPTIMIZE_ACCELERATION)
		gsl_vector_set(x, 3, tcp_seed.a);

	const gsl_multimin_fdfminimizer_type *T = gsl_multimin_fdfminimizer_conjugate_fr;
	gsl_multimin_fdfminimizer *s = gsl_multimin_fdfminimizer_alloc(T, 4);

	// int gsl_multimin_fdfminimizer_set (gsl_multimin_fdfminimizer * s, gsl_multimin_function_fdf * fdf, const gsl_vector * x, double step_size, double tol)
	gsl_multimin_fdfminimizer_set(s, &my_func, x, 0.01, 0.1);

	size_t iter = 0;
	int status;
	do
	{
		iter++;

		status = gsl_multimin_fdfminimizer_iterate(s);

		if (status == GSL_ENOPROG) // minimizer is unable to improve on its current estimate, either due to numerical difficulty or a genuine local minimum
			break;

		status = gsl_multimin_test_gradient(s->gradient, 0.16); // esta funcao retorna GSL_CONTINUE ou zero

		//	--Debug with GNUPLOT

		//		TrajectoryLookupTable::TrajectoryControlParameters tcp_temp = fill_in_tcp(s->x, &params);
		//		char path_name[20];
		//		sprintf(path_name, "path/%lu.txt", iter);
		//		FILE *path_file = fopen("gnu_tests/gnuplot_traj.txt", "w");
		//		print_lane(simulate_car_from_parameters(target_td, tcp_temp, target_td.v_i, target_td.phi_i, true), path_file);
		//		fclose(path_file);
		//		printf("Estou na: %lu iteracao, sf: %lf  \n", iter, s->f);
		//		getchar();
		//	--
//		params.suitable_acceleration = compute_suitable_acceleration(gsl_vector_get(x, 3), target_td, target_v);

	} while (/*(s->f > MAX_LANE_DIST) &&*/ (status == GSL_CONTINUE) && (iter < 50));

//	printf("iter = %ld\n", iter);

	TrajectoryLookupTable::TrajectoryControlParameters tcp = fill_in_tcp(s->x, &params);

	if (tcp.tt < 0.2)
	{
//		printf(">>>>>>>>>>>>>> tt < 0.2\n");
		tcp.valid = false;
	}

//	printf("plan_cost = %lf\n", params.plan_cost);
	if (params.plan_cost > 3.6)
	{
//		printf(">>>>>>>>>>>>>> plan_cost > 3.6\n");
		tcp.valid = false;
	}

	gsl_multimin_fdfminimizer_free(s);
	gsl_vector_free(x);

	// print_tcp(tcp);
	return (tcp);
}


TrajectoryLookupTable::TrajectoryControlParameters
get_optimized_trajectory_control_parameters(TrajectoryLookupTable::TrajectoryControlParameters tcp_seed,
		TrajectoryLookupTable::TrajectoryDimensions target_td, double target_v, ObjectiveFunctionParams &params,
		bool has_previous_good_tcp)
{
	get_optimization_params(target_v, tcp_seed, target_td, params);
	compute_suitable_acceleration_and_tt(params, tcp_seed, target_td, target_v);

	if (has_previous_good_tcp)
		return (tcp_seed);

	gsl_multimin_function_fdf my_func;

	my_func.n = 3;
	my_func.f = my_f;
	my_func.df = my_df;
	my_func.fdf = my_fdf;
	my_func.params = &params;

	/* Starting point, x */
	gsl_vector *x = gsl_vector_alloc(3);
	gsl_vector_set(x, 0, tcp_seed.k2);
	gsl_vector_set(x, 1, tcp_seed.k3);
	if (params.optimize_time == OPTIMIZE_TIME)
		gsl_vector_set(x, 2, tcp_seed.tt);
	if (params.optimize_time == OPTIMIZE_DISTANCE)
		gsl_vector_set(x, 2, tcp_seed.s);
	if (params.optimize_time == OPTIMIZE_ACCELERATION)
		gsl_vector_set(x, 2, tcp_seed.a);

	const gsl_multimin_fdfminimizer_type *T = gsl_multimin_fdfminimizer_conjugate_fr;
	gsl_multimin_fdfminimizer *s = gsl_multimin_fdfminimizer_alloc(T, 3);

	gsl_multimin_fdfminimizer_set(s, &my_func, x, 0.01, 0.1);

	size_t iter = 0;
	int status;
	do
	{
		iter++;

		status = gsl_multimin_fdfminimizer_iterate(s);
		if (status == GSL_ENOPROG) // minimizer is unable to improve on its current estimate, either due to numerical difficulty or a genuine local minimum
			break;

		status = gsl_multimin_test_gradient(s->gradient, 0.16); // esta funcao retorna GSL_CONTINUE ou zero

	} while ((params.plan_cost > 0.005) && (status == GSL_CONTINUE) && (iter < 30));

	TrajectoryLookupTable::TrajectoryControlParameters tcp = fill_in_tcp(s->x, &params);

	if ((tcp.tt < 0.2) || (params.plan_cost > 0.07)) // too short plan or bad minimum (s->f should be close to zero) mudei de 0.05 para outro
		tcp.valid = false;

	if (target_td.dist < 3.0 && tcp.valid == false) // para debugar
		tcp.valid = false;

	gsl_multimin_fdfminimizer_free(s);
	gsl_vector_free(x);

	//	if (tcp.valid)
	//	{
	//		print_path(simulate_car_from_parameters(target_td, tcp, target_td.v_i, target_td.phi_i, false));
	//		print_path(params.detailed_lane);
	//		FILE *gnuplot_pipe = popen("gnuplot -persist", "w");
	//		fprintf(gnuplot_pipe, "set xrange [-15:45]\nset yrange [-15:15]\n"
	//				"plot './gnuplot_path.txt' using 1:2:3:4 w vec size  0.3, 10 filled\n");
	//		fflush(gnuplot_pipe);
	//		pclose(gnuplot_pipe);
	//		getchar();
	//		system("pkill gnuplot");
	//	}

//	printf("Iteracoes: %lu \n", iter);
	return (tcp);
}


TrajectoryLookupTable::TrajectoryControlParameters
get_optimized_trajectory_control_parameters(TrajectoryLookupTable::TrajectoryControlParameters tcp_seed,
		TrajectoryLookupTable::TrajectoryDiscreteDimensions tdd, double target_v)
{
	ObjectiveFunctionParams params;

	TrajectoryLookupTable::TrajectoryDimensions target_td = convert_to_trajectory_dimensions(tdd, tcp_seed);
	TrajectoryLookupTable::TrajectoryControlParameters tcp = get_optimized_trajectory_control_parameters(tcp_seed, target_td, target_v, params, false);

	return (tcp);
}


TrajectoryLookupTable::TrajectoryControlParameters
get_complete_optimized_trajectory_control_parameters(TrajectoryLookupTable::TrajectoryControlParameters tcp_seed,
		TrajectoryLookupTable::TrajectoryDimensions target_td, double target_v, vector<carmen_ackerman_path_point_t> detailed_lane,
		bool use_lane, bool has_previous_good_tcp)
{
	TrajectoryLookupTable::TrajectoryControlParameters tcp_complete;
	ObjectiveFunctionParams params;
	params.detailed_lane = detailed_lane;
	params.use_lane = use_lane;

	tcp_complete = get_optimized_trajectory_control_parameters(tcp_seed, target_td, target_v, params, has_previous_good_tcp);

	// Atencao: params.suitable_acceleration deve ser preenchido na funcao acima para que nao seja alterado no inicio da otimizacao abaixo
//	if (tcp_complete.valid)
		tcp_complete = optimized_lane_trajectory_control_parameters(tcp_complete, target_td, target_v, params);

	return (tcp_complete);
}
