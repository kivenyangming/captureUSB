#define WIN32_LEAN_AND_MEAN
#include <stdio.h>
#include <new>
#include <windows.h>
#include <mfapi.h> //��Microsoft Media Foundation API�ķ���
#include <mfidl.h>
#include <mfreadwrite.h>
#include <Wmcodecdsp.h>
#include <assert.h>
#include <Dbt.h>
#include <shlwapi.h>
#include "capture.h"

HRESULT CopyAttribute(IMFAttributes* pSrc, IMFAttributes* pDest, const GUID& key); // ��һ��IMFAttributes���������Ե���һ��IMFAttributes����GUID����keyָ����Ҫ���Ƶ����Եļ���

//����һ��ģ�庯�������ڰ�ȫ���ͷ�COM��������ü�������COM�����ٱ�ʹ��ʱ����Ҫ������Release�������������ü�����������Ϊ0ʱ��������Զ����١�
template <class T> void SafeRelease(T** ppT)
{
    if (*ppT)
    {
        (*ppT)->Release();
        *ppT = nullptr;
    }
}

// DeviceList������;Clear������������豸�б�
void DeviceList::Clear()
{
    for (UINT32 i = 0; i < m_cDevices; i++)
    {
        SafeRelease(&m_ppDevices[i]);
    }
    CoTaskMemFree(m_ppDevices);
    m_ppDevices = nullptr;
    m_cDevices = 0;
}

// EnumerateDevices����ö���豸
HRESULT DeviceList::EnumerateDevices()
{
    HRESULT hr = S_OK;
    IMFAttributes* pAttributes = nullptr;

    Clear();

    hr = MFCreateAttributes(&pAttributes, 1);

    if (SUCCEEDED(hr))
    {
        hr = pAttributes->SetGUID(
            MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
            MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID
        );
    }

    if (SUCCEEDED(hr))
    {
        hr = MFEnumDeviceSources(pAttributes, &m_ppDevices, &m_cDevices);
    }

    SafeRelease(&pAttributes);

    return hr;
}

// GetDevice��GetDeviceName���ڻ�ȡ�豸��Ϣ��
HRESULT DeviceList::GetDevice(UINT32 index, IMFActivate** ppActivate)
{
    if (index >= Count())
    {
        return E_INVALIDARG;
    }

    *ppActivate = m_ppDevices[index];
    (*ppActivate)->AddRef();

    return S_OK;
}

// GetDevice��GetDeviceName���ڻ�ȡ�豸��Ϣ��
HRESULT DeviceList::GetDeviceName(UINT32 index, WCHAR** ppszName)
{
    if (index >= Count())
    {
        return E_INVALIDARG;
    }

    HRESULT hr = S_OK;

    hr = m_ppDevices[index]->GetAllocatedString(
        MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME,
        ppszName,
        nullptr
    );

    return hr;
}

// ��̬���������ڴ���CCapture���ʵ����������һ�����ھ��hwnd���ô��ڽ������¼���Ϣ����ͨ��ppCapture������������´�����CCapture�����ָ�롣
HRESULT CCapture::CreateInstance(HWND hwnd, CCapture** ppCapture)
{
    if (ppCapture == nullptr)
    {
        return E_POINTER;
    }

    CCapture* pCapture = new (std::nothrow) CCapture(hwnd);

    if (pCapture == nullptr)
    {
        return E_OUTOFMEMORY;
    }

    *ppCapture = pCapture;

    return S_OK;
}


// CCapture��Ĺ��캯�����������������캯����ʼ�����󣬶�������������������Դ��
CCapture::CCapture(HWND hwnd) :
    m_pReader(nullptr),
    m_pWriter(nullptr),
    m_hwndEvent(hwnd),
    m_nRefCount(1),
    m_bFirstSample(FALSE),
    m_llBaseTime(0),
    m_pwszSymbolicLink(nullptr)
{
    InitializeCriticalSection(&m_critsec);
}
CCapture::~CCapture()
{
    assert(m_pReader == nullptr);
    assert(m_pWriter == nullptr);
    DeleteCriticalSection(&m_critsec);
}

// ʵ��COM�������ü�������ĳ�Ա������AddRef�������ü�������Release�������ü�����
ULONG CCapture::AddRef()
{
    return InterlockedIncrement(&m_nRefCount);
}
ULONG CCapture::Release()
{
    ULONG uCount = InterlockedDecrement(&m_nRefCount);
    if (uCount == 0)
    {
        delete this;
    }
    return uCount;
}

// ����ʵ��COM�ӿڲ�ѯ�ĳ�Ա������������ͻ��������ض��Ľӿ�ָ�롣
HRESULT CCapture::QueryInterface(REFIID riid, void** ppv)
{
    static const QITAB qit[] =
    {
        QITABENT(CCapture, IMFSourceReaderCallback),
        { 0 },
    };
    return QISearch(this, qit, riid, ppv);
}

// IMFSourceReaderCallback�ӿڵ�ʵ�֣����ڴ����ȡ���������ݡ���������������ȡ״̬��������������־��ʱ������������ݡ� 
HRESULT CCapture::OnReadSample(HRESULT hrStatus, DWORD, DWORD, LONGLONG llTimeStamp, IMFSample* pSample)
{
    // DWORD /*dwStreamIndex*/
    // DWORD /*dwStreamFlags*/
    // IMFSample* pSample      // Can be nullptr
    
    EnterCriticalSection(&m_critsec);

    if (!IsCapturing())
    {
        LeaveCriticalSection(&m_critsec);
        return S_OK;
    }

    HRESULT hr = S_OK;

    if (FAILED(hrStatus))
    {
        hr = hrStatus;
        goto done;
    }

    if (pSample)
    {
        if (m_bFirstSample)
        {
            m_llBaseTime = llTimeStamp;
            m_bFirstSample = FALSE;
        }

        // rebase the time stamp
        llTimeStamp -= m_llBaseTime;

        hr = pSample->SetSampleTime(llTimeStamp);

        if (FAILED(hr)) { goto done; }

        hr = m_pWriter->WriteSample(0, pSample);

        if (FAILED(hr)) { goto done; }

        // Print sample data
        PrintSampleData(pSample);
    }

    // Read another sample.
    hr = m_pReader->ReadSample(
        (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM,
        0,
        nullptr,   // actual
        nullptr,   // flags
        nullptr,   // timestamp
        nullptr    // sample
    );

done:
    if (FAILED(hr))
    {
        NotifyError(hr);
    }

    LeaveCriticalSection(&m_critsec);
    return hr;
}

//��ӡͼ����������
void CCapture::PrintSampleData(IMFSample* pSample)
{
    IMFMediaBuffer* pBuffer = nullptr;
    BYTE* pData = nullptr;
    DWORD maxLen, currentLen;

    if (SUCCEEDED(pSample->GetBufferByIndex(0, &pBuffer)) &&
        SUCCEEDED(pBuffer->Lock(&pData, &maxLen, &currentLen)))
    {
        char* pCharData = reinterpret_cast<char*>(pData);
        /*
        for (DWORD i = 0; i < currentLen; i++)
        {
            printf("%02X ", pCharData[i]);
            if ((i + 1) % 32 == 0)
            {
                printf("\n");
            }
        }
        printf("\n");
        */
        pBuffer->Unlock();
    }

    SafeRelease(&pBuffer);
}

//��ý��Դ����׼����ȡ���ݡ�
HRESULT CCapture::OpenMediaSource(IMFMediaSource* pSource)
{
    HRESULT hr = S_OK;
    IMFAttributes* pAttributes = nullptr;

    hr = MFCreateAttributes(&pAttributes, 2);

    if (SUCCEEDED(hr))
    {
        hr = pAttributes->SetUnknown(MF_SOURCE_READER_ASYNC_CALLBACK, this);
    }

    if (SUCCEEDED(hr))
    {
        hr = MFCreateSourceReaderFromMediaSource(
            pSource,
            pAttributes,
            &m_pReader
        );
    }

    SafeRelease(&pAttributes);
    return hr;
}

//��ʼ������Ƶ��������һ����������ļ����ͱ��������
HRESULT CCapture::StartCapture(IMFActivate* pActivate, const WCHAR* pwszFileName, const EncodingParameters& param)
{
    HRESULT hr = S_OK;
    IMFMediaSource* pSource = nullptr;

    EnterCriticalSection(&m_critsec);

    hr = pActivate->ActivateObject(
        __uuidof(IMFMediaSource),
        (void**)&pSource
    );

    if (SUCCEEDED(hr))
    {
        hr = pActivate->GetAllocatedString(
            MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK,
            &m_pwszSymbolicLink,
            nullptr
        );
    }

    if (SUCCEEDED(hr))
    {
        hr = OpenMediaSource(pSource);
    }

    if (SUCCEEDED(hr))
    {
        hr = MFCreateSinkWriterFromURL(
            pwszFileName,
            nullptr,
            nullptr,
            &m_pWriter
        );
    }

    if (SUCCEEDED(hr))
    {
        hr = ConfigureCapture(param);
    }

    if (SUCCEEDED(hr))
    {
        m_bFirstSample = TRUE;
        m_llBaseTime = 0;

        hr = m_pReader->ReadSample(
            (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM,
            0,
            nullptr,
            nullptr,
            nullptr,
            nullptr
        );
    }

    SafeRelease(&pSource);
    LeaveCriticalSection(&m_critsec);
    return hr;
}

// ��������Ự��
HRESULT CCapture::EndCaptureSession()
{
    EnterCriticalSection(&m_critsec);
    HRESULT hr = S_OK;

    if (m_pWriter)
    {
        hr = m_pWriter->Finalize();
    }

    SafeRelease(&m_pWriter);
    SafeRelease(&m_pReader);

    LeaveCriticalSection(&m_critsec);

    return hr;
}

// ����Ƿ����ڲ�����Ƶ��
BOOL CCapture::IsCapturing()
{
    EnterCriticalSection(&m_critsec);
    BOOL bIsCapturing = (m_pWriter != nullptr);

    LeaveCriticalSection(&m_critsec);

    return bIsCapturing;
}

// ����豸�Ƿ�ʧ��
HRESULT CCapture::CheckDeviceLost(DEV_BROADCAST_HDR* pHdr, BOOL* pbDeviceLost)
{
    if (pbDeviceLost == nullptr)
    {
        return E_POINTER;
    }
    EnterCriticalSection(&m_critsec);

    DEV_BROADCAST_DEVICEINTERFACE* pDi = nullptr;

    *pbDeviceLost = FALSE;

    if (!IsCapturing())
    {
        goto done;
    }
    if (pHdr == nullptr)
    {
        goto done;
    }
    if (pHdr->dbch_devicetype != DBT_DEVTYP_DEVICEINTERFACE)
    {
        goto done;
    }

    pDi = (DEV_BROADCAST_DEVICEINTERFACE*)pHdr;

    if (m_pwszSymbolicLink)
    {
        if (_wcsicmp(m_pwszSymbolicLink, pDi->dbcc_name) == 0)
        {
            *pbDeviceLost = TRUE;
        }
    }
done:
    LeaveCriticalSection(&m_critsec);
    return S_OK;
}

// ����Դ��ȡ�������ܰ�������ý�����͵ȡ�
HRESULT ConfigureSourceReader(IMFSourceReader* pReader)
{
    GUID subtypes[] = {
    MFVideoFormat_NV12, MFVideoFormat_YUY2, MFVideoFormat_UYVY,
    MFVideoFormat_RGB32, MFVideoFormat_RGB24, MFVideoFormat_IYUV
    };
    HRESULT hr = S_OK;
    BOOL    bUseNativeType = FALSE;

    GUID subtype = { 0 };

    IMFMediaType* pType = nullptr;

    hr = pReader->GetNativeMediaType(
        (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM,
        0,  // Type index
        &pType
    );

    if (FAILED(hr)) { goto done; }

    hr = pType->GetGUID(MF_MT_SUBTYPE, &subtype);

    if (FAILED(hr)) { goto done; }

    for (UINT32 i = 0; i < ARRAYSIZE(subtypes); i++)
    {
        if (subtype == subtypes[i])
        {
            hr = pReader->SetCurrentMediaType(
                (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                nullptr,
                pType
            );

            bUseNativeType = TRUE;
            break;
        }
    }

    if (!bUseNativeType)
    {
        for (UINT32 i = 0; i < ARRAYSIZE(subtypes); i++)
        {
            hr = pType->SetGUID(MF_MT_SUBTYPE, subtypes[i]);

            if (FAILED(hr)) { goto done; }

            hr = pReader->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr,pType);

            if (SUCCEEDED(hr))
            {
                break;
            }
        }
    }
done:
    SafeRelease(&pType);
    return hr;
}

// �����ڲ���������������Դ��
HRESULT ConfigureEncoder(const EncodingParameters& params, IMFMediaType* pType, IMFSinkWriter* pWriter, DWORD* pdwStreamIndex)
{
    HRESULT hr = S_OK;
    IMFMediaType* pType2 = nullptr;

    hr = MFCreateMediaType(&pType2);

    if (SUCCEEDED(hr))
    {
        hr = pType2->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    }

    if (SUCCEEDED(hr))
    {
        hr = pType2->SetGUID(MF_MT_SUBTYPE, params.subtype);
    }

    if (SUCCEEDED(hr))
    {
        hr = pType2->SetUINT32(MF_MT_AVG_BITRATE, params.bitrate);
    }

    if (SUCCEEDED(hr))
    {
        hr = CopyAttribute(pType, pType2, MF_MT_FRAME_SIZE);
    }

    if (SUCCEEDED(hr))
    {
        hr = CopyAttribute(pType, pType2, MF_MT_FRAME_RATE);
    }

    if (SUCCEEDED(hr))
    {
        hr = CopyAttribute(pType, pType2, MF_MT_PIXEL_ASPECT_RATIO);
    }

    if (SUCCEEDED(hr))
    {
        hr = CopyAttribute(pType, pType2, MF_MT_INTERLACE_MODE);
    }

    if (SUCCEEDED(hr))
    {
        hr = pWriter->AddStream(pType2, pdwStreamIndex);
    }

    SafeRelease(&pType2);
    return hr;
}

// ���ò�����̣���������Դ��ȡ���ͱ�������
HRESULT CCapture::ConfigureCapture(const EncodingParameters& param)
{
    HRESULT hr = S_OK;
    DWORD sink_stream = 0;
    IMFMediaType* pType = nullptr;

    hr = ConfigureSourceReader(m_pReader);

    if (SUCCEEDED(hr))
    {
        hr = m_pReader->GetCurrentMediaType(
            (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM,
            &pType
        );
    }

    if (SUCCEEDED(hr))
    {
        hr = ConfigureEncoder(param, pType, m_pWriter, &sink_stream);
    }

    if (SUCCEEDED(hr))
    {
        hr = MFTRegisterLocalByCLSID(
            __uuidof(CColorConvertDMO),
            MFT_CATEGORY_VIDEO_PROCESSOR,
            L"",
            MFT_ENUM_FLAG_SYNCMFT,
            0,
            nullptr,
            0,
            nullptr
        );
    }

    if (SUCCEEDED(hr))
    {
        hr = m_pWriter->SetInputMediaType(sink_stream, pType, nullptr);
    }

    if (SUCCEEDED(hr))
    {
        hr = m_pWriter->BeginWriting();
    }

    SafeRelease(&pType);
    return hr;
}

// �ڲ���������������Դ��
HRESULT CCapture::EndCaptureInternal()
{
    HRESULT hr = S_OK;
    if (m_pWriter)
    {
        hr = m_pWriter->Finalize();
    }

    SafeRelease(&m_pWriter);
    SafeRelease(&m_pReader);

    CoTaskMemFree(m_pwszSymbolicLink);
    m_pwszSymbolicLink = nullptr;

    return hr;
}

// ����֮ǰ�����ĸ������Ժ�����ʵ�֣����ڴ�һ��IMFAttributes���������Ե���һ����
HRESULT CopyAttribute(IMFAttributes* pSrc, IMFAttributes* pDest, const GUID& key)
{
    PROPVARIANT var;
    PropVariantInit(&var);

    HRESULT hr = S_OK;

    hr = pSrc->GetItem(key, &var);
    if (SUCCEEDED(hr))
    {
        hr = pDest->SetItem(key, var);
    }
    PropVariantClear(&var);
    return hr;
}

