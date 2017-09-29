// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved.

#include "Capture.h"

extern CaptureManager *g_pEngine;
extern int g_device_type;

// Implements the window procedure for the video preview window.

int paintCount = 0;
namespace PreviewWnd
{
    HBRUSH hBackgroundBrush = 0;
	HBRUSH hTargetBrush = 0;
	RECT   TargeRect;
    BOOL OnCreate(HWND /*hwnd*/, LPCREATESTRUCT /*lpCreateStruct*/)
    {
        hBackgroundBrush = CreateSolidBrush(RGB(0,0,0));
		hTargetBrush = CreateSolidBrush(RGB(255, 0, 0));
        return (hBackgroundBrush != NULL);
    }

    void OnDestroy(HWND hwnd)
    {
        DeleteObject(hBackgroundBrush);
    }

    void OnPaint(HWND hwnd)
    {
		
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

		if (paintCount == 0) { //initial background
			FillRect(hdc, &ps.rcPaint, hBackgroundBrush);
			printf("On paint %d\n", paintCount++);
		}
		else
		{
			LONG center_x = (ps.rcPaint.right + ps.rcPaint.left) / 2;
			LONG center_y = (ps.rcPaint.bottom + ps.rcPaint.top) / 2 ;
			LONG range_x = 40;
			LONG range_y = 80;

			TargeRect.left = center_x - range_x;
			TargeRect.top = center_y - range_y;
			TargeRect.right = center_x + range_x;
			TargeRect.bottom = center_y + range_y;
		}

		g_pEngine->UpdateVideo(hdc);
		if (g_device_type == 1)
			FrameRect(hdc, &TargeRect, hTargetBrush);
        EndPaint(hwnd, &ps);
    }

    void OnSize(HWND hwnd, UINT state, int /*cx*/, int /*cy*/)
    {
        if (state == SIZE_RESTORED)
        {
            InvalidateRect(hwnd, NULL, FALSE);
        }
    }

    LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
    {
        switch (uMsg)
        {
        HANDLE_MSG(hwnd, WM_CREATE,  OnCreate);
        HANDLE_MSG(hwnd, WM_PAINT,   OnPaint);
        HANDLE_MSG(hwnd, WM_SIZE,    OnSize);
        HANDLE_MSG(hwnd, WM_DESTROY, OnDestroy);

        case WM_ERASEBKGND:
            return 1;
        }
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
};


HWND CreatePreviewWindow(HINSTANCE hInstance, HWND hParent)
{
    // Register the window class.
    const wchar_t CLASS_NAME[]  = L"Capture Engine Preview Window Class";
    
    WNDCLASS wc = { };

    wc.lpfnWndProc   = PreviewWnd::WindowProc;
    wc.hInstance     = hInstance;
    wc.lpszClassName = CLASS_NAME;

    RegisterClass(&wc);

    RECT rc;
    GetClientRect(hParent, &rc);

    // Create the window.
    return CreateWindowEx(0, CLASS_NAME, NULL, 
        WS_CHILD | WS_VISIBLE, 0, 0, 0, 0,
        hParent, NULL, hInstance, NULL);
};
