# VCamSample
This solution contains a Media Foundation Virtual Camera Sample. It works only on Windows 11 thanks to the [MFCreateVirtualCamera](https://learn.microsoft.com/en-us/windows/win32/api/mfvirtualcamera/nf-mfvirtualcamera-mfcreatevirtualcamera) API.

There are two projects in the solution:

* **VCamSampleSource**: the Media Source that provides RGB32 and NV12 streaming samples.
* **VCamSample**: the "driver" application that does very little but calls `MFCreateVirtualCamera`.



To test the project:

* Build in debug or release
* Go to the build output and register the media source (a COM object) with a command similar to this: `regsvr32 VCamSampleSource.dll` (you *must* run this as administrator, it' not possible to register a Virtual Camera media source in `HKCU`, only in `HKLM` since it will be loaded by multiple processes)
* Run the VCamSample app.
* Run for example the Windows Camera app or using a Web Browser ImageCapture API

You should now see something like this in Camera App

![Screenshot 2024-01-22 131726](https://github.com/smourier/VCamSample/assets/5328574/50b27acb-3cf7-4d41-9298-84f7c1358148)

And this in Windows' Edge Web Browser using this testing page: https://googlechrome.github.io/samples/image-capture/grab-frame-take-photo.html.

![Screenshot 2024-01-22 133220](https://github.com/smourier/VCamSample/assets/5328574/1f7d34e9-5646-4f26-bc9a-534e3bc9d625)

## Notes

* The media source uses `Direct2D` and `DirectWrite` to create images. It will then create Media Foundation samples from these. To create MF samples, it can use:
  * The GPU, if a Direct3D manager has been provided by the environment. This is the case of the Windows 11 camera app.
  * The CPU, if no Direct3D environment has been provided. In this case, the media source uses a WIC bitmap as a render target and it then copies the bits over to an MF sample. ImageCapture API code embedded in Chrome or Edge, Teams, etc. is an example of such a D3D-less environment.
  * If you want to force CPU usage at all times, you can change the code in `MediaStream::SetD3DManager` and put the lines here in comment.

* The media source provides RGB32 and NV12 formats as most setups prefer the NV12 format. Samples are initially created as RGB32 (Direct2D) and converted to NV12. To convert the samples, the media source uses two ways:
  * The GPU, if a Direct3D manager has been provided, using Media Foundation's [Video Processor MFT](https://learn.microsoft.com/en-us/windows/win32/medfound/video-processor-mft).
  * The CPU, if no Direct3D environment has been provided. In this case, the RGB to NV12 conversion is done in the code (so on the CPU).
  * If you want to force RGB32 mode, you can change the code in `MediaStream::Initialize` and set the media types array size to 1 (check comments in the code).


## Tracing

The code output lots of interesting traces. It's quite important in this virtual camera environment because there's not just your process that's involved but at least 4: the VCamSample app, the Windows Frame Server, the Windows camera monitor, and the reader. They all load the media source in-process.

Tracing here  doesn't use `OutputDebugString` because it's 100% old, crappy, truncating text, slow, etc. Instead it uses Event Tracing for Windows ("ETW") in "string-only" mode (the mode where it's very simple and you don't have register traces and complicated readers...).

So to read these ETW traces, use WpfTraceSpy you can download here https://github.com/smourier/TraceSpy. Configure an ETW Provider with the GUID set to `964d4572-adb9-4f3a-8170-fcbecec27467`
