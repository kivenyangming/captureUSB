#pragma once // ֻ����һ��ͷ�ļ�����ֹ�ظ�����

// ������һ����Ϣ������Ӧ�ó���Ԥ������
const UINT WM_APP_PREVIEW_ERROR = WM_APP + 1;    // wparam = HRESULT

// DeviceList �����ڹ����豸�б�
class DeviceList
{
private:
    UINT32      m_cDevices; // �豸����
    IMFActivate** m_ppDevices; // ָ���豸��������ָ������

public:
    // ���캯��
    DeviceList() : m_ppDevices(nullptr), m_cDevices(0)
    {
    }

    // ��������
    ~DeviceList()
    {
        Clear();
    }

    // �����豸����
    UINT32  Count() const { return m_cDevices; }

    // ����豸�б�
    void    Clear();

    // ö���豸
    HRESULT EnumerateDevices();

    // ��ȡָ���������豸�������
    HRESULT GetDevice(UINT32 index, IMFActivate** ppActivate);

    // ��ȡָ���������豸����
    HRESULT GetDeviceName(UINT32 index, WCHAR** ppszName);
};

// EncodingParameters �ṹ�����ڴ洢�������
struct EncodingParameters
{
    GUID    subtype; // ý������������
    UINT32  bitrate; // ���������
};

// CCapture ��ʵ���� IMFSourceReaderCallback �ӿڣ�������Ƶ����
class CCapture : public IMFSourceReaderCallback
{
public:
    // ��̬���������ڴ��� CCapture ʵ��
    static HRESULT CreateInstance(
        HWND     hwnd,
        CCapture** ppPlayer
    );

    // IUnknown ����
    STDMETHODIMP QueryInterface(REFIID iid, void** ppv);
    STDMETHODIMP_(ULONG) AddRef();
    STDMETHODIMP_(ULONG) Release();

    // IMFSourceReaderCallback ����
    STDMETHODIMP OnReadSample(
        HRESULT hrStatus,
        DWORD dwStreamIndex,
        DWORD dwStreamFlags,
        LONGLONG llTimestamp,
        IMFSample* pSample
    );

    // �¼���������ĿǰΪ��ʵ��
    STDMETHODIMP OnEvent(DWORD, IMFMediaEvent*)
    {
        return S_OK;
    }

    // ˢ�´�������ĿǰΪ��ʵ��
    STDMETHODIMP OnFlush(DWORD)
    {
        return S_OK;
    }

    // ��ʼ����
    HRESULT     StartCapture(IMFActivate* pActivate, const WCHAR* pwszFileName, const EncodingParameters& param);

    // ��������Ự
    HRESULT     EndCaptureSession();

    // ����Ƿ����ڲ���
    BOOL        IsCapturing();

    // ����豸�Ƿ�ʧ
    HRESULT     CheckDeviceLost(DEV_BROADCAST_HDR* pHdr, BOOL* pbDeviceLost);

    // ��ӡ��������
    void        PrintSampleData(IMFSample* pSample);

protected:
    // ״̬ö��
    enum State
    {
        State_NotReady = 0, // δ׼����
        State_Ready,         // ��׼����
        State_Capturing,     // ���ڲ���
    };

    // ˽�й��캯����ʹ�� CreateInstance ��������ʵ����
    CCapture(HWND hwnd);

    // ˽���������������� Release ���������ͷ�
    virtual ~CCapture();

    // ֪ͨ����
    void    NotifyError(HRESULT hr) { PostMessage(m_hwndEvent, WM_APP_PREVIEW_ERROR, (WPARAM)hr, 0L); }

    // ��ý��Դ
    HRESULT OpenMediaSource(IMFMediaSource* pSource);

    // ���ò���
    HRESULT ConfigureCapture(const EncodingParameters& param);

    // �ڲ���������
    HRESULT EndCaptureInternal();

    // ��Ա����
    long                    m_nRefCount;        // ���ü���
    CRITICAL_SECTION        m_critsec;         // �ٽ���

    HWND                    m_hwndEvent;        // �����¼���Ӧ�ó��򴰿�

    IMFSourceReader* m_pReader; // Դ��ȡ��
    IMFSinkWriter* m_pWriter;  // ������д����

    BOOL                    m_bFirstSample;    // �Ƿ��ǵ�һ������
    LONGLONG                m_llBaseTime;      // ��׼ʱ��

    WCHAR* m_pwszSymbolicLink; // ���������ַ���
};