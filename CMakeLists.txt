cmake_minimum_required(VERSION 2.4.6)

project(ptu_laser_assembler)

# search for everything we need to build the package
find_package(catkin REQUIRED COMPONENTS
    roscpp
    geometry_msgs
    tf
    pointmatcher_ros
#  cmake_modules
)

# Pointmatcher librairy include
find_package(pointmatcher 1.1.0 REQUIRED)
message(STATUS "Using libpointmatcher version ${pointmatcher_VERSION}")
message(STATUS "Using libpointmatcher version ${POINTMATCHER_INCLUDE_DIRS}")

#ROS hydro indigo require the cmake_modules before importing Eigen
#find_package(cmake_modules REQUIRED)

# since we need eigen search it as well
# find_package makes the ${..._INCLUDE_DIRS} ${..._LIBRARIES} variables we use later
find_package(Eigen REQUIRED)

# export the dependencies of this package for who ever depends on us
catkin_package(
    CATKIN_DEPENDS roscpp geometry_msgs
    DEPENDS eigen
)

# tell catkin where to find the headers for this project
include_directories(
  include
  ${catkin_INCLUDE_DIRS}
  ${EIGEN_INCLUDE_DIRS}
  ${POINTMATCHER_INCLUDE_DIRS}
)

# make the executable
add_executable(ptu_scan_assembler 
src/ptu_scan_assembler.cpp
)

# to build the executable we depend on other packets,
# they need to be build beforehand, especially the messages

add_dependencies(ptu_scan_assembler ${catkin_EXPORTED_TARGETS} tf ${PROJECT_NAME}_gencf)

# link the executable to the necessary libs
target_link_libraries(ptu_scan_assembler
   yaml-cpp
   ${POINTMATCHER_LIBRARIES}
  ${catkin_LIBRARIES}
)

