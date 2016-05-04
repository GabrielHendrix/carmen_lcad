
#include <carmen/xsens_interface.h>
#include <carmen/gps_nmea_interface.h>
#include <carmen/bumblebee_basic_interface.h>
#include <carmen/stereo_velodyne_messages.h>
#include <carmen/writelog.h>
#include <carmen/carmen.h>
#include "read_kitti.h"


void
desalloc_velodyne_data(carmen_velodyne_variable_scan_message velodyne_message)
{
	for (int i = 0; i < velodyne_message.number_of_shots; i++)
	{
		free(velodyne_message.partial_scan[i].distance);
		free(velodyne_message.partial_scan[i].intensity);
	}

	free(velodyne_message.partial_scan);
}


void
publish_velodyne(carmen_velodyne_variable_scan_message velodyne_message)
{
	IPC_RETURN_TYPE err;

	err = IPC_publishData("carmen_stereo_velodyne_scan_message8", &velodyne_message);
	carmen_test_ipc_exit(err, "Could not publish", CARMEN_STEREO_VELODYNE_SCAN_MESSAGE_FMT);
}


void
publish_camera(carmen_bumblebee_basic_stereoimage_message cam)
{
	IPC_RETURN_TYPE err;

	err = IPC_publishData(CARMEN_BUMBLEBEE_BASIC_STEREOIMAGE9_NAME, &cam);
	carmen_test_ipc_exit(err, "Could not publish", CARMEN_BUMBLEBEE_BASIC_STEREOIMAGE_FMT);
}


void
publish_gps_xsens_and_velocity(carmen_gps_gpgga_message gps_msg, carmen_xsens_global_quat_message xsens_msg, carmen_robot_ackerman_velocity_message velocity_msg)
{
	IPC_RETURN_TYPE err;

	err = IPC_publishData(CARMEN_GPS_GPGGA_MESSAGE_NAME, &gps_msg);
	carmen_test_ipc_exit(err, "Could not publish", CARMEN_GPS_GPGGA_MESSAGE_FMT);

	err = IPC_publishData(CARMEN_XSENS_GLOBAL_QUAT_NAME, &xsens_msg);
	carmen_test_ipc_exit(err, "Could not publish", CARMEN_XSENS_GLOBAL_QUAT_FMT);

	err = IPC_publishData(CARMEN_ROBOT_ACKERMAN_VELOCITY_NAME, &velocity_msg);
	carmen_test_ipc_exit(err, "Could not publish", CARMEN_ROBOT_ACKERMAN_VELOCITY_FMT);
}


void
read_velodyne_and_save_to_log(double carmen_initial_time, carmen_FILE *g, char *timestamp_filename, char *dir)
{
	int line;
	double timestamp;
	int year, month, day, hour, minute;
	double second;

	FILE *f = fopen(timestamp_filename, "r");
	double first_kitti_timestamp = 0;
	int is_first = 1;
	line = 0;

	while (!feof(f))
	{
		int n = fscanf(f, "%d-%d-%d %d:%d:%lf", &year, &month, &day, &hour, &minute, &second);
		timestamp = second + 60 * minute + hour * 3600;

		printf("VELODYNE %d %d %d %d %d %lf\n", n, year, month, hour, minute, second);

		if (n != 6) continue;

		if (is_first)
		{
			first_kitti_timestamp = timestamp;
			is_first = 0;
		}

		timestamp = (timestamp - first_kitti_timestamp) + carmen_initial_time;

		carmen_velodyne_variable_scan_message velodyne_message = read_velodyne(dir, line, timestamp);
		publish_velodyne(velodyne_message);
		carmen_logwrite_write_variable_velodyne_scan(&velodyne_message, g, timestamp);
		desalloc_velodyne_data(velodyne_message);

		line++;
	}

	fclose(f);
}


void
read_camera_and_save_to_log(double carmen_initial_time, carmen_FILE *g, char *camera_filename, char *dir_left, char *dir_right)
{
	int line;
	double timestamp;
	int year, month, day, hour, minute;
	double second;

	FILE *f = fopen(camera_filename, "r");
	double first_kitti_timestamp = 0;
	int is_first = 1;
	line = 0;

	while (!feof(f))
	{
		int n = fscanf(f, "%d-%d-%d %d:%d:%lf", &year, &month, &day, &hour, &minute, &second);
		timestamp = second + 60 * minute + hour * 3600;

		if (n != 6)
			continue;

		if (is_first)
		{
			first_kitti_timestamp = timestamp;
			is_first = 0;
		}

		printf("imagem: %d\n", line);

		timestamp = (timestamp - first_kitti_timestamp) + carmen_initial_time;

		carmen_bumblebee_basic_stereoimage_message cam =
			read_camera(dir_left, dir_right, line, timestamp);

		publish_camera(cam);
		carmen_logwrite_write_bumblebee_basic_steroimage(&cam, 9, g, timestamp, 1);

		free(cam.raw_left);
		free(cam.raw_right);

		line++;
	}

	fclose(f);
}


void
read_gps_imu_and_save_to_log(double carmen_initial_time, carmen_FILE *g, char *gps_filename, char *dir_gps)
{
	int line;
	double timestamp;
	int year, month, day, hour, minute;
	double second;

	FILE *f = fopen(gps_filename, "r");
	double first_kitti_timestamp = 0;
	int is_first = 1;
	line = 0;

	carmen_gps_gpgga_message gps_msg;
	carmen_xsens_global_quat_message xsens_msg;
	carmen_robot_ackerman_velocity_message velocity_msg;

	while (!feof(f))
	{
		int n = fscanf(f, "%d-%d-%d %d:%d:%lf", &year, &month, &day, &hour, &minute, &second);
		timestamp = second + 60 * minute + hour * 3600;

		if (n != 6)
			continue;

		if (is_first)
		{
			first_kitti_timestamp = timestamp;
			is_first = 0;
		}

		printf("gps: %d\n", line);

		timestamp = (timestamp - first_kitti_timestamp) + carmen_initial_time;

		read_gps(dir_gps, line, timestamp, &gps_msg, &xsens_msg, &velocity_msg);

		publish_gps_xsens_and_velocity(gps_msg, xsens_msg, velocity_msg);

		carmen_logwrite_write_xsens_quat(&xsens_msg, g, timestamp);
		carmen_logger_write_gps_gpgga(&gps_msg, g, timestamp);
		carmen_logwrite_write_robot_ackerman_velocity(&velocity_msg, g, timestamp);

		line++;
	}

	fclose(f);
}


void
define_messages()
{
	IPC_RETURN_TYPE err;

	err = IPC_defineMsg("carmen_stereo_velodyne_scan_message8", IPC_VARIABLE_LENGTH, CARMEN_VELODYNE_VARIABLE_SCAN_MESSAGE_FMT);
	carmen_test_ipc_exit(err, "Could not define", "carmen_stereo_velodyne_scan_message8");

	err = IPC_defineMsg(CARMEN_GPS_GPGGA_MESSAGE_NAME, IPC_VARIABLE_LENGTH, CARMEN_GPS_GPGGA_MESSAGE_FMT);
	carmen_test_ipc_exit(err, "Could not define", CARMEN_GPS_GPGGA_MESSAGE_NAME);

	carmen_bumblebee_basic_define_messages(10);
	carmen_xsens_define_messages();

}


int
main(int argc, char **argv)
{
	char *dir, *dir_left_cam, *dir_right_cam, *dir_gps;
	char *timestamp_filename, *camera_filename, *gps_filename;

	if (argc < 8)
	{
		printf("Use %s <diretorio com as nuvens> <arquivo de timestamps do velodyne> <dir imagens esquerda> <dir imagens direita> <arquivo timestamp camera> <dir gps> <arquivo timestamp gps>\n", argv[0]);
		return 0;
	}

	dir = argv[1];
	timestamp_filename = argv[2];
	dir_left_cam = argv[3];
	dir_right_cam = argv[4];
	camera_filename = argv[5];
	dir_gps = argv[6];
	gps_filename = argv[7];

	carmen_ipc_initialize(argc, argv);
	define_messages();

	double carmen_initial_time = carmen_get_time();

	carmen_FILE *g = carmen_fopen("log.txt", "w");

	read_velodyne_and_save_to_log(carmen_initial_time, g, timestamp_filename, dir);
	read_camera_and_save_to_log(carmen_initial_time, g, camera_filename, dir_left_cam, dir_right_cam);
	read_gps_imu_and_save_to_log(carmen_initial_time, g, gps_filename, dir_gps);

	carmen_fclose(g);
	return 0;
}
