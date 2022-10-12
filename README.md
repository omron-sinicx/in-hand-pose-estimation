# Overview
This package implements an action server for updateDistribution action.

When an object is gripped and its pose has ambiguity, there exist three acts to clarify its pose, "touch", "look", "place", "grasp" and "push".

- "touch" : To move the gripper and let the object to touch some other object.
- "look" : To take an image of the object from a camera
- "place" : To release the object and place it on a support surface
- "grasp" : To grasp the object by the gripper with two flat fingers
- "push" : To push the object by the gripper

When the action server receives a pose belief, which means a pair of the shape of the object and its current pose with ambiguity, and information about one of these acts,
it calculates the updated pose with ambiguity of the object.


More specifically, we use two methods to represent uncertainties of poses.

- RPY representation: A pose of the object is regarded as a 6-dimensional vector (x, y, z, roll, pitch, yaw).
The first three values (x, y, z) represents the translation.
The latter three values (roll, pitch, yaw) represents the rotation.
A pose with ambiguity is represented by a pair of a 6-dimensional vector, which means the mean of the pose, and a 6x6 matrix, which means the covariance matrix of the distribution of the pose.
- Lie representation: We use the approach in http://ncfrn.cim.mcgill.ca/members/pubs/barfoot_tro14.pdf .
A pose of the object is regarded as an element of the 3-dimensional special Euclidean group $SE(3)$.
A pose with ambiguity is represented by a pair of the mean of the pose and a 6x6 covariance matrix, which represents a distribution in the 6-dimensional vector space identified with $se(3)$, the Lie algebra of corresponding to $SE(3)$.


For the description of acts, see "Message types" section.

Additionally, this package provides a service server for visualize pose beliefs.
When the server is called with a pose belief, it publishes a marker array visualizing the pose belief.

# Test Sample

Launch the action server and a test client:
```
roslaunch o2ac_pose_distribution_updater visualize_test.launch
```
If this package works correctly, the results of place, grasp and push acts are visualized repeatedly.

# Usage

To run the action server for updateDistribution action and the visualization server, run the following command:

```
roslaunch o2ac_pose_distribution_updater distribution_updater.launch
```

Then a node named `pose_distribution_updater` is launched and provides the action server to update distributions named `update_distribution` and
the service server to visualize pose beliefs named `visualize_pose_belief`.

The service server publishes marker arrays to specified topic (default: `o2ac_pose_belief_visualization_marker_array`).


The action to update distribution is defined in `o2ac_msgs/updateDistributionAction.h`.

The service to visualize pose beliefs is defined in `o2ac_msgs/visualizePoseBelief.h`.

Sample Code of Client:
```
#include "o2ac_msgs/updateDistributionAction.h"
#include "o2ac_msgs/visualizePoseBelief.h"
...
int main(...)
{
	...
	ros::NodeHandle nd;

	actionlib::SimpleActionClient<o2ac_msgs::updateDistributionAction> update_client("update_distribution", true);
	update_client.waitForServer();
	ros::ServiceClient visualizer_client = nd.serviceClient<o2ac_msgs::visualizePoseBelief>("visualize_pose_belief");
	...
	moveit_msgs::CollisionObject object;
	geometry_msgs::PoseWithCovariance distribution;
	...
	whlle(...){
		...
		o2ac_msgs::updateDistributionGoal goal;
		goal.gripped_object = object;
		goal.distribution = distribution;
		...
		// take some observation
		...
		// update the distribution
		update_client.sendGoal(goal);
		update_client.waitForResult();		
		auto result = update_client.getResult();
		if(result->seccess){
			distribution = result->distribution;
		}
		...
		// visualize the pose belief
		o2ac_msgs::visualizePoseBelief pose_belief;
		pose_belief.object = object;
		pose_belief.distribution = distribution;
		visualizer_client.call(pose_belief);
		...
	}
	...
}
```

## Parameters for action server
This section desribes the parameters for action server.

The parameters are written in `launch/distribution_updater.launch` file.

### For Guassian particle filter

The update of distribution for "touch" act and "look" act is calculated by Gaussian particle filter.

- `number_of_particles` : the number of particle
- `noise_variance`: a 6 dimensional vector, which represents the variance of noise in each step

### For touch act

There exists two objects to touch, "ground" and "box".

- `ground_size`: 3 dimensional vector representing the size of the ground object
- `ground_position`: 3 dimensional vector representing the position of the ground object
- `box_size`: 3 dimensional vector representing the size of the box object
- `box_position`: 3 dimensional vector representing the position of the box object
- `distance_threshold`: If the distance between two objects is less than this value, they are regarded as touching each other.

### For look act
- `look_threshold`: If the value of the grayscaled image at a pixel is less than or equal to this value, the object is regarded as containing the pixel.
- `calibration_object_points`: a vector representing the 3 dimensional coorinates of the points which are used for camera calibration. 
This vector is the concatenation of the coordinates of the points, e.g., if the points are point (x0, y0, z0), point (x1, y1, z1) and point (x2, y2, z2), this vector is (x0, y0, z0, x1, y1, z1, x2, y2, z2).
- `calibration_image_points`: a vector representing the 2 dimensional coordinates of calibration points projected by the camera.
This vector is the concatenation of the coordinates of the points, e.g., if the points are point (x0, y0), point (x1, y1) and point (x2, y2), this vector is (x0, y0, x1, y1, x2, y2).

The following four parameters are the intrinsic parameters of the camera.

- `camera_fx` and  `camera_fy`: focal length in terms of pixel
- `camera_cx` and `camera_cy`: the principal point

### For visualization

- `marker_array_topic_name`: the name of topic to which the marker arrays to visualize pose beliefs are published
- `visualization_scale`: the scale of the object
- `mean_color`: the color of the visualized mean pose of the object, represented by RGBA
- `variance_color`: the color of the visualized pose of the object representing uncertainties, represented by RGBA
- `number_of_particles_to_visualize`: the number of particles to visualize as pose uncertainties

# Message types

This section describe message, action and service used in this package.


These types are defined in the package `o2ac_msgs`.

## Observation messages
This section describe messages to send information about the three acts.

### `TouchObservation.msg`
- `uint8 touched_object_id`: the index of touched object. If this value is 0, the touched object is the ground object. If this value is 1, the touched object is the box object. 

### `LookObservation.msg`
- `sensor_msgs/Image looked_image`: looked image represented as bgr8 image
- `std_msgs/uint32[4] ROI`: an array of length 4 representing the range of interests of the image. The range of interests is a rectangle and this array is [top boundary, bottom boundary, left boundary, right boundary].

### `PlaceObservation.msg`
- `float64 support_surface`: the z coordinate of the support surface where the object is placed

## updateDistribution action
This section describe updateDistribution action, an action to send current poses and information about acts and receive new poses.

### Goal
- `uint8 observation_type`: the type of the act. If this value is `TOUCH_OBSERVATION` (constant, equal to 0), the act is "touch". If it is `LOOK_OBSERVATION` (constant, equal to 1), the act is "look". If it is `PLACE_OBSERVATION` (constant, equal to 2), the act is "place". If it is `GRASP_OBSERVATION` (constant, equal to 3), the act is "grasp". If it is `PUSH_OBSERVATION` (constant, equal to 4), the act is "push".
- `TouchObservation touch_observation`: When the type of act is "touch", this represents information about the act.
- `LookObservation look_observation`: When the type of act is "look", this represents information about the act.
- `PlaceObservation place_observation`: When the type of act is "place", this represents information about the act.
- `geometry_msgs/PoseStamped gripper_pose`: the pose of the gripper when the action is executed
- `moveit_msgs/CollisionObject gripped_object`: a CollisionObject representing the gripped object
- `uint8 distribution_type`: whether method is used to represent uncertainty of the pose. If this value is `RPY_COVARIANCE` (constant, equal to 0), the `covariance` attribute of the following `distribution` is interpreted as covariance matrix in the space of x, y, z, roll, pitch, yaw. If it is `LIE_COVARIANCE` (constant, equal to 1),  the `covariance` attribute is interpreted as covariance matrix in the vector space identified with the Lie algebra $se(3)$.
- `geometry_msgs/PoseWithCovarianceStamped distribution`: the current distribution of the pose of the object

### Result
- `bool success`: If update is calculated successfully, this value is `true`. Otherwise, it is `false`.
- `geometry_msgs/PoseWithCovarianceStamped distribution`: If update is calculated successfully, this stores the updated distribution of the pose of the object
- `std_msgs/String error_message`: If update is not calculated successfully, this string represents the error message.

### Feedback
Nothing

## visualizePoseBelief service
This section describe visualizePoseBelief service, a service to receive a pose belief and publish a markey array to visualize it.

## Request
- `moveit_msgs/CollisionObject object`: a CollisionObject representing the object
- `uint8 distribution_type`: whether method is used to represent uncertainty of the pose. Its meaning is the same as the case of `updateDistributionGoal`.
- `geometry_msgs/PoseWithCovarianceStamped distribution`: the current distribution of the pose of the object
- `duration lifetime`: the lifetime of the marker

## Response
Nothing

# Details of messages
This section describe messages to send information about the three acts.


# File Structure
```
o2ac_pose_distribution_updater           # package direcotory
├── CMakeLists.txt
├── include                              # directory containing header files
│   └── o2ac_pose_distribution_updater   # header files for this package
│       ├── base			 # directory containing header files without ros
│	│   ├── conversions.hpp              # conversion functions, header only
│  	│   ├── convex_hull.hpp              # fuctions about convex hulls
│       │   ├── estimator.hpp                # class calculating distributions
│       │   ├── grasp_action_helpers.hpp     # functions for calculations associated to grasp action
│       │   ├── operators_for_Lie_distribution.hpp     # functions for Lie distribution, header only
│       │   ├── place_action_helpers.hpp     # functions for calculations associated to place action
│       │   ├── planners.hpp                 # class of planners
│       │   ├── planner_helpers.hpp          # functions for calculations associated to planning
│       │   ├── push_action_helpers.hpp      # functions for calculations associated to push action
│       │   ├── random_particle.hpp          # function to generate random particles
│       │   └── read_stl.hpp                 # function to read stl files
│       ├── ros				 # directory containing header files with ros
│       │   ├── distribution_conversions.hpp # fuctions to convert between PRY and Lie
│       │   ├── pose_belief_visualizer.hpp   # class to visualize pose beliefs
│       │   ├── ros_converted_estimator.hpp  # class calculating distributions, wrapped for ros message input
│       │   └── ros_converters.hpp           # conversion functions associated with ros message
│  	└── test			 # directory containing header files for test
│           ├── test.hpp                     # procedures for unit test
│           └── test_tools.hpp               # functions associated to testing
├── launch                               # direcotory containing launch files
│   ├── distribution_updater.launch      # for launching action server for updateDistribution action
│   └── estimator configure              # configuration file for estimator
├── package.xml                          # package discription
├── README.md                            # this README
├── src                                  # direcotory containing source files
│   ├── base				 # direcotory containing source files without ros
│   │	├── conversions.cpp                  # implementation of conversions.hpp
│   │	├── estimator.cpp                    # implementation of estimator.hpp
│   │	├── convex_hull.cpp                  # implementation of convex_hull.hpp
│   │	├── grasp_action_helpers.cpp         # implementation of grasp_action_helpers.hpp
│   │	├── place_action_helpers.cpp         # implementation of place_action_helpers.hpp
│   │	├── planners.cpp                     # implementation of planner.hpp
│   │	├── planner_helpers.cpp              # implementation of planner_helpers.hpp
│   │	├── push_action_helpers.cpp          # implementation of push_action_helpers.hpp
│   │	├── random_particle.cpp              # implementation of random_particle.hpp
│   │	└── read_stl.cpp                     # implementation of read_stl.hpp
│   ├── ros				 # direcotory containing source files with ros
│   │	├── action_server.cpp                # implementation of the action server
│   │	├── distribution_conversions.cpp     # implementation of distribution_conversios.hpp
│   │	├── pose_belief_visualizer.cpp       # implementation of pose_belief_visualizer.hpp
│   │	├── ros_converted_estimator.cpp      # implementation of ros_converted_estimator.hpp
│   │	└── ros_converters.cpp               # implementation of ros_converters.hpp
│   └── test                             # sources for unit test
│       ├── look_test.cpp                # implementation of look_test in test.hpp
│       ├── place_test.cpp               # implementation of place_test in test.hpp
│       ├── test_client.cpp              # test client which executes touch, look and place tests
│       ├── touch_test.cpp               # implementation of touch_test in test.hpp
│       └── ...                          # other tests
├── test                                 # files used in unit test
│   ├── CAD                              # stl files
│   │   └── ...
│   ├── grasp_test_cones_Lie_1.txt       # test cases for grasp test
│   ├── ...
│   ├── look_action_images               # jpg files used in look test
│   │   └── ...
│   ├── look_gripper_tip.csv             # csv files used in look test
│   ├── place_test_cones_1.txt           # test cases for place test
│   ├── ...
│   ├── test.rviz                        # rviz config file for unit test
│   ├── test_client.launch               # launch file for test client
│   ├── touch_input_gearmotor_1.txt      # test case for touch test
│   ├── unit_test.test                   # .test case file for unit test
│   ├── visualize_test.rviz              # rviz config file for test sample
│   ├── visualize_test.launch            # launch file for test sample
│   └── visualize_test_config.txt        # config text file for test sample
└── test_results
```
