/*
Helper functions for the place action
 */

#include "o2ac_pose_distribution_updater/base/place_action_helpers.hpp"
#include "o2ac_pose_distribution_updater/base/convex_hull.hpp"
const double EPS = 1e-9, LARGE_EPS = 1e-3;

int argmin(const std::vector<double> &vec) {
  // robust argmin

  int min_id = 0;
  for (int i = 1; i < vec.size(); i++) {
    if (vec[min_id] - EPS > vec[i]) {
      min_id = i;
    }
  }
  return min_id;
}

void find_three_points(const std::vector<Eigen::Vector3d> &current_vertices,
                       const Eigen::Vector3d &current_center_of_gravity,
                       int &ground_touch_vertex_id_1,
                       int &ground_touch_vertex_id_2,
                       int &ground_touch_vertex_id_3,
                       Eigen::Quaterniond &rotation, bool &stability,
                       const bool balance_check) {
  // Given the coordinates of vertices and center of gravity, find the three
  // points touching the ground after placing The object rotation occured by
  // placing is stored to 'rotation' Stabliity after placing is checked and
  // stored to 'stability'

  const double INF = 1e9;

  int number_of_vertices = current_vertices.size();

  // The first point touching the ground is the vertice with the minimum z
  // coordinate
  std::vector<double> current_vertices_z(number_of_vertices);
  for (int i = 0; i < number_of_vertices; i++) {
    current_vertices_z[i] = current_vertices[i](2);
  }
  ground_touch_vertex_id_1 = argmin(current_vertices_z);

  // After the first point touched the ground, the object rotates with an axis,
  // which is orthogonal to both z axis and the line connecting the first point
  // and the center of gravity The direction of rotation is the one such that
  // the center of gravity approached the ground
  Eigen::Vector3d first_axis =
      (current_vertices[ground_touch_vertex_id_1] - current_center_of_gravity)
          .cross(Eigen::Vector3d::UnitZ());
  if (balance_check && first_axis.norm() < EPS) {
    throw std::runtime_error("Balanced at the first rotation");
  }
  first_axis = first_axis.normalized();
  // When another point touches the ground, the rotation stops
  // So the second touching point is the vertices with the minimum rotation
  // angle to touch the ground
  std::vector<double> first_angles(number_of_vertices);
  for (int i = 0; i < number_of_vertices; i++) {
    Eigen::Vector3d v1v2 =
        current_vertices[i] - current_vertices[ground_touch_vertex_id_1];
    first_angles[i] =
        (i == ground_touch_vertex_id_1
             ? INF
             : std::atan2(abs(v1v2(2)),
                          first_axis(1) * v1v2(0) - first_axis(0) * v1v2(1)));
  }
  ground_touch_vertex_id_2 = argmin(first_angles);

  // calculate the coordinates of vertices and the center of gravity after the
  // first rotation
  Eigen::AngleAxisd first_rotation(first_angles[ground_touch_vertex_id_2],
                                   first_axis);
  Eigen::Vector3d rotated_center_of_gravity =
      first_rotation * current_center_of_gravity;
  std::vector<Eigen::Vector3d> rotated_vertices(number_of_vertices);
  for (int i = 0; i < number_of_vertices; i++) {
    rotated_vertices[i] = first_rotation * current_vertices[i];
  }

  // After the second point touched the ground, the object rotates with an axis,
  // which is the line connecting the first touching point and the second
  // touching point
  Eigen::Vector3d second_axis = (rotated_vertices[ground_touch_vertex_id_2] -
                                 rotated_vertices[ground_touch_vertex_id_1])
                                    .normalized();
  // The direction of rotation is the one such that the center of gravity
  // approached the ground
  double direction =
      (rotated_center_of_gravity - rotated_vertices[ground_touch_vertex_id_1])
          .cross(second_axis)(2);
  if (balance_check && std::abs(direction) < EPS) {
    throw std::runtime_error("Balanced at the second rotation");
  }
  if (direction < 0.0) {
    second_axis = -second_axis;
  }
  // When another point touches the ground, the rotation stops
  // So the third touching point is the vertices with the minimum rotation angle
  // to touch the ground
  std::vector<double> second_angles(number_of_vertices);
  for (int i = 0; i < number_of_vertices; i++) {
    Eigen::Vector3d v1v3 =
        rotated_vertices[i] - rotated_vertices[ground_touch_vertex_id_1];
    second_angles[i] =
        (i == ground_touch_vertex_id_1 || i == ground_touch_vertex_id_2
             ? INF
             : std::atan2(abs(v1v3(2)),
                          second_axis(1) * v1v3(0) - second_axis(0) * v1v3(1)));
  }
  ground_touch_vertex_id_3 = argmin(second_angles);

  // calculate the total rotation
  Eigen::AngleAxisd second_rotation(second_angles[ground_touch_vertex_id_3],
                                    second_axis);
  rotation = second_rotation * first_rotation;

  // stability check

  // calculate the coordinates after the second rotation
  Eigen::Vector3d final_center_of_gravity =
      second_rotation * rotated_center_of_gravity;
  std::vector<Eigen::Vector3d> final_vertices(number_of_vertices);
  for (int i = 0; i < number_of_vertices; i++) {
    final_vertices[i] = second_rotation * rotated_vertices[i];
  }

  // calculate the convex hull of the vertices touching the ground
  double min_z = final_vertices[ground_touch_vertex_id_1](2);

  std::vector<Eigen::Vector2d> points_on_ground;

  for (auto &vertex : final_vertices) {
    if (vertex(2) <= min_z + EPS) {
      points_on_ground.push_back((Eigen::Vector2d)vertex.head<2>());
    }
  }

  // If the center of geometry projected to ground is in the convex hull, the
  // object is stable
  Eigen::Vector2d projected_center_of_gravity =
      final_center_of_gravity.head<2>();

  stability =
      check_inside_convex_hull(projected_center_of_gravity, points_on_ground);
}

template <typename T>
const Eigen::Transform<T, 3, Eigen::Isometry> calculate_transform_after_placing(
    const Eigen::Transform<T, 3, Eigen::Isometry> &old_transform,
    const Eigen::Vector3d &center_of_gravity,
    const Eigen::Vector3d &ground_touch_vertex_1,
    const Eigen::Vector3d &ground_touch_vertex_2,
    const Eigen::Vector3d &ground_touch_vertex_3, const double &support_surface,
    const Eigen::Isometry3d &gripper_transform) {
  // Given the current pose 'old_transform' as Eigen Transform,
  // return the pose after placing as Eigen Transform

  // To use AutoDiff, template T is used as Scalar type

  using point = Eigen::Matrix<T, 3, 1>;

  // calculate the current coordinates
  Eigen::Transform<T, 3, Eigen::Isometry> gripper_transform_T =
      gripper_transform.cast<T>();
  Eigen::Transform<T, 3, Eigen::Isometry> current_transform =
      gripper_transform_T * old_transform;
  point current_center_of_gravity =
      current_transform * center_of_gravity.cast<T>();
  point current_ground_touch_vertex_1 =
      current_transform * ground_touch_vertex_1.cast<T>();
  point current_ground_touch_vertex_2 =
      current_transform * ground_touch_vertex_2.cast<T>();
  point current_ground_touch_vertex_3 =
      current_transform * ground_touch_vertex_3.cast<T>();

  // calculate the first rotation
  point v1v2 = current_ground_touch_vertex_2 - current_ground_touch_vertex_1;
  point first_axis = (current_ground_touch_vertex_1 - current_center_of_gravity)
                         .cross(point::UnitZ())
                         .normalized();
  T first_angle =
      atan2(abs(v1v2(2)), first_axis(1) * v1v2(0) - first_axis(0) * v1v2(1));
  Eigen::AngleAxis<T> first_rotation(first_angle, first_axis);

  // calculate the coordinates after first rotation
  point rotated_center_of_gravity = first_rotation * current_center_of_gravity;
  point rotated_ground_touch_vertex_1 =
      first_rotation * current_ground_touch_vertex_1;
  point rotated_ground_touch_vertex_2 =
      first_rotation * current_ground_touch_vertex_2;
  point rotated_ground_touch_vertex_3 =
      first_rotation * current_ground_touch_vertex_3;

  // calculate the second rotation
  point v1v3 = rotated_ground_touch_vertex_3 - rotated_ground_touch_vertex_1;
  point second_axis =
      (rotated_ground_touch_vertex_2 - rotated_ground_touch_vertex_1)
          .normalized();
  if ((rotated_center_of_gravity - rotated_ground_touch_vertex_1)
          .cross(second_axis)(2) < 0.0) {
    second_axis = -second_axis;
  }
  T second_angle =
      atan2(abs(v1v3(2)), second_axis(1) * v1v3(0) - second_axis(0) * v1v3(1));
  Eigen::AngleAxis<T> second_rotation(second_angle, second_axis);

  // calculate the coordinates after second rotation
  point final_center_of_gravity = second_rotation * rotated_center_of_gravity;
  point final_ground_touch_vertex_1 =
      second_rotation * rotated_ground_touch_vertex_1;
  point final_ground_touch_vertex_2 =
      second_rotation * rotated_ground_touch_vertex_2;
  point final_ground_touch_vertex_3 =
      second_rotation * rotated_ground_touch_vertex_3;
  // The translation is occured to hold the physical restraints
  point final_translation;
  final_translation
      << current_center_of_gravity(0) -
             final_center_of_gravity(
                 0), // The x-coordinate of the center of gravity is not changed
      current_center_of_gravity(1) -
          final_center_of_gravity(
              1), // The y-coordinate of the center of gravity is not changed
      support_surface - final_ground_touch_vertex_1(
                            2); // The z-coordinate of the vertices touching the
                                // ground is that of the ground
  // calculate the pose after placing
  return gripper_transform_T.inverse() *
         Eigen::Translation<T, 3>(final_translation) * second_rotation *
         first_rotation * current_transform;
}

// Calculate the function from the pose before placing to the pose after placing
// and its Jacobian. To use AutoDiff in Eigen-unsupported, the function is
// calculated in the class "calculate_particle".

class calculate_particle {

public:
  Eigen::Vector3d center_of_gravity, ground_touch_vertex_1,
      ground_touch_vertex_2,
      ground_touch_vertex_3; // the coordinates of center of gravity, first
                             // touching point, second touching point and third
                             // touching point
  double support_surface;    // the z-coordinate of the ground
  Eigen::Isometry3d gripper_transform; // The gripper transform

  calculate_particle(const Eigen::Vector3d &center_of_gravity,
                     const Eigen::Vector3d &ground_touch_vertex_1,
                     const Eigen::Vector3d &ground_touch_vertex_2,
                     const Eigen::Vector3d &ground_touch_vertex_3,
                     const double &support_surface,
                     const Eigen::Isometry3d &gripper_transform) {
    this->center_of_gravity = center_of_gravity;
    this->ground_touch_vertex_1 = ground_touch_vertex_1;
    this->ground_touch_vertex_2 = ground_touch_vertex_2;
    this->ground_touch_vertex_3 = ground_touch_vertex_3;
    this->support_surface = support_surface;
    this->gripper_transform = gripper_transform;
  }

  // Neede by Eigen AutoDiff
  enum { InputsAtCompileTime = 6, ValuesAtCompileTime = 6 };

  // Also needed by Eigen AutoDiff
  typedef Eigen::Matrix<double, 6, 1> InputType;
  typedef Eigen::Matrix<double, 6, 1> ValueType;

  // The Vector function from the particle representing the pose before placing
  // to the particle representing the pose after placing. To use AutoDiff, the
  // type of coordinates is templated by typename "T".
  template <typename T>
  void operator()(const Eigen::Matrix<T, 6, 1> &current_particle,
                  Eigen::Matrix<T, 6, 1> *result_particle) const {

    // convert current_particle to transform
    Eigen::Transform<T, 3, Eigen::Isometry> old_transform =
        particle_to_eigen_transform(current_particle);

    // calculate transform after placing

    Eigen::Transform<T, 3, Eigen::Isometry> result_transform =
        calculate_transform_after_placing(
            old_transform, center_of_gravity, ground_touch_vertex_1,
            ground_touch_vertex_2, ground_touch_vertex_3, support_surface,
            gripper_transform);

    // convert it to Particle
    result_particle->block(0, 0, 3, 1) = result_transform.translation();
    Eigen::Quaternion<T> result_rotation(result_transform.rotation());
    T roll, pitch, yaw;
    quaternion_to_RPY(result_rotation.w(), result_rotation.x(),
                      result_rotation.y(), result_rotation.z(), roll, pitch,
                      yaw);
    result_particle->block(3, 0, 3, 1) << roll, pitch, yaw;
  }
};

void place_update_distribution(const Particle &old_mean,
                               const CovarianceMatrix &old_covariance,
                               const Eigen::Vector3d &center_of_gravity,
                               const Eigen::Vector3d &ground_touch_vertex_1,
                               const Eigen::Vector3d &ground_touch_vertex_2,
                               const Eigen::Vector3d &ground_touch_vertex_3,
                               const double &support_surface,
                               const Eigen::Isometry3d &gripper_transform,
                               Particle &new_mean,
                               CovarianceMatrix &new_covariance) {
  // Calculate the particle after placing and its Jacobian
  Eigen::AutoDiffJacobian<calculate_particle> calculate_particle_AD(
      center_of_gravity, ground_touch_vertex_1, ground_touch_vertex_2,
      ground_touch_vertex_3, support_surface, gripper_transform);
  // By Eigen AutoDiff, calculate_particle_AD automatically calculates the
  // operation of calculate_particle and its Jacobian
  CovarianceMatrix Jacobian;
  calculate_particle_AD(old_mean, &new_mean, &Jacobian);

  // The covariance of the function value is calculated by the covariance of the
  // argument and Jacobian.
  new_covariance = Jacobian * old_covariance * Jacobian.transpose();
}

place_calculator::place_calculator(const Eigen::Isometry3d &old_mean,
                                   const Eigen::Vector3d &center_of_gravity,
                                   const std::vector<Eigen::Vector3d> &vertices,
                                   const double &support_surface,
                                   const Eigen::Isometry3d &gripper_transform,
                                   const bool balance_check,
                                   const bool stability_check) {
  this->center_of_gravity = center_of_gravity;
  this->support_surface = support_surface;
  this->gripper_transform = gripper_transform;
  this->old_mean = old_mean;

  Eigen::Vector3d current_center_of_gravity =
      gripper_transform * old_mean * center_of_gravity;
  int number_of_vertices = vertices.size();
  std::vector<Eigen::Vector3d> current_vertices(number_of_vertices);
  for (int i = 0; i < number_of_vertices; i++) {
    current_vertices[i] = gripper_transform * old_mean * vertices[i];
  }

  // calculate the three vertices of the object touching the ground
  int ground_touch_vertex_id_1, ground_touch_vertex_id_2,
      ground_touch_vertex_id_3; // The first point touching the ground, and
                                // the second and the third.
  Eigen::Quaterniond rotation;
  bool stability;
  find_three_points(current_vertices, current_center_of_gravity,
                    ground_touch_vertex_id_1, ground_touch_vertex_id_2,
                    ground_touch_vertex_id_3, rotation, stability,
                    balance_check);

  ground_touch_vertex_1 = vertices[ground_touch_vertex_id_1];
  ground_touch_vertex_2 = vertices[ground_touch_vertex_id_2];
  ground_touch_vertex_3 = vertices[ground_touch_vertex_id_3];

  // If the object is not stable after placing, throw exception
  if (stability_check && !stability) {
    throw std::runtime_error("Unstable after placing");
  }

  // calculate new mean

  // The translation is occured to hold the physical restraints
  Eigen::Vector3d final_center_of_gravity =
      rotation * current_center_of_gravity;
  Eigen::Vector3d final_ground_touch_vertex_1 =
      rotation * current_vertices[ground_touch_vertex_id_1];
  Eigen::Vector3d final_translation;
  final_translation
      << current_center_of_gravity(0) -
             final_center_of_gravity(
                 0), // The x-coordinate of the center of gravity is not changed
      current_center_of_gravity(1) -
          final_center_of_gravity(
              1), // The y-coordinate of the center of gravity is not changed
      support_surface - final_ground_touch_vertex_1(
                            2); // The z-coordinate of the vertices touching the
                                // ground is that of the ground

  new_mean = gripper_transform.inverse() *
             Eigen::Translation3d(final_translation) * rotation *
             gripper_transform * old_mean;
}

class calculate_perturbation : public place_calculator {

public:
  using place_calculator::place_calculator;

  // Neede by Eigen AutoDiff
  enum { InputsAtCompileTime = 6, ValuesAtCompileTime = 6 };

  // Also needed by Eigen AutoDiff
  typedef Eigen::Matrix<double, 6, 1> InputType;
  typedef Eigen::Matrix<double, 6, 1> ValueType;

  // The Vector function from the particle representing the pose before placing
  // to the particle representing the pose after placing. To use AutoDiff, the
  // type of coordinates is templated by typename "T".
  template <typename T>
  void operator()(const Eigen::Matrix<T, 6, 1> &input_perturbation,
                  Eigen::Matrix<T, 6, 1> *output_perturbation) const {

    // add perturbation to old_mean
    Eigen::Transform<T, 3, Eigen::Isometry> input_transform =
        Eigen::Transform<T, 3, Eigen::Isometry>(
            Eigen::Matrix<T, 4, 4>::Identity() +
            hat_operator<T>(input_perturbation)) *
        old_mean.cast<T>(); // the first approximation of
                            // exp(hat_operator(input_perturbation)) * old_mean

    // calculate transform after placing

    Eigen::Transform<T, 3, Eigen::Isometry> result_transform =
        calculate_transform_after_placing(
            input_transform, center_of_gravity, ground_touch_vertex_1,
            ground_touch_vertex_2, ground_touch_vertex_3, support_surface,
            gripper_transform);

    // calculate perturbation in result_transform
    *output_perturbation = check_operator<T>(
        -Eigen::Matrix<T, 4, 4>::Identity() +
        (result_transform * new_mean.cast<T>().inverse())
            .matrix()); // the first approximation of
                        // check_operator(log(result_transform * new_mean^{-1}))
  }
};

void place_update_Lie_distribution(const Eigen::Isometry3d &old_mean,
                                   const CovarianceMatrix &old_covariance,
                                   const Eigen::Vector3d &center_of_gravity,
                                   const std::vector<Eigen::Vector3d> &vertices,
                                   const double &support_surface,
                                   const Eigen::Isometry3d &gripper_transform,
                                   Eigen::Isometry3d &new_mean,
                                   CovarianceMatrix &new_covariance) {
  // Calculate the particle after placing and its Jacobian
  Eigen::AutoDiffJacobian<calculate_perturbation> calculate_perturbation_AD(
      old_mean, center_of_gravity, vertices, support_surface,
      gripper_transform);

  new_mean = calculate_perturbation_AD.new_mean;

  // By Eigen AutoDiff, calculate_particle_AD automatically calculates the
  // operation of calculate_particle and its Jacobian
  Eigen::Matrix<double, 6, 1> mean_perturbation;
  CovarianceMatrix Jacobian;
  calculate_perturbation_AD(Eigen::Matrix<double, 6, 1>::Zero(),
                            &mean_perturbation, &Jacobian);
  assert(mean_perturbation.norm() < LARGE_EPS);

  // The covariance of the function value is calculated by the covariance of the
  // argument and Jacobian.
  new_covariance = Jacobian * old_covariance * Jacobian.transpose();
}
