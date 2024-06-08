#include "pch.h"
#include "Undocumented.h"
#include "Tools.h"
#include "EnumNames.h"
#include "MFTools.h"
#include "FrameGenerator.h"
#include <sddl.h>
#include "Rgb2NV12.h"

HRESULT FrameGenerator::EnsureRenderTarget(UINT width, UINT height)
{
	if (!HasD3DManager())
	{
		// create a D2D1 render target from WIC bitmap
		wil::com_ptr_nothrow<ID2D1Factory> d2d1Factory;
		RETURN_IF_FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_MULTI_THREADED, IID_PPV_ARGS(&d2d1Factory)));

		wil::com_ptr_nothrow<IWICImagingFactory> wicFactory;
		RETURN_IF_FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&wicFactory)));

		RETURN_IF_FAILED(wicFactory->CreateBitmap(width, height, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnDemand, &_bitmap));

		D2D1_RENDER_TARGET_PROPERTIES props{};
		props.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
		props.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
		RETURN_IF_FAILED(d2d1Factory->CreateWicBitmapRenderTarget(_bitmap.get(), props, &_renderTarget));

		RETURN_IF_FAILED(CreateRenderTargetResources(width, height));
	}

	_prevTime = MFGetSystemTime();
	_frame = 0;
	return S_OK;
}

const bool FrameGenerator::HasD3DManager() const
{
	return _texture != nullptr;
}

HRESULT FrameGenerator::SetD3DManager(IUnknown* manager, UINT width, UINT height)
{
	RETURN_HR_IF_NULL(E_POINTER, manager);
	RETURN_HR_IF(E_INVALIDARG, !width || !height);

	RETURN_IF_FAILED(manager->QueryInterface(&_dxgiManager));
	RETURN_IF_FAILED(_dxgiManager->OpenDeviceHandle(&_deviceHandle));

	wil::com_ptr_nothrow<ID3D11Device> device;
	RETURN_IF_FAILED(_dxgiManager->GetVideoService(_deviceHandle, IID_PPV_ARGS(&device)));

	// create a texture/surface to write
	CD3D11_TEXTURE2D_DESC desc
	(
		DXGI_FORMAT_B8G8R8A8_UNORM,
		width,
		height,
		1,
		1,
		D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET
	);
	RETURN_IF_FAILED(device->CreateTexture2D(&desc, nullptr, &_texture));
	wil::com_ptr_nothrow<IDXGISurface> surface;
	RETURN_IF_FAILED(_texture.copy_to(&surface));

	// create a D2D1 render target from 2D GPU surface
	wil::com_ptr_nothrow<ID2D1Factory> d2d1Factory;
	RETURN_IF_FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_MULTI_THREADED, IID_PPV_ARGS(&d2d1Factory)));

	auto props = D2D1::RenderTargetProperties
	(
		D2D1_RENDER_TARGET_TYPE_DEFAULT,
		D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_PREMULTIPLIED)
	);
	RETURN_IF_FAILED(d2d1Factory->CreateDxgiSurfaceRenderTarget(surface.get(), props, &_renderTarget));

	RETURN_IF_FAILED(CreateRenderTargetResources(width, height));

	// create GPU RGB => NV12 converter
	RETURN_IF_FAILED(CoCreateInstance(CLSID_VideoProcessorMFT, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&_converter)));

	wil::com_ptr_nothrow<IMFAttributes> atts;
	RETURN_IF_FAILED(_converter->GetAttributes(&atts));
	TraceMFAttributes(atts.get(), L"VideoProcessorMFT");

	MFT_OUTPUT_STREAM_INFO info{};
	RETURN_IF_FAILED(_converter->GetOutputStreamInfo(0, &info));
	WINTRACE(L"FrameGenerator::EnsureRenderTarget CLSID_VideoProcessorMFT flags:0x%08X size:%u alignment:%u", info.dwFlags, info.cbSize, info.cbAlignment);

	wil::com_ptr_nothrow<IMFMediaType> inputType;
	RETURN_IF_FAILED(MFCreateMediaType(&inputType));
	inputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
	inputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
	MFSetAttributeSize(inputType.get(), MF_MT_FRAME_SIZE, width, height);
	RETURN_IF_FAILED(_converter->SetInputType(0, inputType.get(), 0));

	wil::com_ptr_nothrow<IMFMediaType> outputType;
	RETURN_IF_FAILED(MFCreateMediaType(&outputType));
	outputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
	outputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
	MFSetAttributeSize(outputType.get(), MF_MT_FRAME_SIZE, width, height);
	RETURN_IF_FAILED(_converter->SetOutputType(0, outputType.get(), 0));

	// make sure the video processor works on GPU
	RETURN_IF_FAILED(_converter->ProcessMessage(MFT_MESSAGE_SET_D3D_MANAGER, (ULONG_PTR)manager));
	return S_OK;
}

// common to CPU & GPU
HRESULT FrameGenerator::CreateRenderTargetResources(UINT width, UINT height)
{
	assert(_renderTarget);
	RETURN_IF_FAILED(_renderTarget->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 1), &_whiteBrush));

	RETURN_IF_FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)&_dwrite));
	RETURN_IF_FAILED(_dwrite->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 40, L"", &_textFormat));
	RETURN_IF_FAILED(_textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER));
	RETURN_IF_FAILED(_textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER));
	_width = width;
	_height = height;
	return S_OK;
}

HRESULT FrameGenerator::Generate(IMFSample* sample, REFGUID format, IMFSample** outSample)
{
	static bool fReportFail = false;
	//
	if (hShareMemory == NULL)
	{
		eventLog.Fire(EVENTLOG_INFORMATION_TYPE, 0x01, 0x01, "hShareMemory == NULL");
		//
		// https://stackoverflow.com/questions/898683/how-to-share-memory-between-services-and-user-processes
		//
		SECURITY_ATTRIBUTES attributes;
		ZeroMemory(&attributes, sizeof(attributes));
		attributes.nLength = sizeof(attributes);
		ConvertStringSecurityDescriptorToSecurityDescriptor(
			L"D:P(A;OICI;GA;;;SY)(A;OICI;GA;;;BA)(A;OICI;GR;;;IU)",
			SDDL_REVISION_1,
			&attributes.lpSecurityDescriptor,
			NULL);
		//
		hShareMemory = CreateFileMappingW(INVALID_HANDLE_VALUE, &attributes, PAGE_READWRITE, 0, 640 * 480 * 4, L"Global\\iFaceVirtualCamVideo");
		if (hShareMemory != NULL)
			eventLog.Fire(EVENTLOG_INFORMATION_TYPE, 0x01, 0x01, "CreateFileMappingW OK");
		else
		{
			if (fReportFail == false)
			{
				eventLog.Fire(EVENTLOG_INFORMATION_TYPE, 0x01, 0x01, "CreateFileMappingW Fail");
				fReportFail = true;
			}
		}
	}
	uint8_t* image = NULL;
	bool fVideoRead = false;
	if (hShareMemory != NULL)
	{
		image = (uint8_t*)MapViewOfFile(hShareMemory, FILE_MAP_READ, 0, 0, 0);
		if (image != NULL)
		{
			fVideoRead = true;
		}
		else
		{
			fVideoRead = false;
			CloseHandle(hShareMemory);
			eventLog.Fire(EVENTLOG_INFORMATION_TYPE, 0x01, 0x01, "MapViewOfFile Fail. Close File Mapping Handle.");
		}
	}
	//
	RETURN_HR_IF_NULL(E_POINTER, sample);
	RETURN_HR_IF_NULL(E_POINTER, outSample);
	*outSample = nullptr;

	wil::com_ptr_nothrow<IMFMediaBuffer> mediaBuffer;

	// remove all existing buffers
	RETURN_IF_FAILED(sample->RemoveAllBuffers());

	// create a buffer from this and add to sample
	DWORD memBufLen = 640 * 480 * 4;
	RETURN_IF_FAILED(MFCreateMemoryBuffer(memBufLen, &mediaBuffer));
	BYTE* pBuffer = NULL;
	DWORD cbMaxLength;
	DWORD cbCurrentLength;
	RETURN_IF_FAILED(mediaBuffer->Lock(&pBuffer, &cbMaxLength, &cbCurrentLength));
	if (fVideoRead == false)
	{
		memset(pBuffer, 0xFF, 640 * 480 * 4);
		mediaBuffer->SetCurrentLength(640 * 480 * 4);
	}
	else
	{
		//memcpy(pBuffer, image, 640 * 480 * 4);
		//
		Rgb2NV12(image, 640, 480, pBuffer);
		mediaBuffer->SetCurrentLength(640 * 480 * 2);
	}
	mediaBuffer->Unlock();
	//
	RETURN_IF_FAILED(sample->AddBuffer(mediaBuffer.get()));

	// if we're on GPU & format is not RGB, convert using GPU
	if (format == MFVideoFormat_NV12)
	{
	}
	sample->AddRef();
	*outSample = sample;

	_frame++;
	eventLog.Fire(EVENTLOG_INFORMATION_TYPE, 0x01, 0x01, "FrameGenerator::Generate OK");
	return S_OK;
}
