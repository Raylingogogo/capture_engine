// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved.

#include "Capture.h"
#include "resource.h"

#include <string>

//EXTERN_GUID(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, 0xa634a91c, 0x822b, 0x41b9, 0xa4, 0x94, 0x4d, 0xe4, 0x64, 0x36, 0x12, 0xb0);

IMFDXGIDeviceManager* g_pDXGIMan = NULL;
ID3D11Device*         g_pDX11Device = NULL;
UINT                  g_ResetToken = 0;

HWND g_hwndPreviewCopy;
bool errorFlag = false;
IUnknown* g_pSelectedDevice = NULL;

//extern parameters define in winmain.cpp
extern int g_threshold, g_op_mode, g_countToCapture, g_device_type, g_resolutionIndex, g_pin_no;
extern int g_frame_rate;
extern WCHAR *g_toolVersion;
extern FILE *file_log;
extern CaptureManager *g_pEngine;
extern HWND initWindow;

STDMETHODIMP CaptureManager::CaptureEngineCB::QueryInterface(REFIID riid, void** ppv)
{
	printf("CaptureEngineCB, IMFCaptureEngineOnEventCallback\n");
	static const QITAB qit[] =
	{
		QITABENT(CaptureEngineCB, IMFCaptureEngineOnEventCallback),
		{ 0 }
	};
	return QISearch(this, qit, riid, ppv);
}

STDMETHODIMP_(ULONG) CaptureManager::CaptureEngineCB::AddRef()
{
	return InterlockedIncrement(&m_cRef);
}

STDMETHODIMP_(ULONG) CaptureManager::CaptureEngineCB::Release()
{
	LONG cRef = InterlockedDecrement(&m_cRef);
	if (cRef == 0)
	{
		delete this;
	}
	return cRef;
}

// Callback method to receive events from the capture engine.
STDMETHODIMP CaptureManager::CaptureEngineCB::OnEvent(_In_ IMFMediaEvent* pEvent)
{
	// Post a message to the application window, so the event is handled 
	// on the application's main thread. 

	if (m_fSleeping && m_pManager != NULL)
	{
		// We're about to fall asleep, that means we've just asked the CE to stop the preview
		// and record.  We need to handle it here since our message pump may be gone.
		GUID    guidType;
		HRESULT hrStatus;
		HRESULT hr = pEvent->GetStatus(&hrStatus);
		if (FAILED(hr))
		{
			printf("pEvent->GetStatus error\n");
			hrStatus = hr;
		}

		hr = pEvent->GetExtendedType(&guidType);
		if (SUCCEEDED(hr))
		{
			if (guidType == MF_CAPTURE_ENGINE_PREVIEW_STOPPED)
			{
				m_pManager->OnPreviewStopped(hrStatus);
				SetEvent(m_pManager->m_hEvent);
			}
			else if (guidType == MF_CAPTURE_ENGINE_RECORD_STOPPED)
			{
				m_pManager->OnRecordStopped(hrStatus);
				SetEvent(m_pManager->m_hEvent);
			}
			else
			{
				// This is an event we don't know about, we don't really care and there's
				// no clean way to report the error so just set the event and fall through.
				SetEvent(m_pManager->m_hEvent);
			}
		}

		return S_OK;
	}
	else
	{
		pEvent->AddRef();  // The application will release the pointer when it handles the message.
		PostMessage(m_hwnd, WM_APP_CAPTURE_EVENT, (WPARAM)pEvent, 0L);
	}

	return S_OK;
}


STDMETHODIMP CaptureManager::CaptureEngineSampleCB::QueryInterface(REFIID riid, void** ppv)
{
	printf("CaptureEngineSampleCB, IMFCaptureEngineOnSampleCallback\n");
	static const QITAB qit[] =
	{
		QITABENT(CaptureEngineSampleCB, IMFCaptureEngineOnSampleCallback),
		{ 0 }
	};
	return QISearch(this, qit, riid, ppv);
}

STDMETHODIMP_(ULONG) CaptureManager::CaptureEngineSampleCB::AddRef()
{
	return InterlockedIncrement(&m_cRef);
}

STDMETHODIMP_(ULONG) CaptureManager::CaptureEngineSampleCB::Release()
{
	LONG cRef = InterlockedDecrement(&m_cRef);
	if (cRef == 0)
	{
		delete this;
	}
	return cRef;
}

int evalueCount, BrightCount, skipFrame, frame_index;
int evalueArr[128];
UINT32 g_Width, g_Height;
BITMAPINFO g_BitmapInfo;
BYTE *g_pbInputData = NULL;
BYTE *g_light_pbInputData = NULL;
bool  g_Capture_photo=FALSE;
LONGLONG firstTimeStamp;
LONGLONG preTimeStamp;
DWORD average_sum[2] = { 0 };
int timestampCounter;
int timestampDiff[256] = { 0 };
DWORD g_light_dwMaxLength = 0;

void initOnSampleVariables() {
	evalueCount = 0;
	BrightCount = -1;
	skipFrame = (g_pin_no==2)?2:30;
	frame_index = 0;
	memset(average_sum, 0, sizeof(LONGLONG)*2);
	memset(timestampDiff, 0, sizeof(int) * 256);
	firstTimeStamp = 0;
	preTimeStamp = 0;
	timestampCounter = 0;
}

HRESULT CaptureManager::CaptureEngineSampleCB::OnSample(IMFSample * pSample)
{
	/*UINT64 illuminationEnabled;
	pSample->GetUINT64(MF_CAPTURE_METADATA_FRAME_ILLUMINATION, &illuminationEnabled);
	printf("%s\n", illuminationEnabled ? "light" : "dark"); */

	//if our count is equal to the countdown of capture, then start capture
	if (evalueCount==g_countToCapture && g_countToCapture!=-1) {
		g_Capture_photo = TRUE;
		printf("start Capture, count=%d\n", g_countToCapture);
	}

	//get the parameters of timing information
	LONGLONG frameTimeStamp, frameDuration;
	pSample->GetSampleTime(&frameTimeStamp); // micro second level
	DWORD tickCount = (DWORD)(frameTimeStamp / 10000); //ms level
	pSample->GetSampleDuration(&frameDuration); // micro second level
	DWORD bufferCount;
	pSample->GetBufferCount(&bufferCount);

	//setup the parameters of video buffer
	HRESULT hr = S_OK;
	DWORD	dwBufferCount = 0;
	IMFMediaBuffer *pSampleBuffer = NULL;
	BYTE *pbInputData;
	DWORD dwCurrentLength, dwMaxLength;
	errno_t err;
	char log_buf[250] = { 0 };

	hr = pSample->GetTotalLength(&dwBufferCount);
	if (FAILED(hr))
	{
		printf("GetTotalLength error\n");
		goto done;
	}

	// Get buffer from pSample
	hr = pSample->ConvertToContiguousBuffer(&pSampleBuffer);
	if (FAILED(hr))
	{
		printf("ConvertToContiguousBuffer error\n");
		goto done;
	}

	//lock this buffer area
	pSampleBuffer->Lock(&pbInputData, &dwMaxLength, &dwCurrentLength);
	//printf("TimeStamp, duration = [%ld, %lld, %ld, %ld]\n", tickCount, frameDuration, bufferCount, dwMaxLength);
	g_pbInputData = pbInputData;

	
	//initialize the first timestamp
	if (evalueCount == 0)
		firstTimeStamp = preTimeStamp = tickCount;

	//cache frame rate
	timestampDiff[timestampCounter++] = (tickCount - preTimeStamp != 0) ? (tickCount - preTimeStamp) : 0;

	//display every one second
	if (tickCount - firstTimeStamp >= 1000) {
		int sum = 0;
		int validNum = 0;
		for (int i = 0; i < timestampCounter; i++) {
			if (timestampDiff[i] != 0) {
				sum += timestampDiff[i];
				validNum++;
			}
		}
		sum /= validNum;
		g_frame_rate = 1000 / sum;
		printf("timestampCounter %d, validNum %d, FPS %d\n", timestampCounter, validNum, g_frame_rate);

		timestampCounter = 0;
		firstTimeStamp = tickCount;
		
		//update FPS to UI
		PostMessage(initWindow, WM_COMMAND, ID_SET_FRAME_RATE, 0L); //update FPS to UI
	}
	preTimeStamp = tickCount;
	

	// Calculate average
	average_sum[frame_index] = 0;
	for (DWORD i = 0; i < dwMaxLength; i++)
		average_sum[frame_index] += *(pbInputData + i);
	average_sum[frame_index] /= dwMaxLength;

	//start to capture if buffering is complete
	if (BrightCount != -1) {
		if (g_Capture_photo) {//allow to capture

			if ((frame_index == 0 && (evalueCount%2 == BrightCount))      ||//light one
				 frame_index == 1 ) {//next one
				 
				printf("frame index %d, this evalue Count %d, BrightCount %d, avg %d\n", frame_index, evalueCount, BrightCount, average_sum[frame_index]);

				if (g_device_type == 0) { //IR
					//inverse
					for (DWORD i = 0; i < dwMaxLength / 2; i++) {
						BYTE tmp;
						tmp = pbInputData[i];
						pbInputData[i] = pbInputData[dwMaxLength - i - 1];
						pbInputData[dwMaxLength - i - 1] = tmp;
					}
				}
				else { //RGB
					for (DWORD i = 0; i < dwMaxLength / 2; i+=3) {
						BYTE tmpR, tmpG, tmpB;
						tmpR = pbInputData[i];
						tmpG = pbInputData[i+1];
						tmpB = pbInputData[i+2];
						pbInputData[i+2] = pbInputData[dwMaxLength - i - 1];
						pbInputData[i+1] = pbInputData[dwMaxLength - i - 2];
						pbInputData[i  ] = pbInputData[dwMaxLength - i - 3];
						pbInputData[dwMaxLength - i - 1] = tmpB;
						pbInputData[dwMaxLength - i - 2] = tmpG;
						pbInputData[dwMaxLength - i - 3] = tmpR;
					}
				}

				if (frame_index == 0) {
					g_light_dwMaxLength = dwMaxLength;
					g_light_pbInputData = (BYTE*)calloc(g_light_dwMaxLength, sizeof(BYTE));
					memcpy(g_light_pbInputData, pbInputData, dwMaxLength);
				}

				//set output file format
				setlocale(LC_ALL, "en_US.UTF-8");
				HANDLE file;
				DWORD write = 0;
				if (frame_index == 0)
					file = CreateFile(L"light.bmp", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);//Sets up the new bmp to be written to
				else
					file = CreateFile(L"dark.bmp", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);//Sets up the new bmp to be written to
				
				//write file headers and bitmap data to output
				BITMAPFILEHEADER fileHeader;
				BITMAPINFOHEADER fileInfo;
				fileHeader.bfType = 19778;//Sets our type to BM or bmp
				fileHeader.bfSize = sizeof(fileHeader.bfOffBits) + sizeof(RGBTRIPLE);//Sets the size equal to the size of the header struct
				fileHeader.bfReserved1 = 0;//sets the reserves to 0
				fileHeader.bfReserved2 = 0;
				fileHeader.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);//Sets offbits equal to the size of file and info header
				WriteFile(file, &fileHeader, sizeof(fileHeader), &write, NULL);

				fileInfo = g_BitmapInfo.bmiHeader;
				fileInfo.biXPelsPerMeter = 2400;
				fileInfo.biYPelsPerMeter = 2400;
				fileInfo.biClrImportant = 0;
				fileInfo.biClrUsed = 0;
				WriteFile(file, &fileInfo, sizeof(fileInfo), &write, NULL);
				
				WriteFile(file, pbInputData, dwMaxLength, &write, NULL);

				if (frame_index == 1)
				{
					int diff = abs((int)(average_sum[0] - average_sum[1]));
					printf("frame 0 average = %d \n", average_sum[0]);
					printf("frame 1 average = %d \n", average_sum[1]);
					printf("Result = %d, %s", diff, diff > g_threshold ? "PASS" : "FAIL");

					// Stream File open
					if ((err = fopen_s(&file_log, "result.txt", "w+")) != 0)
						printf("The log file was not opened\n");

					//write tool version to ouput file
					_bstr_t b(g_toolVersion);
					char *outputVersion = b;

					// Write to log file
					sprintf_s(log_buf, "%s \n[diff, frame 1, frame 2] = [%d, %d, %d]\n%s\n", diff > g_threshold ? "PASS" : "FAIL", diff, average_sum[0], average_sum[1], outputVersion);
					fwrite(log_buf, 1, sizeof(log_buf), file_log);
					fclose(file_log);

					//save frame difference
					HANDLE file2;
					DWORD write2 = 0;
					file2 = CreateFile(L"difference.bmp", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);//Sets up the new bmp to be written to
					WriteFile(file2, &fileHeader, sizeof(fileHeader), &write2, NULL);
					WriteFile(file2, &fileInfo, sizeof(fileInfo), &write2, NULL);

					//subtract
					for (DWORD i = 0; i < g_light_dwMaxLength; i++) 
						g_light_pbInputData[i] = (g_light_pbInputData[i] > pbInputData[i]) ? g_light_pbInputData[i] - pbInputData[i] : 0;

					WriteFile(file2, g_light_pbInputData, g_light_dwMaxLength, &write2, NULL);

					//release parameters
					pSampleBuffer->Unlock();
					pSampleBuffer->Release();
					g_pEngine->StopPreview();
					g_pSelectedDevice->Release();

					//release device selected and exit
					if (errorFlag==true)
						printf("capture ok, and recovery complete\n");
					exit(0);
				}
				frame_index++;
			}
			else {
				printf("skip this frame %d, avg %d\n", evalueCount, average_sum[frame_index]);
			}
		} else {//start display
			switch (g_op_mode)
			{
			case 1:
				RedrawWindow(g_hwndPreviewCopy, NULL, NULL, RDW_INTERNALPAINT | RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
				break;
			case 2:
				if (BrightCount != -1) {
					if (evalueCount % 2 == BrightCount) { //display light one
						RedrawWindow(g_hwndPreviewCopy, NULL, NULL, RDW_INTERNALPAINT | RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
					}
				}
				break;
			case 3:
				if (BrightCount != -1) {
					if (evalueCount % 2 == ((BrightCount + 1)%2)) { //display dark one
						RedrawWindow(g_hwndPreviewCopy, NULL, NULL, RDW_INTERNALPAINT | RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
					}
				}
				break;
			default:
				break;
			}
		}
	}

	//add avg to evaluation array
	if (evalueCount < skipFrame)
		evalueArr[evalueCount] = average_sum[frame_index];

	//judge the light or dark
	if (evalueCount == skipFrame-1) {
		printf("prepare rendering ok\n");
		int oddSum=0, evenSum=0;
		for (int i = 0; i <= evalueCount; i++) {
			//printf("i %d, avg %d\n", i, evalueArr[i]);
			if (i % 2 == 1) {
				oddSum += evalueArr[i];
			}
			else {
				evenSum += evalueArr[i];
			}

			if (i == evalueCount) { // last one
				BrightCount = (oddSum > evenSum) ? 1 : 0;
				//printf("brightCount==%d\n",BrightCount);
				//init bitmap
				ZeroMemory(&g_BitmapInfo, sizeof(BITMAPINFO));
				g_BitmapInfo.bmiHeader.biBitCount = 24;
				g_BitmapInfo.bmiHeader.biWidth = g_Width;
				g_BitmapInfo.bmiHeader.biHeight = g_Height;
				g_BitmapInfo.bmiHeader.biPlanes = 1;
				g_BitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
				g_BitmapInfo.bmiHeader.biSizeImage = g_Width * g_Height * (24 / 8);;
				g_BitmapInfo.bmiHeader.biCompression = BI_RGB;
			}
		}
	}

	evalueCount++;

	// Output result
	pSampleBuffer->Unlock();
	pSampleBuffer->Release();

done:
	return hr;
}

HRESULT CreateDX11Device(_Out_ ID3D11Device** ppDevice, _Out_ ID3D11DeviceContext** ppDeviceContext, _Out_ D3D_FEATURE_LEVEL* pFeatureLevel)
{
	HRESULT hr = S_OK;
	static const D3D_FEATURE_LEVEL levels[] = {
		D3D_FEATURE_LEVEL_11_1,
		D3D_FEATURE_LEVEL_11_0,
		D3D_FEATURE_LEVEL_10_1,
		D3D_FEATURE_LEVEL_10_0,
		D3D_FEATURE_LEVEL_9_3,
		D3D_FEATURE_LEVEL_9_2,
		D3D_FEATURE_LEVEL_9_1
	};


	hr = D3D11CreateDevice(
		nullptr,
		D3D_DRIVER_TYPE_HARDWARE,
		nullptr,
		D3D11_CREATE_DEVICE_VIDEO_SUPPORT,
		levels,
		ARRAYSIZE(levels),
		D3D11_SDK_VERSION,
		ppDevice,
		pFeatureLevel,
		ppDeviceContext
	);

	if (SUCCEEDED(hr))
	{
		ID3D10Multithread* pMultithread;
		hr = ((*ppDevice)->QueryInterface(IID_PPV_ARGS(&pMultithread)));

		if (SUCCEEDED(hr))
		{
			pMultithread->SetMultithreadProtected(TRUE);
		}

		SafeRelease(&pMultithread);

	}

	return hr;
}

HRESULT CreateD3DManager()
{
	HRESULT hr = S_OK;
	D3D_FEATURE_LEVEL FeatureLevel;
	ID3D11DeviceContext* pDX11DeviceContext;

	hr = CreateDX11Device(&g_pDX11Device, &pDX11DeviceContext, &FeatureLevel);

	if (SUCCEEDED(hr))
	{
		hr = MFCreateDXGIDeviceManager(&g_ResetToken, &g_pDXGIMan);
	}

	if (SUCCEEDED(hr))
	{
		hr = g_pDXGIMan->ResetDevice(g_pDX11Device, g_ResetToken);
	}

	SafeRelease(&pDX11DeviceContext);

	return hr;
}

HRESULT
CaptureManager::InitializeCaptureManager(HWND hwndPreview, IUnknown* pUnk)
{
	g_pSelectedDevice = pUnk;
	HRESULT                         hr = S_OK;
	IMFAttributes*                  pAttributes = NULL;
	IMFCaptureEngineClassFactory*   pFactory = NULL;

	DestroyCaptureEngine();

	m_hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	if (NULL == m_hEvent)
	{
		hr = HRESULT_FROM_WIN32(GetLastError());
		goto Exit;
	}

	m_pCallback = new (std::nothrow) CaptureEngineCB(m_hwndEvent);
	if (m_pCallback == NULL)
	{
		hr = E_OUTOFMEMORY;
		goto Exit;
	}

	m_pSampleCallback = new (std::nothrow) CaptureEngineSampleCB(m_hwndPreview);
	if (m_pSampleCallback == NULL)
	{
		hr = E_OUTOFMEMORY;
		goto Exit;
	}

	m_pCallback->m_pManager = this;
	m_hwndPreview = hwndPreview;
	g_hwndPreviewCopy = hwndPreview;

	//Create a D3D Manager
	hr = CreateD3DManager();
	if (FAILED(hr))
	{
		printf("CreateD3DManager error\n");
		goto Exit;
	}
	hr = MFCreateAttributes(&pAttributes, 1);
	if (FAILED(hr))
	{
		printf("MFCreateAttributes error\n");
		goto Exit;
	}
	hr = pAttributes->SetUnknown(MF_CAPTURE_ENGINE_D3D_MANAGER, g_pDXGIMan);
	if (FAILED(hr))
	{
		printf("pAttributes->SetUnknown error\n");
		goto Exit;
	}

	/*hr = pAttributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
	if (FAILED(hr))
	{
		printf("pAttributes->SetUINT32 error\n");
		goto Exit;
	}*/

	// Create the factory object for the capture engine.
	hr = CoCreateInstance(CLSID_MFCaptureEngineClassFactory, NULL,
		CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pFactory));
	if (FAILED(hr))
	{
		printf("CoCreateInstance error\n");
		goto Exit;
	}

	// Create and initialize the capture engine.
	hr = pFactory->CreateInstance(CLSID_MFCaptureEngine, IID_PPV_ARGS(&m_pEngine));
	if (FAILED(hr))
	{
		printf("pFactory->CreateInstance error\n");
		goto Exit;
	}

	hr = m_pEngine->Initialize(m_pCallback, pAttributes, NULL, pUnk);
	if (FAILED(hr))
	{
		printf("m_pEngine->Initialize error\n");
		goto Exit;
	}

Exit:
	if (NULL != pAttributes)
	{
		pAttributes->Release();
		pAttributes = NULL;
	}
	if (NULL != pFactory)
	{
		pFactory->Release();
		pFactory = NULL;
	}
	return hr;
}

void CaptureManager::setGCapture(bool capture)
{
	g_Capture_photo = capture;
}

// Handle an event from the capture engine. 
// NOTE: This method is called from the application's UI thread. 
HRESULT CaptureManager::OnCaptureEvent(WPARAM wParam, LPARAM lParam)
{
	GUID guidType;
	HRESULT hrStatus;

	IMFMediaEvent *pEvent = reinterpret_cast<IMFMediaEvent*>(wParam);

	HRESULT hr = pEvent->GetStatus(&hrStatus);
	if (FAILED(hr))
	{
		printf("pEvent->GetStatus error\n");
		hrStatus = hr;
	}
	hr = pEvent->GetExtendedType(&guidType);
	if (SUCCEEDED(hr))
	{

#ifdef _DEBUG
		LPOLESTR str;
		if (SUCCEEDED(StringFromCLSID(guidType, &str)))
		{
			DBGMSG((L"MF_CAPTURE_ENGINE_EVENT: %s (hr = 0x%X)\n", str, hrStatus));
			CoTaskMemFree(str);
		}
#endif

		if (guidType == MF_CAPTURE_ENGINE_INITIALIZED)
		{
			printf("OnCaptureEngineInitialized\n");
			OnCaptureEngineInitialized(hrStatus);
			SetErrorID(hrStatus, IDS_ERR_INITIALIZE);
		}
		else if (guidType == MF_CAPTURE_ENGINE_PREVIEW_STARTED)
		{
			printf("OnPreviewStarted\n");
			OnPreviewStarted(hrStatus);
			SetErrorID(hrStatus, IDS_ERR_PREVIEW);
		}
		else if (guidType == MF_CAPTURE_ENGINE_PREVIEW_STOPPED)
		{
			printf("OnPreviewStopped\n");
			OnPreviewStopped(hrStatus);
			SetErrorID(hrStatus, IDS_ERR_PREVIEW);
		}
		else if (guidType == MF_CAPTURE_ENGINE_RECORD_STARTED)
		{
			printf("OnRecordStarted\n");
			OnRecordStarted(hrStatus);
			SetErrorID(hrStatus, IDS_ERR_RECORD);
		}
		else if (guidType == MF_CAPTURE_ENGINE_RECORD_STOPPED)
		{
			printf("OnRecordStopped\n");
			OnRecordStopped(hrStatus);
			SetErrorID(hrStatus, IDS_ERR_RECORD);
		}
		else if (guidType == MF_CAPTURE_ENGINE_PHOTO_TAKEN)
		{
			printf("m_bPhotoPending\n");
			//m_bPhotoPending = false;
			m_pEngine->TakePhoto();//continuous take photo 
			SetErrorID(hrStatus, IDS_ERR_PHOTO);
		}
		else if (guidType == MF_CAPTURE_ENGINE_ERROR)
		{
			printf("DestroyCaptureEngine\n");
			DestroyCaptureEngine();
			/*SetErrorID(hrStatus, IDS_ERR_CAPTURE);*/

			delete g_pEngine;
			g_pEngine = NULL;

			//re-init
			while (FAILED(CaptureManager::CreateInstance(initWindow, &g_pEngine)))
			{
				printf("CaptureManager::CreateInstance error\n");
				//goto done;
				Sleep(10000);
			}

			Sleep(5000);

			//capture again
			errorFlag = true;
			PostMessage(initWindow, WM_COMMAND, ID_CAPTURE_FRAME, 0L);
			SUCCEEDED(hrStatus);
		}
		else if (FAILED(hrStatus))
		{
			printf("pEvent->GetStatus error2\n");
			SetErrorID(hrStatus, IDS_ERR_CAPTURE);
		}
	}

	pEvent->Release();
	SetEvent(m_hEvent);
	return hrStatus;
}


void CaptureManager::OnCaptureEngineInitialized(HRESULT& hrStatus)
{
	if (hrStatus == MF_E_NO_CAPTURE_DEVICES_AVAILABLE)
	{
		hrStatus = S_OK;  // No capture device. Not an application error.
	}
}

void CaptureManager::OnPreviewStarted(HRESULT& hrStatus)
{
	m_bPreviewing = SUCCEEDED(hrStatus);
}

void CaptureManager::OnPreviewStopped(HRESULT& hrStatus)
{
	m_bPreviewing = false;
}

void CaptureManager::OnRecordStarted(HRESULT& hrStatus)
{
	m_bRecording = SUCCEEDED(hrStatus);
}

void CaptureManager::OnRecordStopped(HRESULT& hrStatus)
{
	m_bRecording = false;
}

HRESULT CaptureManager::StartPreview(bool capture_photo)
{
	initOnSampleVariables();

	if (m_pEngine == NULL)
	{
		return MF_E_NOT_INITIALIZED;
	}

	if (m_bPreviewing == true)
	{
		return S_OK;
	}

	IMFCaptureSink *pSink = NULL;
	IMFCaptureRecordSink *pRecord = NULL;
	IMFCapturePhotoSink *pPhoto = NULL;
	IMFMediaType *pMediaType = NULL;
	IMFMediaType *pMediaType2 = NULL;
	IMFCaptureSource *pSource = NULL;
	DWORD dwSinkStreamIndex;
	HRESULT hr = S_OK;
	GUID subType;
	UINT32 uiNumerator, uiDenominator, uiFps;

	// Get a pointer to the preview sink.
	if (m_pPreview == NULL)
	{
		switch (g_pin_no)
		{
		case 1://record
			hr = m_pEngine->GetSink(MF_CAPTURE_ENGINE_SINK_TYPE_RECORD, &pSink);
			printf("Select record pin\n");
			break;
		case 2://still
			hr = m_pEngine->GetSink(MF_CAPTURE_ENGINE_SINK_TYPE_PHOTO, &pSink);
			printf("Select still pin\n");
			break;
		case 0://preview
		default:
			hr = m_pEngine->GetSink(MF_CAPTURE_ENGINE_SINK_TYPE_PREVIEW, &pSink);
			printf("Select preview pin\n");
			break;
		}

		if (FAILED(hr))
		{
			printf("m_pEngine->GetSink error\n");
			goto done;
		}

		switch (g_pin_no)
		{
		case 1://record
			hr = pSink->QueryInterface(IID_PPV_ARGS(&pRecord));
			printf("query record pin\n");
			break;
		case 2://still
			hr = pSink->QueryInterface(IID_PPV_ARGS(&pPhoto));
			printf("query still pin\n");
			break;
		case 0://preview
		default:
			hr = pSink->QueryInterface(IID_PPV_ARGS(&m_pPreview));
			printf("query preview pin\n");
			break;
		}

		if (FAILED(hr))
		{
			printf("pSink->QueryInterface error\n");
			goto done;
		}

		/*hr = m_pPreview->SetRenderHandle(m_hwndPreview);
		if (FAILED(hr))
		{
			goto done;
		}*/

		hr = m_pEngine->GetSource(&pSource);
		if (FAILED(hr))
		{
			printf("m_pEngine->GetSource error\n");
			goto done;
		}

		
		int count = 0;
		HRESULT nativeTypeErrorCode = S_OK;
		int streamIndex = 0;
		//query suppported resolution
		while (nativeTypeErrorCode == S_OK) {
			nativeTypeErrorCode = pSource->GetAvailableDeviceMediaType(streamIndex, count, &pMediaType);
			if (nativeTypeErrorCode == MF_E_NO_MORE_TYPES)
				break;
			UINT32 m1_width, m1_height;
			hr = MFGetAttributeSize(pMediaType, MF_MT_FRAME_SIZE, &m1_width, &m1_height);
			if (FAILED(hr))
			{
				printf("MFGetAttributeSize error\n");
				goto done;
			}

			//hr = MFGetAttributeSize(pMediaType, MF_MT_FRAME_SIZE, &m1_width, &m1_height);
			UINT32 numeratorFrameRate, denominatorFrameRate;
			hr = MFGetAttributeRatio(pMediaType, MF_MT_FRAME_RATE, &numeratorFrameRate, &denominatorFrameRate);
			if (FAILED(hr))
			{
				printf("MFGetAttributeRatio error\n");
				goto done;
			}

			printf("[Supported resolution %d] (W*H)=%d*%d, %d FPS\n", count, m1_width, m1_height, numeratorFrameRate/denominatorFrameRate);
			count++;
		}

		if (g_resolutionIndex >= count) {
			g_resolutionIndex = 0;
			printf("wrong resolution index, and set it to default\n");
		}
		
		//select resolution
		int myRes = pSource->GetAvailableDeviceMediaType(streamIndex, g_resolutionIndex, &pMediaType);
		if (myRes != S_OK) {
			printf("fail to GetAvailableDeviceMediaType\n");
			goto done;
		}

		// Configure the video format for the preview sink.
		switch (g_pin_no)
		{
		case 1://record
			hr = pSource->GetCurrentDeviceMediaType((DWORD)MF_CAPTURE_ENGINE_PREFERRED_SOURCE_STREAM_FOR_VIDEO_RECORD, &pMediaType);
			printf("Get record pin\n");
			break;
		case 2://still
			hr = pSource->GetCurrentDeviceMediaType((DWORD)MF_CAPTURE_ENGINE_PREFERRED_SOURCE_STREAM_FOR_PHOTO, &pMediaType);
			printf("Get still pin\n");
			break;
		case 0://preview
		default:
			hr = pSource->GetCurrentDeviceMediaType((DWORD)MF_CAPTURE_ENGINE_PREFERRED_SOURCE_STREAM_FOR_VIDEO_PREVIEW, &pMediaType);
			printf("Get preview pin\n");
			break;
		}
		
		if (FAILED(hr))
		{
			printf("pSource->GetCurrentDeviceMediaType error\n");
			goto done;
		}

		// Keep the format of YUY2 (MEDIASUBTYPE_YUY2)
		hr = CloneVideoMediaType(pMediaType, MFVideoFormat_RGB24, &pMediaType2);
		if (FAILED(hr))
		{
			printf("CloneVideoMediaType error\n");
			goto done;
		}

		// Print current media type
		pMediaType2->GetGUID(MF_MT_SUBTYPE, &subType);
		printf("[Format] GUID: %08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X\n",
			subType.Data1, subType.Data2, subType.Data3,
			subType.Data4[0], subType.Data4[1], subType.Data4[2], subType.Data4[3],
			subType.Data4[4], subType.Data4[5], subType.Data4[6], subType.Data4[7]);

		hr = MFGetAttributeRatio(pMediaType2, MF_MT_FRAME_RATE, &uiNumerator, &uiDenominator);
		if (FAILED(hr))
		{
			printf("MFGetAttributeRatio error \n");
			goto done;
		}
		uiFps = uiNumerator / uiDenominator;
		printf("[Format] frame rate = %d \n", uiFps);

		/*hr = MFSetAttributeSize(pMediaType2, MF_MT_FRAME_SIZE, 1920, 1080);
		if (FAILED(hr))
		{
			printf("fail to set 1080p\n");
			goto done;
		}*/

		UINT32 uiWidth, uiHeight;
		hr = MFGetAttributeSize(pMediaType2, MF_MT_FRAME_SIZE, &uiWidth, &uiHeight);
		if (FAILED(hr))
		{
			printf("MFGetAttributeSize error\n");
			goto done;
		}
		printf("[Format] width = %d, Height = %d\n", uiWidth, uiHeight);
		g_Width = uiWidth;
		g_Height = uiHeight;

		hr = pMediaType2->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
		if (FAILED(hr))
		{
			printf("pMediaType2->SetUINT32 error\n");
			goto done;
		}

		// Connect the video stream to the preview sink.

		switch (g_pin_no)
		{
		case 1://record
			hr = pRecord->AddStream((DWORD)MF_CAPTURE_ENGINE_PREFERRED_SOURCE_STREAM_FOR_VIDEO_RECORD, pMediaType2, NULL, &dwSinkStreamIndex);
			printf("Add record pin\n");
			break;
		case 2://still
			hr = pPhoto->AddStream((DWORD)MF_CAPTURE_ENGINE_PREFERRED_SOURCE_STREAM_FOR_PHOTO, pMediaType2, NULL, &dwSinkStreamIndex);
			printf("Add still pin\n");
			break;
		case 0://preview
		default:
			hr = m_pPreview->AddStream((DWORD)MF_CAPTURE_ENGINE_PREFERRED_SOURCE_STREAM_FOR_VIDEO_PREVIEW, pMediaType2, NULL, &dwSinkStreamIndex);
			printf("Add preview pin\n");
			break;
		}
		if (FAILED(hr))
		{
			printf("select pin error\n");
			goto done;
		}

		// Set callback of sink
		switch (g_pin_no)
		{
		case 1://record
			hr = pRecord->SetSampleCallback(dwSinkStreamIndex, m_pSampleCallback);
			printf("SetSampleCallback record pin\n");
			break;
		case 2://still
			hr = pPhoto->SetSampleCallback(m_pSampleCallback);
			printf("SetSampleCallback still pin\n");
			break;
		case 0://preview
		default:
			hr = m_pPreview->SetSampleCallback(dwSinkStreamIndex, m_pSampleCallback);
			printf("SetSampleCallback preview pin\n");
			break;
		}

		if (FAILED(hr))
		{
			printf("m_pPreview->SetSampleCallback error\n");
			goto done;
		}

		//
		switch (g_pin_no)
		{
		case 1://record
			hr = m_pEngine->StartRecord();
			printf("StartRecord\n");
			break;
		case 2://still
			hr = m_pEngine->TakePhoto();
			printf("TakePhoto\n");
			break;
		case 0://preview
		default:
			hr = m_pEngine->StartPreview();
			printf("StartPreview\n");
			break;
		}

		if (FAILED(hr))
		{
			printf("m_pEngine->StartPreview error\n");
			goto done;
		}
	}

	g_Capture_photo = capture_photo;
	if (capture_photo) 		
		printf("Capture Frame mode \n");
	
	if (!m_fPowerRequestSet && m_hpwrRequest != INVALID_HANDLE_VALUE)
	{
		// NOTE:  By calling this, on SOC systems (AOAC enabled), we're asking the system to not go
		// into sleep/connected standby while we're streaming.  However, since we don't want to block
		// the device from ever entering connected standby/sleep, we're going to latch ourselves to
		// the monitor on/off notification (RegisterPowerSettingNotification(GUID_MONITOR_POWER_ON)).
		// On SOC systems, this notification will fire when the user decides to put the device in
		// connected standby mode--we can trap this, turn off our media streams and clear this
		// power set request to allow the device to go into the lower power state.
		m_fPowerRequestSet = (TRUE == PowerSetRequest(m_hpwrRequest, PowerRequestExecutionRequired));
	}
done:
	SafeRelease(&pSink);
	SafeRelease(&pMediaType);
	SafeRelease(&pMediaType2);
	SafeRelease(&pSource);

	return hr;
}

HRESULT CaptureManager::StopPreview()
{
	//init capture parameters
	initOnSampleVariables();

	//close engine
	HRESULT hr = S_OK;

	if (m_pEngine == NULL)
	{
		return MF_E_NOT_INITIALIZED;
	}

	if (!m_bPreviewing)
	{
		return S_OK;
	}
	hr = m_pEngine->StopPreview();
	if (FAILED(hr))
	{
		printf("m_pEngine->StopPreview error\n");
		goto done;
	}
	WaitForResult();

	if (m_fPowerRequestSet && m_hpwrRequest != INVALID_HANDLE_VALUE)
	{
		PowerClearRequest(m_hpwrRequest, PowerRequestExecutionRequired);
		m_fPowerRequestSet = false;
	}
done:
	return hr;
}

// Helper function to get the frame size from a video media type.
inline HRESULT GetFrameSize(IMFMediaType *pType, UINT32 *pWidth, UINT32 *pHeight)
{
	return MFGetAttributeSize(pType, MF_MT_FRAME_SIZE, pWidth, pHeight);
}

// Helper function to get the frame rate from a video media type.
inline HRESULT GetFrameRate(
	IMFMediaType *pType,
	UINT32 *pNumerator,
	UINT32 *pDenominator
)
{
	return MFGetAttributeRatio(
		pType,
		MF_MT_FRAME_RATE,
		pNumerator,
		pDenominator
	);
}


HRESULT GetEncodingBitrate(IMFMediaType *pMediaType, UINT32 *uiEncodingBitrate)
{
	UINT32 uiWidth;
	UINT32 uiHeight;
	float uiBitrate;
	UINT32 uiFrameRateNum;
	UINT32 uiFrameRateDenom;

	HRESULT hr = GetFrameSize(pMediaType, &uiWidth, &uiHeight);
	if (FAILED(hr))
	{
		printf("GetFrameSize error\n");
		goto done;
	}

	hr = GetFrameRate(pMediaType, &uiFrameRateNum, &uiFrameRateDenom);
	if (FAILED(hr))
	{
		printf("GetFrameRate error\n");
		goto done;
	}

	uiBitrate = uiWidth / 3.0f * uiHeight * uiFrameRateNum / uiFrameRateDenom;

	*uiEncodingBitrate = (UINT32)uiBitrate;

done:

	return hr;
}

HRESULT ConfigureVideoEncoding(IMFCaptureSource *pSource, IMFCaptureRecordSink *pRecord, REFGUID guidEncodingType)
{
	IMFMediaType *pMediaType = NULL;
	IMFMediaType *pMediaType2 = NULL;
	GUID guidSubType = GUID_NULL;

	// Configure the video format for the recording sink.
	HRESULT hr = pSource->GetCurrentDeviceMediaType((DWORD)MF_CAPTURE_ENGINE_PREFERRED_SOURCE_STREAM_FOR_VIDEO_RECORD, &pMediaType);
	if (FAILED(hr))
	{
		printf("pSource->GetCurrentDeviceMediaType error\n");
		goto done;
	}

	hr = CloneVideoMediaType(pMediaType, guidEncodingType, &pMediaType2);
	if (FAILED(hr))
	{
		printf("CloneVideoMediaType error\n");
		goto done;
	}


	hr = pMediaType->GetGUID(MF_MT_SUBTYPE, &guidSubType);
	if (FAILED(hr))
	{
		printf("pMediaType->GetGUID error\n");
		goto done;
	}

	if (guidSubType == MFVideoFormat_H264_ES || guidSubType == MFVideoFormat_H264)
	{
		//When the webcam supports H264_ES or H264, we just bypass the stream. The output from Capture engine shall be the same as the native type supported by the webcam
		hr = pMediaType2->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
	}
	else
	{
		UINT32 uiEncodingBitrate;
		hr = GetEncodingBitrate(pMediaType2, &uiEncodingBitrate);
		if (FAILED(hr))
		{
			printf("GetEncodingBitrate error\n");
			goto done;
		}

		hr = pMediaType2->SetUINT32(MF_MT_AVG_BITRATE, uiEncodingBitrate);
	}

	if (FAILED(hr))
	{
		printf("pMediaType2->SetGUID or pMediaType2->SetUINT32 error\n");
		goto done;
	}

	// Connect the video stream to the recording sink.
	DWORD dwSinkStreamIndex;
	hr = pRecord->AddStream((DWORD)MF_CAPTURE_ENGINE_PREFERRED_SOURCE_STREAM_FOR_VIDEO_RECORD, pMediaType2, NULL, &dwSinkStreamIndex);

done:
	SafeRelease(&pMediaType);
	SafeRelease(&pMediaType2);
	return hr;
}

HRESULT ConfigureAudioEncoding(IMFCaptureSource *pSource, IMFCaptureRecordSink *pRecord, REFGUID guidEncodingType)
{
	IMFCollection *pAvailableTypes = NULL;
	IMFMediaType *pMediaType = NULL;
	IMFAttributes *pAttributes = NULL;

	// Configure the audio format for the recording sink.

	HRESULT hr = MFCreateAttributes(&pAttributes, 1);
	if (FAILED(hr))
	{
		printf("MFCreateAttributes error\n");
		goto done;
	}

	// Enumerate low latency media types
	hr = pAttributes->SetUINT32(MF_LOW_LATENCY, TRUE);
	if (FAILED(hr))
	{
		printf("pAttributes->SetUINT32 error\n");
		goto done;
	}


	// Get a list of encoded output formats that are supported by the encoder.
	hr = MFTranscodeGetAudioOutputAvailableTypes(guidEncodingType, MFT_ENUM_FLAG_ALL | MFT_ENUM_FLAG_SORTANDFILTER,
		pAttributes, &pAvailableTypes);
	if (FAILED(hr))
	{
		printf("MFTranscodeGetAudioOutputAvailableTypes error\n");
		goto done;
	}

	// Pick the first format from the list.
	hr = GetCollectionObject(pAvailableTypes, 0, &pMediaType);
	if (FAILED(hr))
	{
		printf("GetCollectionObject error\n");
		goto done;
	}

	// Connect the audio stream to the recording sink.
	DWORD dwSinkStreamIndex;
	hr = pRecord->AddStream((DWORD)MF_CAPTURE_ENGINE_PREFERRED_SOURCE_STREAM_FOR_AUDIO, pMediaType, NULL, &dwSinkStreamIndex);
	if (hr == MF_E_INVALIDSTREAMNUMBER)
	{
		//If an audio device is not present, allow video only recording
		hr = S_OK;
	}
done:
	SafeRelease(&pAvailableTypes);
	SafeRelease(&pMediaType);
	SafeRelease(&pAttributes);
	return hr;
}

HRESULT CaptureManager::StartRecord(PCWSTR pszDestinationFile)
{
	if (m_pEngine == NULL)
	{
		return MF_E_NOT_INITIALIZED;
	}

	if (m_bRecording == true)
	{
		return MF_E_INVALIDREQUEST;
	}

	PWSTR pszExt = PathFindExtension(pszDestinationFile);

	GUID guidVideoEncoding;
	GUID guidAudioEncoding;

	if (wcscmp(pszExt, L".mp4") == 0)
	{
		guidVideoEncoding = MFVideoFormat_H264;
		guidAudioEncoding = MFAudioFormat_AAC;
	}
	else if (wcscmp(pszExt, L".wmv") == 0)
	{
		guidVideoEncoding = MFVideoFormat_WMV3;
		guidAudioEncoding = MFAudioFormat_WMAudioV9;
	}
	else if (wcscmp(pszExt, L".wma") == 0)
	{
		guidVideoEncoding = GUID_NULL;
		guidAudioEncoding = MFAudioFormat_WMAudioV9;
	}
	else
	{
		return MF_E_INVALIDMEDIATYPE;
	}

	IMFCaptureSink *pSink = NULL;
	IMFCaptureRecordSink *pRecord = NULL;
	IMFCaptureSource *pSource = NULL;

	HRESULT hr = m_pEngine->GetSink(MF_CAPTURE_ENGINE_SINK_TYPE_RECORD, &pSink);
	if (FAILED(hr))
	{
		printf("m_pEngine->GetSink error\n");
		goto done;
	}

	hr = pSink->QueryInterface(IID_PPV_ARGS(&pRecord));
	if (FAILED(hr))
	{
		printf("pSink->QueryInterface error\n");
		goto done;
	}

	hr = m_pEngine->GetSource(&pSource);
	if (FAILED(hr))
	{
		printf("m_pEngine->GetSource error\n");
		goto done;
	}

	// Clear any existing streams from previous recordings.
	hr = pRecord->RemoveAllStreams();
	if (FAILED(hr))
	{
		printf("pRecord->RemoveAllStreams error\n");
		goto done;
	}

	hr = pRecord->SetOutputFileName(pszDestinationFile);
	if (FAILED(hr))
	{
		printf("pRecord->SetOutputFileName error\n");
		goto done;
	}

	// Configure the video and audio streams.
	if (guidVideoEncoding != GUID_NULL)
	{
		hr = ConfigureVideoEncoding(pSource, pRecord, guidVideoEncoding);
		if (FAILED(hr))
		{
			printf("ConfigureVideoEncoding error\n");

			goto done;
		}
	}

	if (guidAudioEncoding != GUID_NULL)
	{
		hr = ConfigureAudioEncoding(pSource, pRecord, guidAudioEncoding);
		if (FAILED(hr))
		{
			printf("ConfigureAudioEncoding error\n");

			goto done;
		}
	}

	hr = m_pEngine->StartRecord();
	if (FAILED(hr))
	{
		printf("m_pEngine->StartRecord error\n");

		goto done;
	}

	m_bRecording = true;

done:
	SafeRelease(&pSink);
	SafeRelease(&pSource);
	SafeRelease(&pRecord);

	return hr;
}

HRESULT CaptureManager::StopRecord()
{
	HRESULT hr = S_OK;

	if (m_bRecording)
	{
		hr = m_pEngine->StopRecord(TRUE, FALSE);
		WaitForResult();
	}

	return hr;
}


HRESULT CaptureManager::TakePhoto(PCWSTR pszFileName)
{
	IMFCaptureSink *pSink = NULL;
	IMFCapturePhotoSink *pPhoto = NULL;
	IMFCaptureSource *pSource;
	IMFMediaType *pMediaType = 0;
	IMFMediaType *pMediaType2 = 0;
	bool bHasPhotoStream = true;

	// Get a pointer to the photo sink.
	HRESULT hr = m_pEngine->GetSink(MF_CAPTURE_ENGINE_SINK_TYPE_PHOTO, &pSink);
	if (FAILED(hr))
	{
		printf("m_pEngine->GetSink error\n");

		goto done;
	}

	hr = pSink->QueryInterface(IID_PPV_ARGS(&pPhoto));
	if (FAILED(hr))
	{
		printf("pSink->QueryInterface error\n");

		goto done;
	}

	hr = m_pEngine->GetSource(&pSource);
	if (FAILED(hr))
	{
		printf("m_pEngine->GetSource error\n");

		goto done;
	}

	hr = pSource->GetCurrentDeviceMediaType((DWORD)MF_CAPTURE_ENGINE_PREFERRED_SOURCE_STREAM_FOR_PHOTO, &pMediaType);
	if (FAILED(hr))
	{
		printf("pSource->GetCurrentDeviceMediaType error\n");

		goto done;
	}

	//Configure the photo format
	hr = CreatePhotoMediaType(pMediaType, &pMediaType2);
	if (FAILED(hr))
	{
		printf("CreatePhotoMediaType error\n");

		goto done;
	}

	hr = pPhoto->RemoveAllStreams();
	if (FAILED(hr))
	{
		printf("pPhoto->RemoveAllStreams error\n");

		goto done;
	}

	DWORD dwSinkStreamIndex;
	// Try to connect the first still image stream to the photo sink
	if (bHasPhotoStream)
	{
		hr = pPhoto->AddStream((DWORD)MF_CAPTURE_ENGINE_PREFERRED_SOURCE_STREAM_FOR_PHOTO, pMediaType2, NULL, &dwSinkStreamIndex);
	}

	if (FAILED(hr))
	{
		printf("pPhoto->AddStream error\n");

		goto done;
	}

	hr = pPhoto->SetOutputFileName(pszFileName);
	if (FAILED(hr))
	{
		printf("pPhoto->SetOutputFileName error\n");

		goto done;
	}

	hr = m_pEngine->TakePhoto();
	if (FAILED(hr))
	{
		printf("m_pEngine->TakePhoto error\n");

		goto done;
	}

	m_bPhotoPending = true;

done:
	SafeRelease(&pSink);
	SafeRelease(&pPhoto);
	SafeRelease(&pSource);
	SafeRelease(&pMediaType);
	SafeRelease(&pMediaType2);
	return hr;
}




HRESULT CaptureManager::UpdateVideo(HDC hdc)
{
	int rv = StretchDIBits(hdc, g_Width, g_Height, -g_Width, -g_Height, 0, 0, g_Width, g_Height,
		g_pbInputData, &g_BitmapInfo,
		DIB_RGB_COLORS, SRCCOPY);
	if (rv == 0) {
		printf("StretchDIBits failed\n");
	}

	return S_OK;
}