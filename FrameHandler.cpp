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


public:

	FrameHandler(std::mutex* m, std::condition_variable* cv, bool* newFrame) :
		mutex(m),
		signalCondition(cv),
		newFrame(newFrame),
		currentFrame(nullptr),
		height(0),
		width(0),
		rowBytes(0),
		yolo("cfg/yolov3.cfg", "weights/yolov3.weights", 1)
	{
		namedWindow("window", WINDOW_AUTOSIZE);

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
		std::vector<bbox_t> res = yolo.detect(mat);
		for (size_t i = 0; i < res.size(); i++)
		{
			Scalar color = Scalar(255, 0, 0);
			rectangle(mat, Point(res[i].x, res[i].y), Point(res[i].x + res[i].w, res[i].y + res[i].h), color);

		}
		//cout << res.size() << endl;
		imshow("window", mat);
		if (cv::waitKey(1));
			return false;
		/*CascadeClassifier cascade;
		String face = "C:/Users/darklake/source/repos/BMPort/face.xml";
		cascade.load(face);
		detectAndDraw(mat, cascade, 1);*/

		return true;
	}
	
	void sendTCPData(string s) 
	{
		


		//s = "{\"data\" : [{qidq:1, qxq:10, qyq:100},{qidq:2, qxq:100, qyq:200}], qkillq : [{qidq:1}]}{END}";
		//replace(s.begin(), s.end(), 'q', '"');
		tcp.setup();
		while (!tcp.isReady()) {}
		tcp.Send(s.c_str());
		tcp.receive();
		tcp.exit();

	}

	void detectAndDraw(Mat& img, CascadeClassifier& cascade, double scale) {

		vector<Rect> faces;
		Mat gray;
		cvtColor(img, gray, COLOR_BGR2GRAY);

		cascade.detectMultiScale(gray, faces, 1.1, 2, 0 | CASCADE_SCALE_IMAGE, Size(30, 30));
		string send = "{\"data\" : [";
		for (size_t i = 0; i < faces.size(); i++)
		{
			Rect r = faces[i];
			Scalar color = Scalar(255, 0, 0);
			rectangle(img, cvPoint(cvRound(r.x * scale), cvRound(r.y * scale)), cvPoint(cvRound((r.x +
				r.width - 1) * scale), cvRound((r.y + r.height - 1) * scale)), color, 3, 8, 0);
			send += "{\"id\":" + to_string(i) + ", \"x\":" + to_string(r.x) + ", \"y\":" + to_string(r.y) + "}";

		}
		send += "]} {END}";
		cout << "Sending:    " << send << endl;
		//sendTCPData(send);
		imshow("face", img);
		if (cv::waitKey(1))
			return;
	}
};