#include "DXFrameProcessor.h"
#include "GDIFrameProcessor.h"
#include "GDIMouseProcessor.h"
#include "ScreenCapture.h"
#include "internal/ThreadManager.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <iostream>
#include <memory>
#include <string>

namespace SL {
namespace Screen_Capture {

    template <class T> void ProcessExit(DUPL_RETURN Ret, T *TData)
    {
        if (Ret != DUPL_RETURN_SUCCESS) {
            if (Ret == DUPL_RETURN_ERROR_EXPECTED) {
                // The system is in a transition state so request the duplication be restarted
                TData->CommonData_.ExpectedErrorEvent = true;
            }
            else {
                // Unexpected error so exit the application
                TData->CommonData_.UnexpectedErrorEvent = true;
            }
        }
    }

    void RequestScreenCapture() {}
    bool CanRequestScreenCapture() { return false; }

    bool IsScreenCaptureEnabled()
    {
        HDESK CurrentDesktop = nullptr;
        CurrentDesktop = OpenInputDesktop(0, FALSE, GENERIC_ALL);
        if (!CurrentDesktop) {
            // We do not have access to the desktop so request a retry
            return false;
        }

        // Attach desktop to this thread
        bool DesktopAttached = SetThreadDesktop(CurrentDesktop) != 0;
        CloseDesktop(CurrentDesktop);
        CurrentDesktop = nullptr;
        if (!DesktopAttached) {
            // We do not have access to the desktop so request a retry
            return false;
        }
        return true;
    }
    template <class T> bool SwitchToInputDesktop(const std::shared_ptr<T> data)
    {
        HDESK CurrentDesktop = nullptr;
        CurrentDesktop = OpenInputDesktop(0, FALSE, GENERIC_ALL);
        if (!CurrentDesktop) {
            data->LoggingCallback_("OpenInputDesktop", ::GetLastError());
            // We do not have access to the desktop so request a retry
            data->CommonData_.ExpectedErrorEvent = true;
            ProcessExit(DUPL_RETURN::DUPL_RETURN_ERROR_EXPECTED, data.get());
            return false;
        }

        // Attach desktop to this thread
        bool DesktopAttached = SetThreadDesktop(CurrentDesktop) != 0;
        DWORD DesktopAttachedError = ::GetLastError();
        CloseDesktop(CurrentDesktop);
        CurrentDesktop = nullptr;
        if (!DesktopAttached) {
            data->LoggingCallback_("SetThreadDesktop", DesktopAttachedError);
            // We do not have access to the desktop so request a retry
            data->CommonData_.ExpectedErrorEvent = true;
            ProcessExit(DUPL_RETURN::DUPL_RETURN_ERROR_EXPECTED, data.get());
            return false;
        }
        return true;
    }
    void RunCaptureMouse(std::shared_ptr<Thread_Data> data)
    {
        if (SwitchToInputDesktop(data))
            TryCaptureMouse<GDIMouseProcessor>(data);
        data->LoggingCallback_("RunCaptureMouse: exiting thread", 0);
    }
    void RunCaptureMonitor(std::shared_ptr<Thread_Data> data, Monitor monitor)
    {
        data->LoggingCallback_("RunCaptureMonitor --> " + std::to_string(monitor.Index), 0);
        // need to switch to the input desktop for capturing...
        if (!SwitchToInputDesktop(data)) {
            data->LoggingCallback_("RunCaptureMonitor: exiting thread", 0);
            return;
        }
#if defined _DEBUG || !defined NDEBUG
        std::cout << "Starting to Capture on Monitor " << Name(monitor) << std::endl;
        std::cout << "Trying DirectX Desktop Duplication " << std::endl;
#endif
        if (data->CaptureMethod_ == CaptureMethod::Gdi || !TryCaptureMonitor<DXFrameProcessor>(data, monitor)) { // if DX is not supported, fallback to GDI capture
#if defined _DEBUG || !defined NDEBUG
            std::cout << "DirectX Desktop Duplication not supported or disabled, falling back to GDI Capturing . . ." << std::endl;
#endif
            TryCaptureMonitor<GDIFrameProcessor>(data, monitor);
        }
        data->LoggingCallback_("RunCaptureMonitor: exiting thread", 0);
    }

    void RunCaptureWindow(std::shared_ptr<Thread_Data> data, Window wnd)
    {
        // need to switch to the input desktop for capturing...
        if (SwitchToInputDesktop(data))
            TryCaptureWindow<GDIFrameProcessor>(data, wnd);
        data->LoggingCallback_("RunCaptureWindow: exiting thread", 0);
    }
} // namespace Screen_Capture
} // namespace SL
