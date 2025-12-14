# DirectX Zero-Copy Video Pipeline

**A high-performance, low-latency video streaming application utilizing a hybrid DirectX 11 & 12 architecture for "Zero-Copy" capture and presentation.**

![License](https://img.shields.io/badge/license-BSD_3--Clause-blue.svg)
![License](https://img.shields.io/badge/license-MIT-blue.svg)
![Language](https://img.shields.io/badge/language-C%2B%2B-00599C.svg)
![Platform](https://img.shields.io/badge/platform-Windows%2010%20%7C%2011-lightgrey.svg)
![DirectX](https://img.shields.io/badge/DirectX-11_%26_12-blue.svg)
![Media Foundation](https://img.shields.io/badge/Media_Foundation-Enabled-green.svg)

## 📖 Overview

This project implements a bare-metal video capture pipeline designed for the security, surveillance, and computer vision industries. By bypassing high-level abstractions and utilizing **DirectX 11/12 Interoperability**, the application achieves a true "zero-copy" workflow. 

It captures raw NV12 frames from a UVC camera directly to the GPU's VRAM, performs hardware-accelerated color conversion using the GPU's dedicated Video Processor Engine (VPE), and presents frames using the low-latency Flip Model.

### Key Features
* **Zero-Copy Capture:** Direct transfer of NV12 frames from UVC to GPU VRAM via Media Foundation.
* **Hybrid Architecture:** Leverage DX11 for capture and DX12 for advanced processing features.
* **Hardware Acceleration:** Uses the GPU's dedicated silicon (VPE) for NV12 $\to$ RGBA conversion, freeing up the CPU.
* **Low Latency:** Optimized swap chain presentation using the DX12 Flip Model.
* **No Vendor Lock-in:** Designed for generic hardware freedom (CCTV, HDMI converters, standard UVC).

---

## 📊 Performance & Benchmarks

### Glass-to-Glass Latency Test
We performed a "Glass-to-Glass" benchmark measuring the time between a real-world event and its rendering on the monitor.

![Latency Benchmark](imgs/Benchmark.png)

* **Average Latency:** ~248ms
* **Target:** Sufficient for 98% of surveillance and CV applications.

**Note on Hardware Constraints:**
The measured 248ms includes significant hardware overhead external to the software pipeline:
* **OS Overhead:** Windows 10/11 compositor and scheduling (~50ms).
* **Hardware Converters:** Generic CCTV $\to$ HDMI and HDMI $\to$ USB converters (~70ms - 170ms).
* **CPU:** Tested on an AMD FX 3850.

*Using enterprise-grade capture cards can reduce hardware latency to ~16ms, significantly lowering the total glass-to-glass time.*

---

## ⚙️ Architecture

This pipeline is orchestrated to ensure the video data never travels back to the system RAM (CPU) once it hits the GPU. This is crucial for maintaining high throughput for AI and Computer Vision tasks.

### 1. The Physical Setup & Pipeline Overview
The system is designed for hardware freedom and reliability. Below is the high-level orchestration of the pipeline.

![Pipeline Overview](imgs/entire_pipeline.png)

### 2. The Software Stack
The core innovation lies in the interoperability between DirectX 11 and DirectX 12 using **Shared Handles**.

#### WMMD (Windows Multimedia Device) Flow
Understanding the underlying OS stack is critical for optimization.
![Windows GPU Stack](imgs/Windows%20GPU%20APIs%20and%20Stack.png)

#### Capture to VRAM
Media Foundation initializes the video source. Instead of copying buffers to CPU RAM, it dumps UVC NV12 frames directly as D3D11 textures.

![Capture Diagram](imgs/How%20the%20UVC_cam_media_foundation_to_GPU_VRAM_transfer_works.png)

#### DX11 / DX12 Interop & Color Conversion
We use **DirectX 12** because it offers superior control over the GPU's **Video Processor Engine (VPE)**.
1.  **DX11:** Captures Texture.
2.  **Shared Handle:** Opens the resource in DX12.
3.  **DX12 VPE:** Converts NV12 $\to$ RGBA using dedicated silicon.

*> **Tip:** Use "DXVA Checker" to verify if your GPU supports NV12 textures and hardware color conversion.*

![Interop Diagram](imgs/DX11_and_DX12_interlop_explain_openSharedHandle.png)

#### Presentation (Swap Chain)
Finally, the RGBA texture is sent to the Swap Chain using the **Flip Model**, ensuring the freshest frame is displayed immediately without unnecessary buffering.

![Swap Chain Stack](imgs/UVC_Cam_to_GPU-VRAM_Stack_2.png)

#### Flip Model Mechanics
A detailed look at how the Flip Model manages back buffers to reduce latency:

![Swap Chain Flip Model](imgs/Swap%20Chain%20Flip%20Model.png)

---

## 💻 Hardware Requirements

* **OS:** Windows 10 or Windows 11.
* **GPU:** Must support DirectX 12 and NV12 texture formats (Most modern NVIDIA/AMD/Intel GPUs).
* **Camera:** Any UVC-compliant device (Webcam, HDMI Capture Card, etc.).


# 🎥 D3D12 Low Latency Hardware Streamer

**File:** `main.cpp`

This project implements a high-performance, low-latency video streaming pipeline. It captures 1080p video (NV12) using Media Foundation (D3D11), transfers it to DirectX 12, uses the GPU Video Engine for color conversion, and renders it to the screen.

---

## ⚙️ Features

* **Hybrid Pipeline:** Uses D3D11 for capture and D3D12 for processing/display.
* **Zero-Copy Interop:** Shares resources between D3D11 and D3D12 using shared handles.
* **Hardware Acceleration:** Uses the dedicated D3D12 Video Processor for NV12 $\to$ RGBA conversion.
* **Low Latency:** Implements `DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT` with a maximum latency of 1 frame.

---

## 🛠️ Build Process

### Prerequisites

* **Visual Studio 2022**
* **Windows SDK** (includes DirectX and Media Foundation headers)
* **Note:** The build commands below utilize specific paths on the `F:` drive as defined in the source code. Ensure these paths exist or adjust them to match your environment.

### 1. Initialize Environment

Open a standard Command Prompt (`cmd.exe`) and initialize the 64-bit build environment:

```cmd
"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
````

### 2\. Compile

Run the compiler (`cl`) with the C++20 standard. Note the inclusion of specific `vcpkg` include and library paths as required by the source configuration:

```cmd
cl /EHsc /std:c++20 main.cpp /Fe:main.exe /I "C:\dev\vcpkg\installed\x64-windows\include" /link /LIBPATH:"C:\dev\vcpkg\installed\x64-windows\lib" mfplat.lib mf.lib mfreadwrite.lib mfuuid.lib ole32.lib d3d12.lib dxgi.lib dxguid.lib
```

> **Note:** The `/Fe:main.exe` flag names the output executable `main.exe`.

-----

## 🚀 How to Run

Ensure your webcam is connected (defaults to the first USB 3.0 device found), then run the executable:

```cmd
main.exe
```

### Controls

  * The window will open at 1920x1080.
  * Close the window to exit the application.

## 🤝 Contributing

This project is intended as a reference implementation for low-level graphics programming. Pull requests optimizing the pipeline or adding support for Compute Shaders / AI integration / OS porting are welcomed.
