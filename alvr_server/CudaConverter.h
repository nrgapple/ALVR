#pragma once

#include <exception>
#include <d3d11.h>
#include <wrl.h>
#include <cuda.h>

#include <cuda_runtime_api.h>
#include <cuda_d3d11_interop.h>

#include <RGBToNV12.h>

#include "Logger.h"

using Microsoft::WRL::ComPtr;

class CudaConverter {
public:
	CudaConverter(ID3D11Device *device, int width, int height)
		: m_width(width)
		, m_height(height)
		, m_registered(false) {
		InitCudaContext(device);

		// Allocate CUDA buffer to pass to NvEncoderCuda
		// format is rgba
		cudaError cuStatus = cudaMallocPitch(&m_cudaLinearMemory, &m_pitch, m_width * 4, m_height);
		if (cuStatus != cudaSuccess) {
			throw MakeException("cudaMallocPitch failed.");
		}
		cudaMemset(m_cudaLinearMemory, 1, m_pitch * m_height);
	}

	~CudaConverter() {
		cudaGraphicsUnregisterResource(m_cudaResource);
		cudaFree(m_cudaLinearMemory);
		cuCtxDestroy(m_cuContext);
	}

	CUcontext GetContext() {
		return m_cuContext;
	}

	void Convert(const ComPtr<ID3D11Texture2D> &texture, const NvEncInputFrame* encoderInputFrame) {
		cudaError cuStatus;

		RegisterTexture(texture);

		cuStatus = cudaGraphicsMapResources(1, &m_cudaResource, 0);
		if (cuStatus != cudaSuccess) {
			throw MakeException("cudaGraphicsMapResources failed.");
		}

		cudaArray *cuArray;
		cuStatus = cudaGraphicsSubResourceGetMappedArray(&cuArray, m_cudaResource, 0, 0);
		if (cuStatus != cudaSuccess) {
			throw MakeException("cudaGraphicsSubResourceGetMappedArray failed.");
		}

		cuStatus = RGBA2NV12(cuArray, (uint8_t *)encoderInputFrame->inputPtr, encoderInputFrame->pitch, m_width, m_height);

		if (cuStatus != cudaSuccess) {
			throw MakeException("cudaMemcpy2DFromArray failed.");
		}

		cudaGraphicsUnmapResources(1, &m_cudaResource, 0);
		if (cuStatus != cudaSuccess) {
			throw MakeException("cudaGraphicsUnmapResources failed.");
		}
	}

private:
	void InitCudaContext(ID3D11Device *device) {
		ComPtr<IDXGIDevice> DXGIDevice;
		ComPtr<IDXGIAdapter> DXGIAdapter;

		HRESULT hr = device->QueryInterface(__uuidof(IDXGIDevice), &DXGIDevice);
		if (FAILED(hr)) {
			throw MakeException("Failed to query IDXGIDevice");
		}

		hr = DXGIDevice->GetAdapter(&DXGIAdapter);
		if (FAILED(hr)) {
			throw MakeException("Failed to get IDXGIAdapter");
		}
		int cuDevice;
		cudaError cuStatus = cudaD3D11GetDevice(&cuDevice, DXGIAdapter.Get());
		if (cuStatus != cudaSuccess) {
			throw MakeException("Failed to get CUDA device.");
		}

		CUresult result = cuInit(0);
		if (result != CUDA_SUCCESS) {
			throw MakeException("cuInit failed.");
		}

		cudaDeviceProp deviceProp;
		cudaGetDeviceProperties(&deviceProp, cuDevice);
		Log("Using CUDA Device %d: %s\n", cuDevice, deviceProp.name);

		result = cuCtxCreate(&m_cuContext, 0, cuDevice);
		if (result != CUDA_SUCCESS) {
			throw MakeException("Failed to create CUDA context.");
		}
	}

	void RegisterTexture(const ComPtr<ID3D11Texture2D> &texture) {
		if (m_registered) {
			return;
		}
		m_registered = true;
		cudaError cuStatus = cudaGraphicsD3D11RegisterResource(&m_cudaResource, texture.Get(), cudaGraphicsRegisterFlagsNone);
		if (cuStatus != cudaSuccess) {
			throw MakeException("cudaGraphicsD3D11RegisterResource failed.");
		}
	}

private:
	CUcontext m_cuContext;
	bool m_registered;
	cudaGraphicsResource *m_cudaResource;
	void *m_cudaLinearMemory;
	size_t m_pitch;
	const int m_width;
	const int m_height;
};