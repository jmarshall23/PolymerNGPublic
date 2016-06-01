// BuildRHI.h
//

#pragma once

//#include "../../rhi/D3D12/Core/pch.h"
//#include "../../rhi/D3D12/Core/TextureManager.h"
//#include <d3d11.h>
#include <wrl.h>
#include <wrl/client.h>
#include <dxgi1_4.h>
#include <d3d11_3.h>
#include <d2d1_3.h>
#include <d2d1effects_2.h>
#include <dwrite_3.h>
#include <wincodec.h>
#include <DirectXColors.h>
#include <DirectXMath.h>
#include <memory>

/*
================================

RHI - render hardware interface

================================
*/
#ifdef BUILD_D3D12
typedef DXGI_FORMAT BuildRHITextureFormat;
#endif

extern "C" void initprintf(const char *f, ...);

//
// BuildImageType
//
enum BuildImageType
{
	IMAGETYPE_1D = 0,
	IMAGETYPE_2D,
	IMAGETYPE_3D,

	IMAGETYPE_NONE
};

//
// BuildImageFormat
//
enum BuildImageFormat
{
	IMAGE_FORMAT_RGBA32 = 0,
	IMAGE_FORMAT_R8,
	IMAGE_FORMAT_R16,
	IMAGE_FORMAT_R8G8,
	IMAGE_FORMAT_DEPTH,
	IMAGE_FORMAT_DXT5
};

//
// BuildShaderTarget
//
enum BuildShaderTarget
{
	SHADER_TARGET_VERTEX = 0,
	SHADER_TARGET_GEOMETRY,
	SHADER_TARGET_FRAGMENT
};

//
// BuildRHITexture
//
class BuildRHITexture
{
public:
	virtual int			GetWidth() = 0;
	virtual int			GetHeight() = 0;
	virtual void		UploadRegion(int x, int y, int width, int height, const void *buffer) const = 0;
};

class BuildRHIMesh
{
public:

};

//
// BuildRHIGPUPerformanceCounter
//
class BuildRHIGPUPerformanceCounter
{
public:
	virtual void			Begin() = 0;
	virtual void			End() = 0;
	virtual UINT64			GetTime() = 0;
};

//
// BuildRHIGPUOcclusionQuery
//
class BuildRHIGPUOcclusionQuery
{
public:
	virtual void			Begin() = 0;
	virtual void			End() = 0;
	virtual bool			IsVisible() = 0;
};

//
// BuildRHIShader
//
class BuildRHIShader
{
public:
	virtual bool LoadShader(BuildShaderTarget target, const char *buffer, int length, bool useGUIVertexLayout) = 0;
};

//
// BuildRHIBlob
//
class BuildRHIBlob
{
public:
	BuildRHIBlob();
	~BuildRHIBlob();

	void SetMemory(void *memory, size_t blob_buffer_length);

	const void *GetMemory() { return blob_buffer; }

	size_t GetMemoryLength() { return blob_buffer_length; }
private:
	void *blob_buffer;
	size_t blob_buffer_length;
};

//
// BuildRHIRenderTarget
//
class BuildRHIRenderTarget
{
public:
	// No public functions yet
	virtual void				AddRenderTarget(BuildRHITexture *image) = 0;
};

//
// BuildRHIInputElementDesc
//
class BuildRHIInputElementDesc
{
public:
	virtual int GetNumElements() = 0;
};

//
// BuildRHIPipelineStateObject
//
class BuildRHIPipelineStateObject
{
public:
	virtual void SetInputLayout(BuildRHIInputElementDesc *desc) = 0;

	virtual void SetDepthState(bool depthEnabled) = 0;
	virtual void SetBlendMode(bool blendEnabled, bool additiveBlend, bool multiplicativeBlend) = 0;
	virtual void SetRasterizeState(bool twoSided) = 0;

	virtual void SetVertexShader(const void* Binary, size_t Size) = 0;
	virtual void SetPixelShader(const void* Binary, size_t Size) = 0;
	virtual void SetGeometryShader(const void* Binary, size_t Size) = 0;
	virtual void SetHullShader(const void* Binary, size_t Size) = 0;
	virtual void SetDomainShader(const void* Binary, size_t Size) = 0;

	virtual void Finalize(int numSamplers) = 0;
};

//
// BuildRHIConstantBuffer
//
class BuildRHIConstantBuffer
{
public:
	virtual void UpdateBuffer(void *data, int size, int offset) = 0;
};



//
// BuildRHIUIVertex
//
__declspec(align(16)) struct BuildRHIUIVertex
{
	float X, Y, Z, W;		
	float U, V, U1, U2;
};

#define MAX_RENDER_TARGETS 8

//
// BuildRHI
//
class BuildRHI
{
public:
	static void Init();

	// Sets the image for the current context.
	static void SetImageForContext(int rootIndex, const BuildRHITexture *image);

	// Direct3D 12 pso not used.
	static void SetPSOForContext(class GraphicsContext& Context, BuildRHIPipelineStateObject *pso);

	// Returns the RHI image format.
	static BuildRHITextureFormat GetRHITextureFormat(BuildImageFormat format);

	// Returns the number of bits for a format.
	static int GetImageBitsFromTextureFormat(BuildRHITextureFormat format);

	// Allocates a PSO, not used.
	static BuildRHIPipelineStateObject *CreatePipelineStateObject();

	// Allocates a shader object.
	static BuildRHIShader *AllocateShaderObject();

	// Loads in a texture from memory.
	static const BuildRHITexture* LoadTextureFromMemory(const std::wstring &textureName, size_t Width, size_t Height, BuildRHITextureFormat Format, const void* InitData, bool allowCPUWrites = false, bool allowCPUReads = false, bool isRenderTargetImage = false);

	// Not used.
	static const BuildRHITexture* LoadTexture3DFromMemory(const std::wstring &textureName, size_t Width, size_t Height, size_t Depth, BuildRHITextureFormat Format, const void* InitData, bool allowCPUWrites = false);

	// Sets the current shader.
	static void SetShader(BuildRHIShader *shader);

	// Sets the constant buffer.
	static void SetConstantBuffer(int index, BuildRHIConstantBuffer *constantBuffer, bool bindToVertexShader = true, bool bindToPixelShader = false);

	// Draws a quad.
	static void DrawUnoptimized2DQuad( BuildRHIUIVertex *vertexes);

	// Allocates a RHI mesh.
	static BuildRHIMesh *AllocateRHIMesh(int vertexSize, int numVertexes, void * initialData, bool isDynamic);

	// Updates a RHI mesh.
	static void UpdateRHIMesh(BuildRHIMesh *mesh, int startVertex, int vertexSize, int numVertexes, void *initialData);

	// Allocates mesh index buffers.
	static void AllocateRHIMeshIndexes(BuildRHIMesh *mesh, int numIndexes, void * initialData, bool isDynamic);

	// Sets the index buffer to a mesh.
	static void SetRHIMeshIndexBuffer(BuildRHIMesh *mesh, BuildRHIMesh *parentMesh);

	// Allocates a constant buffer.
	static BuildRHIConstantBuffer *AllocateRHIConstantBuffer(int size, void *initialData);

	// Draws a quad without indexes.
	static void DrawUnoptimizedQuad(BuildRHIShader *shader, BuildRHIMesh *mesh, int startVertex, int numVertexes);

	// Draws a indexed quad.
	static void DrawIndexedQuad(BuildRHIShader *shader, BuildRHIMesh *mesh, int startVertex, int startIndex, int numIndexes);

	// Toggles write to depth buffer.
	static void SetDepthEnable(bool depthEnable);

	// Allocates a render target
	static BuildRHIRenderTarget *AllocateRHIRenderTarget(BuildRHITexture *diffuseTexture, BuildRHITexture *depthTexture, BuildRHITexture *stencilTexture);

	// Binds a render target.
	static void BindRenderTarget(BuildRHIRenderTarget *renderTarget);

	// Copies a image to another image.
	static void CopyImageToAnotherImage(const BuildRHITexture *src, const BuildRHITexture *dst, int x, int y, int width, int height);

	// Reads back pixels from a image(assumes the caller allocated enough memory for the copy :) ).
	static void ReadBackPixelsFromImage(const BuildRHITexture *image, byte *buffer);

	// Allocates a occlusion query.
	static BuildRHIGPUOcclusionQuery *AllocateOcclusionQuery();

	// Allocates a performance counter.
	static BuildRHIGPUPerformanceCounter *AllocatePerformanceCounter();
};

extern BuildRHIInputElementDesc *ui_VertexElementDescriptor;
extern BuildRHIInputElementDesc *world_VertexElementDescriptor;
extern BuildRHI rhi;
