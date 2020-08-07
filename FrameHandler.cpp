#include "include/TCPClient.h"
#include "yolo_v2_class.hpp"

using namespace cv;
using namespace std;

#define OPENCV 1

class FrameHandler {

private:
	void* currentFrame;
	int height;
	int width;
	int rowBytes;
	std::mutex*	mutex;
	std::condition_variable*	signalCondition;
	bool* newFrame;
	TCPClient tcp;
	Detector yolo;
	CascadeClassifier cascade;


public:

	FrameHandler(std::mutex* m, std::condition_variable* cv, bool* newFrame) :
		mutex(m),
		signalCondition(cv),
		newFrame(newFrame),
		currentFrame(nullptr),
		height(0),
		width(0),
		rowBytes(0),
		yolo("C:/Users/darklake/repos/darknet/cfg/yolov3.cfg", "C:/Users/darklake/source/repos/weights/yolov3.weights", 1)
	{
		namedWindow("window", WINDOW_AUTOSIZE);
		String face = "C:/Users/darklake/source/repos/BMPort/face.xml";
		cascade.load(face);
		tcp.setup();
		while (!tcp.isReady()) {}
		//tcp.setup(HOST, PORT);
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
		//image_t img;
		//img.h = height;
		//img.w = width;
		//img.c = 3;
		//img.data = (float*)currentFrame;
		//std::vector<bbox_t> res = yolo.detect(mat);
		//string send = "{\"camera\" : \"3\",";
		//send += "\"data\" : [";
		//for (size_t i = 0; i < res.size(); i++)
		//{
		//	Scalar color = Scalar(255, 0, 0);
		//	
		//	rectangle(mat, Point(res[i].x, res[i].y), Point(res[i].x + res[i].w, res[i].y + res[i].h), color);
		//	int newX =  ((100 * (res[i].x + (res[i].w / 2)) / 1920));
		//	int newY =  ((100 * (res[i].y + (res[i].h / 2)) / 1080));
		//	cout << "x " << res[i].x << endl;
		//	cout << "w " << res[i].w << endl;
		//	cout << res[i].x + (res[i].w / 2) << endl;
	

		//	send += "{\"id\":" + to_string(res[i].obj_id) + ", \"x\":" + to_string(newY) + ", \"y\":" + to_string(newX) + "}";

		//}
		//send += "], \"kill\" : []} {END}";
		//sendTCPData(send);
		////cout << res.size() << endl;
		//imshow("window", mat);
		//if (cv::waitKey(1));
		//	return false; 
		

		detectAndDraw(mat, cascade, 1); 

		return true;
	}
	
	void sendTCPData(string s) 
	{
		


		//s = "{\"data\" : [{qidq:1, qxq:10, qyq:100},{qidq:2, qxq:100, qyq:200}], qkillq : [{qidq:1}]}{END}";
		//replace(s.begin(), s.end(), 'q', '"');
		/*tcp.setup();
		while (!tcp.isReady()) {}*/
		tcp.Send(s.c_str());
		tcp.receive();
		cout << "after" << endl;
		//tcp.exit();

	}

	void detectAndDraw2(Mat& img, CascadeClassifier& cascade, double scale) {
	
		//Converting image from BGR to HSV color space.
		Mat hsv;
		cv::medianBlur(img, img, 3);
		cvtColor(img, hsv, COLOR_BGR2HSV);

		Mat mask1, mask2;
		// Creating masks to detect the upper and lower red color.
		cv::GaussianBlur(hsv, hsv, cv::Size(9, 9), 3, 3);
		//inRange(hsv, Scalar(0, 100, 100), Scalar(10, 255, 255), mask1);
		inRange(hsv, Scalar(160, 100, 100), Scalar(179, 255, 255), mask2);
		inRange(hsv, Scalar(150, 50, 50), Scalar(190, 255, 255), mask1);
		// Generating the final mask
		//mask1 = mask1 + mask2;
		cv::Mat red_hue_image;
		//cv::addWeighted(mask1, 1.0, mask2, 1.0, 0.0, red_hue_image);
		//cv::GaussianBlur(mask1, mask1, cv::Size(9, 9), 2, 2);
		std::vector<cv::Vec3f> circles;
		cv::HoughCircles(mask1, circles, 3, 1, mask1.rows / 8, 100, 20, 0, 0);
		string send = "{\"data\" : [";
		for (size_t current_circle = 0; current_circle < circles.size(); ++current_circle) {
			cv::Point center(std::round(circles[current_circle][0]), std::round(circles[current_circle][1]));
			int radius = std::round(circles[current_circle][2]);
			
			cv::circle(img, center, radius, cv::Scalar(0, 255, 0), 5);
			int newX = 100 -((100 * circles[current_circle][0]) / 1920);
			int newY = ((100 * circles[current_circle][1]) / 1080);
			send += "{\"id\":" + to_string(current_circle) + ", \"x\":" + to_string(newY) + ", \"y\":" + to_string(newX) + "}";

		}
		send += "]} {END}";
		cout << "Sending:    " << send << endl;
		sendTCPData(send);
		imshow("face", mask1);
		if (cv::waitKey(1))
			return;
	}

	
	void detectAndDraw(Mat& img, CascadeClassifier& cascade, double scale) {

		vector<Rect> faces;
		Mat gray;
		cvtColor(img, gray, COLOR_BGR2GRAY);

		cascade.detectMultiScale(gray, faces, 1.1, 2, 0 | CASCADE_SCALE_IMAGE, Size(60, 60));
		string send = "{\"data\" : [";
		for (size_t i = 0; i < faces.size(); i++)
		{
			Rect r = faces[i];
			Scalar color = Scalar(255, 0, 0);
			rectangle(img, cvPoint(cvRound(r.x * scale), cvRound(r.y * scale)), cvPoint(cvRound((r.x +
				r.width - 1) * scale), cvRound((r.y + r.height - 1) * scale)), color, 3, 8, 0);
			int newX = 100 -((100 * (r.x + (r.width / 2)) / 1920));
			int newY =  ((100 * (r.y + (r.height / 2)) / 1080));
			send += "{\"id\":" + to_string(i) + ", \"x\":" + to_string(newY) + ", \"y\":" + to_string(newX) + "}";

		}
		send += "]} {END}";
		cout << "Sending:    " << send << endl;
		sendTCPData(send);
		imshow("face", img);
		if (cv::waitKey(1))
			return;
	}
};