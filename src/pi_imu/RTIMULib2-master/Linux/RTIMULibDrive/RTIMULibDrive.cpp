////////////////////////////////////////////////////////////////////////////
//
//  This file is part of RTIMULib
//
//  Copyright (c) 2014-2015, richards-tech, LLC
//
//  Permission is hereby granted, free of charge, to any person obtaining a copy of
//  this software and associated documentation files (the "Software"), to deal in
//  the Software without restriction, including without limitation the rights to use,
//  copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the
//  Software, and to permit persons to whom the Software is furnished to do so,
//  subject to the following conditions:
//
//  The above copyright notice and this permission notice shall be included in all
//  copies or substantial portions of the Software.
//
//  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
//  INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
//  PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
//  HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
//  OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
//  SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.


#include "RTIMULib.h"
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <iostream>
#include <sys/socket.h>
#include <netdb.h>
#include <sys/time.h>
#include <time.h>


#define PORT 3457
#define SOCKET_DATA_PACKET_SIZE	2048

int server_fd;
int file;

void
extract_camera_configuration(char *cam_config, int &image_width, int &image_height, int &frame_rate, int &brightness, int &contrast)
{
	char *token;

	token = strtok(cam_config, "*");

	printf ("--- Connected! Widith: %s ", token);
	image_width = atoi(token);

	token = strtok (NULL, "*");
	printf ("Height: %s ", token);
	image_height = atoi(token);

	token = strtok (NULL, "*");
	printf ("Frame Rate: %s ", token);
	frame_rate = atoi(token);

	token = strtok (NULL, "*");
	printf ("Brightness: %s ", token);
	brightness = atoi(token);

	token = strtok (NULL, "*");
	printf ("Contrast: %s ---\n", token);
	contrast = atoi(token);
}

int
connect_with_client()
{
    int new_socket;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);

    // Creating socket file descriptor
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0)
    {
        perror("--- Socket Failed ---\n");
        return (-1);
    }
    //printf("--- OPT\n");
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt)))
    {
        perror("--- Setsockopt Failed ---\n");
        return (-1);
    }
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons( PORT );
    
    //printf("--- BIND\n");
    // Forcefully attaching socket to the port defined
    if (bind(server_fd, (struct sockaddr *) &address, sizeof(address)) < 0)
    {
        perror("--- Bind Failed ---\n");
        return (-1);
    }
    //printf("--- Listen\n");
    if (listen(server_fd, 3) < 0)
    {
        perror("-- Listen Failed ---\n");
        return (-1);
    }
    
    printf("--- Waiting for connection! --\n");
    if ((new_socket = accept(server_fd, (struct sockaddr *) &address, (socklen_t *) &addrlen)) < 0)
    {
        perror("--- Accept Failed ---\n");
        return (-1);
    }
    printf("--- Connection established successfully! ---\n");

    return (new_socket);
}

double
get_time(void)
{
  struct timeval tv;
  double t;

  if (gettimeofday(&tv, NULL) < 0)
    printf("time error\n");
  t = tv.tv_sec + tv.tv_usec/1000000.0;
  return t;
}

int main()
{
    //int sampleCount = 0;
    //int sampleRate = 0;
    uint64_t rateTimer;
    uint64_t displayTimer;
    uint64_t now;
	
	int pi_socket = connect_with_client();

    //  Using RTIMULib here allows it to use the .ini file generated by RTIMULibDemo.
    //  Or, you can create the .ini in some other directory by using:
    //      RTIMUSettings *settings = new RTIMUSettings("<directory path>", "RTIMULib");
    //  where <directory path> is the path to where the .ini file is to be loaded/saved

    RTIMUSettings *settings = new RTIMUSettings("RTIMULib");

    RTIMU *imu = RTIMU::createIMU(settings);

    if ((imu == NULL) || (imu->IMUType() == RTIMU_TYPE_NULL)) {
        printf("No IMU found\n");
        exit(1);
    }

    //  This is an opportunity to manually override any settings before the call IMUInit

    //  set up IMU

    imu->IMUInit();

    //  this is a convenient place to change fusion parameters

    imu->setSlerpPower(0.02);
    imu->setGyroEnable(true);
    imu->setAccelEnable(true);
    imu->setCompassEnable(true);

    //  set up for rate timer

    rateTimer = displayTimer = RTMath::currentUSecsSinceEpoch();

    //  now just process data
	
	unsigned char rpi_imu_data[SOCKET_DATA_PACKET_SIZE];

    while (1) {
        //  poll at the rate recommended by the IMU

        usleep(imu->IMUGetPollInterval() * 100);

        while (imu->IMURead()) {
            RTIMU_DATA imuData = imu->getIMUData();
         //   sampleCount++;

            now = RTMath::currentUSecsSinceEpoch();

            //  display 10 times per second

            if ((now - displayTimer) > 100000) {
                printf("%s\n",  RTMath::displayDegrees("", imuData.fusionPose));
                fflush(stdout);
                displayTimer = now;
            }

            //  update rate every second

            if ((now - rateTimer) > 1000000) {
                //sampleRate = sampleCount;
                //sampleCount = 0;
                rateTimer = now;
            }
		sprintf((char *) rpi_imu_data, "%f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f*\n", imuData.accel.x(), imuData.accel.y(), imuData.accel.z(), 
			imuData.gyro.x(), imuData.gyro.y(), imuData.gyro.z(),
			imuData.fusionQPose.scalar(), imuData.fusionQPose.x(), imuData.fusionQPose.y(), imuData.fusionQPose.z(), 
			imuData.compass.x(), imuData.compass.y(), imuData.compass.z(), imuData.fusionPose.x(), imuData.fusionPose.y(), imuData.fusionPose.z());

	 	int result = send(pi_socket, rpi_imu_data, SOCKET_DATA_PACKET_SIZE, MSG_NOSIGNAL);  // Returns number of bytes read, 0 in case of connection lost, -1 in case of error
		if (result == -1)
		{
			printf("--- Disconnected ---\n");
           		close(server_fd);
            		sleep(3);
            
           		 pi_socket = connect_with_client();
        	}
        }
    }
}

