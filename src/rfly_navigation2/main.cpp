// include ROS Libraries
#include <ros/ros.h>
#include <geometry_msgs/PointStamped.h>
#include <sensor_msgs/Imu.h>
#include <sensor_msgs/PointCloud.h>
#include <sensor_msgs/PointCloud2.h>
#include <sensor_msgs/point_cloud_conversion.h>
#include <std_msgs/Bool.h>
// OpenCV Libraries
#include <opencv2/opencv.hpp>
#include "opencv2/imgproc/imgproc.hpp"
#include "opencv2/core/core.hpp"
#include "opencv2/highgui/highgui.hpp"
// C++ Libraries
#include <iostream>
#include <stdio.h>
#include <math.h>

using namespace std;
using namespace cv;

// Create a publisher object for distance
ros::Publisher pub_dist;
geometry_msgs::PointStamped dist_msg;
geometry_msgs::PointStamped dist_msg_pub;

// Create a publisher object for velocity
ros::Publisher pub_vel;
geometry_msgs::PointStamped vel_msg;

// Create a publisher object for feature type
ros::Publisher pub_feature;
std_msgs::Bool feature_msg;

// Initialising Certain constant Matrices

Mat Cbe1 = Mat::eye(3, 3, CV_64FC1);
Mat Cbe2 = Mat::eye(3, 3, CV_64FC1);
Mat H1 = (Mat_<double>(1, 7) << 0, 0, 0, 1, 0, 0, 0);
Mat H3 = (Mat_<double>(3, 7) << 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0);
Mat Q = 0.01 * (Mat::eye(7, 7, CV_64FC1));
Mat R3 = 0.01 * (Mat::eye(3, 3, CV_64FC1));
Mat X = (Mat_<double>(7, 1) << 0, 0, 0, 0, 0, 0, 0);
Mat P = Mat::eye(7, 7, CV_64FC1);
Mat w = Mat::zeros(3, 1, CV_64FC1);
Mat camera_param = (Mat_<double>(3, 3) << 320, 0, 160, 0, 320, 120, 0, 0, 1);

double R1 = 0.001;
double a, b, c, d, a1, b1, c1, d1, home_ds, ds, ds2, time1, time2, delt, Tx_pyr, Ty_pyr, dt_pyr;
double x_d = 0;
double y_d = 0;
bool co1 = false;
bool co2 = false;
bool co3 = false;
const int que_len = 5;
deque<geometry_msgs::PointStamped> dist_msg_que(que_len);
deque<geometry_msgs::PointStamped> vel_msg_que(que_len);
int orb_call_cnt = 0;
int pry_call_cnt = 0;

//...................Function Definition for Skew Matrix...........
Mat skewmatrix(Mat v)
{
	double v1 = v.at<double>(0, 0);
	double v2 = v.at<double>(1, 0);
	double v3 = v.at<double>(2, 0);
	Mat R = (Mat_<double>(3, 3) << 0, -v3, v2, v3, 0, -v1, -v2, v1, 0);
	return R;
}
//.................................................................
//...................LKF_constants Definition......................
struct lkf_constants
{
	Mat Ad;
	Mat G;
};
lkf_constants calculate_lkf_constants(Mat Acc, Mat Cbe, double Ts)
{
	lkf_constants L;
	Mat F = Acc;
	double g = 9.8;
	Mat Ad = (Mat_<double>(7, 7) << 1, 0, 0, 0, -Ts, 0, 0, 0, 1, 0, 0, 0, -Ts, 0, 0, 0, 1, 0, 0, 0, -Ts, 0, 0, -Ts, 1, 0, 0, Ts * Ts / 2, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1);

	double c31 = Cbe.at<double>(2, 0);
	double c32 = Cbe.at<double>(2, 1);
	double c33 = Cbe.at<double>(2, 2);

	double f1 = F.at<double>(0, 0);
	double f2 = F.at<double>(1, 0);
	double f3 = F.at<double>(2, 0);

	Mat G = (Mat_<double>(7, 1) << (f1 + c31 * g) * Ts, (f2 + c32 * g) * Ts, (f3 + c33 * g) * Ts, -(f3 + c33 * g) * (Ts * Ts) / 2, 0, 0, 0);

	L.Ad = Ad;
	L.G = G;
	return L;
}
//..........................................................
//...................LKF_Predict and LKF_Update Definition..
struct KF
{
	Mat X;
	Mat P;
};
void lkf_predict(Mat A, Mat G)
{
	Mat At;
	transpose(A, At);
	X = A * X + G;
	P = A * P * At + Q;
}
void lkf_update(Mat im_vel, int valid)
{

	double Vx = im_vel.at<double>(0, 0);
	double Vy = im_vel.at<double>(1, 0);

	Mat I = Mat::eye(7, 7, CV_64FC1);

	double Z1 = ds2;
	Mat Z3 = (Mat_<double>(3, 1) << Vx, Vy, ds2);
	if (valid == 1)
	{
		Mat H3t;
		transpose(H3, H3t);

		Mat y = Z3 - H3 * X;
		Mat S = H3 * P * H3t + R3;
		Mat Si;
		invert(S, Si);
		Mat K = P * H3t * Si;
		X = X + K * y;
		P = (I - K * H3) * P;
	}
	else
	{
		Mat H1t;
		transpose(H1, H1t);

		Mat y = Z1 - H1 * X;
		Mat S = H1 * P * H1t + R1;
		Mat Si;
		invert(S, Si);
		Mat K = P * H1t * Si;
		X = X + K * y;
		P = (I - K * H1) * P;
	}
}
//..........................................................
//..... Function Definition of Get Rotation.................
Mat getrotation(double a, double b, double c, double d)
{
	//The function converts quaternions into Cbe matrix
	double R11 = a * a + b * b - c * c - d * d;
	double R12 = 2 * (b * c - a * d);
	double R13 = 2 * (b * d + a * c);
	double R21 = 2 * (b * c + a * d);
	double R22 = a * a - b * b + c * c - d * d;
	double R23 = 2 * (c * d - a * b);
	double R31 = 2 * (b * d - a * c);
	double R32 = 2 * (c * d + a * b);
	double R33 = a * a - b * b - c * c + d * d;

	Mat Cbe = (Mat_<double>(3, 3) << R11, R12, R13, R21, R22, R23, R31, R32, R33); // 3.59 body to E
	return Cbe;
}
//.........................................................
//......Function Definition of Get Angular Rates...........
Mat quat2rate(double delt, double a, double b, double c, double d, double a1, double b1, double c1, double d1)
{
	double dela = a - a1;
	double delb = b - b1;
	double delc = c - c1;
	double deld = d - d1;

	Mat A = (Mat_<double>(4, 1) << dela / delt, delb / delt, delc / delt, deld / delt);
	Mat B = (Mat_<double>(4, 4) << a, -b, -c, -d, b, a, -d, c, c, d, a, -b, d, -c, b, a);
	Mat Bi;
	invert(B, Bi);
	Mat wb = 2 * 180 / M_PI * (Bi * A);
	Mat w = Mat::zeros(3, 1, CV_64FC1);
	w.at<double>(0, 0) = wb.at<double>(1, 0);
	w.at<double>(1, 0) = wb.at<double>(2, 0);
	w.at<double>(2, 0) = wb.at<double>(3, 0);
	return w;
}
//.........................................................
//..........Function Definition for Image Velocity.........
int img_vel_cnt = 0;
double vel_total_time = 0;
Mat image_velocity(double u, double v, double Ts)
{
	img_vel_cnt++;
	ros::Time begin_time = ros::Time::now();

	Mat P1 = (Mat_<double>(2, 1) << 160, 120);
	Mat P2 = (Mat_<double>(2, 1) << 160 - u, 120 - v);

	Mat Cbi = (Mat_<double>(3, 3) << 0, 1, 0, -1, 0, 0, 0, 0, 1);
	Mat Cbit;
	transpose(Cbi, Cbit);

	Mat wc = Cbi * w;

	Mat inv_camera_param;
	invert(camera_param, inv_camera_param);

	Mat e3 = (Mat_<double>(3, 1) << 0, 0, 1);
	Mat e3t;
	transpose(e3, e3t);

	Mat p1_temp = (Mat_<double>(3, 1) << P1.at<double>(0, 0), P1.at<double>(0, 1), 1);
	Mat p2_temp = (Mat_<double>(3, 1) << P2.at<double>(0, 0), P2.at<double>(0, 1), 1);

	Mat p1 = inv_camera_param * p1_temp;
	Mat p2 = inv_camera_param * p2_temp;

	Mat Ep1 = -ds * p1 / (e3t * Cbe1 * p1);
	Mat Ep2 = -ds2 * p2 / (e3t * Cbe2 * p2);

	Mat I = Mat::eye(3, 3, CV_64FC1);
	Mat sk_w = skewmatrix(wc);
	Mat Stav = (Ep1 - (I + Ts * sk_w) * Ep2) / Ts;
	//Stav = (Ep1-(eye(3,3)+Ts*skewmatrix(pi/180*w))*Ep2)/Ts;

	double Tx = Stav.at<double>(0, 0);
	double Ty = Stav.at<double>(0, 1);
	Mat temp1 = (Mat_<double>(3, 1) << Tx, Ty, 0);
	Mat vel = Cbit * temp1;

	vel_total_time += (ros::Time::now() - begin_time).toSec();
	if (img_vel_cnt % 10 == 0) {
		cout << "img_vel_cnt: " << img_vel_cnt << ", vel_total_time: " << vel_total_time << endl;
		cout << "orb_call_cnt: " << orb_call_cnt << ", pry_call_cnt: " << pry_call_cnt << endl;
	}
	return vel;
}

// Call Back Function
int imu_cnt = 0;
const int imu_int = 1;
double imu_total_time = 0;
void imuCb(const sensor_msgs::Imu &msg)
{
	imu_cnt++;
	ros::Time begin_time = ros::Time::now();
	if (imu_cnt % imu_int != 0)
		return;
	// cout << "w: " << msg.orientation.w << ", x: " << msg.orientation.x << ", y: " << msg.orientation.y << ", z: " << msg.orientation.z << endl;
	// cout << "ax: " << msg.linear_acceleration.x << ", ay: " << msg.linear_acceleration.y << ", az: " << msg.linear_acceleration.z << endl;
	// cout << "wx: " << msg.angular_velocity.x << ", wy: " << msg.angular_velocity.y << ", wz: " << msg.angular_velocity.z << endl;
	if (!co1)
	{
		//	std_msgs::Header h = msg->header;
		double sec = msg.header.stamp.sec;
		double nsec = msg.header.stamp.nsec;
		time2 = sec + nsec / 1000000000;
		co1 = true;
		cout << X << endl;
	}
	else
	{
		time1 = time2;
		//	std_msgs::Header h = msg->header;
		double sec = msg.header.stamp.sec;
		double nsec = msg.header.stamp.nsec;
		time2 = sec + nsec / 1000000000;
		delt = time2 - time1;

		double acc1 = msg.linear_acceleration.x;
		double acc2 = msg.linear_acceleration.y;
		double acc3 = msg.linear_acceleration.z;
		Mat Acc = (Mat_<double>(3, 1) << acc1, acc2, acc3);

		a1 = a;
		b1 = b;
		c1 = c;
		d1 = d;
		a = msg.orientation.w;
		b = msg.orientation.x;
		c = msg.orientation.y;
		d = msg.orientation.z;

		Cbe2.copyTo(Cbe1);
		Cbe2 = getrotation(a, b, c, d);
		// w = quat2rate(delt, a, b, c, d, a1, b1, c1, d1);
		w.at<double>(0, 0) = msg.angular_velocity.x;
		w.at<double>(1, 0) = msg.angular_velocity.y;
		w.at<double>(2, 0) = msg.angular_velocity.z;

		lkf_constants lkc = calculate_lkf_constants(Acc, Cbe2, delt);
		lkf_predict(lkc.Ad, lkc.G);
		Mat im_vel = Mat::zeros(2, 1, CV_64FC1);
		lkf_update(im_vel, 0);

		double tempx = X.at<double>(0, 0);
		double tempy = X.at<double>(1, 0);
		double tempz = X.at<double>(2, 0);

		Mat temp_X = (Mat_<double>(3, 1) << tempx, tempy, tempz);
		Mat X_world = Cbe2 * temp_X;
		double x_ds = x_d;
		double y_ds = y_d;

		x_d = x_ds + delt * X_world.at<double>(0, 0);
		y_d = y_ds + delt * X_world.at<double>(1, 0);
		double z_d = X.at<double>(3, 0);

		geometry_msgs::PointStamped dist_msg_back;
		dist_msg_back.header = msg.header;
		dist_msg_back.point.x = x_d;
		dist_msg_back.point.y = y_d;
		dist_msg_back.point.z = -z_d;

		auto dist_msg_front = dist_msg_que.front();
		dist_msg_que.pop_front();
		dist_msg.header = dist_msg_back.header;
		dist_msg.point.x += (dist_msg_back.point.x - dist_msg_front.point.x) / que_len;
		dist_msg.point.y += (dist_msg_back.point.y - dist_msg_front.point.y) / que_len;
		dist_msg.point.z += (dist_msg_back.point.z - dist_msg_front.point.z) / que_len;
		dist_msg_que.push_back(dist_msg_back);

		dist_msg_pub = dist_msg;
		// dist_msg_pub.point.x = (dist_msg.point.x - 0.003*imu_cnt)*1.28;
		// dist_msg_pub.point.y = (dist_msg.point.y - 0.0026*imu_cnt)*1.2;
		dist_msg_pub.point.x = (dist_msg.point.x - 0.0015*imu_cnt)*1.2;
		dist_msg_pub.point.y = (dist_msg.point.y - 0.0010*imu_cnt)*1.2;
		dist_msg_pub.point.z += 0.8;

		geometry_msgs::PointStamped vel_msg_back;
		vel_msg_back.header = msg.header;
		vel_msg_back.point.x = X_world.at<double>(0, 0);
		vel_msg_back.point.y = X_world.at<double>(1, 0);
		vel_msg_back.point.z = X_world.at<double>(2, 0);

		auto vel_msg_front = vel_msg_que.front();
		vel_msg_que.pop_front();
		vel_msg.header = vel_msg_back.header;
		vel_msg.point.x += (vel_msg_back.point.x - vel_msg_front.point.x) / que_len;
		vel_msg.point.y += (vel_msg_back.point.y - vel_msg_front.point.y) / que_len;
		vel_msg.point.z += (vel_msg_back.point.z - vel_msg_front.point.z) / que_len;
		vel_msg_que.push_back(vel_msg_back);

		// pub_dist.publish(dist_msg);
		// pub_vel.publish(vel_msg);

		// cout << "x_d=" << x_d << " y_d=" << y_d << " z_d=" << z_d << endl;
		// cout << "vel_x=" << X_world.at<double>(0, 0) << " vel_y=" << X_world.at<double>(1, 0) << " vel_z=" << X_world.at<double>(2, 0) << endl;
	}

	imu_total_time += (ros::Time::now() - begin_time).toSec();
	if (imu_cnt % 10 == 0) {
		cout << "imu_cnt: " << imu_cnt << ", imu_total_time: " << imu_total_time << endl;
	}
};

// Call Back Function
void floworbCb(const geometry_msgs::Point &msg)
{
	double Tx = msg.x;
	double Ty = msg.y;
	double dt = msg.z;
	Mat im_vel;
	if (dt == 0)
	{
		im_vel = image_velocity(Tx_pyr, Ty_pyr, dt_pyr);
		feature_msg.data = false;
		pry_call_cnt++;
		// cout << "pyramids" << endl;
	}
	else
	{
		im_vel = image_velocity(Tx, Ty, dt);
		feature_msg.data = true;
		orb_call_cnt++;
		// cout << "orb" << endl;
	}
	lkf_update(im_vel, 1);
	pub_feature.publish(feature_msg);
};

void flowpyrCb(const geometry_msgs::Point &msg)
{
	Tx_pyr = msg.x;
	Ty_pyr = msg.y;
	dt_pyr = msg.z;
};

double pointCloud2ToZ(const sensor_msgs::PointCloud2 &msg)
{
	sensor_msgs::PointCloud out_pointcloud;
	sensor_msgs::convertPointCloud2ToPointCloud(msg, out_pointcloud);
	// for (int i=0; i<out_pointcloud.points.size(); i++) {
	// 	cout << out_pointcloud.points[i].x << ", " << out_pointcloud.points[i].y << ", " << out_pointcloud.points[i].z << endl;
	// }
	cout << "------" << endl;
	return out_pointcloud.points[0].z;
}

void lidarCb(const sensor_msgs::PointCloud2 &msg)
{
	if (!co2)
	{
		// calibrate in different home point
		home_ds = 1.195547;
		ds2 = 0;
		ds = ds2;
		co2 = true;
	}
	else
	{
		ds = ds2;
		ds2 = pointCloud2ToZ(msg) - home_ds;
		if (ds2 < 0.05)
			ds2 = ds;
		// cout <<"distance is "<<ds2<<endl;
	}
}
//.........................................................

int main(int argc, char **argv)
{
	a = 1;
	b = 0;
	c = 0;
	d = 0;
	a1 = 1;
	b1 = 0;
	c1 = 0;
	d1 = 0;
	ds = 1;
	ds2 = 1;
	time1 = 0;
	time2 = 0;
	delt = 0.05;
	Tx_pyr = 0;
	Ty_pyr = 0;
	dt_pyr = 0.05;

	ros::init(argc, argv, "orb");
	//create a ros node handle
	ros::NodeHandle nh;
	ros::Subscriber sub1 = nh.subscribe("sim/imu", 1, &imuCb);		   //guidance/left_image
	ros::Subscriber sub2 = nh.subscribe("sim/lidar", 1, &lidarCb); //guidance/left_image
	ros::Subscriber sub3 = nh.subscribe("rfly/orb", 1, &floworbCb);		   //guidance/left_image
	ros::Subscriber sub4 = nh.subscribe("rfly/pyramids", 1, &flowpyrCb);   //guidance/left_image

	pub_dist = nh.advertise<geometry_msgs::PointStamped>("rfly/dist", 1);
	pub_vel = nh.advertise<geometry_msgs::PointStamped>("rfly/velocity", 1);
	pub_feature = nh.advertise<std_msgs::Bool>("rfly/feature", 1);

	ros::Rate loop_rate(20);
	while (ros::ok())
	{
		pub_dist.publish(dist_msg_pub);
		pub_vel.publish(vel_msg);
		ros::spinOnce();
		loop_rate.sleep();
	}
}
