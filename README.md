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

## 🛠️ Building the Project

1.  Clone the repository:
    ```bash
    git clone [https://github.com/rajhlinux/UVC-DirectX-Zero-Copy-Low-Latency-GPU-Accelerated.git](https://github.com/rajhlinux/UVC-DirectX-Zero-Copy-Low-Latency-GPU-Accelerated.git)
    ```
2.  Open the solution file in **Visual Studio 2019/2022**.
3.  Ensure the **Windows 10/11 SDK** is installed.
4.  Build for `Release` / `x64`.

## 🤝 Contributing

This project is intended as a reference implementation for low-level graphics programming. Pull requests optimizing the pipeline or adding support for Compute Shaders / AI integration are welcome.

## 📄 License

[MIT License](LICENSE)
