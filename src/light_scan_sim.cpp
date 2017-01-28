/**
 * light_scan_sim light_scan_sim.cpp
 * @brief Monitor map and tf data, publish simulated laser scan
 *
 * @copyright 2017 Joseph Duchesne
 * @author Joseph Duchesne
 * 
 */

#include "light_scan_sim/light_scan_sim.h"
#include <math.h>

/**
 * @brief Initialize light scan sim class
 *
 * @param node The ros node handle
 */
LightScanSim::LightScanSim(ros::NodeHandle node) {

  // Todo: Load settings

  ray_cast_ = std::make_shared<RayCast>();

  // Subscribe / Publish
  map_sub_ = node.subscribe(map_topic_, 1, &LightScanSim::MapCallback, this);
  laser_pub_ = node.advertise<sensor_msgs::LaserScan>(laser_topic_, 1);
}

/**
 * @brief Recieve the subscribed map and process its data
 *
 * @param grid The map occupancy grid
 */ 
void LightScanSim::MapCallback(const nav_msgs::OccupancyGrid::Ptr& grid)
{
  map_ = *grid;  // Copy the entire message
  
  // Convert OccupancyGrid to cv::Mat, uint8_t
  cv::Mat map_mat = cv::Mat(map_.info.height, map_.info.width,
                            CV_8UC1, map_.data.data());
  // Set unknown space (255) to free space (0)
  // 4 = threshold to zero, inverted
  // See: http://docs.opencv.org/3.1.0/db/d8e/tutorial_threshold.html
  cv::threshold(map_mat, map_mat, 254, 255, 4); 

  // Update map
  ray_cast_->SetMap(map_mat, map_.info.resolution);
  
  // Create transform from map tf to image tf
  map_to_image_.setOrigin(tf::Vector3(map_.info.origin.position.x,
                                      map_.info.origin.position.y,
                                      map_.info.origin.position.z));
  // Image is in standard right hand orientation
  map_to_image_.setRotation(tf::createQuaternionFromRPY(0, 0, 0));

  map_loaded_ = true;
}


/**
 * @brief Generate and publish the simulated laser scan
 */
void LightScanSim::Update() {
  if (!map_loaded_) {
    ROS_WARN("LightScanSim: Update called, no map yet");
    return;
  }

  // Broadcast the tf representing the map image
  tf_broadcaster_.sendTransform(
    tf::StampedTransform(map_to_image_, ros::Time::now(),
                         map_frame_, image_frame_));

  // Use that transform to generate a point in image space
  tf::StampedTransform image_to_laser;
  try{
    tf_listener_.lookupTransform(image_frame_, laser_frame_,
                                 ros::Time(0), image_to_laser);
  } catch (tf::TransformException &ex) {
    ROS_WARN("LightScanSim: %s",ex.what());
    return;
  }

  // Convert that point from m to px
  cv::Point laser_point(image_to_laser.getOrigin().x()/map_.info.resolution,
                        image_to_laser.getOrigin().y()/map_.info.resolution);
  // And get the yaw
  double roll, pitch, yaw;
  image_to_laser.getBasis().getRPY(roll, pitch, yaw);

  // Generate the ray cast laser scan at that point and orientation
  sensor_msgs::LaserScan scan = ray_cast_->Scan(laser_point, yaw);

  // Set the header values
  scan.header.stamp = image_to_laser.stamp_;  // Use correct time
  scan.header.frame_id = laser_frame_;  // set laser's tf

  // And publish the laser scan
  laser_pub_.publish(scan);
}