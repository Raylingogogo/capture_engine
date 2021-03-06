// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved.

#include "Capture.h"
#include "resource.h"

#include <shlobj.h>
#include <Shlwapi.h>
#include <powrprof.h>
#include <ks.h>
#include <iostream>
#include <atlcomcli.h>
#include <iostream>
#include <string>

int g_threshold, g_op_mode, g_countToCapture, g_device_type, g_select_no, g_resolutionIndex, g_pin_no;
int g_sum=0, g_validNum=0;

WCHAR *g_toolVersion = L"version: 20180412";
FILE *file_log;
HWND initWindow;

#define DEFAULT_WIN_POS_X	200
#define DEFAULT_WIN_POS_Y	100
#define DEFAULT_WIN_WIDTH	500
#define DEFAULT_WIN_HEIGHT	500

// Main function for the console
int main(int argc, char **argv) {

	if (argc < 8)
	{
		printf("[Error] PLease follow the format below\n");
		printf("CaptureEngine.exe [threshold] [OPmode] [countToCapture] [deviceType] [selectDeviceNo] [resolution] [pinNO]\n");
		printf("[threshold]: An integer used to judge pass or fail\n");
		printf("[OPmode]: \n");
		printf("          0: no display mode \n");
		printf("          1: display normally\n");
		printf("          2: display light mode \n");
		printf("          3: display dark mode \n");
		printf("[countToCapture]: Start capture after numbers of frame\n");
		printf("[deviceType]: \n");
		printf("              0: IR camera\n");
		printf("              1: RGB camera\n");
		printf("[selectDeviceNo]: An integer(device count-1) used to determine the device you wanna test\n\n");
		printf("The variables below is used in IR camera, but you should provide in normal camera !\n");
		printf("[resolution]: An integer used to select provided resolution (default value should be 0)\n");
		printf("[pinNO]: \n");
		printf("       0: preview pin\n");
		printf("       1: record pin\n");
		printf("       2: still pin\n");

		exit(1);
	}

	std::string threshold(argv[1]);
	std::string op_mode(argv[2]);
	std::string countToCapture(argv[3]);
	std::string select_devType(argv[4]);
	std::string select_devNo(argv[5]);
	std::string select_resolution(argv[6]);
	std::string select_pin(argv[7]);

	g_threshold = std::stoi(threshold);
	g_op_mode = std::stoi(op_mode);
	g_countToCapture = std::stoi(countToCapture);
	g_device_type = std::stoi(select_devType);
	g_select_no = std::stoi(select_devNo);
	g_resolutionIndex = std::stoi(select_resolution);
	g_pin_no = std::stoi(select_pin);

	// Calling the wWinMain function to start the GUI program
	// Parameters:
	// GetModuleHandle(NULL) - To get a handle to the current instance
	// NULL - Previous instance is not needed
	// NULL - Command line parameters are not needed
	// 1 - To show the window normally

	wWinMain(GetModuleHandle(NULL), NULL, NULL, 1);

	//system("pause");
	return 0;
}

// Include the v6 common controls in the manifest
#pragma comment(linker, \
    "\"/manifestdependency:type='Win32' "\
    "name='Microsoft.Windows.Common-Controls' "\
    "version='6.0.0.0' "\
    "processorArchitecture='*' "\
    "publicKeyToken='6595b64144ccf1df' "\
    "language='*'\"")

CaptureManager *g_pEngine = NULL;
HPOWERNOTIFY    g_hPowerNotify = NULL;
HPOWERNOTIFY    g_hPowerNotifyMonitor = NULL;
SYSTEM_POWER_CAPABILITIES   g_pwrCaps;
bool            g_fSleepState = false;

INT_PTR CALLBACK ChooseDeviceDlgProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);


INT WINAPI wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE /*hPrevInstance*/, _In_ LPWSTR /*lpCmdLine*/, _In_ INT nCmdShow)
{
	bool bCoInit = false, bMFStartup = false;

	// Initialize the common controls
	const INITCOMMONCONTROLSEX icex = { sizeof(INITCOMMONCONTROLSEX), ICC_WIN95_CLASSES };
	InitCommonControlsEx(&icex);

	// Note: The shell common File dialog requires apartment threading.
	HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
	if (FAILED(hr))
	{
		printf("CoInitializeEx error\n");

		goto done;
	}
	bCoInit = true;

	hr = MFStartup(MF_VERSION);
	if (FAILED(hr))
	{
		printf("MFStartup error\n");

		goto done;
	}

	bMFStartup = true;

	HWND hwnd = CreateMainWindow(hInstance);
	if (hwnd == 0)
	{
		ShowError(NULL, L"CreateMainWindow failed.", hr);
		goto done;
	}

	ShowWindow(hwnd, nCmdShow);

	// Run the message loop.

	MSG msg;
	while (GetMessage(&msg, NULL, 0, 0))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

done:
	if (FAILED(hr))
	{
		printf("MFStartup error2\n");

		ShowError(NULL, L"Failed to start application", hr);
	}
	if (bMFStartup)
	{
		MFShutdown();
	}
	if (bCoInit)
	{
		CoUninitialize();
	}
	return 0;
}


// Dialog functions

HRESULT OnInitDialog(HWND hwnd, ChooseDeviceParam *pParam);
HRESULT OnOK(HWND hwnd, ChooseDeviceParam *pParam);

// Window procedure for the "Choose Device" dialog.

INT_PTR CALLBACK ChooseDeviceDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	static ChooseDeviceParam *pParam = NULL;

	switch (msg)
	{
	case WM_INITDIALOG:
		pParam = (ChooseDeviceParam*)lParam;
		OnInitDialog(hwnd, pParam);
		return TRUE;

	case WM_COMMAND:
		switch (LOWORD(wParam))
		{
		case IDOK:
			OnOK(hwnd, pParam);
			EndDialog(hwnd, LOWORD(wParam));
			return TRUE;

		case IDCANCEL:
			EndDialog(hwnd, LOWORD(wParam));
			return TRUE;
		}
		break;
	}

	return FALSE;
}

// Handler for WM_INITDIALOG

HRESULT OnInitDialog(HWND hwnd, ChooseDeviceParam *pParam)
{
	HRESULT hr = S_OK;

	HWND hList = GetDlgItem(hwnd, IDC_DEVICE_LIST);

	// Display a list of the devices.

	for (DWORD i = 0; i < pParam->count; i++)
	{
		WCHAR *szFriendlyName = NULL;
		UINT32 cchName;

		hr = pParam->ppDevices[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME,
			&szFriendlyName, &cchName);
		if (FAILED(hr))
		{
			printf("pParam->ppDevices[i]->GetAllocatedString error\n");

			break;
		}

		int index = ListBox_AddString(hList, szFriendlyName);

		ListBox_SetItemData(hList, index, i);

		CoTaskMemFree(szFriendlyName);
	}

	// Assume no selection for now.
	pParam->selection = (UINT32)-1;

	if (pParam->count == 0)
	{
		// If there are no devices, disable the "OK" button.
		EnableWindow(GetDlgItem(hwnd, IDOK), FALSE);
	}
	else
	{
		// Select the first device in the list.
		ListBox_SetCurSel(hList, 0);
	}

	return hr;
}

// Handler for the OK button

HRESULT OnOK(HWND hwnd, ChooseDeviceParam *pParam)
{
	HWND hList = GetDlgItem(hwnd, IDC_DEVICE_LIST);

	// Get the current selection and return it to the application.
	int sel = ListBox_GetCurSel(hList);

	if (sel != LB_ERR)
	{
		pParam->selection = (UINT32)ListBox_GetItemData(hList, sel);
	}

	return S_OK;
}


HWND CreateStatusBar(HWND hParent, UINT nID)
{
	return CreateStatusWindow(WS_CHILD | WS_VISIBLE, L"", hParent, nID);
}

BOOL StatusSetText(HWND hwnd, int iPart, const TCHAR* szText, BOOL bNoBorders = FALSE, BOOL bPopOut = FALSE)
{
	UINT flags = 0;
	if (bNoBorders)
	{
		flags |= SBT_NOBORDERS;
	}
	if (bPopOut)
	{
		flags |= SBT_POPOUT;
	}

	return (BOOL)SendMessage(hwnd, SB_SETTEXT, (WPARAM)(iPart | flags), (LPARAM)szText);
}



// Implements the window procedure for the main application window.

namespace MainWindow
{
	HWND hPreview = NULL;
	HWND hStatus = NULL;
	bool bRecording = false;
	bool bPreviewing = false;
	IMFActivate* pSelectedDevice = NULL;
	static bool capture_photo = 0;
	int x_diff = 0, y_diff = 0;
	wchar_t PhotoFileName[MAX_PATH];

	inline void _SetStatusText(const WCHAR *szStatus)
	{
		StatusSetText(hStatus, 0, szStatus);
	}

	void OnChooseDevice(HWND hwnd);
	BOOL OnCreate(HWND hwnd, LPCREATESTRUCT lpCreateStruct);
	void OnPaint(HWND hwnd);
	void OnSize(HWND hwnd, UINT state, int cx, int cy);
	void OnDestroy(HWND hwnd);
	void OnChooseDevice(HWND hwnd);
	void OnStartRecord(HWND hwnd);
	void OnStopRecord(HWND hwnd);
	void OnCommand(HWND hwnd, int id, HWND hwndCtl, UINT codeNotify);

	void UpdateUI(HWND hwnd)
	{
		if (g_pEngine->IsRecording() != bRecording)
		{
			bRecording = g_pEngine->IsRecording();
			if (bRecording)
			{
				SetMenuItemText(GetMenu(hwnd), ID_CAPTURE_RECORD, L"Stop Recording");
			}
			else
			{
				SetMenuItemText(GetMenu(hwnd), ID_CAPTURE_RECORD, L"Start Recording");
			}
		}

		if (g_pEngine->IsPreviewing() != bPreviewing)
		{
			bPreviewing = g_pEngine->IsPreviewing();
			if (bPreviewing)
			{
				SetMenuItemText(GetMenu(hwnd), ID_CAPTURE_PREVIEW, L"Stop Preview");
			}
			else
			{
				SetMenuItemText(GetMenu(hwnd), ID_CAPTURE_PREVIEW, L"Start Preview");
			}
		}
		BOOL bEnableRecording = TRUE;
		BOOL bEnablePhoto = TRUE;

		if (bRecording)
		{
			_SetStatusText(L"Recording");
		}
		else if (g_pEngine->IsPreviewing())
		{
			_SetStatusText(L"Previewing");
		}
		else
		{
			_SetStatusText(L"Please select a device or start preview (using the default device).");
			bEnableRecording = FALSE;
		}

		if (!g_pEngine->IsPreviewing() || g_pEngine->IsPhotoPending())
		{
			bEnablePhoto = FALSE;
		}

		EnableMenuItem(GetMenu(hwnd), ID_CAPTURE_RECORD, bEnableRecording ? MF_ENABLED : MF_GRAYED);
		EnableMenuItem(GetMenu(hwnd), ID_CAPTURE_TAKEPHOTO, bEnablePhoto ? MF_ENABLED : MF_GRAYED);
	}


	BOOL OnCreate(HWND hwnd, LPCREATESTRUCT /*lpCreateStruct*/)
	{
		BOOL                fSuccess = FALSE;
		IMFAttributes*      pAttributes = NULL;
		HRESULT             hr = S_OK;

		initWindow = hwnd;

		WCHAR titleName[100] = L"[IR Test Tool] ";
		wcscat(titleName, g_toolVersion);

		//set window size and title
		SetWindowText(hwnd, titleName);
		SetWindowPos(hwnd, HWND_TOPMOST, DEFAULT_WIN_POS_X, DEFAULT_WIN_POS_Y, DEFAULT_WIN_WIDTH, DEFAULT_WIN_HEIGHT, SWP_NOZORDER);

		hPreview = CreatePreviewWindow(GetModuleHandle(NULL), hwnd);
		if (hPreview == NULL)
		{
			goto done;
		}

		hStatus = CreateStatusBar(hwnd, IDC_STATUS_BAR);
		if (hStatus == NULL)
		{
			goto done;
		}

		if (FAILED(CaptureManager::CreateInstance(hwnd, &g_pEngine)))
		{
			printf("CaptureManager::CreateInstance error\n");

			goto done;
		}

		/*hr = g_pEngine->InitializeCaptureManager(hPreview, pSelectedDevice);
		if (FAILED(hr))
		{
			printf("g_pEngine->InitializeCaptureManager error\n");

			ShowError(hwnd, IDS_ERR_SET_DEVICE, hr);
			goto done;
		}*/

		// Register for connected standy changes.  This should come through the normal
		// WM_POWERBROADCAST messages that we're already handling below.
		// We also want to hook into the monitor on/off notification for AOAC (SOC) systems.
		g_hPowerNotify = RegisterSuspendResumeNotification((HANDLE)hwnd, DEVICE_NOTIFY_WINDOW_HANDLE);
		g_hPowerNotifyMonitor = RegisterPowerSettingNotification((HANDLE)hwnd, &GUID_MONITOR_POWER_ON, DEVICE_NOTIFY_WINDOW_HANDLE);
		ZeroMemory(&g_pwrCaps, sizeof(g_pwrCaps));
		GetPwrCapabilities(&g_pwrCaps);

		UpdateUI(hwnd);
		fSuccess = TRUE;


		if (g_op_mode == 0) {
			capture_photo = 1;
		}

		// Post message to enum device
		PostMessage(hwnd, WM_COMMAND, ID_CAPTURE_CHOOSEDEVICE, 0L);
		// Note: Need to wait for device preparation
		//Sleep(300);
		PostMessage(hwnd, WM_COMMAND, ID_CAPTURE_PREVIEW, 0L);

		//start caputre in initialization
		/*if (g_op_mode == 0) {
			Sleep(5000);
			PostMessage(hwnd, WM_COMMAND, ID_CAPTURE_FRAME, 0L);
		}*/
	done:
		SafeRelease(&pAttributes);
		return fSuccess;
	}

	void OnPaint(HWND hwnd)
	{
		PAINTSTRUCT ps;
		HDC hdc = BeginPaint(hwnd, &ps);

		FillRect(hdc, &ps.rcPaint, (HBRUSH)(COLOR_WINDOW + 1));

		EndPaint(hwnd, &ps);
		//printf("on Paint\n");
	}


	void OnSize(HWND /*hwnd*/, UINT state, int cx, int cy)
	{
		//Caculate the x, y diff (Declare Window x/y - real rectange x/y) at beginning. Will be used for adjust the window size
		if (x_diff == 0 && y_diff == 0)
		{
			x_diff = DEFAULT_WIN_WIDTH - cx;
			y_diff = DEFAULT_WIN_HEIGHT - cy;
			printf("OnSize = %d %d, ", cx, cy);
			printf("diff = %d %d\n", x_diff, y_diff);
		}

		if (state == SIZE_RESTORED || state == SIZE_MAXIMIZED)
		{
			// Resize the status bar.
			SendMessageW(hStatus, WM_SIZE, 0, 0);

			// Resize the preview window.
			RECT statusRect;
			SendMessageW(hStatus, SB_GETRECT, 0, (LPARAM)&statusRect);
			cy -= (statusRect.bottom - statusRect.top);

			MoveWindow(hPreview, 0, 0, cx, cy, TRUE);
		}
	}

	void OnDestroy(HWND hwnd)
	{
		delete g_pEngine;
		g_pEngine = NULL;
		if (g_hPowerNotify)
		{
			UnregisterSuspendResumeNotification(g_hPowerNotify);
			g_hPowerNotify = NULL;
		}
		//PostQuitMessage(0);
	}

	void OnChooseDevice(HWND hwnd)
	{
		ChooseDeviceParam param;

		IMFAttributes *pAttributes = NULL;

		HRESULT hr = MFCreateAttributes(&pAttributes, 1);
		if (FAILED(hr))
		{
			printf("MFCreateAttributes error\n");
			goto done;
		}

		// Ask for source type = video capture devices
		hr = pAttributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
			MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
		if (FAILED(hr))
		{
			printf("pAttributes->SetGUID error\n");

			goto done;
		}

		if (g_device_type == 0) {
			printf("find IR camera\n");
			hr = pAttributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_CATEGORY, KSCATEGORY_SENSOR_CAMERA);
			
			//hr = pAttributes->SetGUID(KSPROPERTY_CAMERACONTROL_EXTENDED_PROPERTY, );
		}
		else {
			printf("find RGB camera\n");
			hr = pAttributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_CATEGORY, KSCATEGORY_VIDEO_CAMERA);
		}

		if (FAILED(hr))
		{
			printf("pAttributes->SetGUID2 error\n");

			goto done;
		}
		// Enumerate devices.
		hr = MFEnumDeviceSources(pAttributes, &param.ppDevices, &param.count);
		if (FAILED(hr))
		{
			printf("MFEnumDeviceSources error\n");

			goto done;
		}

		// Check Camera enumeration. If camera is not found, exit and write out log
		if (param.count == 0)
		{
			char log_buf[100] = { 0 };

			// Stream File open
			if ((fopen_s(&file_log, "result.txt", "w+")) != 0)
				printf("The log file was not opened\n");

			_bstr_t b(g_toolVersion);
			char *outputVersion = b;
			// Write to log file
			if (g_device_type == 0) {
				printf("[ERROR] IR CAMERA NOT FOUND \n");
				sprintf_s(log_buf, "%s \n%s \n%s \n", "FAIL", "[ERROR] IR CAMERA NOT FOUND", outputVersion);
			}
			else if (g_device_type == 1) {
				printf("[ERROR] RGB CAMERA NOT FOUND \n");
				sprintf_s(log_buf, "%s \n%s \n%s \n", "FAIL", "[ERROR] RGB CAMERA NOT FOUND", outputVersion);
			}
			else {
				printf("[ERROR] UNKNOWN CAMERA  NOT FOUND \n");
				sprintf_s(log_buf, "%s \n%s \n%s \n", "FAIL", "[ERROR] UNKNOWN CAMERA NOT FOUND", outputVersion);
			}
			
			fwrite(log_buf, 1, sizeof(log_buf), file_log);
			fclose(file_log);

			exit(1);
		}
		else {
			printf("MFEnumDeviceSources device count %d\n", param.count);
		}

		// Ask the user to select one.
		// Skip selection and use default
		//pop out dialog
		/*INT_PTR result = DialogBoxParam(GetModuleHandle(NULL),
		MAKEINTRESOURCE(IDD_CHOOSE_DEVICE), hwnd,
		ChooseDeviceDlgProc, (LPARAM)&param);*/
		

		//if ((result == IDOK) && (param.selection != (UINT32)-1))
		if (1)
		{
			//UINT iDevice = param.selection;
			UINT iDevice = g_select_no; //Default use 1
			if (iDevice >= param.count)
			{
				hr = E_UNEXPECTED;
				goto done;
			}

			hr = g_pEngine->InitializeCaptureManager(hPreview, param.ppDevices[iDevice]);
			if (FAILED(hr))
			{
				printf("g_pEngine->InitializeCaptureManager error\n");

				goto done;
			}
			SafeRelease(&pSelectedDevice);
			pSelectedDevice = param.ppDevices[iDevice];
			pSelectedDevice->AddRef();

			// Get Friendly Name
			WCHAR *szFriendlyName = NULL;
			UINT32 cchName;

			hr = param.ppDevices[iDevice]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME,
				&szFriendlyName, &cchName);
			if (FAILED(hr))
			{
				printf("param.ppDevices[iDevice]->GetAllocatedString error\n");

				goto done;
			}
			wprintf(L"Device Name = %s \n", szFriendlyName);
			CoTaskMemFree(szFriendlyName);
		}

	done:
		SafeRelease(&pAttributes);
		if (FAILED(hr))
		{
			printf("OnChooseDevice end error\n");

			ShowError(hwnd, IDS_ERR_SET_DEVICE, hr);
		}
		UpdateUI(hwnd);
	}


	void OnStartRecord(HWND hwnd)
	{
		IFileSaveDialog *pFileSave = NULL;
		IShellItem *pItem = NULL;
		PWSTR pszFileName = NULL;

		HRESULT hr = CoCreateInstance(CLSID_FileSaveDialog, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pFileSave));
		if (FAILED(hr))
		{
			printf("CoCreateInstance error\n");

			goto done;
		}
		hr = pFileSave->SetTitle(L"Select File Name");
		if (FAILED(hr))
		{
			printf("pFileSave->SetTitle error\n");

			goto done;
		}

		hr = pFileSave->SetFileName(L"MyVideo.mp4");
		if (FAILED(hr))
		{
			printf("pFileSave->SetFileName error\n");

			goto done;
		}

		hr = pFileSave->SetDefaultExtension(L"mp4");
		if (FAILED(hr))
		{
			printf("pFileSave->SetDefaultExtension error\n");

			goto done;
		}

		const COMDLG_FILTERSPEC rgSpec[] =
		{
			{ L"MP4 (H.264/AAC)", L"*.mp4" },
			{ L"Windows Media Video", L"*.wmv" },
			{ L"All Files", L"*.*" },
		};
		hr = pFileSave->SetFileTypes(ARRAYSIZE(rgSpec), rgSpec);
		if (FAILED(hr))
		{
			printf("pFileSave->SetFileTypes error\n");

			goto done;
		}

		hr = pFileSave->Show(hwnd);
		if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED))
		{
			hr = S_OK;      // The user canceled the dialog.
			goto done;
		}
		if (FAILED(hr))
		{
			printf("pFileSave->Show error\n");

			goto done;
		}

		hr = pFileSave->GetResult(&pItem);
		if (FAILED(hr))
		{
			printf("pFileSave->GetResult error\n");

			goto done;
		}

		hr = pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFileName);
		if (FAILED(hr))
		{
			printf("pItem->GetDisplayName error\n");

			goto done;
		}

		hr = g_pEngine->StartRecord(pszFileName);
		if (FAILED(hr))
		{
			printf("g_pEngine->StartRecord error\n");

			goto done;
		}

	done:
		CoTaskMemFree(pszFileName);
		SafeRelease(&pItem);
		SafeRelease(&pFileSave);

		if (FAILED(hr))
		{
			printf("OnStartRecord done error\n");

			ShowError(hwnd, IDS_ERR_RECORD, hr);
		}
		UpdateUI(hwnd);
	}

	void OnStopRecord(HWND hwnd)
	{
		HRESULT hr = g_pEngine->StopRecord();
		if (FAILED(hr))
		{
			printf("g_pEngine->StopRecord error\n");

			ShowError(hwnd, IDS_ERR_RECORD, hr);
		}
		UpdateUI(hwnd);
	}
	void OnStopPreview(HWND hwnd)
	{
		HRESULT hr = g_pEngine->StopPreview();
		if (FAILED(hr))
		{
			printf("g_pEngine->StopPreview error\n");

			ShowError(hwnd, IDS_ERR_RECORD, hr);
		}
		UpdateUI(hwnd);
	}
	void OnStartPreview(HWND hwnd, bool capture_photo)
	{
		HRESULT hr = g_pEngine->StartPreview(capture_photo);
		if (FAILED(hr))
		{
			printf("g_pEngine->StartPreview error\n");

			ShowError(hwnd, IDS_ERR_RECORD, hr);
		}

		RECT statusRect;
		SendMessageW(hStatus, SB_GETRECT, 0, (LPARAM)&statusRect);
		
		/* Caculate new window size (cx, cy): streaming resolution added the x/y diff (previously saved) & satus bar */
		int cx = g_Width + x_diff;
		int cy = g_Height + y_diff + (statusRect.bottom);
		
		/* get current window position */
		RECT MainWinRect;
		GetWindowRect(hwnd, (LPRECT)&MainWinRect);
		printf("x = %d, y = %d, cx = %d, cy = %d\n", MainWinRect.left, MainWinRect.top, cx, cy);
		
		/* update the new windows size and original position*/
		MoveWindow(hwnd, MainWinRect.left, MainWinRect.top, cx, cy, TRUE);
		
		UpdateUI(hwnd);

	}
	void OnTakePhoto(HWND hwnd)
	{
		wchar_t filename[MAX_PATH];

		// Get the path to the Documents folder.
		IShellItem *psi = NULL;
		PWSTR pszFolderPath = NULL;

		HRESULT hr = SHCreateItemInKnownFolder(FOLDERID_Documents, 0, NULL, IID_PPV_ARGS(&psi));
		if (FAILED(hr))
		{
			printf("SHCreateItemInKnownFolder error\n");

			goto done;
		}

		hr = psi->GetDisplayName(SIGDN_FILESYSPATH, &pszFolderPath);
		if (FAILED(hr))
		{
			printf("psi->GetDisplayName error\n");

			goto done;
		}

		// Construct a file name based on the current time.

		SYSTEMTIME time;
		GetLocalTime(&time);

		hr = StringCchPrintf(filename, MAX_PATH, L"MyPhoto%04u_%02u%02u_%02u%02u%02u.jpg",
			time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond);
		if (FAILED(hr))
		{
			printf("StringCchPrintf error\n");

			goto done;
		}

		LPTSTR path = PathCombine(PhotoFileName, pszFolderPath, filename);
		if (path == NULL)
		{
			hr = E_FAIL;
			goto done;
		}
		wprintf(L"%s\n", path);

		hr = g_pEngine->TakePhoto(filename);
		if (FAILED(hr))
		{
			printf("g_pEngine->TakePhoto error\n");

			goto done;
		}

		_SetStatusText(path);

	done:
		SafeRelease(&psi);
		CoTaskMemFree(pszFolderPath);

		if (FAILED(hr))
		{
			printf("OnTakePhoto done error\n");

			ShowError(hwnd, IDS_ERR_PHOTO, hr);
		}
		UpdateUI(hwnd);
	}

	void OnCommand(HWND hwnd, int id, HWND /*hwndCtl*/, UINT /*codeNotify*/)
	{
		switch (id)
		{
		case ID_CAPTURE_CHOOSEDEVICE:
			OnChooseDevice(hwnd);
			break;

		case ID_CAPTURE_RECORD:
			if (g_pEngine->IsRecording())
			{
				OnStopRecord(hwnd);
			}
			else
			{
				OnStartRecord(hwnd);
			}
			break;

		case ID_CAPTURE_TAKEPHOTO:
			// Can't use traditional function to do capturing...Can't get data
			OnTakePhoto(hwnd);
			break;
		case ID_CAPTURE_PREVIEW:
			printf("capture_photo=%d, count to capture %d\n", capture_photo, g_countToCapture);
			Sleep(100);
			if (g_pEngine->IsPreviewing())
			{
				OnStopPreview(hwnd);
			}
			else
			{
				OnStartPreview(hwnd, capture_photo);
			}
			capture_photo = 0;
			break;
		case ID_CAPTURE_FRAME:
		{
			capture_photo = 1;

			printf("capture directly\n");
			g_pEngine->setGCapture(capture_photo);
			
			break;
		}
		case ID_SET_FRAME_RATE:
		{
			//printf("set frame rate\n");
			int g_frame_rate = 1000 / (g_sum / g_validNum);
			printf("validNum %d, FPS %d\n", g_validNum, g_frame_rate);
			WCHAR titleName[100] = L"[IR Test Tool] ";
			wcscat(titleName, g_toolVersion);

			WCHAR frameRate[16];
			swprintf_s(frameRate, L"   FPS:%d", g_frame_rate);
			wcscat(titleName, frameRate);

			//set window size and title
			SetWindowText(hwnd, titleName);

			break;
		}
		case ID_JUMP_TO_FAIL:
		{
			printf("jump to fail\n");

			char log_buf[100] = { 0 };
			if ((fopen_s(&file_log, "result.txt", "w+")) != 0)
				printf("The log file was not opened\n");

			_bstr_t b(g_toolVersion);
			char *outputVersion = b;
			// Write to log file
			sprintf_s(log_buf, "%s \n%s \n%s \n", "FAIL", "[ERROR] IMAGE QUALITY CHECK FAIL", outputVersion);
			fwrite(log_buf, 1, sizeof(log_buf), file_log);
			fclose(file_log);

			exit(1);

			break;
		}
		}
	}


	LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
	{
		switch (uMsg)
		{
			HANDLE_MSG(hwnd, WM_CREATE, OnCreate);
			HANDLE_MSG(hwnd, WM_PAINT, OnPaint);
			HANDLE_MSG(hwnd, WM_SIZE, OnSize);
			HANDLE_MSG(hwnd, WM_DESTROY, OnDestroy);
			HANDLE_MSG(hwnd, WM_COMMAND, OnCommand);

		case WM_ERASEBKGND:
			return 1;

		case WM_APP_CAPTURE_EVENT:
		{
			if (g_pEngine)
			{
				HRESULT hr = g_pEngine->OnCaptureEvent(wParam, lParam);
				if (FAILED(hr))
				{
					printf("g_pEngine->OnCaptureEvent error\n");

					ShowError(hwnd, g_pEngine->ErrorID(), hr);
					InvalidateRect(hwnd, NULL, FALSE);
				}
			}
			
			UpdateUI(hwnd);
		}
		return 0;
		case WM_POWERBROADCAST:
		{
			switch (wParam)
			{
			case PBT_APMSUSPEND:
				printf("++WM_POWERBROADCAST++ Stopping both preview & record stream.\n");
				g_fSleepState = true;
				g_pEngine->SleepState(g_fSleepState);
				g_pEngine->StopRecord();
				g_pEngine->StopPreview();
				g_pEngine->DestroyCaptureEngine();
				printf("++WM_POWERBROADCAST++ streams stopped, capture engine destroyed.\n");
				break;
			case PBT_APMRESUMEAUTOMATIC:
				printf("++WM_POWERBROADCAST++ Reinitializing capture engine.\n");
				g_fSleepState = false;
				g_pEngine->SleepState(g_fSleepState);
				g_pEngine->InitializeCaptureManager(hPreview, pSelectedDevice);
				break;
			case PBT_POWERSETTINGCHANGE:
			{
				// We should only be in here for GUID_MONITOR_POWER_ON.
				POWERBROADCAST_SETTING* pSettings = (POWERBROADCAST_SETTING*)lParam;

				// If this is a SOC system (AoAc is true), we want to check our current
				// sleep state and based on whether the monitor is being turned on/off,
				// we can turn off our media streams and/or re-initialize the capture
				// engine.
				if (pSettings != NULL && g_pwrCaps.AoAc && pSettings->PowerSetting == GUID_MONITOR_POWER_ON)
				{
					DWORD   dwData = *((DWORD*)pSettings->Data);
					if (dwData == 0 && !g_fSleepState)
					{
						// This is a AOAC machine, and we're about to turn off our monitor, let's stop recording/preview.
						printf("++WM_POWERBROADCAST++ Stopping both preview & record stream.\n");
						g_fSleepState = true;
						g_pEngine->SleepState(g_fSleepState);
						g_pEngine->StopRecord();
						g_pEngine->StopPreview();
						g_pEngine->DestroyCaptureEngine();
						printf("++WM_POWERBROADCAST++ streams stopped, capture engine destroyed.\n");
					}
					else if (dwData != 0 && g_fSleepState)
					{
						printf("++WM_POWERBROADCAST++ Reinitializing capture engine.\n");
						g_fSleepState = false;
						g_pEngine->SleepState(g_fSleepState);
						g_pEngine->InitializeCaptureManager(hPreview, pSelectedDevice);
					}
				}
			}
			break;
			case PBT_APMRESUMESUSPEND:
			default:
				// Don't care about this one, we always get the resume automatic so just
				// latch onto that one.
				printf("++WM_POWERBROADCAST++ (wParam=%u,lParam=%u)\n", wParam, lParam);
				break;
			}
		}
		return 1;
		}
		return DefWindowProc(hwnd, uMsg, wParam, lParam);
	}
};

HWND CreateMainWindow(HINSTANCE hInstance)
{
	// Register the window class.
	const wchar_t CLASS_NAME[] = L"Capture Engine Window Class";

	WNDCLASS wc = {};

	wc.lpfnWndProc = MainWindow::WindowProc;
	wc.hInstance = hInstance;
	wc.lpszClassName = CLASS_NAME;
	wc.lpszMenuName = MAKEINTRESOURCE(IDR_MENU1);

	RegisterClass(&wc);

	// Create the window.
	return CreateWindowEx(0, CLASS_NAME, L"Capture Application",
		WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
		NULL, NULL, hInstance, NULL);
};
