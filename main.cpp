// 包含Windows API和Media Foundation相关的头文件
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <assert.h>
#include <strsafe.h>
#include <shlwapi.h>
#include <Dbt.h>
#include <ks.h>
#include <ksmedia.h>
#include <iostream>

// 包含自定义的头文件，可能是用于捕获功能的实现
#include "capture.h"

// 定义一个模板函数用于安全释放COM对象;当COM对象不再需要时，这个函数会释放对象并将其指针设置为nullptr
template <class T> void SafeRelease(T** ppT)
{
    if (*ppT) // 检查指针是否非空
    {
        (*ppT)->Release(); // 调用COM对象的Release方法来减少引用计数
        *ppT = nullptr; // 将指针设置为nullptr，防止悬挂指针
    }
}

const UINT32 TARGET_BIT_RATE = 1920 * 1080 * 3;// 定义目标比特率，用于视频编码
DeviceList  g_devices;// DeviceList可能是一个用于存储设备列表的类
CCapture* g_pCapture = nullptr;// CCapture可能是一个用于视频捕获的类
HDEVNOTIFY  g_hdevnotify = nullptr;// HDEVNOTIFY用于注册设备通知

// 应用程序的入口点
int main()
{
    // 启用堆损坏时的终止，这是一个安全特性，用于检测堆损坏
    (void)HeapSetInformation(nullptr, HeapEnableTerminationOnCorruption, nullptr, 0);

    HRESULT hr = S_OK; // HRESULT用于表示函数调用的成功或失败

    // 初始化COM库，COINIT_APARTMENTTHREADED表示线程单元模型，COINIT_DISABLE_OLE1DDE禁用OLE1 DDE
    hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    if (FAILED(hr)) // 如果初始化失败
    {
        std::cerr << "Failed to initialize COM library." << std::endl; // 输出错误信息
        return -1; // 返回错误代码
    }

    // 初始化Media Foundation
    if (SUCCEEDED(hr)) // 如果COM初始化成功
    {
        hr = MFStartup(MF_VERSION); // 初始化Media Foundation库
        if (FAILED(hr)) // 如果初始化失败
        {
            std::cerr << "Failed to initialize Media Foundation." << std::endl; // 输出错误信息
            CoUninitialize(); // 反初始化COM库
            return -1; // 返回错误代码
        }
    }

    // 注册设备通知，用于捕获设备连接或断开事件
    DEV_BROADCAST_DEVICEINTERFACE di = { 0 }; // 初始化设备广播结构
    di.dbcc_size = sizeof(di); // 设置结构大小
    di.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE; // 设备接口类型
    di.dbcc_classguid = KSCATEGORY_CAPTURE; // 使用的设备类别GUID

    g_hdevnotify = RegisterDeviceNotification(nullptr, &di, DEVICE_NOTIFY_WINDOW_HANDLE); // 注册设备通知

    if (g_hdevnotify == nullptr) // 如果注册失败
    {
        hr = HRESULT_FROM_WIN32(GetLastError()); // 获取错误代码
        std::cerr << "Failed to register device notification." << std::endl; // 输出错误信息
        MFShutdown(); // 关闭Media Foundation
        CoUninitialize(); // 反初始化COM库
        return -1; // 返回错误代码
    }


    // 枚举视频捕获设备
    if (SUCCEEDED(hr)) // 如果设备通知注册成功
    {
        hr = g_devices.EnumerateDevices(); // 枚举设备
        if (FAILED(hr)) // 如果枚举失败
        {
            std::cerr << "Failed to enumerate devices." << std::endl; // 输出错误信息
            UnregisterDeviceNotification(g_hdevnotify); // 注销设备通知
            MFShutdown(); // 关闭Media Foundation
            CoUninitialize(); // 反初始化COM库
            return -1; // 返回错误代码
        }
    }
    
    // 没有枚举到设备 直接返回错误码
    if (FAILED(hr) && g_devices.Count() < 1) {
        std::cerr << "No capture devices found." << std::endl; // 输出没有找到捕获设备信息
        return -1;// 返回错误代码
    }
    // 选择第一个设备并开始捕获
    IMFActivate* pActivate = nullptr; // 定义一个激活对象指针
    hr = g_devices.GetDevice(0, &pActivate); // 获取第一个设备
    if (FAILED(hr)) {
        std::cerr << "Failed to get device." << std::endl; // 输出获取设备失败信息
        pActivate = nullptr
        return -1;
    }

    hr = CCapture::CreateInstance(nullptr, &g_pCapture); // 创建捕获实例
    if (FAILED(hr)) {
        std::cerr << "Failed to create capture instance." << std::endl; // 输出创建捕获实例失败信息
        SafeRelease(&pActivate); // 释放激活对象
        return -1;
    }

    // ------------------------------------------------------------------------------------//
    WCHAR pszFile[MAX_PATH] = L"capture.mp4"; // 定义输出文件路径
    EncodingParameters params; // 定义编码参数
    params.subtype = MFVideoFormat_H264; // 视频编码格式
    params.bitrate = TARGET_BIT_RATE; // 目标比特率

    hr = g_pCapture->StartCapture(pActivate, pszFile, params); // 开始捕获
    if (FAILED(hr)) // 如果开始捕获失败
    {
        std::cerr << "Failed to start capture." << std::endl; // 输出错误信息
        SafeRelease(&pActivate); // 释放激活对象
        SafeRelease(&g_pCapture); // 释放捕获实例
        UnregisterDeviceNotification(g_hdevnotify); // 注销设备通知
        MFShutdown(); // 关闭Media Foundation
        CoUninitialize(); // 反初始化COM库
        return -1; // 返回错误代码
    }
    // 等待10秒
    Sleep(10000);
    // 停止捕获
    hr = g_pCapture->EndCaptureSession(); // 结束捕获会话
    if (FAILED(hr)) // 如果停止捕获失败
    {
        std::cerr << "Failed to stop capture." << std::endl; // 输出错误信息
    }
    SafeRelease(&pActivate); // 释放激活对象
    SafeRelease(&g_pCapture); // 释放捕获实例
    // ------------------------------------------------------------------------------------//

    // 清除设备列表
    g_devices.Clear(); // 清除设备列表

    // 注销设备通知
    if (g_hdevnotify) // 如果有注册设备通知
    {
        UnregisterDeviceNotification(g_hdevnotify); // 注销设备通知
    }

    // 关闭Media Foundation
    MFShutdown(); // 关闭Media Foundation
    CoUninitialize(); // 反初始化COM库

    return 0; // 返回成功代码
}