#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include "opencv2/core/core_c.h"
#include <iostream>
#include <objbase.h>
#include "DeckLinkAPI_h.h"
#include <array>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>

using namespace cv;
using namespace std;

#define INT32_UNSIGNED unsigned int
#define INT32_SIGNED signed int
#define INT64_SIGNED LONGLONG

// Video mode parameters
const BMDDisplayMode      kDisplayMode = bmdModeHD1080p30;
const BMDVideoInputFlags  kInputFlag = bmdVideoInputFlagDefault;
const BMDPixelFormat      kPixelFormat = bmdFormat8BitYUV;

// Frame parameters
const INT32_UNSIGNED kFrameDuration = 1000;
const INT32_UNSIGNED kTimeScale = 30000;
const INT32_UNSIGNED kSynchronizedCaptureGroup = 2;

static const BMDTimeScale kMicroSecondsTimeScale = 1000000;

class DeckLinkDevice;

#define MAT_REFCOUNT(mat) \
 (mat.u ? (mat.u->refcount) : 0)

class CvMatDeckLinkVideoFrame : public IDeckLinkVideoFrame
{
public:
	cv::Mat mat;

	CvMatDeckLinkVideoFrame(int row, int cols)
		: mat(row, cols, CV_8UC4)
	{}

	//
	// IDeckLinkVideoFrame
	//

	long STDMETHODCALLTYPE GetWidth()
	{
		return mat.rows;
	}
	long STDMETHODCALLTYPE GetHeight()
	{
		return mat.cols;
	}
	long STDMETHODCALLTYPE GetRowBytes()
	{
		return mat.step;
	}
	BMDPixelFormat STDMETHODCALLTYPE GetPixelFormat()
	{
		return bmdFormat8BitBGRA;
	}
	BMDFrameFlags STDMETHODCALLTYPE GetFlags()
	{
		return 0;
	}
	HRESULT STDMETHODCALLTYPE GetBytes(void** buffer)
	{
		*buffer = mat.data;
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE GetTimecode(BMDTimecodeFormat format,
		IDeckLinkTimecode** timecode)
	{
		*timecode = nullptr; return S_OK;
	}
	HRESULT STDMETHODCALLTYPE GetAncillaryData(IDeckLinkVideoFrameAncillary** ancillary)
	{
		*ancillary = nullptr; return S_OK;
	}

	//
	// IDeckLinkVideoFrame
	//

	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, LPVOID* ppv)
	{
		return E_NOINTERFACE;
	}

	ULONG STDMETHODCALLTYPE AddRef()
	{
		mat.addref(); return MAT_REFCOUNT(mat);
	}
	ULONG STDMETHODCALLTYPE Release()
	{
		mat.release();
		if (MAT_REFCOUNT(mat) == 0) delete this;
		return MAT_REFCOUNT(mat);
	}
};

INT32_SIGNED AtomicIncrement(volatile INT32_SIGNED* value)
{
	return InterlockedIncrement((LONG*)value);
}

INT32_SIGNED AtomicDecrement(volatile INT32_SIGNED* value)
{
	return InterlockedDecrement((LONG*)value);
}



class InputCallback : public IDeckLinkInputCallback
{
public:
	InputCallback(DeckLinkDevice* deckLinkDevice) :
		m_deckLinkDevice(deckLinkDevice),
		m_refCount(1)
	{
	}

	HRESULT STDMETHODCALLTYPE VideoInputFormatChanged(BMDVideoInputFormatChangedEvents notificationEvents, IDeckLinkDisplayMode* newDisplayMode, BMDDetectedVideoInputFormatFlags detectedSignalFlags) override;

	HRESULT STDMETHODCALLTYPE VideoInputFrameArrived(IDeckLinkVideoInputFrame* videoFrame, IDeckLinkAudioInputPacket* audioPacket) override;

	// IUnknown needs only a dummy implementation
	HRESULT	STDMETHODCALLTYPE QueryInterface(REFIID iid, LPVOID* ppv) override
	{
		return E_NOINTERFACE;
	}

	ULONG STDMETHODCALLTYPE AddRef() override
	{
		return ++m_refCount;
	}

	ULONG STDMETHODCALLTYPE Release() override
	{
		INT32_UNSIGNED newRefValue = --m_refCount;

		if (newRefValue == 0)
			delete this;

		return newRefValue;
	}

private:
	DeckLinkDevice* m_deckLinkDevice;
	std::atomic<INT32_SIGNED> m_refCount;
};

class NotificationCallback : public IDeckLinkNotificationCallback
{
public:
	NotificationCallback(DeckLinkDevice* deckLinkDevice) :
		m_deckLinkDevice(deckLinkDevice),
		m_refCount(1)
	{
	}

	HRESULT STDMETHODCALLTYPE Notify(BMDNotifications topic, uint64_t param1, uint64_t param2) override;

	// IUnknown needs only a dummy implementation
	HRESULT	STDMETHODCALLTYPE QueryInterface(REFIID iid, LPVOID* ppv) override
	{
		return E_NOINTERFACE;
	}

	ULONG STDMETHODCALLTYPE AddRef() override
	{
		return AtomicIncrement(&m_refCount);
	}

	ULONG STDMETHODCALLTYPE Release() override
	{
		INT32_UNSIGNED newRefValue = AtomicDecrement(&m_refCount);

		if (newRefValue == 0)
			delete this;

		return newRefValue;
	}
private:
	DeckLinkDevice* m_deckLinkDevice;
	INT32_SIGNED m_refCount;
};

class DeckLinkDevice
{
public:
	DeckLinkDevice() :
		m_index(0),
		m_deckLink(nullptr),
		m_deckLinkConfig(nullptr),
		m_deckLinkStatus(nullptr),
		m_deckLinkNotification(nullptr),
		m_notificationCallback(nullptr),
		m_deckLinkInput(nullptr),
		m_inputCallback(nullptr)
	{
	}

	HRESULT setup(IDeckLink* deckLink, unsigned index)
	{
		m_deckLink = deckLink;
		m_index = index;

		// Obtain the configuration interface for the DeckLink device
		HRESULT result = m_deckLink->QueryInterface(IID_IDeckLinkConfiguration, (void**)&m_deckLinkConfig);
		if (result != S_OK)
		{
			fprintf(stderr, "Could not obtain the IDeckLinkConfiguration interface - result = %08x\n", result);
			goto bail;
		}

		// Set the synchronized capture group number. This can be any 32-bit number
		// All devices enabled for synchronized capture with the same group number are started together
		result = m_deckLinkConfig->SetInt(bmdDeckLinkConfigCaptureGroup, kSynchronizedCaptureGroup);
		if (result != S_OK)
		{
			fprintf(stderr, "Could not set capture group - result = %08x\n", result);
			goto bail;
		}

		// Obtain the status interface for the DeckLink device
		result = m_deckLink->QueryInterface(IID_IDeckLinkStatus, (void**)&m_deckLinkStatus);
		if (result != S_OK)
		{
			fprintf(stderr, "Could not obtain the IDeckLinkStatus interface - result = %08x\n", result);
			goto bail;
		}

		// Obtain the notification interface for the DeckLink device
		result = m_deckLink->QueryInterface(IID_IDeckLinkNotification, (void**)&m_deckLinkNotification);
		if (result != S_OK)
		{
			fprintf(stderr, "Could not obtain the IDeckLinkNotification interface - result = %08x\n", result);
			goto bail;
		}

		m_notificationCallback = new NotificationCallback(this);
		if (m_notificationCallback == nullptr)
		{
			fprintf(stderr, "Could not create notification callback object\n");
			goto bail;
		}

		// Set the callback object to the DeckLink device's notification interface
		result = m_deckLinkNotification->Subscribe(bmdStatusChanged, m_notificationCallback);
		if (result != S_OK)
		{
			fprintf(stderr, "Could not set notification callback - result = %08x\n", result);
			goto bail;
		}

		// Obtain the input interface for the DeckLink device
		result = m_deckLink->QueryInterface(IID_IDeckLinkInput, (void**)&m_deckLinkInput);
		if (result != S_OK)
		{
			fprintf(stderr, "Could not obtain the IDeckLinkInput interface - result = %08x\n", result);
			goto bail;
		}

		// Create an instance of output callback
		m_inputCallback = new InputCallback(this);
		if (m_inputCallback == nullptr)
		{
			fprintf(stderr, "Could not create input callback object\n");
			goto bail;
		}

		// Set the callback object to the DeckLink device's input interface
		result = m_deckLinkInput->SetCallback(m_inputCallback);
		if (result != S_OK)
		{
			fprintf(stderr, "Could not set input callback - result = %08x\n", result);
			goto bail;
		}

	bail:
		return result;
	}

	HRESULT waitForSignalLock()
	{
		// When performing synchronized capture, all participating devices need to have their signal locked
		std::unique_lock<std::mutex> guard(m_mutex);

		HRESULT result = S_OK;

		m_signalCondition.wait(guard, [this, &result]()
			{
				INT64_SIGNED displayMode;
				result = m_deckLinkStatus->GetInt(bmdDeckLinkStatusDetectedVideoInputMode, &displayMode);
				if (result != S_OK && result != S_FALSE)
				{
					fprintf(stderr, "Could not query input status - result = %08x\n", result);
					return true;
				}

				return (BMDDisplayMode)displayMode == kDisplayMode;
			});

		return result;
	}

	void notifyVideoInputChanged()
	{
		m_signalCondition.notify_all();
	}

	HRESULT prepareForCapture()
	{
		// Enable video output
		HRESULT result = m_deckLinkInput->EnableVideoInput(kDisplayMode, kPixelFormat, kInputFlag);
		if (result != S_OK)
		{
			fprintf(stderr, "Could not enable video input - result = %08x\n", result);
			goto bail;
		}

	bail:
		return result;
	}

	HRESULT startCapture()
	{
		HRESULT result = m_deckLinkInput->StartStreams();
		if (result != S_OK)
		{
			fprintf(stderr, "Could not start - result = %08x\n", result);
			goto bail;
		}

	bail:
		return result;
	}

	HRESULT stopCapture()
	{
		HRESULT result = m_deckLinkInput->StopStreams();
		if (result != S_OK)
		{
			fprintf(stderr, "Could not stop - result = %08x\n", result);
			goto bail;
		}

	bail:
		return result;
	}

	HRESULT cleanUpFromCapture()
	{
		HRESULT result = m_deckLinkInput->DisableVideoInput();
		if (result != S_OK)
		{
			fprintf(stderr, "Could not disable - result = %08x\n", result);
			goto bail;
		}

	bail:
		return result;
	}

	HRESULT frameArrived(IDeckLinkVideoInputFrame* videoFrame)
	{
		BMDTimeValue time;
		HRESULT result = videoFrame->GetStreamTime(&time, nullptr, kTimeScale);
		if (result != S_OK)
		{
			fprintf(stderr, "Could not get stream time from frame - result = %08x\n", result);
			return S_OK;
		}

		unsigned frames = (unsigned)((time % kTimeScale) / kFrameDuration);
		unsigned seconds = (unsigned)((time / kTimeScale) % 60);
		unsigned minutes = (unsigned)((time / kTimeScale / 60) % 60);
		unsigned hours = (unsigned)(time / kTimeScale / 60 / 60);

		BMDTimeValue hwTime;
		result = videoFrame->GetHardwareReferenceTimestamp(kMicroSecondsTimeScale, &hwTime, NULL);
		if (result != S_OK)
		{
			fprintf(stderr, "Could not get hardwar reference time from frame - result = %08x\n", result);
			return S_OK;
		}

		printf("[%llu.%06llu] Device #%u: Frame %02u:%02u:%02u:%03u arrived\n", hwTime / kMicroSecondsTimeScale, hwTime % kMicroSecondsTimeScale, m_index, hours, minutes, seconds, frames);
		

		void* data;
		if (FAILED(videoFrame->GetBytes(&data)))
			return false;
		//cv::Mat mat = cv::Mat(videoFrame->GetHeight(), videoFrame->GetWidth(), CV_8UC2);//, data, videoFrame->GetRowBytes());
		Mat mat(20, 20, CV_8UC3, Scalar(0, 0, 0));
		//cv::cvtColor(mat, mat, 107);
		cv::imshow("Display window", mat);
		//this->frame = mat;
		

		return S_OK;
	}

	~DeckLinkDevice()
	{
		if (m_inputCallback)
		{
			m_deckLinkInput->SetCallback(nullptr);
			m_inputCallback->Release();
		}

		if (m_notificationCallback)
		{
			m_deckLinkNotification->Unsubscribe(bmdStatusChanged, m_notificationCallback);
			m_notificationCallback->Release();
		}

		if (m_deckLink)
			m_deckLink->Release();

		if (m_deckLinkConfig)
			m_deckLinkConfig->Release();

		if (m_deckLinkStatus)
			m_deckLinkStatus->Release();

		if (m_deckLinkInput)
			m_deckLinkInput->Release();

		if (m_deckLinkNotification)
			m_deckLinkNotification->Release();
	}


	cv::Mat frame;
private:
	unsigned										m_index;
	IDeckLink* m_deckLink;
	IDeckLinkConfiguration* m_deckLinkConfig;
	IDeckLinkStatus* m_deckLinkStatus;
	IDeckLinkNotification* m_deckLinkNotification;
	NotificationCallback* m_notificationCallback;
	IDeckLinkInput* m_deckLinkInput;
	InputCallback* m_inputCallback;
	std::mutex										m_mutex;
	std::condition_variable							m_signalCondition;
	
};

HRESULT InputCallback::VideoInputFormatChanged(BMDVideoInputFormatChangedEvents notificationEvents, IDeckLinkDisplayMode* newDisplayMode, BMDDetectedVideoInputFormatFlags detectedSignalFlags)
{
	return S_OK;
}

HRESULT InputCallback::VideoInputFrameArrived(IDeckLinkVideoInputFrame* videoFrame, IDeckLinkAudioInputPacket* audioPacket)
{
	if (!videoFrame) // || (videoFrame->GetFlags() & bmdFrameHasNoInputSource))
	{
		printf("No valid frame\n");
		return S_OK;
	}

	return m_deckLinkDevice->frameArrived(videoFrame);
}

HRESULT NotificationCallback::Notify(BMDNotifications topic, uint64_t param1, uint64_t param2)
{
	if (topic != bmdStatusChanged)
		return S_OK;

	if ((BMDDeckLinkStatusID)param1 != bmdDeckLinkStatusDetectedVideoInputMode)
		return S_OK;

	m_deckLinkDevice->notifyVideoInputChanged();
	return S_OK;
}

static BOOL supportsSynchronizedCapture(IDeckLink* deckLink)
{
	IDeckLinkProfileAttributes* attributes = nullptr;
	HRESULT result = E_FAIL;

	result = deckLink->QueryInterface(IID_IDeckLinkProfileAttributes, (void**)&attributes);
	if (result != S_OK)
		return false;

	BOOL supported = false;
	result = attributes->GetFlag(BMDDeckLinkSupportsSynchronizeToCaptureGroup, &supported);
	if (result != S_OK)
		supported = false;

	attributes->Release();

	return supported;
}

int main()
{
	IDeckLinkIterator* deckLinkIterator = NULL;
	IDeckLink* deckLink;
	HRESULT result;
	result = CoInitialize(NULL);

	result = CoCreateInstance(CLSID_CDeckLinkIterator, NULL, CLSCTX_ALL, IID_IDeckLinkIterator, (void**)&deckLinkIterator);


	if (result != S_OK)
	{
		fprintf(stderr, "Error - result = %08x\n", result);
	}
	else
	{
		cout << "Iterator created" << endl;
	}
	
	result = deckLinkIterator->Next(&deckLink);
	result = deckLinkIterator->Next(&deckLink);
	result = deckLinkIterator->Next(&deckLink);
	result = deckLinkIterator->Next(&deckLink);
	result = deckLinkIterator->Next(&deckLink);

	if (result != S_OK)
	{
		fprintf(stderr, "Could not find DeckLink device - result = %08x\n", result);
	}
	else
	{
		cout << "Found Device" << endl;
	}

	BSTR temp;
	result = deckLink->GetDisplayName(&temp);

	if (result != S_OK)
	{
		fprintf(stderr, "Could not find DeckLink device - result = %08x\n", result);
	}
	else
	{
		// Your wchar_t*
		wstring ws(temp);
		// your new String
		string str(ws.begin(), ws.end());
		// Show String
		cout << "Found Device " << str << endl;
	}

	DeckLinkDevice one;
	//deckLink->Release();
	result = one.setup(deckLink, 0);
	if (result != S_OK)
	{
		fprintf(stderr, "Could not find DeckLink device - result = %08x\n", result);
	}
	else
	{
		cout << "Found Device" << endl;
	}
	result = one.prepareForCapture();
	if (result != S_OK)
	{
		fprintf(stderr, "Could not find DeckLink device - result = %08x\n", result);
	}
	else
	{
		cout << "Found Device" << endl;
	}
	result = one.startCapture();
	if (result != S_OK)
	{
		fprintf(stderr, "Could not find DeckLink device - result = %08x\n", result);
	}
	else
	{
		cout << "Found Device" << endl;
	}
	//namedWindow("Display window", WINDOW_AUTOSIZE);
	//while (true) {

	//	cv::imshow("Display window", one.frame);
	//	if (cv::waitKey(10) >= 0)
	//		break;

	//}
	getchar();
}