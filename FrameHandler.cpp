#include "include/TCPClient.h"
#include "yolo_v2_class.hpp"
#include <string>
#include<mutex>

using namespace cv;
using namespace std;

#define OPENCV 1
#ifndef YOLO_V2_CLASS_HPP
struct bbox_t {
	unsigned int x, y, w, h;       // (x,y) - top-left corner, (w, h) - width & height of bounded box
	float prob;                    // confidence - probability that the object was found correctly
	unsigned int obj_id;           // class of object - from range [0, classes-1]
	unsigned int track_id;         // tracking id for video (0 - untracked, 1 - inf - tracked object)
	unsigned int frames_counter;   // counter of frames on which the object was detected
	float x_3d, y_3d, z_3d;        // center of object (in Meters) if ZED 3D Camera is used
};
#endif
class FrameHandler {

private:
	void* currentFrame;
	int height;
	int width;
	int rowBytes;
	std::mutex*	mutex;
	std::condition_variable*	signalCondition;
	bool* newFrame;
	Detector yolo;
	CascadeClassifier cascade;
	vector<vector<bbox_t>>* detections;
	int camNum;
	std::mutex* tcpLock;
	bool* sending;
	condition_variable* tcpCV;


public:

	FrameHandler(int cN, vector<vector<bbox_t>>* d, std::mutex* tL, bool* s, condition_variable* tCV) :
		currentFrame(nullptr),
		height(0),
		width(0),
		rowBytes(0),
		camNum(cN),
		detections(d),
		tcpLock(tL),
		sending(s),
		tcpCV(tCV),
		yolo("C:/Users/darklake/repos/darknet/cfg/yolov3.cfg", "C:/Users/darklake/source/repos/weights/yolov3.weights", 0)
	{
		namedWindow(to_string(camNum), WINDOW_AUTOSIZE);
		String face = "C:/Users/darklake/source/repos/BMPort/face.xml";
		cascade.load(face);

	}

	void setup(std::mutex* m, std::condition_variable* cv, bool* newF) {
		mutex = m;
		signalCondition = cv;
		newFrame = newF;
	}

	void accFrame(void* fb, int h, int w, int rowB) 
	{
		height = h; 
		width = w;
		rowBytes = rowB;
		currentFrame = fb;
	} 

	bool calFrame()  
	{

		std::unique_lock<std::mutex> guard(*mutex);
		signalCondition->wait(guard, [nf = this->newFrame] {return *nf; });
		*newFrame = false;
		cv::Mat mat = cv::Mat(height, width, CV_8UC2, currentFrame, rowBytes);
		guard.unlock();
		if (mat.rows == 0)
			return false;
		cv::cvtColor(mat, mat, 108);

		std::vector<bbox_t> res = detectYOLO(mat);
		std::unique_lock<std::mutex> lk(*tcpLock);
		tcpCV->wait(lk, [s = *sending] {return !s; });
		for (int i = 0; i < res.size(); i++) {
			//cout << "ID: " << res[i].obj_id << endl; 
			//cout << "Prob: " << res[i].prob << endl;
			//cout << "Track: " << res[i].track_id << endl;
			//cout << "Frames: " << res[i].frames_counter << endl;
			if (res[i].prob > 0.1)
				(*detections)[camNum].push_back(res[i]);
		}
		draw(mat, res);
		return true;
	}
	
	std::vector<bbox_t> detectYOLO(Mat& img) {
		return yolo.detect(img);
	}
	void draw(Mat& img, std::vector<bbox_t> res) {
		for (size_t i = 0; i < res.size(); i++)
		{
			Scalar color = Scalar(255, 0, 0);
			if (res[i].prob > 0.3)
				rectangle(img, Point(res[i].x, res[i].y), Point(res[i].x + res[i].w, res[i].y + res[i].h), color);
		}

		imshow(to_string(camNum), img);
		if (cv::waitKey(1));
			return; 
	}


	void detectCVRedCircle(Mat& img, double scale) {
	
		//Converting image from BGR to HSV color space.
		Mat hsv;
		cv::medianBlur(img, img, 3);
		cvtColor(img, hsv, COLOR_BGR2HSV);

		Mat mask1, mask2;
		// Creating masks to detect the upper and lower red color.
		cv::GaussianBlur(hsv, hsv, cv::Size(9, 9), 3, 3);
		inRange(hsv, Scalar(160, 100, 100), Scalar(179, 255, 255), mask2);
		inRange(hsv, Scalar(150, 50, 50), Scalar(190, 255, 255), mask1);
		// Generating the final mask
		mask1 = mask1 + mask2;
		cv::Mat red_hue_image;

		std::vector<cv::Vec3f> circles;
		cv::HoughCircles(mask1, circles, 3, 1, mask1.rows / 8, 100, 20, 0, 0);
		for (size_t current_circle = 0; current_circle < circles.size(); ++current_circle) {
			cv::Point center(std::round(circles[current_circle][0]), std::round(circles[current_circle][1]));

		}
	}

	
	void detectCVFace(Mat& img, double scale) {

		vector<Rect> faces;
		Mat gray;
		cvtColor(img, gray, COLOR_BGR2GRAY);

		cascade.detectMultiScale(gray, faces, 1.1, 2, 0 | CASCADE_SCALE_IMAGE, Size(60, 60));
		for (size_t i = 0; i < faces.size(); i++)
		{
			Rect r = faces[i];

		}
	}
};