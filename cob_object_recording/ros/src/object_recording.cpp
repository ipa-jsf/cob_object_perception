#include <cob_object_recording/object_recording.h>
#include <boost/filesystem.hpp>
#include <fstream>

//ObjectRecording::ObjectRecording()
//{
//	it_ = 0;
//	sync_input_ = 0;
//}

ObjectRecording::ObjectRecording(ros::NodeHandle nh)
: node_handle_(nh)
{
	prev_marker_array_size_ = 0;

	// subscribers
	input_marker_detection_sub_.subscribe(node_handle_, "input_marker_detections", 1);
	it_ = new image_transport::ImageTransport(node_handle_);
	color_image_sub_.subscribe(*it_, "input_color_image", 1);
	//input_pointcloud_sub_ = node_handle_.subscribe("input_pointcloud_segments", 10, &ObjectRecording::inputCallback, this);
	input_pointcloud_sub_.subscribe(node_handle_, "input_pointcloud", 1);
//	input_color_camera_info_sub_ = node_handle_.subscribe("input_color_camera_info", 1, &ObjectRecording::calibrationCallback, this);

	// input synchronization
	sync_input_ = new message_filters::Synchronizer< message_filters::sync_policies::ApproximateTime<cob_object_detection_msgs::DetectionArray, sensor_msgs::PointCloud2, sensor_msgs::Image> >(10);
	sync_input_->connectInput(input_marker_detection_sub_, input_pointcloud_sub_, color_image_sub_);

	service_server_start_recording_ = node_handle_.advertiseService("start_recording", &ObjectRecording::startRecording, this);
	service_server_stop_recording_ = node_handle_.advertiseService("stop_recording", &ObjectRecording::stopRecording, this);
	service_server_save_recorded_object_ = node_handle_.advertiseService("save_recorded_object", &ObjectRecording::saveRecordedObject, this);

	recording_pose_marker_array_publisher_ = node_handle_.advertise<visualization_msgs::MarkerArray>("recording_pose_marker_array", 0);

	// todo: read in parameters
	sharpness_threshold_ = 0.8;
	pan_divisions_ = 6;
	tilt_divisions_ = 2;
	preferred_recording_distance_ = 0.8;
	distance_threshold_translation_ = 0.08;
	distance_threshold_orientation_ = 8./180.*CV_PI;
}

ObjectRecording::~ObjectRecording()
{
	recording_pose_marker_array_publisher_.shutdown();

	if (it_ != 0) delete it_;
	if (sync_input_ != 0) delete sync_input_;
}


bool ObjectRecording::startRecording(cob_object_detection_msgs::StartObjectRecording::Request &req, cob_object_detection_msgs::StartObjectRecording::Response &res)
{
	ROS_INFO("Request to start recording received.");

	// clear data container
	recording_data_.clear();

	// prepare data container
	int dataset_size = pan_divisions_ * tilt_divisions_ + 1;
	recording_data_.resize(dataset_size);
	double pan_step = 360./(180.*pan_divisions_) * CV_PI;
	double tilt_step = 90./(180.*tilt_divisions_) * CV_PI;
	double pan_max = 359.9/180.*CV_PI;
	double tilt_min = -89.9/180.*CV_PI;
	int index = 0;
	std::cout << "New perspectives added:\n";
	for (double tilt=-tilt_step/2; tilt>tilt_min; tilt-=tilt_step)
		for (double pan=0; pan<pan_max; pan+=pan_step, ++index)
		{
			std::cout << index+1 << ". tilt=" << tilt << " \t pan=" << pan << std::endl;
			computePerspective(pan, tilt, preferred_recording_distance_, recording_data_[index].pose_desired);
		}
	std::cout << index+1 << ". tilt=" << -90./180*CV_PI << " \t pan=0" << std::endl;
	computePerspective(0., -90./180*CV_PI, preferred_recording_distance_, recording_data_[index].pose_desired);

	// register callback function for data processing
	registered_callback_ = sync_input_->registerCallback(boost::bind(&ObjectRecording::inputCallback, this, _1, _2, _3));

	return true;
}

bool ObjectRecording::stopRecording(cob_object_detection_msgs::StopObjectRecording::Request &req, cob_object_detection_msgs::StopObjectRecording::Response &res)
{
	ROS_INFO("Request to stop recording received.");

	res.recording_stopped = true;
	if (req.stop_although_model_is_incomplete == false)
	{
		for (unsigned int i=0; i<recording_data_.size(); ++i)
			res.recording_stopped &= recording_data_[i].perspective_recorded;
		if (res.recording_stopped == false)
		{
			ROS_INFO("Recording not stopped since data collection is not yet complete.");
			return false;
		}
	}

	registered_callback_.disconnect();

	ROS_INFO("Stopped recording.");

	return true;
}

bool ObjectRecording::saveRecordedObject(cob_object_detection_msgs::SaveRecordedObject::Request &req, cob_object_detection_msgs::SaveRecordedObject::Response &res)
{
	ROS_INFO("Request to save recorded data received.");

	return true;
}

/// callback for the incoming pointcloud data stream
void ObjectRecording::inputCallback(const cob_object_detection_msgs::DetectionArray::ConstPtr& input_marker_detections_msg, const sensor_msgs::PointCloud2::ConstPtr& input_pointcloud_msg, const sensor_msgs::Image::ConstPtr& input_image_msg)
{
	//std::cout << "Recording data..." << std::endl;

	if (input_marker_detections_msg->detections.size() == 0)
	{
		ROS_INFO("ObjectRecording::inputCallback: No markers detected.\n");
		return;
	}

	// convert color image to cv::Mat
	cv_bridge::CvImageConstPtr color_image_ptr;
	cv::Mat color_image;
	if (convertColorImageMessageToMat(input_image_msg, color_image_ptr, color_image) == false)
		return;

	// convert point cloud 2 message to pointcloud
	typedef pcl::PointXYZRGB PointType;
	pcl::PointCloud<PointType> input_pointcloud;
	pcl::fromROSMsg(*input_pointcloud_msg, input_pointcloud);

	// compute mean coordinate system if multiple markers detected
	tf::Transform fiducial_pose = computeMarkerPose(input_marker_detections_msg);

	// check image quality (sharpness)
	double avg_sharpness = 0.;
	for (unsigned int i=0; i<input_marker_detections_msg->detections.size(); ++i)
		avg_sharpness += input_marker_detections_msg->detections[i].score;
	avg_sharpness /= (double)input_marker_detections_msg->detections.size();

	if (avg_sharpness < sharpness_threshold_)
	{
		ROS_WARN("ObjectRecording::inputCallback: Image quality too low. Discarding image with sharpness %.3f (threshold = %.3f)", avg_sharpness, sharpness_threshold_);
		return;
	}

	// compute whether the camera is close to one of the target perspectives
	for (unsigned int i=0; i<recording_data_.size(); ++i)
	{
		tf::Transform pose_recorded = fiducial_pose.inverse();

		// check translational distance to camera
		double distance_translation = recording_data_[i].pose_desired.getOrigin().distance(pose_recorded.getOrigin());
		if (distance_translation > distance_threshold_translation_)
			continue;

		// check rotational distance to camera frame
		double distance_orientation = recording_data_[i].pose_desired.getRotation().angle(pose_recorded.getRotation());
		if (distance_orientation > distance_threshold_orientation_)
			continue;

//		std::cout << "  distance=" << distance_translation << "\t angle=" << distance_orientation << "(t=" << distance_threshold_orientation_ << ")" << std::endl;
//		std::cout << "recording_data_[i].pose_desired: XYZ=(" << recording_data_[i].pose_desired.getOrigin().getX() << ", " << recording_data_[i].pose_desired.getOrigin().getY() << ", " << recording_data_[i].pose_desired.getOrigin().getZ() << "), WABC=(" << recording_data_[i].pose_desired.getRotation().getW() << ", " << recording_data_[i].pose_desired.getRotation().getX() << ", " << recording_data_[i].pose_desired.getRotation().getY() << ", " << recording_data_[i].pose_desired.getRotation().getZ() << "\n";
//		std::cout << "                  pose_recorded: XYZ=(" << pose_recorded.getOrigin().getX() << ", " << pose_recorded.getOrigin().getY() << ", " << pose_recorded.getOrigin().getZ() << "), WABC=(" << pose_recorded.getRotation().getW() << ", " << pose_recorded.getRotation().getX() << ", " << pose_recorded.getRotation().getY() << ", " << pose_recorded.getRotation().getZ() << "\n";

		// check that pose distance is at least as close as last time
		double distance_pose = distance_translation + distance_orientation;
		if (distance_pose > recording_data_[i].distance_to_desired_pose)
			continue;

		// check that sharpness score is at least as good as last time
		if (avg_sharpness < recording_data_[i].sharpness_score)
			continue;

		// save data
		recording_data_[i].image = color_image;
		recording_data_[i].pointcloud = input_pointcloud;
		recording_data_[i].pose_recorded = pose_recorded;
		recording_data_[i].distance_to_desired_pose = distance_pose;
		recording_data_[i].sharpness_score = avg_sharpness;
		recording_data_[i].perspective_recorded = true;
	}

	// display the markers indicating the already recorded perspectives and the missing
	publishRecordingPoseMarkers(input_marker_detections_msg, fiducial_pose);
}


/// Converts a color image message to cv::Mat format.
bool ObjectRecording::convertColorImageMessageToMat(const sensor_msgs::Image::ConstPtr& image_msg, cv_bridge::CvImageConstPtr& image_ptr, cv::Mat& image)
{
	try
	{
		image_ptr = cv_bridge::toCvShare(image_msg, sensor_msgs::image_encodings::BGR8);
	}
	catch (cv_bridge::Exception& e)
	{
		ROS_ERROR("ObjectCategorization: cv_bridge exception: %s", e.what());
		return false;
	}
	image = image_ptr->image;

	return true;
}

tf::Transform ObjectRecording::computeMarkerPose(const cob_object_detection_msgs::DetectionArray::ConstPtr& input_marker_detections_msg)
{
	tf::Vector3 mean_translation;
	tf::Quaternion mean_orientation(0.,0.,0.);
	for (unsigned int i=0; i<input_marker_detections_msg->detections.size(); ++i)
	{
		tf::Point translation;
		tf::pointMsgToTF(input_marker_detections_msg->detections[i].pose.pose.position, translation);
		tf::Quaternion orientation;
		tf::quaternionMsgToTF(input_marker_detections_msg->detections[i].pose.pose.orientation, orientation);

		if (i==0)
		{
			mean_translation = translation;
			mean_orientation = orientation;
		}
		else
		{
			mean_translation += translation;
			mean_orientation += orientation;
		}
	}
	mean_translation /= (double)input_marker_detections_msg->detections.size();
	mean_orientation /= (double)input_marker_detections_msg->detections.size();
	mean_orientation.normalize();
	return tf::Transform(mean_orientation, mean_translation);
}


void ObjectRecording::publishRecordingPoseMarkers(const cob_object_detection_msgs::DetectionArray::ConstPtr& input_marker_detections_msg, tf::Transform fiducial_pose)
{
	// 3 arrows for each coordinate system of each detected fiducial
	unsigned int marker_array_size = 3*recording_data_.size();
	if (marker_array_size >= prev_marker_array_size_)
		marker_array_msg_.markers.resize(marker_array_size);

	// Publish marker array
	for (unsigned int i=0; i<recording_data_.size(); ++i)
	{
		// publish a coordinate system from arrow markers for each object
		tf::Transform pose = fiducial_pose * recording_data_[i].pose_desired;

		for (unsigned int j=0; j<3; ++j)
		{
			unsigned int idx = 3*i+j;
			marker_array_msg_.markers[idx].header = input_marker_detections_msg->header;
			marker_array_msg_.markers[idx].ns = "object_recording";
			marker_array_msg_.markers[idx].id =  idx;
			marker_array_msg_.markers[idx].type = visualization_msgs::Marker::ARROW;
			marker_array_msg_.markers[idx].action = visualization_msgs::Marker::ADD;
			marker_array_msg_.markers[idx].color.a = (recording_data_[i].perspective_recorded==false ? 0.85 : 0.15);
			marker_array_msg_.markers[idx].color.r = 0;
			marker_array_msg_.markers[idx].color.g = 0;
			marker_array_msg_.markers[idx].color.b = 0;

			marker_array_msg_.markers[idx].points.resize(2);
			marker_array_msg_.markers[idx].points[0].x = 0.0;
			marker_array_msg_.markers[idx].points[0].y = 0.0;
			marker_array_msg_.markers[idx].points[0].z = 0.0;
			marker_array_msg_.markers[idx].points[1].x = 0.0;
			marker_array_msg_.markers[idx].points[1].y = 0.0;
			marker_array_msg_.markers[idx].points[1].z = 0.0;

			if (j==0)
			{
				marker_array_msg_.markers[idx].points[1].x = 0.2;
				marker_array_msg_.markers[idx].color.r = 255;
			}
			else if (j==1)
			{
				marker_array_msg_.markers[idx].points[1].y = 0.2;
				marker_array_msg_.markers[idx].color.g = 255;
			}
			else if (j==2)
			{
				marker_array_msg_.markers[idx].points[1].z = 0.2;
				marker_array_msg_.markers[idx].color.b = 255;
			}

			marker_array_msg_.markers[idx].pose.position.x = pose.getOrigin().getX();
			marker_array_msg_.markers[idx].pose.position.y = pose.getOrigin().getY();
			marker_array_msg_.markers[idx].pose.position.z = pose.getOrigin().getZ();
			tf::quaternionTFToMsg(pose.getRotation(), marker_array_msg_.markers[idx].pose.orientation);

			marker_array_msg_.markers[idx].lifetime = ros::Duration(1);
			marker_array_msg_.markers[idx].scale.x = 0.01; // shaft diameter
			marker_array_msg_.markers[idx].scale.y = 0.015; // head diameter
			marker_array_msg_.markers[idx].scale.z = 0.0; // head length 0=default
		}
	}

	if (prev_marker_array_size_ > marker_array_size)
	{
		for (unsigned int i = marker_array_size; i < prev_marker_array_size_; ++i)
		{
			marker_array_msg_.markers[i].action = visualization_msgs::Marker::DELETE;
		}
	}
	prev_marker_array_size_ = marker_array_size;

	recording_pose_marker_array_publisher_.publish(marker_array_msg_);
}

void ObjectRecording::computePerspective(const double& pan, const double& tilt, const double& preferred_recording_distance, tf::Transform& perspective_pose)
{
	tf::Transform pose1, pose2, pose3;
	pose1.setOrigin(tf::Vector3(preferred_recording_distance, 0., 0.));			// recording distance
	pose1.setRotation(tf::Quaternion(-90./180.*CV_PI, 0., 90./180.*CV_PI));		// rotation in camera direction
	pose2.setOrigin(tf::Vector3(0.,0.,0.));
	pose2.setRotation(tf::Quaternion(tilt, 0., 0.));		// orientation in tilt direction
	pose3.setOrigin(tf::Vector3(0.,0.,0.));
	pose3.setRotation(tf::Quaternion(0., 0., pan));			// orientation in pan direction
	perspective_pose = pose3 * pose2 * pose1;
}

//void ObjectRecording::calibrationCallback(const sensor_msgs::CameraInfo::ConstPtr& calibration_msg)
//{
////	pointcloud_height_ = calibration_msg->height;
////	pointcloud_width_ = calibration_msg->width;
//	cv::Mat temp(3,4,CV_64FC1);
//	for (int i=0; i<12; i++)
//		temp.at<double>(i/4,i%4) = calibration_msg->P.at(i);
////		std::cout << "projection_matrix: [";
////		for (int v=0; v<3; v++)
////			for (int u=0; u<4; u++)
////				std::cout << temp.at<double>(v,u) << " ";
////		std::cout << "]" << std::endl;
//	color_camera_matrix_ = temp;
//}


int main (int argc, char** argv)
{
	// Initialize ROS, specify name of node
	ros::init(argc, argv, "object_recording");

	// Create a handle for this node, initialize node
	ros::NodeHandle nh;

	// Create and initialize an instance of ObjectRecording
	ObjectRecording objectRecording(nh);

	ros::spin();

	return (0);
}