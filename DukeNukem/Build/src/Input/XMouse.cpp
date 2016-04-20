//--------------------------------------------------------------------------------------
// File: Mouse.cpp
//
// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved.
//
// http://go.microsoft.com/fwlink/?LinkId=248929
//--------------------------------------------------------------------------------------

#include "../Xbox/Xboxutilpch.h"
#include "XMouse.h"

#include "../Xbox/PlatformHelpers.h"

using namespace DirectX;
using Microsoft::WRL::ComPtr;


#if defined(WINAPI_FAMILY) && (WINAPI_FAMILY == WINAPI_FAMILY_APP)

//======================================================================================
// Windows Store or universal Windows app implementation
//======================================================================================

//
// For a Windows Store app or universal Windows app, add the following to your existing
// application methods:
//
// void App::SetWindow(CoreWindow^ window )
// {
//     m_mouse->SetWindow(window);
// }
// 
// void App::OnDpiChanged(DisplayInformation^ sender, Object^ args)
// {
//     m_mouse->SetDpi(sender->LogicalDpi);
// }
//

#include <Windows.Devices.Input.h>

class Mouse::Impl
{
public:
	Impl(Mouse* owner) :
		mOwner(owner),
		mDPI(96.f),
		mMode(MODE_ABSOLUTE)
	{
		mPointerPressedToken.value = 0;
		mPointerReleasedToken.value = 0;
		mPointerMovedToken.value = 0;
		mPointerWheelToken.value = 0;
		mPointerMouseMovedToken.value = 0;

		if (s_mouse)
		{
			throw std::exception("Mouse is a singleton");
		}

		s_mouse = this;

		memset(&mState, 0, sizeof(State));

		mScrollWheelValue.reset(CreateEventEx(nullptr, nullptr, CREATE_EVENT_MANUAL_RESET, EVENT_MODIFY_STATE | SYNCHRONIZE));
		mRelativeRead.reset(CreateEventEx(nullptr, nullptr, CREATE_EVENT_MANUAL_RESET, EVENT_MODIFY_STATE | SYNCHRONIZE));
		if (!mScrollWheelValue
			|| !mRelativeRead)
		{
			throw std::exception("CreateEventEx");
		}
	}

	~Impl()
	{
		s_mouse = nullptr;

		RemoveHandlers();
	}

	void GetState(State& state) const
	{
		memcpy(&state, &mState, sizeof(State));

		DWORD result = WaitForSingleObjectEx(mScrollWheelValue.get(), 0, FALSE);
		if (result == WAIT_FAILED)
			throw std::exception("WaitForSingleObjectEx");

		if (result == WAIT_OBJECT_0)
		{
			state.scrollWheelValue = 0;
		}

		if (mMode == MODE_RELATIVE)
		{
			result = WaitForSingleObjectEx(mRelativeRead.get(), 0, FALSE);

			if (result == WAIT_FAILED)
				throw std::exception("WaitForSingleObjectEx");

			if (result == WAIT_OBJECT_0)
			{
				state.x = 0;
				state.y = 0;
			}
			else
			{
				SetEvent(mRelativeRead.get());
			}
		}

		state.positionMode = mMode;
	}

	void ResetScrollWheelValue()
	{
		SetEvent(mScrollWheelValue.get());
	}

	void SetMode(Mode mode)
	{
		using namespace Microsoft::WRL;
		using namespace Microsoft::WRL::Wrappers;
		using namespace ABI::Windows::UI::Core;
		using namespace ABI::Windows::Foundation;

		if (mMode == mode)
			return;

		ComPtr<ICoreWindowStatic> statics;
		HRESULT hr = GetActivationFactory(HStringReference(RuntimeClass_Windows_UI_Core_CoreWindow).Get(), statics.GetAddressOf());
		ThrowIfFailed(hr);

		ComPtr<ICoreWindow> window;
		hr = statics->GetForCurrentThread(window.GetAddressOf());
		ThrowIfFailed(hr);

		if (mode == MODE_RELATIVE)
		{
			hr = window->get_PointerCursor(mCursor.ReleaseAndGetAddressOf());
			ThrowIfFailed(hr);

			hr = window->put_PointerCursor(nullptr);
			ThrowIfFailed(hr);

			SetEvent(mRelativeRead.get());

			mMode = MODE_RELATIVE;
		}
		else
		{
			if (!mCursor)
			{
				ComPtr<ICoreCursorFactory> factory;
				hr = GetActivationFactory(HStringReference(RuntimeClass_Windows_UI_Core_CoreCursor).Get(), factory.GetAddressOf());
				ThrowIfFailed(hr);

				hr = factory->CreateCursor(CoreCursorType_Arrow, 0, mCursor.GetAddressOf());
				ThrowIfFailed(hr);
			}

			hr = window->put_PointerCursor(mCursor.Get());
			ThrowIfFailed(hr);

			mCursor.Reset();

			mMode = MODE_ABSOLUTE;
		}
	}

	void SetWindow(ABI::Windows::UI::Core::ICoreWindow* window)
	{
		using namespace Microsoft::WRL;
		using namespace Microsoft::WRL::Wrappers;
		using namespace ABI::Windows::Foundation;
		using namespace ABI::Windows::Devices::Input;

		if (mWindow.Get() == window)
			return;

		RemoveHandlers();

		mWindow = window;

		if (!window)
		{
			mCursor.Reset();
			mMouse.Reset();
			return;
		}

		ComPtr<IMouseDeviceStatics> mouseStatics;
		HRESULT hr = GetActivationFactory(HStringReference(RuntimeClass_Windows_Devices_Input_MouseDevice).Get(), mouseStatics.GetAddressOf());
		ThrowIfFailed(hr);

		hr = mouseStatics->GetForCurrentView(mMouse.ReleaseAndGetAddressOf());
		ThrowIfFailed(hr);

		typedef __FITypedEventHandler_2_Windows__CDevices__CInput__CMouseDevice_Windows__CDevices__CInput__CMouseEventArgs MouseMovedHandler;
		hr = mMouse->add_MouseMoved(Callback<MouseMovedHandler>(MouseMovedEvent).Get(), &mPointerMouseMovedToken);
		ThrowIfFailed(hr);

		typedef __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CPointerEventArgs PointerHandler;
		auto cb = Callback<PointerHandler>(PointerEvent);

		hr = window->add_PointerPressed(cb.Get(), &mPointerPressedToken);
		ThrowIfFailed(hr);

		hr = window->add_PointerReleased(cb.Get(), &mPointerReleasedToken);
		ThrowIfFailed(hr);

		hr = window->add_PointerMoved(cb.Get(), &mPointerMovedToken);
		ThrowIfFailed(hr);

		hr = window->add_PointerWheelChanged(Callback<PointerHandler>(PointerWheel).Get(), &mPointerWheelToken);
		ThrowIfFailed(hr);
	}

	State           mState;
	float           mDPI;
	Mouse*          mOwner;

	static Mouse::Impl* s_mouse;

private:
	Mode            mMode;

	ComPtr<ABI::Windows::UI::Core::ICoreWindow> mWindow;
	ComPtr<ABI::Windows::Devices::Input::IMouseDevice> mMouse;
	ComPtr<ABI::Windows::UI::Core::ICoreCursor> mCursor;

	ScopedHandle    mScrollWheelValue;
	ScopedHandle    mRelativeRead;

	EventRegistrationToken mPointerPressedToken;
	EventRegistrationToken mPointerReleasedToken;
	EventRegistrationToken mPointerMovedToken;
	EventRegistrationToken mPointerWheelToken;
	EventRegistrationToken mPointerMouseMovedToken;

	void RemoveHandlers()
	{
		if (mWindow)
		{
			mWindow->remove_PointerPressed(mPointerPressedToken);
			mPointerPressedToken.value = 0;

			mWindow->remove_PointerReleased(mPointerReleasedToken);
			mPointerReleasedToken.value = 0;

			mWindow->remove_PointerMoved(mPointerMovedToken);
			mPointerMovedToken.value = 0;

			mWindow->remove_PointerWheelChanged(mPointerWheelToken);
			mPointerWheelToken.value = 0;
		}

		if (mMouse)
		{
			mMouse->remove_MouseMoved(mPointerMouseMovedToken);
			mPointerMouseMovedToken.value = 0;
		}
	}

	static HRESULT PointerEvent(IInspectable *, ABI::Windows::UI::Core::IPointerEventArgs*args)
	{
		using namespace ABI::Windows::Foundation;
		using namespace ABI::Windows::UI::Input;
		using namespace ABI::Windows::Devices::Input;

		if (!s_mouse)
			return S_OK;

		ComPtr<IPointerPoint> currentPoint;
		HRESULT hr = args->get_CurrentPoint(currentPoint.GetAddressOf());
		ThrowIfFailed(hr);

		ComPtr<IPointerDevice> pointerDevice;
		hr = currentPoint->get_PointerDevice(pointerDevice.GetAddressOf());
		ThrowIfFailed(hr);

		PointerDeviceType devType;
		hr = pointerDevice->get_PointerDeviceType(&devType);
		ThrowIfFailed(hr);

		if (devType == PointerDeviceType::PointerDeviceType_Mouse)
		{
			ComPtr<IPointerPointProperties> props;
			hr = currentPoint->get_Properties(props.GetAddressOf());
			ThrowIfFailed(hr);

			boolean value;
			hr = props->get_IsLeftButtonPressed(&value);
			ThrowIfFailed(hr);
			s_mouse->mState.leftButton = value != 0;

			hr = props->get_IsRightButtonPressed(&value);
			ThrowIfFailed(hr);
			s_mouse->mState.rightButton = value != 0;

			hr = props->get_IsMiddleButtonPressed(&value);
			ThrowIfFailed(hr);
			s_mouse->mState.middleButton = value != 0;

			hr = props->get_IsXButton1Pressed(&value);
			ThrowIfFailed(hr);
			s_mouse->mState.xButton1 = value != 0;

			hr = props->get_IsXButton2Pressed(&value);
			ThrowIfFailed(hr);
			s_mouse->mState.xButton2 = value != 0;
		}

		if (s_mouse->mMode == MODE_ABSOLUTE)
		{
			Point pos;
			hr = currentPoint->get_Position(&pos);
			ThrowIfFailed(hr);

			float dpi = s_mouse->mDPI;

			s_mouse->mState.x = static_cast<int>(pos.X * dpi / 96.f + 0.5f);
			s_mouse->mState.y = static_cast<int>(pos.Y * dpi / 96.f + 0.5f);
		}

		return S_OK;
	}

	static HRESULT PointerWheel(IInspectable *, ABI::Windows::UI::Core::IPointerEventArgs*args)
	{
		using namespace ABI::Windows::Foundation;
		using namespace ABI::Windows::UI::Input;
		using namespace ABI::Windows::Devices::Input;

		if (!s_mouse)
			return S_OK;

		ComPtr<IPointerPoint> currentPoint;
		HRESULT hr = args->get_CurrentPoint(currentPoint.GetAddressOf());
		ThrowIfFailed(hr);

		ComPtr<IPointerDevice> pointerDevice;
		hr = currentPoint->get_PointerDevice(pointerDevice.GetAddressOf());
		ThrowIfFailed(hr);

		PointerDeviceType devType;
		hr = pointerDevice->get_PointerDeviceType(&devType);
		ThrowIfFailed(hr);

		if (devType == PointerDeviceType::PointerDeviceType_Mouse)
		{
			ComPtr<IPointerPointProperties> props;
			hr = currentPoint->get_Properties(props.GetAddressOf());
			ThrowIfFailed(hr);

			INT32 value;
			hr = props->get_MouseWheelDelta(&value);
			ThrowIfFailed(hr);

			HANDLE evt = s_mouse->mScrollWheelValue.get();
			if (WaitForSingleObjectEx(evt, 0, FALSE) == WAIT_OBJECT_0)
			{
				s_mouse->mState.scrollWheelValue = 0;
				ResetEvent(evt);
			}

			s_mouse->mState.scrollWheelValue += value;

			if (s_mouse->mMode == MODE_ABSOLUTE)
			{
				Point pos;
				hr = currentPoint->get_Position(&pos);
				ThrowIfFailed(hr);

				float dpi = s_mouse->mDPI;

				s_mouse->mState.x = static_cast<int>(pos.X * dpi / 96.f + 0.5f);
				s_mouse->mState.y = static_cast<int>(pos.Y * dpi / 96.f + 0.5f);
			}
		}

		return S_OK;
	}

	static HRESULT MouseMovedEvent(IInspectable *, ABI::Windows::Devices::Input::IMouseEventArgs* args)
	{
		using namespace ABI::Windows::Devices::Input;

		if (!s_mouse)
			return S_OK;

		if (s_mouse->mMode == MODE_RELATIVE)
		{
			MouseDelta delta;
			HRESULT hr = args->get_MouseDelta(&delta);
			ThrowIfFailed(hr);

			s_mouse->mState.x = delta.X;
			s_mouse->mState.y = delta.Y;

			ResetEvent(s_mouse->mRelativeRead.get());
		}

		return S_OK;
	}
};


Mouse::Impl* Mouse::Impl::s_mouse = nullptr;


void Mouse::SetWindow(ABI::Windows::UI::Core::ICoreWindow* window)
{
	pImpl->SetWindow(window);
}


void Mouse::SetDpi(float dpi)
{
	auto pImpl = Impl::s_mouse;

	if (!pImpl)
		return;

	pImpl->mDPI = dpi;
}


#elif defined(_XBOX_ONE) || ( defined(WINAPI_FAMILY) && (WINAPI_FAMILY == WINAPI_FAMILY_PHONE_APP) )

//======================================================================================
// Null device for Windows Phone and Xbox One
//======================================================================================

class Mouse::Impl
{
public:
	Impl(Mouse* owner) :
		mOwner(owner)
	{
		if (s_mouse)
		{
			throw std::exception("Mouse is a singleton");
		}

		s_mouse = this;
	}

	~Impl()
	{
		s_mouse = nullptr;
	}

	void GetState(State& state) const
	{
		memset(&state, 0, sizeof(State));
	}

	void ResetScrollWheelValue()
	{
	}

	void SetMode(Mode mode)
	{
		UNREFERENCED_PARAMETER(mode);
	}

	Mouse*  mOwner;

	static Mouse::Impl* s_mouse;
};

Mouse::Impl* Mouse::Impl::s_mouse = nullptr;

#else

//======================================================================================
// Win32 desktop implementation
//======================================================================================

//
// For a Win32 desktop application, in your window setup be sure to call this method:
//
// m_mouse->SetWindow(hwnd);
//
// And call this static function from your Window Message Procedure
//
// LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
// {
//     switch (message)
//     {
//     case WM_ACTIVATEAPP:
//     case WM_INPUT:
//     case WM_MOUSEMOVE:
//     case WM_LBUTTONDOWN:
//     case WM_LBUTTONUP:
//     case WM_RBUTTONDOWN:
//     case WM_RBUTTONUP:
//     case WM_MBUTTONDOWN:
//     case WM_MBUTTONUP:
//     case WM_MOUSEWHEEL:
//     case WM_XBUTTONDOWN:
//     case WM_XBUTTONUP:
//     case WM_MOUSEHOVER:
//         Mouse::ProcessMessage(message, wParam, lParam);
//         break;
//
//     }
// }
//

class Mouse::Impl
{
public:
	Impl(Mouse* owner) :
		mOwner(owner),
		mMode(MODE_ABSOLUTE),
		mLastX(0),
		mLastY(0),
		mInFocus(true)
	{
		if (s_mouse)
		{
			throw std::exception("Mouse is a singleton");
		}

		s_mouse = this;

		memset(&mState, 0, sizeof(State));

		mScrollWheelValue.reset(CreateEventEx(nullptr, nullptr, CREATE_EVENT_MANUAL_RESET, EVENT_MODIFY_STATE | SYNCHRONIZE));
		mRelativeRead.reset(CreateEventEx(nullptr, nullptr, CREATE_EVENT_MANUAL_RESET, EVENT_MODIFY_STATE | SYNCHRONIZE));
		mAbsoluteMode.reset(CreateEventEx(nullptr, nullptr, 0, EVENT_MODIFY_STATE | SYNCHRONIZE));
		mRelativeMode.reset(CreateEventEx(nullptr, nullptr, 0, EVENT_MODIFY_STATE | SYNCHRONIZE));
		if (!mScrollWheelValue
			|| !mRelativeRead
			|| !mAbsoluteMode
			|| !mRelativeMode)
		{
			throw std::exception("CreateEventEx");
		}
	}

	~Impl()
	{
		s_mouse = nullptr;
	}

	void GetState(State& state) const
	{
		memcpy(&state, &mState, sizeof(State));
		state.positionMode = mMode;

		DWORD result = WaitForSingleObjectEx(mScrollWheelValue.get(), 0, FALSE);
		if (result == WAIT_FAILED)
			throw std::exception("WaitForSingleObjectEx");

		if (result == WAIT_OBJECT_0)
		{
			state.scrollWheelValue = 0;
		}

		if (state.positionMode == MODE_RELATIVE)
		{
			result = WaitForSingleObjectEx(mRelativeRead.get(), 0, FALSE);

			if (result == WAIT_FAILED)
				throw std::exception("WaitForSingleObjectEx");

			if (result == WAIT_OBJECT_0)
			{
				state.x = 0;
				state.y = 0;
			}
			else
			{
				SetEvent(mRelativeRead.get());
			}
		}
	}

	void ResetScrollWheelValue()
	{
		SetEvent(mScrollWheelValue.get());
	}

	void SetMode(Mode mode)
	{
		if (mMode == mode)
			return;

		SetEvent((mode == MODE_ABSOLUTE) ? mAbsoluteMode.get() : mRelativeMode.get());

		TRACKMOUSEEVENT tme;
		tme.cbSize = sizeof(tme);
		tme.dwFlags = TME_HOVER;
		tme.hwndTrack = mWindow;
		tme.dwHoverTime = 1;
		if (!TrackMouseEvent(&tme))
		{
			throw std::exception("TrackMouseEvent");
		}
	}

	void SetWindow(HWND window)
	{
		if (mWindow == window)
			return;

		RAWINPUTDEVICE Rid;
		Rid.usUsagePage = 0x1 /* HID_USAGE_PAGE_GENERIC */;
		Rid.usUsage = 0x2 /* HID_USAGE_GENERIC_MOUSE */;
		Rid.dwFlags = RIDEV_INPUTSINK;
		Rid.hwndTarget = window;
		if (!RegisterRawInputDevices(&Rid, 1, sizeof(RAWINPUTDEVICE)))
		{
			throw std::exception("RegisterRawInputDevices");
		}

		mWindow = window;
	}

	State           mState;

	Mouse*          mOwner;

	static Mouse::Impl* s_mouse;

private:
	HWND            mWindow;
	Mode            mMode;

	ScopedHandle    mScrollWheelValue;
	ScopedHandle    mRelativeRead;
	ScopedHandle    mAbsoluteMode;
	ScopedHandle    mRelativeMode;

	int             mLastX;
	int             mLastY;

	bool            mInFocus;

	friend void Mouse::ProcessMessage(UINT message, WPARAM wParam, LPARAM lParam);

	void ClipToWindow()
	{
		RECT rect;
		GetClientRect(mWindow, &rect);

		POINT ul;
		ul.x = rect.left;
		ul.y = rect.top;

		POINT lr;
		lr.x = rect.right;
		lr.y = rect.bottom;

		MapWindowPoints(mWindow, nullptr, &ul, 1);
		MapWindowPoints(mWindow, nullptr, &lr, 1);

		rect.left = ul.x;
		rect.top = ul.y;

		rect.right = lr.x;
		rect.bottom = lr.y;

		ClipCursor(&rect);
	}
};


Mouse::Impl* Mouse::Impl::s_mouse = nullptr;


void Mouse::SetWindow(HWND window)
{
	pImpl->SetWindow(window);
}


void Mouse::ProcessMessage(UINT message, WPARAM wParam, LPARAM lParam)
{
	auto pImpl = Impl::s_mouse;

	if (!pImpl)
		return;

	HANDLE evts[3];
	evts[0] = pImpl->mScrollWheelValue.get();
	evts[1] = pImpl->mAbsoluteMode.get();
	evts[2] = pImpl->mRelativeMode.get();
	switch (WaitForMultipleObjectsEx(_countof(evts), evts, FALSE, 0, FALSE))
	{
	case WAIT_OBJECT_0:
		pImpl->mState.scrollWheelValue = 0;
		ResetEvent(evts[0]);
		break;

	case (WAIT_OBJECT_0 + 1) :
	{
		pImpl->mMode = MODE_ABSOLUTE;
		ClipCursor(nullptr);

		POINT point;
		point.x = pImpl->mLastX;
		point.y = pImpl->mLastY;
		if (MapWindowPoints(pImpl->mWindow, nullptr, &point, 1))
		{
			SetCursorPos(point.x, point.y);
		}
		ShowCursor(TRUE);
		pImpl->mState.x = pImpl->mLastX;
		pImpl->mState.y = pImpl->mLastY;
	}
							 break;

	case (WAIT_OBJECT_0 + 2) :
	{
		ResetEvent(pImpl->mRelativeRead.get());

		pImpl->mMode = MODE_RELATIVE;
		pImpl->mState.x = pImpl->mState.y = 0;

		ShowCursor(FALSE);

		pImpl->ClipToWindow();
	}
							 break;

	case WAIT_FAILED:
		throw std::exception("WaitForMultipleObjectsEx");
	}

	switch (message)
	{
	case WM_ACTIVATEAPP:
		if (wParam)
		{
			pImpl->mInFocus = true;

			if (pImpl->mMode == MODE_RELATIVE)
			{
				pImpl->mState.x = pImpl->mState.y = 0;

				ShowCursor(FALSE);

				pImpl->ClipToWindow();
			}
		}
		else
		{
			int scrollWheel = pImpl->mState.scrollWheelValue;
			memset(&pImpl->mState, 0, sizeof(State));
			pImpl->mState.scrollWheelValue = scrollWheel;

			pImpl->mInFocus = false;
		}
		return;

	case WM_INPUT:
		if (pImpl->mInFocus && pImpl->mMode == MODE_RELATIVE)
		{
			RAWINPUT raw;
			UINT rawSize = sizeof(raw);

			UINT resultData = GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, &raw, &rawSize, sizeof(RAWINPUTHEADER));
			if (resultData == UINT(-1))
			{
				throw std::exception("GetRawInputData");
			}

			if (raw.header.dwType == RIM_TYPEMOUSE)
			{
				if (!(raw.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE))
				{
					pImpl->mState.x = raw.data.mouse.lLastX;
					pImpl->mState.y = raw.data.mouse.lLastY;

					ResetEvent(pImpl->mRelativeRead.get());
				}

				// Note that with Remote Desktop input comes through as MOUSE_MOVE_ABSOLUTE | MOUSE_VIRTUAL_DESKTOP,
				// so this imlementation doesn't suport relative mode via remote desktop.
			}
		}
		return;

	case WM_MOUSEMOVE:
		break;

	case WM_LBUTTONDOWN:
		pImpl->mState.leftButton = true;
		break;

	case WM_LBUTTONUP:
		pImpl->mState.leftButton = false;
		break;

	case WM_RBUTTONDOWN:
		pImpl->mState.rightButton = true;
		break;

	case WM_RBUTTONUP:
		pImpl->mState.rightButton = false;
		break;

	case WM_MBUTTONDOWN:
		pImpl->mState.middleButton = true;
		break;

	case WM_MBUTTONUP:
		pImpl->mState.middleButton = false;
		break;

	case WM_MOUSEWHEEL:
		pImpl->mState.scrollWheelValue += GET_WHEEL_DELTA_WPARAM(wParam);
		return;

	case WM_XBUTTONDOWN:
		switch (GET_XBUTTON_WPARAM(wParam))
		{
		case XBUTTON1:
			pImpl->mState.xButton1 = true;
			break;

		case XBUTTON2:
			pImpl->mState.xButton2 = true;
			break;
		}
		break;

	case WM_XBUTTONUP:
		switch (GET_XBUTTON_WPARAM(wParam))
		{
		case XBUTTON1:
			pImpl->mState.xButton1 = false;
			break;

		case XBUTTON2:
			pImpl->mState.xButton2 = false;
			break;
		}
		break;

	case WM_MOUSEHOVER:
		break;

	default:
		// Not a mouse message, so exit
		return;
	}

	if (pImpl->mMode == MODE_ABSOLUTE)
	{
		// All mouse messages provide a new pointer position
		int xPos = static_cast<short>(LOWORD(lParam)); // GET_X_LPARAM(lParam);
		int yPos = static_cast<short>(HIWORD(lParam)); // GET_Y_LPARAM(lParam);

		pImpl->mState.x = pImpl->mLastX = xPos;
		pImpl->mState.y = pImpl->mLastY = yPos;
	}
}

#endif

#pragma warning( disable : 4355 )

// Public constructor.
Mouse::Mouse()
	: pImpl(new Impl(this))
{
}


// Move constructor.
Mouse::Mouse(Mouse&& moveFrom)
	: pImpl(std::move(moveFrom.pImpl))
{
	pImpl->mOwner = this;
}


// Move assignment.
Mouse& Mouse::operator= (Mouse&& moveFrom)
{
	pImpl = std::move(moveFrom.pImpl);
	pImpl->mOwner = this;
	return *this;
}


// Public destructor.
Mouse::~Mouse()
{
}


Mouse::State Mouse::GetState() const
{
	State state;
	pImpl->GetState(state);
	return state;
}


void Mouse::ResetScrollWheelValue()
{
	pImpl->ResetScrollWheelValue();
}


void Mouse::SetMode(Mode mode)
{
	pImpl->SetMode(mode);
}


Mouse& Mouse::Get()
{
	if (!Impl::s_mouse || !Impl::s_mouse->mOwner)
		throw std::exception("Mouse is a singleton");

	return *Impl::s_mouse->mOwner;
}



//======================================================================================
// ButtonStateTracker
//======================================================================================

#define UPDATE_BUTTON_STATE(field) field = static_cast<ButtonState>( ( !!state.field ) | ( ( !!state.field ^ !!lastState.field ) << 1 ) );

void Mouse::ButtonStateTracker::Update(const Mouse::State& state)
{
	UPDATE_BUTTON_STATE(leftButton);

	assert((!state.leftButton && !lastState.leftButton) == (leftButton == UP));
	assert((state.leftButton && lastState.leftButton) == (leftButton == HELD));
	assert((!state.leftButton && lastState.leftButton) == (leftButton == RELEASED));
	assert((state.leftButton && !lastState.leftButton) == (leftButton == PRESSED));

	UPDATE_BUTTON_STATE(middleButton);
	UPDATE_BUTTON_STATE(rightButton);
	UPDATE_BUTTON_STATE(xButton1);
	UPDATE_BUTTON_STATE(xButton2);

	lastState = state;
}

#undef UPDATE_BUTTON_STATE


void Mouse::ButtonStateTracker::Reset()
{
	memset(this, 0, sizeof(ButtonStateTracker));
}