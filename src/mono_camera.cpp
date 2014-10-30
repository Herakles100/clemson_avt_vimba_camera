/// Copyright (c) 2014,
/// Systems, Robotics and Vision Group
/// University of the Balearic Islands
/// All rights reserved.
///
/// Redistribution and use in source and binary forms, with or without
/// modification, are permitted provided that the following conditions are met:
///     * Redistributions of source code must retain the above copyright
///       notice, this list of conditions and the following disclaimer.
///     * Redistributions in binary form must reproduce the above copyright
///       notice, this list of conditions and the following disclaimer in the
///       documentation and/or other materials provided with the distribution.
///     * All advertising materials mentioning features or use of this software
///       must display the following acknowledgement:
///       This product includes software developed by
///       Systems, Robotics and Vision Group, Univ. of the Balearican Islands
///     * Neither the name of Systems, Robotics and Vision Group, University of
///       the Balearican Islands nor the names of its contributors may be used
///       to endorse or promote products derived from this software without
///       specific prior written permission.
///
/// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
/// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
/// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
/// ARE DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
/// DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
/// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
/// LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
/// ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
/// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
/// THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include <avt_vimba_camera/mono_camera.h>

#include <ros/ros.h>
#include <ros/console.h>
#include <sensor_msgs/image_encodings.h>
#include <sensor_msgs/fill_image.h>
#include <driver_base/SensorLevels.h>

#include <boost/lexical_cast.hpp>
#include <sstream>
#include <string>

#define DEBUG_PRINTS 1

namespace avt_vimba_camera {

MonoCamera::MonoCamera(ros::NodeHandle nh, ros::NodeHandle nhp) : nh_(nh), nhp_(nhp), it_(nhp) {
  // Prepare node handle for the camera
  // TODO use nodelets with getMTNodeHandle()

  // Start Vimba & list all available cameras
  api_.start();

  // Set the image publisher before the streaming
  pub_  = it_.advertiseCamera("image_raw",  1);

  // Set camera info manager
  info_man_  = boost::shared_ptr<camera_info_manager::CameraInfoManager>(new camera_info_manager::CameraInfoManager(nhp_));

  // Set the frame callback
  cam_.setCallback(boost::bind(&avt_vimba_camera::MonoCamera::frameCallback, this, _1));

  // Start the camera
  nhp_.param("ip", ip_, std::string(""));
  nhp_.param("guid", guid_, std::string(""));
  nhp_.param("camera_info_url", camera_info_url_, std::string(""));
  cam_.start(ip_, guid_);

  // Start dynamic_reconfigure & run configure()
  reconfigure_server_.setCallback(boost::bind(&avt_vimba_camera::MonoCamera::configure, this, _1, _2));
}

MonoCamera::~MonoCamera(void) {
  cam_.stop();
  pub_.shutdown();
}

void MonoCamera::frameCallback(const FramePtr& vimba_frame_ptr) {
  ros::Time ros_time = ros::Time::now();
  if (pub_.getNumSubscribers() > 0) {
    sensor_msgs::Image img;
    if (api_.frameToImage(vimba_frame_ptr, img)) {
      sensor_msgs::CameraInfo ci = info_man_->getCameraInfo();
      ci.header.stamp = img.header.stamp = ros_time;
      pub_.publish(img, ci);
    } else {
      ROS_WARN_STREAM("Function frameToImage returned 0. No image published.");
    }
  }
}

/** Dynamic reconfigure callback
*
*  Called immediately when callback first defined. Called again
*  when dynamic reconfigure starts or changes a parameter value.
*
*  @param newconfig new Config values
*  @param level bit-wise OR of reconfiguration levels for all
*               changed parameters (0xffffffff on initial call)
**/
void MonoCamera::configure(Config& newconfig, uint32_t level) {
  try {
    // resolve frame ID using tf_prefix parameter
    if (newconfig.frame_id == "") {
      newconfig.frame_id = "camera";
    }
    // The device has to be closed to change these params
    if (level & driver_base::SensorLevels::RECONFIGURE_CLOSE) {
      cam_.stop();
      cam_.start(ip_, guid_);
      cam_.updateConfig(newconfig);
    // The device has to stop streaming to change these params
    } else if (level & driver_base::SensorLevels::RECONFIGURE_STOP) {
      cam_.stop();
      cam_.start(ip_, guid_);
      cam_.updateConfig(newconfig);
    } else {
      cam_.updateConfig(newconfig);
    }
    updateCameraInfo(newconfig);
  } catch (const std::exception& e) {
    ROS_ERROR_STREAM("Error reconfiguring mono_camera node : " << e.what());
  }
}

void MonoCamera::updateCameraInfo(const avt_vimba_camera::AvtVimbaCameraConfig& config) {

  // Get camera_info from the manager
  sensor_msgs::CameraInfo ci = info_man_->getCameraInfo();

  // Set the frame id
  ci.header.frame_id = config.frame_id;

  // Set the operational parameters in CameraInfo (binning, ROI)
  ci.height    = config.height;
  ci.width     = config.width;
  ci.binning_x = config.binning_x;
  ci.binning_y = config.binning_y;

  // ROI in CameraInfo is in unbinned coordinates, need to scale up
  ci.roi.x_offset = config.roi_offset_x;
  ci.roi.y_offset = config.roi_offset_y;
  ci.roi.height   = config.roi_height;
  ci.roi.width    = config.roi_width;

  // set the new URL and load CameraInfo (if any) from it
  if (config.camera_info_url != camera_info_url_) {
    info_man_->setCameraName(config.frame_id);
    if (info_man_->validateURL(config.camera_info_url)) {
      info_man_->loadCameraInfo(config.camera_info_url);
      ci = info_man_->getCameraInfo();
    } else {
      ROS_WARN_STREAM("Camera info URL not valid: " << config.camera_info_url);
    }
  }

  bool roiMatchesCalibration = (ci.height == config.roi_height
                              && ci.width == config.roi_width);
  bool resolutionMatchesCalibration = (ci.width == config.width
                                   && ci.height == config.height);
  // check
  ci.roi.do_rectify = roiMatchesCalibration || resolutionMatchesCalibration;

  // push the changes to manager
  info_man_->setCameraInfo(ci);
}

};
