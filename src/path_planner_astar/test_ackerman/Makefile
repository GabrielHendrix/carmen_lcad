
include ../../Makefile.conf

# Path planner A-star
MODULE_NAME = Test Ackerman Model
MODULE_COMMENT = Just a test

LINK = g++
CFLAGS += -Wall -g 
CXXFLAGS = -std=c++11

IFLAGS += -I$(CARMEN_HOME)/sharedlib/prob_models -I$(CARMEN_HOME)/sharedlib/libcarmodel -I$(CARMEN_HOME)/sharedlib/libcontrol
LFLAGS += -L$(CARMEN_HOME)/sharedlib/prob_models -L$(CARMEN_HOME)/sharedlib/libcarmodel -L$(CARMEN_HOME)/sharedlib/libcontrol

LFLAGS += -lglobal -lipc -lparam_interface -llocalize_ackerman_interface -lrddf_interface -lobstacle_distance_mapper_interface \
	-lobstacle_avoider_interface -lrobot_ackerman_interface -lsimulator_ackerman_interface -lbase_ackerman_interface \
	-lmap_server_interface -lgrid_mapping -lmap_io -lmap_interface -lcollision_detection -lnavigator_ackerman_interface -lcarmodel -lrddf_interface `pkg-config --libs opencv`

SOURCES = test_ackerman_model.cpp 
#PUBLIC_INCLUDES = path_planner_astar.h
# Public libraries, linked to 'carmen/lib'.

PUBLIC_BINARIES = test_ackerman_model
TARGETS = test_ackerman_model

test_ackerman_model: test_ackerman_model.o

include ../../Makefile.rules

