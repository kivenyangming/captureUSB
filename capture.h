#pragma once // 只包含一次头文件，防止重复包含

// 定义了一个消息，用于应用程序预览错误
const UINT WM_APP_PREVIEW_ERROR = WM_APP + 1;    // wparam = HRESULT

// DeviceList 类用于管理设备列表
class DeviceList
{
private:
    UINT32      m_cDevices; // 设备数量
    IMFActivate** m_ppDevices; // 指向设备激活对象的指针数组

public:
    // 构造函数
    DeviceList() : m_ppDevices(nullptr), m_cDevices(0)
    {
    }

    // 析构函数
    ~DeviceList()
    {
        Clear();
    }

    // 返回设备数量
    UINT32  Count() const { return m_cDevices; }

    // 清空设备列表
    void    Clear();

    // 枚举设备
    HRESULT EnumerateDevices();

    // 获取指定索引的设备激活对象
    HRESULT GetDevice(UINT32 index, IMFActivate** ppActivate);

    // 获取指定索引的设备名称
    HRESULT GetDeviceName(UINT32 index, WCHAR** ppszName);
};

// EncodingParameters 结构体用于存储编码参数
struct EncodingParameters
{
    GUID    subtype; // 媒体类型子类型
    UINT32  bitrate; // 编码比特率
};

// CCapture 类实现了 IMFSourceReaderCallback 接口，用于视频捕获
class CCapture : public IMFSourceReaderCallback
{
public:
    // 静态方法，用于创建 CCapture 实例
    static HRESULT CreateInstance(
        HWND     hwnd,
        CCapture** ppPlayer
    );

    // IUnknown 方法
    STDMETHODIMP QueryInterface(REFIID iid, void** ppv);
    STDMETHODIMP_(ULONG) AddRef();
    STDMETHODIMP_(ULONG) Release();

    // IMFSourceReaderCallback 方法
    STDMETHODIMP OnReadSample(
        HRESULT hrStatus,
        DWORD dwStreamIndex,
        DWORD dwStreamFlags,
        LONGLONG llTimestamp,
        IMFSample* pSample
    );

    // 事件处理方法，目前为空实现
    STDMETHODIMP OnEvent(DWORD, IMFMediaEvent*)
    {
        return S_OK;
    }

    // 刷新处理方法，目前为空实现
    STDMETHODIMP OnFlush(DWORD)
    {
        return S_OK;
    }

    // 开始捕获
    HRESULT     StartCapture(IMFActivate* pActivate, const WCHAR* pwszFileName, const EncodingParameters& param);

    // 结束捕获会话
    HRESULT     EndCaptureSession();

    // 检查是否正在捕获
    BOOL        IsCapturing();

    // 检查设备是否丢失
    HRESULT     CheckDeviceLost(DEV_BROADCAST_HDR* pHdr, BOOL* pbDeviceLost);

    // 打印样本数据
    void        PrintSampleData(IMFSample* pSample);

protected:
    // 状态枚举
    enum State
    {
        State_NotReady = 0, // 未准备好
        State_Ready,         // 已准备好
        State_Capturing,     // 正在捕获
    };

    // 私有构造函数，使用 CreateInstance 方法进行实例化
    CCapture(HWND hwnd);

    // 私有析构函数，调用 Release 方法进行释放
    virtual ~CCapture();

    // 通知错误
    void    NotifyError(HRESULT hr) { PostMessage(m_hwndEvent, WM_APP_PREVIEW_ERROR, (WPARAM)hr, 0L); }

    // 打开媒体源
    HRESULT OpenMediaSource(IMFMediaSource* pSource);

    // 配置捕获
    HRESULT ConfigureCapture(const EncodingParameters& param);

    // 内部结束捕获
    HRESULT EndCaptureInternal();

    // 成员变量
    long                    m_nRefCount;        // 引用计数
    CRITICAL_SECTION        m_critsec;         // 临界区

    HWND                    m_hwndEvent;        // 接收事件的应用程序窗口

    IMFSourceReader* m_pReader; // 源读取器
    IMFSinkWriter* m_pWriter;  // 接收器写入器

    BOOL                    m_bFirstSample;    // 是否是第一个样本
    LONGLONG                m_llBaseTime;      // 基准时间

    WCHAR* m_pwszSymbolicLink; // 符号链接字符串
};