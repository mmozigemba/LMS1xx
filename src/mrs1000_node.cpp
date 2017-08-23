/*
 * Copyright (c) 2017 Fraunhofer Institute for Manufacturing Engineering and Automation (IPA)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <sstream>
#include <lms1xx/mrs1000.h>
#include <ros/ros.h>
#include <sensor_msgs/PointCloud2.h>
#include <sensor_msgs/point_cloud2_iterator.h>
#include <sensor_msgs/MultiEchoLaserScan.h>
#include <sensor_msgs/LaserScan.h>
#include "lms1xx/colaa_conversion.h"

static size_t getLayerIndex(uint16_t layer)
{
  switch (layer) {
  case CoLaALayers::Layer2:
    return 0;
  case CoLaALayers::Layer3:
    return 1;
  case CoLaALayers::Layer1:
    return 2;
  case CoLaALayers::Layer4:
    return 3;
  }
  return 0;
}

int main(int argc, char **argv)
{
  // laser data
  MRS1000 laser;
  ScanConfig cfg;
  ScanOutputRange output_range;
  ScanDataConfig data_cfg;
  sensor_msgs::PointCloud2 cloud;
  sensor_msgs::MultiEchoLaserScan multi_scan;
  sensor_msgs::LaserScan scan;

  // parameters
  std::string host;
  std::string frame_id;
  int port;

  ros::init(argc, argv, "mrs1000");
  ros::NodeHandle nh;
  ros::NodeHandle n("~");
  ros::Publisher cloud_pub = nh.advertise<sensor_msgs::PointCloud2>("cloud", 1);

  std::vector<ros::Publisher> layer_multi_pubs;
  layer_multi_pubs.push_back(nh.advertise<sensor_msgs::MultiEchoLaserScan>("scan_layer_2_multi", 1));
  layer_multi_pubs.push_back(nh.advertise<sensor_msgs::MultiEchoLaserScan>("scan_layer_3_multi", 1));
  layer_multi_pubs.push_back(nh.advertise<sensor_msgs::MultiEchoLaserScan>("scan_layer_1_multi", 1));
  layer_multi_pubs.push_back(nh.advertise<sensor_msgs::MultiEchoLaserScan>("scan_layer_4_multi", 1));

  std::vector<ros::Publisher> layer_pubs;
  layer_pubs.push_back(nh.advertise<sensor_msgs::LaserScan>("scan_layer_2", 1));
  layer_pubs.push_back(nh.advertise<sensor_msgs::LaserScan>("scan_layer_3", 1));
  layer_pubs.push_back(nh.advertise<sensor_msgs::LaserScan>("scan_layer_1", 1));
  layer_pubs.push_back(nh.advertise<sensor_msgs::LaserScan>("scan_layer_4", 1));

  n.param<std::string>("host", host, "192.168.1.2");
  n.param<std::string>("frame_id", frame_id, "laser");
  n.param<int>("port", port, 2111);

  size_t scan_count = 275 * 4 + 1; // 275° with a resolution of 0.25° (+ 1)

  cloud.header.frame_id = frame_id;
  cloud.header.stamp = ros::Time::now();
  cloud.height = 4; // 4 layers
  cloud.width = scan_count;

  // TODO individual frames for layers?
  multi_scan.range_min = .2;
  multi_scan.range_max = 64;
  multi_scan.header.frame_id = frame_id;
  multi_scan.ranges.resize(3);
  multi_scan.intensities.resize(3);

  scan.range_min = .2;
  scan.range_max = 64;
  scan.header.frame_id = frame_id;
  scan.ranges.resize(scan_count);
  scan.intensities.resize(scan_count);

  for (size_t i = 0; i < multi_scan.ranges.size(); ++i)
  {
    multi_scan.ranges[i].echoes.resize(scan_count);
    multi_scan.intensities[i].echoes.resize(scan_count);
  }

  //Fill the fields using the PointCloudModifier
  sensor_msgs::PointCloud2Modifier modifier(cloud);
  modifier.setPointCloud2Fields(4,
    "x", 1, sensor_msgs::PointField::FLOAT32,
    "y", 1, sensor_msgs::PointField::FLOAT32,
    "z", 1, sensor_msgs::PointField::FLOAT32,
    "intensity", 1, sensor_msgs::PointField::FLOAT32);
  //modifier.setPointCloud2FieldsByString(2, "xyz", "intensity");
  cloud.is_bigendian = false;
  cloud.is_dense = false;

  while (ros::ok())
  {
    ROS_INFO_STREAM("Connecting to laser at " << host);
    laser.connect(host, port);
    if (!laser.isConnected())
    {
      ROS_WARN("Unable to connect, retrying.");
      ros::Duration(1).sleep();
      continue;
    }

    //laser.stopMeasurement();

    ROS_DEBUG("Logging in to laser.");
    laser.login();

    cfg = laser.getScanConfig();
    output_range = laser.getScanOutputRange();

    ROS_DEBUG("Laser configuration: scaningFrequency %d, activeSensors %d, angleResolution %d, startAngle %d, stopAngle %d",
              cfg.scan_frequency, cfg.num_sectors, cfg.angualar_resolution, cfg.start_angle, cfg.stop_angle);
    ROS_DEBUG("Laser output range: angleResolution %d, startAngle %d, stopAngle %d",
              output_range.angular_resolution, output_range.start_angle, output_range.stop_angle);

    multi_scan.scan_time = 100.0 / cfg.scan_frequency;
    multi_scan.time_increment = (output_range.angular_resolution / 10000.0) / 360.0 / multi_scan.scan_time;
    scan.scan_time = multi_scan.scan_time;
    scan.time_increment = multi_scan.time_increment;

    ROS_INFO("Connected to laser.");

    data_cfg.output_channel = 7; // 1 + 2 + 3
    data_cfg.remission = true;
    data_cfg.resolution = 0;
    data_cfg.encoder = 0;
    data_cfg.position = false;
    data_cfg.device_name = false;
    data_cfg.comment = false;
    data_cfg.timestamp = 1;
    data_cfg.output_interval = 1; // all scans

    ROS_DEBUG("Setting scan data configuration.");
    laser.setScanDataConfig(data_cfg);

    ROS_DEBUG("Setting echo configuration");
    laser.setEchoFilter(CoLaAEchoFilter::AllEchoes);

    ROS_DEBUG("Setting application mode");
    laser.enableRangingApplication();

    laser.saveConfig();

    ROS_INFO("Starting device...");
    laser.startDevice(); // Log out to properly re-enable system after config

    laser.startMeasurement();
    //ros::Duration(20.0).sleep();

    ROS_INFO("...started. Starting continuous measurements");
    laser.scanContinuous(true);
    //laser.requestLastScan();

    sensor_msgs::PointCloud2Iterator<float>iter_x(cloud, "x");
    sensor_msgs::PointCloud2Iterator<float>iter_y(cloud, "y");
    sensor_msgs::PointCloud2Iterator<float>iter_z(cloud, "z");
    sensor_msgs::PointCloud2Iterator<float>iter_int(cloud, "intensity");
    sensor_msgs::PointCloud2Iterator<float>start_iter_x(cloud, "x");
    sensor_msgs::PointCloud2Iterator<float>start_iter_y(cloud, "y");
    sensor_msgs::PointCloud2Iterator<float>start_iter_z(cloud, "z");
    sensor_msgs::PointCloud2Iterator<float>start_iter_int(cloud, "intensity");
    bool synced = false;

    while (ros::ok())
    {
      ros::Time start = ros::Time::now();

      cloud.header.stamp = start;

      //scanDataLayerMRS data;
      ScanData data;
      ROS_DEBUG("Reading scan data.");

      if (laser.getScanData(&data))
      {
        CoLaAConversion::fillLaserScan(scan, data);
        ROS_DEBUG("Publishing scan data");
        layer_pubs.at(getLayerIndex(data.header.status_info.layer_angle)).publish(scan);

        // Publish Multiecho scan for this layer
        CoLaAConversion::fillMultiEchoLaserScan(multi_scan, data);
        ROS_DEBUG("Publishing multi scan data.");
        layer_multi_pubs.at(getLayerIndex(data.header.status_info.layer_angle)).publish(multi_scan);

        // reset iterators when receiving the first one, so we collect all layers in one cloud
        if (data.header.status_info.layer_angle == CoLaALayers::Layer2)
        {
          iter_x = start_iter_x;
          iter_y = start_iter_y;
          iter_z = start_iter_z;
          iter_int = start_iter_int;
          synced = true;
        }

        if (!synced)
          continue;

        CoLaAConversion::fillPointCloud2(iter_x, iter_y, iter_z, iter_int, data);

        // Check if this is the last layer of the msg
        if (data.header.status_info.layer_angle == CoLaALayers::Layer4)
        {
          ROS_DEBUG("Publishing scan data.");
          cloud_pub.publish(cloud);
        }
      }
      else
      {
        ROS_ERROR("Laser timed out on delivering scan, attempting to reinitialize.");
        ros::Duration(10.0).sleep();
        break;
      }

      ros::spinOnce();
    }

    laser.disconnect();
  }

  return 0;
}
