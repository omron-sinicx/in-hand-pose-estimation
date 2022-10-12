/*
The implementation of the touch test
*/

#include "o2ac_pose_distribution_updater/base/read_stl.hpp"
#include "o2ac_pose_distribution_updater/ros/distribution_conversions.hpp"
#include "o2ac_pose_distribution_updater/test/test_tools.hpp"
#include <actionlib/client/simple_action_client.h>
#include <random>
#include <visualization_msgs/Marker.h>

void print_distribution(geometry_msgs::PoseWithCovariance &distribution) {
  puts("Covariance:");
  for (int i = 0; i < 6; i++) {
    for (int j = 0; j < 6; j++) {
      printf("%+8lf ", distribution.covariance[6 * i + j]);
    }
    putchar('\n');
  }
}

int main(int argc, char **argv) {
  ros::init(argc, argv, "test_client");
  ros::NodeHandle nd;

  // create the client
  actionlib::SimpleActionClient<o2ac_msgs::updateDistributionAction> client(
      "update_distribution", true);
  client.waitForServer();

  // create the visualizer client
  ros::ServiceClient visualizer_client =
      nd.serviceClient<o2ac_msgs::visualizePoseBelief>("visualize_pose_belief");

  ros::Duration(1.0).sleep();

  // read object
  std::shared_ptr<moveit_msgs::CollisionObject> object(
      new moveit_msgs::CollisionObject());
  object->pose = to_Pose(0., 0., 0., 1., 0., 0., 0.);
  std::vector<Eigen::Vector3d> vertices;
  std::vector<boost::array<int, 3>> triangles;
  read_stl_from_file_path("/root/o2ac-ur/catkin_ws/src/o2ac_assembly_database/"
                          "config/wrs_assembly_2020/meshes/02-PANEL.stl",
                          vertices, triangles);
  for (auto &vertex : vertices) {
    vertex /= 1000.0; // milimeter -> meter
  }
  add_mesh_to_CollisionObject(object, vertices, triangles,
                              Eigen::Isometry3d::Identity());

  FILE *in = fopen("/root/o2ac-ur/catkin_ws/src/o2ac_pose_distribution_updater/"
                   "test/visualize_test_config.txt",
                   "r");
  double lifetime, position_deviation, angle_deviation;
  fscanf(in, "%lf%lf%lf", &lifetime, &position_deviation, &angle_deviation);
  double x, y, z, qw, qx, qy, qz;

  // set gripper pose
  fscanf(in, "%lf%lf%lf%lf%lf%lf%lf", &x, &y, &z, &qw, &qx, &qy, &qz);
  geometry_msgs::Pose gripper_pose = to_Pose(x, y, z, qw, qx, qy, qz);
  std::string gripper_frame_id = "gripper_frame";
  auto current_time = ros::Time::now();
  broadcast_gripper_pose(gripper_frame_id, current_time, gripper_pose);

  // set pose mean
  fscanf(in, "%lf%lf%lf%lf%lf%lf%lf", &x, &y, &z, &qw, &qx, &qy, &qz);
  auto initial_pose = to_Pose(x, y, z, qw, qx, qy, qz);
  // used to convert covariance
  Eigen::Isometry3d eigen_mean_pose;
  tf::poseMsgToEigen(initial_pose, eigen_mean_pose);
  CovarianceMatrix AD_mean = Adjoint<double>(eigen_mean_pose);

  // moved gripper
  fscanf(in, "%lf%lf%lf%lf%lf%lf%lf", &x, &y, &z, &qw, &qx, &qy, &qz);
  std::string moved_gripper_frame_id = "moved_gripper_frame";
  auto moved_gripper_pose = to_Pose(x, y, z, qw, qx, qy, qz);
  broadcast_gripper_pose(moved_gripper_frame_id, ros::Time::now(),
                         moved_gripper_pose);
  fclose(in);

  while (ros::ok()) {
    geometry_msgs::PoseWithCovarianceStamped current_distribution;

    current_distribution.header.frame_id = gripper_frame_id;
    current_distribution.header.stamp = current_time;

    current_distribution.pose.pose = initial_pose;

    // set covariance randomly
    std::random_device seed_generator;
    std::default_random_engine engine(seed_generator());
    std::uniform_real_distribution<double> uniform_distribution(-1.0, 1.0);
    CovarianceMatrix deviation, covariance;
    for (int i = 0; i < 6; i++) {
      for (int j = 0; j < 6; j++) {
        deviation(i, j) = (j < 3 ? position_deviation : angle_deviation) *
                          uniform_distribution(engine);
      }
    }
    covariance = deviation.transpose() * deviation;
    // transform covariance from object frame to gripper frame
    current_distribution.pose.covariance =
        matrix_6x6_to_array_36(AD_mean * covariance * AD_mean.transpose());

    // visualize initial pose belief
    puts("Generate covariance");
    send_pose_belief(visualizer_client, *object, 1, lifetime,
                     current_distribution);
    print_distribution(current_distribution.pose);

    ros::Duration(1.0).sleep();

    // PLACE ACTION
    o2ac_msgs::updateDistributionGoal goal;
    goal.observation_type = goal.PLACE_OBSERVATION;
    goal.gripper_pose.pose = gripper_pose;
    goal.place_observation.support_surface = 0.0;
    goal.distribution_type = 1;
    goal.distribution = current_distribution;
    goal.gripped_object = *object;

    // Send a call
    puts("PLACE");
    client.sendGoal(goal);
    client.waitForResult();

    auto result = client.getResult();

    assert(result->success);

    current_distribution = result->distribution;

    ros::Duration(1.0).sleep();

    // GRASP ACTION
    goal.observation_type = goal.GRASP_OBSERVATION;
    goal.gripper_pose.pose = gripper_pose;
    goal.distribution_type = 1;
    goal.distribution = current_distribution;
    goal.gripped_object = *object;

    // Send a call
    puts("GRASP");
    client.sendGoal(goal);
    client.waitForResult();

    result = client.getResult();

    assert(result->success);

    current_distribution = result->distribution;

    ros::Duration(1.0).sleep();

    // PUSH ACTION
    // move gripper
    current_distribution.header.frame_id = moved_gripper_frame_id;

    // push the object
    goal.observation_type = goal.PUSH_OBSERVATION;
    goal.gripper_pose.pose = moved_gripper_pose;
    goal.distribution_type = 1;
    goal.distribution = current_distribution;
    goal.gripped_object = *object;

    // Send a call
    puts("PUSH");
    client.sendGoal(goal);
    client.waitForResult();

    result = client.getResult();

    current_distribution = result->distribution;

    ros::Duration(2.0).sleep();
  }
  return 0;
}
