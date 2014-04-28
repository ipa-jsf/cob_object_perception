#include "perspective_transformation.h"

#include <pcl/filters/extract_indices.h>
#include <pcl/common/pca.h>

#define PI 3.14159265

p_transformation::p_transformation()
{
}

void p_transformation::run_pca(cv::Mat *source, cv::Mat *depth, const sensor_msgs::PointCloud2ConstPtr& pointcloud, visualization_msgs::MarkerArray* marker)
{

// Initialize Vectors
	std::vector<float> eigen1;
	std::vector<float> eigen2;
	std::vector<float> eigen3;
	float a,b,c,d;
	cv::Mat test=(*source).clone();
//	Set color points without depth to 0
	for(int i=0;i<(*source).rows;i++)
	{
		for(int j=0;j<(*source).cols;j++)
		{
			if((*depth).at<float>(i,j)<=0 || (*depth).at<float>(i,j)!=(*depth).at<float>(i,j))
			{
				(*source).at<cv::Vec3b>(i,j)[0]=0;
				(*source).at<cv::Vec3b>(i,j)[1]=0;
				(*source).at<cv::Vec3b>(i,j)[2]=0;
			}
		}
	}

	cv::imshow("test", (*source));

	float orig_x,orig_y,orig_z;

	try
	{
		pcl::PointCloud<pcl::PointXYZ>::Ptr input_cloud2 (new pcl::PointCloud<pcl::PointXYZ>);
		pcl::fromROSMsg(*pointcloud, *input_cloud2);
		pcl::PointCloud<pcl::PointXYZRGB>::Ptr input_cloud (new pcl::PointCloud<pcl::PointXYZRGB>);
		pcl::fromROSMsg(*pointcloud, *input_cloud);
		std::vector<int> indices;
		pcl::removeNaNFromPointCloud(*input_cloud2,*input_cloud2, indices);


		// PCA with pcl library
			pcl::PCA< pcl::PointXYZ > pca;
			pca.setInputCloud(input_cloud2);
			Eigen::Matrix3f eigen_vectors_copy =pca.getEigenVectors();



			//fill vectors with pcl pca results
				eigen1.push_back((float)eigen_vectors_copy(0,0));
				eigen1.push_back((float)eigen_vectors_copy(1,0));
				eigen1.push_back((float)eigen_vectors_copy(2,0));

				eigen2.push_back((float)eigen_vectors_copy(0,1));
				eigen2.push_back((float)eigen_vectors_copy(1,1));
				eigen2.push_back((float)eigen_vectors_copy(2,1));

				eigen3.push_back((float)eigen_vectors_copy(0,2));
				eigen3.push_back((float)eigen_vectors_copy(1,2));
				eigen3.push_back((float)eigen_vectors_copy(2,2));

				Eigen::Vector4f meanpoint = pca.getMean();

				std::cout<<eigen1[0]<<" "<<eigen1[1]<<" "<<eigen1[2]<<"eigen1"<<std::endl;
				std::cout<<eigen2[0]<<" "<<eigen2[1]<<" "<<eigen2[2]<<"eigen2"<<std::endl;
				std::cout<<eigen3[0]<<" "<<eigen3[1]<<" "<<eigen3[2]<<"eigen3"<<std::endl;
	//			Set z direction
				if(eigen3[2]<0)
				{
					for(int i=0;i<3;i++)
					{
						eigen1[i]=eigen1[i]*-1;
						eigen2[i]=eigen2[i]*-1;
						eigen3[i]=eigen3[i]*-1;
					}
				}

			// Koordinatengleichung der Ebene aufstellen  ax + by + cz - d = 0
				a = eigen3[0]; //eigen1[1]*eigen2[2] - eigen1[2]*eigen2[1];
				b = eigen3[1]; //eigen1[2]*eigen2[0] - eigen1[0]*eigen2[2];
				c = eigen3[2]; //eigen1[0]*eigen2[1] - eigen1[1]*eigen2[0];
				d = (float)meanpoint[0]*a + (float)meanpoint[1]*b + meanpoint[2]*c;
				orig_x=(double)meanpoint[0];
				orig_y=(double)meanpoint[1];
				orig_z=(double)meanpoint[2];
	//End PCA








	// Uebernomme version
		cv::Point3d p1, p2;
		if (a==0. && b==0. && c!=0)
		{
			p1.x = 0;
			p1.y = 0;
			p1.z = -d/c;
			p2.x = 1;
			p2.y = 0;
			p2.z = -d/c;
		}
		else if (a==0. && c==0. && b!=0)
		{
			p1.x = 0;
			p1.y = -d/b;
			p1.z = 0;
			p2.x = 1;
			p2.y = -d/b;
			p2.z = 0;
		}
		else if (b==0. && c==0. && a!=0)
		{
			p1.x = -d/a;
			p1.y = 0;
			p1.z = 0;
			p2.x = -d/a;
			p2.y = 1;
			p2.z = 0;
		}
		else if (a==0. && c!=0)
		{
			p1.x = 0;
			p1.y = 0;
			p1.z = -d/c;
			p2.x = 1;
			p2.y = 0;
			p2.z = -d/c;
		}
		else if (b==0. && c!=0)
		{
			p1.x = 0;
			p1.y = 0;
			p1.z = -d/c;
			p2.x = 0;
			p2.y = 1;
			p2.z = -d/c;
		}
		else if (c==0. && a!=0)
		{
			p1.x = -d/a;
			p1.y = 0;
			p1.z = 0;
			p2.x = -d/a;
			p2.y = 0;
			p2.z = 1;
		}
		else if(c!=0)
		{
			p1.x = 0;
			p1.y = 0;
			p1.z = -d/c;
			p2.x = 1;
			p2.y = 0;
			p2.z = (-d-a)/c;
		}else{
			std::cout <<"Fehler Ebenengleichung: Division durch 0"<<std::endl;
			return;
		}

		std::cout<<a<<"a "<<b<<"b "<<c<<"c "<<d<<"d "<<std::endl;

		// compute two normalized directions
		cv::Point3d dirS, dirT, normal(a,b,c);
		double lengthNormal = cv::norm(normal);
		// if (c<0.)
		// lengthNormal *= -1;
		normal.x /= lengthNormal;
		normal.y /= lengthNormal;
		normal.z /= lengthNormal;
		dirS = p2-p1;
		double lengthS = cv::norm(dirS);
		dirS.x /= lengthS;
		dirS.y /= lengthS;
		dirS.z /= lengthS;
		dirT.x = normal.y*dirS.z - normal.z*dirS.y;
		dirT.y = normal.z*dirS.x - normal.x*dirS.z;
		dirT.z = normal.x*dirS.y - normal.y*dirS.x;
		double lengthT = cv::norm(dirT);
		dirT.x /= lengthT;
		dirT.y /= lengthT;
		dirT.z /= lengthT;

		// c) compute transformation from camera frame (x,y,z) to plane frame (x,y,z)
		cv::Mat t = (cv::Mat_<double>(3,1) << p1.x, p1.y, p1.z);
		cv::Mat R = (cv::Mat_<double>(3,3) << dirS.x, dirT.x, normal.x, dirS.y, dirT.y, normal.y, dirS.z, dirT.z, normal.z);
		cv::Mat RTt = R.t()*t;



	//	(*marker).markers.resize(3);
	//
	//	// x vector
	//	(*marker).markers[0].header.frame_id = "/cam3d_rgb_optical_frame";
	//	(*marker).markers[0].header.stamp = ros::Time::now();
	//	(*marker).markers[0].ns = "markertest";
	//	(*marker).markers[0].id = 0;
	//
	//    (*marker).markers[0].type = visualization_msgs::Marker::ARROW;
	//    (*marker).markers[0].action = visualization_msgs::Marker::ADD;
	//    (*marker).markers[0].color.a = 1.f;
	//    (*marker).markers[0].color.r = 1.f;
	//    (*marker).markers[0].color.g = 0.f;
	//    (*marker).markers[0].color.b = 0.f;
	//
	//	(*marker).markers[0].scale.x = 0.015;
	//	(*marker).markers[0].scale.y = 0.01;
	//	(*marker).markers[0].scale.z = 0;
	//	(*marker).markers[0].lifetime = ros::Duration();
	////  Normals
	//	(*marker).markers[0].points.resize(2);
	//	(*marker).markers[0].points[0].x=orig_x;
	//	(*marker).markers[0].points[0].y=orig_y;
	//	(*marker).markers[0].points[0].z=orig_z;
	//	(*marker).markers[0].points[1].x=eigen1[0]+orig_x;
	//	(*marker).markers[0].points[1].y=eigen1[1]+orig_y;
	//	(*marker).markers[0].points[1].z=eigen1[2]+orig_z;
	////	Axis of plane
	////	(*marker).markers[0].points[0].x=0;
	////	(*marker).markers[0].points[0].y=0;
	////	(*marker).markers[0].points[0].z=0;
	////	(*marker).markers[0].points[1].x=xpc[0];
	////	(*marker).markers[0].points[1].y=xpc[1];
	////	(*marker).markers[0].points[1].z=xpc[2];
	//
	//	// y vector
	//	(*marker).markers[1].header.frame_id = "/cam3d_rgb_optical_frame";
	//	(*marker).markers[1].header.stamp = ros::Time::now();
	//	(*marker).markers[1].ns = "markertest";
	//	(*marker).markers[1].id = 1;
	//
	//    (*marker).markers[1].type = visualization_msgs::Marker::ARROW;
	//    (*marker).markers[1].action = visualization_msgs::Marker::ADD;
	//    (*marker).markers[1].color.a = 1.f;
	//    (*marker).markers[1].color.r = 0.f;
	//    (*marker).markers[1].color.g = 1.f;
	//    (*marker).markers[1].color.b = 0.f;
	//
	//	(*marker).markers[1].scale.x = 0.015;
	//	(*marker).markers[1].scale.y = 0.01;
	//	(*marker).markers[1].scale.z = 0;
	//	(*marker).markers[1].lifetime = ros::Duration();
	////  Normals
	//	(*marker).markers[1].points.resize(2);
	//	(*marker).markers[1].points[0].x=orig_x;
	//	(*marker).markers[1].points[0].y=orig_y;
	//	(*marker).markers[1].points[0].z=orig_z;
	//	(*marker).markers[1].points[1].x=eigen2[0]+orig_x;
	//	(*marker).markers[1].points[1].y=eigen2[1]+orig_y;
	//	(*marker).markers[1].points[1].z=eigen2[2]+orig_z;
	////	Axis of plane
	////	(*marker).markers[1].points[0].x=0;
	////	(*marker).markers[1].points[0].y=0;
	////	(*marker).markers[1].points[0].z=0;
	////	(*marker).markers[1].points[1].x=ypc[0];
	////	(*marker).markers[1].points[1].y=ypc[1];
	////	(*marker).markers[1].points[1].z=ypc[2];
	//
	//
	//	// z vector
	//	(*marker).markers[2].header.frame_id = "/cam3d_rgb_optical_frame";
	//	(*marker).markers[2].header.stamp = ros::Time::now();
	//	(*marker).markers[2].ns = "markertest";
	//	(*marker).markers[2].id = 2;
	//
	//    (*marker).markers[2].type = visualization_msgs::Marker::ARROW;
	//    (*marker).markers[2].action = visualization_msgs::Marker::ADD;
	//    (*marker).markers[2].color.a = 1.f;
	//    (*marker).markers[2].color.r = 0.f;
	//    (*marker).markers[2].color.g = 0.f;
	//    (*marker).markers[2].color.b = 1.f;
	//
	//	(*marker).markers[2].scale.x = 0.015;
	//	(*marker).markers[2].scale.y = 0.01;
	//	(*marker).markers[2].scale.z = 0;
	//	(*marker).markers[2].lifetime = ros::Duration();
	////  Normals
	//	(*marker).markers[2].points.resize(2);
	//	(*marker).markers[2].points[0].x=orig_x;
	//	(*marker).markers[2].points[0].y=orig_y;
	//	(*marker).markers[2].points[0].z=orig_z;
	//	(*marker).markers[2].points[1].x=eigen3[0]+orig_x;
	//	(*marker).markers[2].points[1].y=eigen3[1]+orig_y;
	//	(*marker).markers[2].points[1].z=eigen3[2]+orig_z;
	//	Axis of plane
	//	(*marker).markers[2].points[0].x=0;
	//	(*marker).markers[2].points[0].y=0;
	//	(*marker).markers[2].points[0].z=0;
	//	(*marker).markers[2].points[1].x=zpc[0];
	//	(*marker).markers[2].points[1].y=zpc[1];
	//	(*marker).markers[2].points[1].z=zpc[2];



		std::vector<cv::Point2f> pointsCamera, pointsPlane;
	// Get matched points to compute transformation matrix
		bool color, distance;
		float maxDistanceToCamera = 30.0;
		cv::Point2f minPlane(1e20,1e20), maxPlane(-1e20,-1e20);
		for(int i=0;i<(*source).rows;i++)
		{
			for(int j=0;j<(*source).cols;j++)
			{
				//Check if point is not black
				color = false;
				if((*source).at<cv::Vec3b>(i,j)[0]!=0 &&(*source).at<cv::Vec3b>(i,j)[1]!=0&&(*source).at<cv::Vec3b>(i,j)[2]!=0)color = true;
				//Check distance to camera
				pcl::PointXYZRGB point = (*input_cloud)[i*(source->cols)+j];
	//			distance = false;
	//			if (point.x*point.x + point.y*point.y + point.z*point.z > maxDistanceToCamera*maxDistanceToCamera) distance = true;

				if(color)
				{
					cv::Mat pointPlane;

					// determine max and min x and y coordinates of the plane
					cv::Mat pointCamera = (cv::Mat_<double>(3,1) << point.x, point.y, point.z);
					if(pointCamera.at<double>(0)==pointCamera.at<double>(0) && pointCamera.at<double>(1)==pointCamera.at<double>(1)&& pointCamera.at<double>(2)==pointCamera.at<double>(2))
					{
						pointPlane = R.t()*pointCamera - RTt;

						pointsPlane.push_back(cv::Point2f(pointPlane.at<double>(0),pointPlane.at<double>(1)));
						pointsCamera.push_back(cv::Point2f(j, i));


						if (minPlane.x>(float)pointPlane.at<double>(0))
						minPlane.x=(float)pointPlane.at<double>(0);
						if (maxPlane.x<(float)pointPlane.at<double>(0))
						maxPlane.x=(float)pointPlane.at<double>(0);
						if (minPlane.y>(float)pointPlane.at<double>(1))
						minPlane.y=(float)pointPlane.at<double>(1);
						if (maxPlane.y<(float)pointPlane.at<double>(1))
						maxPlane.y=(float)pointPlane.at<double>(1);
					}

				}

			}
		}

		//	Initialize Final image


	//	Homography H
		float birdEyeResolution = 300;
		std::vector<cv::Point2f> inputpoint;
		std::vector<cv::Point2f> outputpoint;
		double step = std::max(1.0, (double)pointsCamera.size()/100.0);
	//	cv::Point2f cameraImagePlaneOffset = cv::Point2f((maxpx+minpx)/2.f - (double)(*source).cols/(2*birdEyeResolution), (maxpy+minpy)/2.f - (double)(*source).rows/(2*birdEyeResolution));
		cv::Point2f cameraImagePlaneOffset = cv::Point2f((maxPlane.x+minPlane.x)/2.f - (double)(*source).cols/(2*birdEyeResolution), (maxPlane.y+minPlane.y)/2.f - (double)(*source).rows/(2*birdEyeResolution));
		for(double i=0;i<pointsPlane.size();i+=step)
		{

					inputpoint.push_back(pointsCamera[(int)i]);
					outputpoint.push_back(birdEyeResolution*(pointsPlane[(int)i]-cameraImagePlaneOffset));

	//				std::cout<<pointsCamera[(int)i]<<"pointcam "<<birdEyeResolution*(pointsPlane[(int)i]-cameraImagePlaneOffset)<<"outputpoint";

		}







		cv::Mat H = findHomography(inputpoint, outputpoint);
		std::cout<<H <<"homogr"<<std::endl;
		cv::Mat imageH = cv::Mat::zeros(480,640,CV_8UC3);

		cv::warpPerspective((*source), imageH, H, imageH.size());
		cv::imshow("Homogrtestt", imageH);
		(*source)=imageH;
	}catch(...)
	{
		cv::imshow("Homogrtestt", test);
		std::cout<<"No transformation found:"<<std::endl;
	}



	cv::waitKey(10);







}
