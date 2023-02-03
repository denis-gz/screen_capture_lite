#include "GDIFrameProcessor.h"
#include <Dwmapi.h>

namespace SL {
namespace Screen_Capture {

    DUPL_RETURN GDIFrameProcessor::Init(std::shared_ptr<Thread_Data> data, const Monitor &monitor)
    {
        SelectedMonitor = monitor;
        auto Ret = DUPL_RETURN_SUCCESS;

        MonitorDC.DC = CreateDCA(Name(SelectedMonitor), NULL, NULL, NULL);
        if (!MonitorDC.DC)
            data->LoggingCallback_("CreateDCA", ::GetLastError());
        CaptureDC.DC = CreateCompatibleDC(MonitorDC.DC);
        if (!CaptureDC.DC)
            data->LoggingCallback_("CreateCompatibleDC", ::GetLastError());
        CaptureBMP.Bitmap = CreateCompatibleBitmap(MonitorDC.DC, Width(SelectedMonitor), Height(SelectedMonitor));
        if (!CaptureBMP.Bitmap)
            data->LoggingCallback_("CreateCompatibleBitmap", ::GetLastError());
        NewImageBuffer = std::make_unique<unsigned char[]>(ImageBufferSize);
        if (!MonitorDC.DC || !CaptureDC.DC || !CaptureBMP.Bitmap) {
            data->LoggingCallback_("GDIFrameProcessor::Init", E_FAIL);
            return DUPL_RETURN::DUPL_RETURN_ERROR_EXPECTED;
        }

        Data = data;
        return Ret;
    }
    DUPL_RETURN GDIFrameProcessor::Init(std::shared_ptr<Thread_Data> data, const Window &selectedwindow)
    {
        // this is needed to fix AERO BitBlt capturing issues
        ANIMATIONINFO str;
        str.cbSize = sizeof(str);
        str.iMinAnimate = 0;
        SystemParametersInfo(SPI_SETANIMATION, sizeof(str), (void *)&str, SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
        SelectedWindow = reinterpret_cast<HWND>(selectedwindow.Handle);
        auto Ret = DUPL_RETURN_SUCCESS;
        NewImageBuffer = std::make_unique<unsigned char[]>(ImageBufferSize);
        MonitorDC.DC = GetWindowDC(SelectedWindow);
        CaptureDC.DC = CreateCompatibleDC(MonitorDC.DC);

        CaptureBMP.Bitmap = CreateCompatibleBitmap(MonitorDC.DC, selectedwindow.Size.x, selectedwindow.Size.y);

        if (!MonitorDC.DC || !CaptureDC.DC || !CaptureBMP.Bitmap) {
            return DUPL_RETURN::DUPL_RETURN_ERROR_EXPECTED;
        }

        Data = data;
        return Ret;
    }
    DUPL_RETURN GDIFrameProcessor::ProcessFrame(const Monitor &currentmonitorinfo, LoggingCallbackT& loggingCallback)
    { 
        auto Ret = DUPL_RETURN_SUCCESS;

        ImageRect ret;
        ret.left = ret.top = 0;
        ret.bottom = Height(SelectedMonitor);
        ret.right = Width(SelectedMonitor);

        // Selecting an object into the specified DC
        auto originalBmp = SelectObject(CaptureDC.DC, CaptureBMP.Bitmap);

        if (!BitBlt(CaptureDC.DC, 0, 0, ret.right, ret.bottom, MonitorDC.DC, 0, 0, SRCCOPY | CAPTUREBLT)) {
            loggingCallback("BitBlt", ::GetLastError());
            // if the screen cannot be captured, return
            SelectObject(CaptureDC.DC, originalBmp);
            return DUPL_RETURN::DUPL_RETURN_ERROR_EXPECTED; // likely a permission issue
        }
        else {

            BITMAPINFOHEADER bi;
            memset(&bi, 0, sizeof(bi));

            bi.biSize = sizeof(BITMAPINFOHEADER);

            bi.biWidth = ret.right;
            bi.biHeight = -ret.bottom;
            bi.biPlanes = 1;
            bi.biBitCount = sizeof(ImageBGRA) * 8; // always 32 bits damnit!!!
            bi.biCompression = BI_RGB;
            bi.biSizeImage = ((ret.right * bi.biBitCount + 31) / (sizeof(ImageBGRA) * 8)) * sizeof(ImageBGRA)  * ret.bottom;
            if (!GetDIBits(MonitorDC.DC, CaptureBMP.Bitmap, 0, (UINT)ret.bottom, NewImageBuffer.get(), (BITMAPINFO *)&bi, DIB_RGB_COLORS))
                loggingCallback("GetDIBits", ::GetLastError());
            SelectObject(CaptureDC.DC, originalBmp);
            ProcessCapture(Data->ScreenCaptureData, *this, currentmonitorinfo, NewImageBuffer.get(), Width(SelectedMonitor)* sizeof(ImageBGRA));
        }

        return Ret;
    }
      
    DUPL_RETURN GDIFrameProcessor::ProcessFrame(Window &selectedwindow, LoggingCallbackT& loggingCallback)
    {
        auto Ret = DUPL_RETURN_SUCCESS;
        auto windowrect = SL::Screen_Capture::GetWindowRect(SelectedWindow);
        ImageRect ret;
        memset(&ret, 0, sizeof(ret));
        ret.bottom = windowrect.ClientRect.bottom;
        ret.left = windowrect.ClientRect.left;
        ret.right = windowrect.ClientRect.right;
        ret.top = windowrect.ClientRect.top;
        selectedwindow.Position.x = windowrect.ClientRect.left;
        selectedwindow.Position.y = windowrect.ClientRect.top;

        if (!IsWindow(SelectedWindow) || selectedwindow.Size.x != Width(ret) || selectedwindow.Size.y != Height(ret)) {
            loggingCallback("ProcessFrame: window size changed", 0);
            return DUPL_RETURN::DUPL_RETURN_ERROR_EXPECTED; // window size changed. This will rebuild everything
        }

        // Selecting an object into the specified DC
        auto originalBmp = SelectObject(CaptureDC.DC, CaptureBMP.Bitmap);
        auto left = -windowrect.ClientBorder.left;
        auto top = -windowrect.ClientBorder.top;

        BOOL result = PrintWindow((HWND)selectedwindow.Handle, CaptureDC.DC, PW_RENDERFULLCONTENT );

        if ( !result ) {
            loggingCallback("PrintWindow", ::GetLastError());
            result = BitBlt(CaptureDC.DC, left, top, ret.right, ret.bottom, MonitorDC.DC, 0, 0, SRCCOPY | CAPTUREBLT);
        }

        if ( !result ) {
            // if the screen cannot be captured, return
            loggingCallback("BitBlt", ::GetLastError());
            SelectObject(CaptureDC.DC, originalBmp);
            return DUPL_RETURN::DUPL_RETURN_ERROR_EXPECTED; // likely a permission issue
        }

        //std::vector<HWND> childrenToComposite = CollectWindowsToComposite((HWND)selectedwindow.Handle);
        //
        //// list is ordered topmost to bottommost, so we visit them in reverse order to let painter's algorithm work
        //for ( auto child = childrenToComposite.rbegin(); child != childrenToComposite.rend(); child++ ) {
        //    auto childRect = SL::Screen_Capture::GetWindowRect( *child );

        //    HDC srcDC = GetWindowDC(*child);

        //    // if this fails we just won't composite this window, so continue with the others to get what we can
        //    BOOL childBlitSuccess = BitBlt(CaptureDC.DC, childRect.ClientRect.left - windowrect.ClientRect.left, childRect.ClientRect.top - windowrect.ClientRect.top,
        //           childRect.ClientRect.right - childRect.ClientRect.left, childRect.ClientRect.bottom - childRect.ClientRect.top, 
        //           srcDC, 0, 0,
        //           SRCCOPY | CAPTUREBLT);
        //    if ( !childBlitSuccess ) {
        //        DWORD err = GetLastError();
        //    }

        //    ReleaseDC(*child, srcDC);
        //}

        BITMAPINFOHEADER bi;
        memset(&bi, 0, sizeof(bi)); 
        bi.biSize = sizeof(BITMAPINFOHEADER); 
        bi.biWidth = Width(ret);
        bi.biHeight = -Height(ret);
        bi.biPlanes = 1;
        bi.biBitCount = sizeof(ImageBGRA) * 8; // always 32 bits damnit!!!
        bi.biCompression = BI_RGB;
        bi.biSizeImage = ((Width(ret) * bi.biBitCount + 31) / (sizeof(ImageBGRA) * 8)) * sizeof(ImageBGRA)  * Height(ret);
        if (!GetDIBits(MonitorDC.DC, CaptureBMP.Bitmap, 0, (UINT)Height(ret), NewImageBuffer.get(), (BITMAPINFO *)&bi, DIB_RGB_COLORS))
            loggingCallback("GetDIBits", ::GetLastError());
        SelectObject(CaptureDC.DC, originalBmp);
        ProcessCapture(Data->WindowCaptureData, *this, selectedwindow, NewImageBuffer.get(), Width(selectedwindow)* sizeof(ImageBGRA));

        return Ret;
    }
} // namespace Screen_Capture
} // namespace SL
