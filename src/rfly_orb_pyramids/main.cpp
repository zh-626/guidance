
// include ROS Libraries
#include <ros/ros.h>

//#include <image_transport/image_transport.h>// image trasport is used for publishing and subscribing an image
#include <cv_bridge/cv_bridge.h> // bridge between ROS and OpenCV
#include <sensor_msgs/image_encodings.h>
#include <sensor_msgs/Image.h>
#include <geometry_msgs/Point.h>

// OpenCV Libraries
#include <opencv2/opencv.hpp>
#include "opencv2/core/core.hpp"
#include "opencv2/highgui/highgui.hpp"
#include "opencv2/features2d/features2d.hpp"

// Standard C++ Headers
#include "stdio.h"
#include <iostream>

using namespace std;
using namespace cv; 

static const string OPENCV_WINDOW = "Image window1";
//static const string I2_WINDOW = "Image window2";

// Create a publisher object
ros::Publisher tform_pub;
geometry_msgs::Point tform_msg;

// Create pointer object to convert from Opencv to ROS and Vice Versa
cv_bridge::CvImagePtr cv_ptr_I1;
cv_bridge::CvImagePtr cv_ptr_I2;

Mat I1_level3,I1_level2,I1_level1,I2_level3,I2_level2,I2_level1;  
Mat u_l3,u_l2,u_l1,v_l3,v_l2,v_l1;

float x_l3[40],x_l2[80],x_l1[160];
float y_l3[30],y_l2[60],y_l1[120];   

bool co = false;
float valid = 1;

int scale = 1;
int delta = 0;
int ddepth = CV_16S;


//..............................START.........................................
// -- Creating u matrix for L1, L2,L3
	Mat x_val(int r, int col){
		float x[col];
		Mat u;
		for(int i=0;i<col;i++) x[i] = i+1;
		Mat A = Mat(1, col, CV_32F,x);		
		cv::repeat(A, r, 1, u);
		return u;
		}
// -- Creating v matrix for L1, L2,L3	
	Mat y_val(int r, int col){
		float y[r];
		Mat v;
		for(int i=0;i<r;i++) y[i] = i+1;
		Mat A = Mat(r, 1, CV_32F,y);		
		cv::repeat(A, 1, col, v);
		return v;
		}
//...............................END................................... 

//..............................START.........................................
Mat gradient_x(Mat I,Mat w){
		Mat grad_x,gxx;		
		// Gradient X
  		Sobel( I/8, grad_x, ddepth, 1, 0, 3, scale, delta, BORDER_DEFAULT );
		grad_x.convertTo(gxx, CV_32FC1);
		Mat gx = w.mul(gxx);
		
		return gx;
		}
Mat gradient_y(Mat I,Mat w){
		Mat grad_y,gyy;
		// Gradient Y
		Sobel( I/8, grad_y, ddepth, 0, 1, 3, scale, delta, BORDER_DEFAULT );
		grad_y.convertTo(gyy, CV_32FC1);
		Mat gy = w.mul(gyy);

		return gy;
		}

Mat gradient_t(Mat I11, Mat I22, Mat w){
		// Gradient It
		Mat I1,I2;
		I11.convertTo(I1,CV_32FC1);
		I22.convertTo(I2,CV_32FC1);
		Mat Itt = I1-I2;
		Mat It = w.mul(Itt);

		return It;
		}
Mat window_w(int a, int r, int c){
		int top = a; int bottom = a;
		int left = a; int right = a;
		Mat image_const = Mat::ones(r-2*a,c-2*a,CV_32FC1);
		Mat w;
		copyMakeBorder( image_const, w, top, bottom, left, right, BORDER_CONSTANT, 0 );
		cv::FileStorage storage4("w.yml", cv::FileStorage::WRITE);
		storage4 << "w" << w;
		storage4.release();

		return w;
		}
Mat image_warping(Mat I, Mat tform){
		
   		/// Set the dst image the same type and size as src
   		Mat dst = Mat::zeros( I.rows, I.cols, I.type() );
		/// Apply the Affine Transform just found to the src image
   		warpAffine( I, dst, tform, dst.size() );
		return dst;
}
//...............................END..................................
Mat iteration(Mat I1,Mat I2, Mat u, Mat v, int a){
			
		Mat w =  window_w(a,I1.rows, I1.cols); 
		Mat gx = gradient_x(I1,w);
		Mat gy = gradient_y(I1,w);
		Mat gt = gradient_t(I1,I2,w);
		
		Mat P = gx.mul(u);
		Mat Q = gx.mul(v);
		Mat R = gy.mul(u);
		Mat S = gy.mul(v);

		Mat A1;
		A1.push_back(gx.reshape(1,gx.rows*gx.cols));
		A1.push_back(P.reshape(1,P.rows*P.cols));
		A1.push_back(Q.reshape(1,Q.rows*Q.cols));
		A1.push_back(gy.reshape(1,gy.rows*gy.cols));
		A1.push_back(R.reshape(1,R.rows*R.cols));
		A1.push_back(S.reshape(1,S.rows*S.cols));

		Mat A2 = A1.reshape(1,6);
		Mat A; transpose(A2,A);	

		Mat B = gt.reshape(1,gt.rows*gt.cols);

		Mat At; transpose(A,At);
		Mat Bt; transpose(B,Bt);
		Mat C1; invert(At*A,C1);
		Mat aff = C1*At*B;

		Mat t_form = Mat::zeros(2,3,CV_32FC1);	
		
		t_form.at<float>(0,0)=1+aff.at<float>(1,0);
		t_form.at<float>(0,1)=aff.at<float>(2,0);
		t_form.at<float>(0,2)=aff.at<float>(0,0);

		t_form.at<float>(1,0)=aff.at<float>(4,0);
		t_form.at<float>(1,1)=1+aff.at<float>(5,0);
		t_form.at<float>(1,2)=aff.at<float>(3,0);
		return t_form;

}
//..............................START.........................................
//This is the main function that uses above defined functions to calculate the optical flow
Mat pyramids(Mat I1, Mat I2){
 				
		pyrDown( I1, I1_level1, Size( I1.cols/2, I1.rows/2 ));
		pyrDown( I1_level1, I1_level2, Size( I1_level1.cols/2, I1_level1.rows/2));
		pyrDown( I1_level2, I1_level3, Size( I1_level2.cols/2, I1_level2.rows/2)); 


		pyrDown( I2, I2_level1, Size( I2.cols/2, I1.rows/2 ));
		pyrDown( I2_level1, I2_level2, Size( I2_level1.cols/2, I2_level1.rows/2));
		pyrDown( I2_level2, I2_level3, Size( I2_level2.cols/2, I2_level2.rows/2));
		
		float a1,a2,a3,b1,b2,b3;
//------------------------Processing Level 3--------------------------------
		Mat t_form3 = Mat::zeros(2,3,CV_32FC1);
		Mat I1_level3_1 = Mat::zeros(I1_level3.rows,I1_level3.cols,CV_32FC1);

		Mat t_form3_1= iteration(I1_level3, I2_level3, u_l3, v_l3,0);
		t_form3_1.copyTo(t_form3);		
		I1_level3_1 = image_warping(I1_level3, t_form3);

		Mat t_form3_2= iteration(I1_level3_1, I2_level3, u_l3, v_l3,5);
		t_form3 = (t_form3_1+t_form3_2)/2;
		
		t_form3.at<float>(0,2) = t_form3_1.at<float>(0,2) + t_form3_2.at<float>(0,2);
		t_form3.at<float>(1,2) = t_form3_1.at<float>(1,2) + t_form3_2.at<float>(1,2);
		I1_level3_1 = image_warping(I1_level3, t_form3);

		Mat t_form3_3= iteration(I1_level3_1, I2_level3, u_l3, v_l3,5);
		t_form3 = (t_form3_1+t_form3_2+t_form3_3)/3;
		t_form3.at<float>(0,2) = t_form3_1.at<float>(0,2) + t_form3_2.at<float>(0,2)+ t_form3_3.at<float>(0,2);
		t_form3.at<float>(1,2) = t_form3_1.at<float>(1,2) + t_form3_2.at<float>(1,2)+ t_form3_3.at<float>(1,2);
		I1_level3_1 = image_warping(I1_level3, t_form3);
		
		a3 = t_form3.at<float>(0,2);
		b3 = t_form3.at<float>(1,2);
//------------------------Processing Level 2--------------------------------
// initialising
		Mat t_form2 = Mat::zeros(2,3,CV_32FC1);
		t_form3.copyTo(t_form2);
		t_form2.at<float>(0,2) = 2*t_form3.at<float>(0,2);
		t_form2.at<float>(1,2) = 2*t_form3.at<float>(1,2);
		Mat I1_level2a = image_warping(I1_level2, t_form2);//I1_level2a acts as original image I1 for level 2
//.............		
//Start Iteration 1
		Mat t_form2_1= iteration(I1_level2a, I2_level2, u_l2, v_l2,10);
		t_form2_1.copyTo(t_form2);		
		Mat I1_level2_1 = image_warping(I1_level2a, t_form2);
//Start Iteration 2
		Mat t_form2_2= iteration(I1_level2_1, I2_level2, u_l2, v_l2,10);
		t_form2 = (t_form2_1+t_form2_2)/2;		
		t_form2.at<float>(0,2) = t_form2_1.at<float>(0,2) + t_form2_2.at<float>(0,2);
		t_form2.at<float>(1,2) = t_form2_1.at<float>(1,2) + t_form2_2.at<float>(1,2);
		I1_level2_1 = image_warping(I1_level2a, t_form2);
//Start Iteration 3
		Mat t_form2_3= iteration(I1_level2_1, I2_level2, u_l2, v_l2,10);
		t_form2 = (t_form2_1+t_form2_2+t_form2_3)/3;
		t_form2.at<float>(0,2) = t_form2_1.at<float>(0,2) + t_form2_2.at<float>(0,2)+ t_form2_3.at<float>(0,2);
		t_form2.at<float>(1,2) = t_form2_1.at<float>(1,2) + t_form2_2.at<float>(1,2)+ t_form2_3.at<float>(1,2);
		I1_level2_1 = image_warping(I1_level2a, t_form2);

		a2 = t_form2.at<float>(0,2);
		b2 = t_form2.at<float>(1,2);
			
//------------------------Processing Level 1-------------------------------
	//Mat flow = iteration(I1_level1, I2_level1, u_l1, v_l1);
//..........................................................................
		Mat a,b,c;
		t_form3.copyTo(a);
		t_form2.copyTo(b);	
		Mat tform = (a+b)/2;
	
		tform.at<float>(0,2)=(8*a3+4*a2);			
		tform.at<float>(1,2)=(8*b3+4*b2);

		Mat pt1 = (Mat_ <float>(3,1)<<160,120,1);
		Mat pt = (Mat_ <float>(2,1)<<160,120);
		Mat flow = tform*pt1 - pt;

return flow;
}
//.............................END.........................................

// Call Back Function
void imageCb(const sensor_msgs::ImageConstPtr& msg){
	if(!co ){
		co = true;
		//ROS_INFO_STREAM(" Alhamdulilah" );  
		try{
       		cv_ptr_I1 = cv_bridge::toCvCopy(msg, sensor_msgs::image_encodings::MONO8);
		}
	
		catch (cv_bridge::Exception& e){
		ROS_ERROR("cv_bridge exception: %s", e.what());
		return;}
	}
	
	else{
	
		//ROS_INFO_STREAM(" ALLAH hu AKBAR" );
		try{
       		cv_ptr_I2 = cv_bridge::toCvCopy(msg, sensor_msgs::image_encodings::MONO8);
		}
	
		catch (cv_bridge::Exception& e){
		ROS_ERROR("cv_bridge exception: %s", e.what());
		return;}
		Mat flow_pyramid = pyramids(cv_ptr_I1->image, cv_ptr_I2->image);

		tform_msg.x = flow_pyramid.at<float>(0,0);
		tform_msg.y = flow_pyramid.at<float>(1,0);
		tform_msg.z = 1;
		cout << flow_pyramid<<endl;

		tform_pub.publish(tform_msg);
		cv_ptr_I2->image.copyTo(cv_ptr_I1->image);

	}

};
 
int main(int argc, char** argv)
 {
	u_l3 = x_val(30,40);u_l2 = x_val(60,80);u_l1 = x_val(120,160);
	v_l3 = y_val(30,40);v_l2 = y_val(60,80);v_l1 = y_val(120,160);
	
   	ros::init(argc, argv, "orb_pyramids");

//create a ros node handle
	ros::NodeHandle nh;
	cv::namedWindow(OPENCV_WINDOW);
	//cv::namedWindow(I2_WINDOW);
// Create a subscriber object
	ros::Subscriber sub = nh.subscribe( "guidance/left_image" , 3 , &imageCb);//guidance/left_image
// publish the constant velocities on turtle2/cmd_vel
	tform_pub = nh.advertise<geometry_msgs::Point> ( "rfly/orbpyramids", 3);
   	ros::spin();
// rosrun topic_tools drop /guidance/left_image 1 2
 }
