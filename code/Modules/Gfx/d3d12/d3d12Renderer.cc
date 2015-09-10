//------------------------------------------------------------------------------
//  d3d12Renderer.cc
//------------------------------------------------------------------------------
#include "Pre.h"
#include "d3d12Renderer.h"
#include "d3d12_impl.h"
#include "d3d12Types.h"
#include "Gfx/Core/displayMgr.h"
#include "Gfx/Resource/gfxResourceContainer.h"
#include <glm/gtc/type_ptr.hpp>

namespace Oryol {
namespace _priv {

//------------------------------------------------------------------------------
d3d12Renderer::d3d12Renderer() :
d3d12Device(nullptr),
d3d12CommandQueue(nullptr),
d3d12CommandAllocator(nullptr),
d3d12CommandList(nullptr),
d3d12RootSignature(nullptr),
frameIndex(0),
curFrameRotateIndex(0),
valid(false),
rtValid(false),
curRenderTarget(nullptr),
curDrawState(nullptr),
d3d12Fence(nullptr),
fenceEvent(nullptr),
d3d12DepthStencil(nullptr),
d3d12RTVHeap(nullptr),
d3d12DSVHeap(nullptr),
rtvDescriptorSize(0),
curBackBufferIndex(0),
samplerDescHeap(nullptr),
curConstantBufferOffset(0) {
    this->d3d12RenderTargets.Fill(nullptr);
}

//------------------------------------------------------------------------------
d3d12Renderer::~d3d12Renderer() {
    o_assert_dbg(!this->valid);
}

//------------------------------------------------------------------------------
void
d3d12Renderer::setup(const GfxSetup& setup, const gfxPointers& ptrs) {
    o_assert_dbg(!this->valid);
    o_assert_dbg(ptrs.displayMgr->d3d12Device);
    o_assert_dbg(ptrs.displayMgr->d3d12CommandQueue);
    o_assert_dbg(0 == this->frameIndex);

    this->valid = true;
    this->gfxSetup = setup;
    this->pointers = ptrs;
    this->d3d12Device = ptrs.displayMgr->d3d12Device;
    this->d3d12CommandQueue = ptrs.displayMgr->d3d12CommandQueue;

    this->createCommandAllocator();
    this->createCommandList();
    this->createFrameSyncObjects();
    this->createDefaultRenderTargets(setup.Width, setup.Height);
    this->createRootSignature();
    this->createFrameResources(setup.GlobalUniformBufferSize, setup.MaxDrawCallsPerFrame);
    this->createSamplerDescriptorHeap();

    // set the root signature for the first frame
    this->d3d12CommandList->SetGraphicsRootSignature(this->d3d12RootSignature);

    this->frameIndex = 1;
}

//------------------------------------------------------------------------------
void
d3d12Renderer::discard() {
    o_assert_dbg(this->valid);
    o_assert_dbg(this->d3d12CommandAllocator);
    o_assert_dbg(this->d3d12CommandList);
    o_assert_dbg(this->d3d12Fence);
    o_assert_dbg(this->fenceEvent);

    // need to wait that the GPU is done before destroying objects
    this->d3d12CommandList->Close();
    this->frameSync();

    this->destroySamplerDescriptorHeap();
    this->destroyFrameResources();
    this->destroyRootSignature();
    this->destroyDefaultRenderTargets();
    this->destroyFrameSyncObjects();
    this->destroyCommandList();
    this->destroyCommandAllocator();
    this->d3d12Allocator.DestroyAll();
    this->d3d12CommandQueue = nullptr;
    this->d3d12Device = nullptr;
    
    this->pointers = gfxPointers();
    this->valid = false;
}

//------------------------------------------------------------------------------
void
d3d12Renderer::createCommandAllocator() {
    o_assert_dbg(this->d3d12Device);
    o_assert_dbg(nullptr == this->d3d12CommandAllocator);

    // FIXME: I think we need d3d12Config::NumFrames allocators,
    // and rotate through them so that a new command list can
    // start recording before the previous is finished
    HRESULT hr = this->d3d12Device->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        __uuidof(ID3D12CommandAllocator),
        (void**)&this->d3d12CommandAllocator);
    o_assert(SUCCEEDED(hr) && this->d3d12CommandAllocator);
}

//------------------------------------------------------------------------------
void
d3d12Renderer::destroyCommandAllocator() {
    if (this->d3d12CommandAllocator) {
        this->d3d12CommandAllocator->Release();
        this->d3d12CommandAllocator = nullptr;
    }
}

//------------------------------------------------------------------------------
void
d3d12Renderer::createCommandList() {
    o_assert_dbg(this->d3d12Device);
    o_assert_dbg(this->d3d12CommandAllocator);
    o_assert_dbg(nullptr == this->d3d12CommandList);

    HRESULT hr = this->d3d12Device->CreateCommandList(
        0,                                  // nodeMask
        D3D12_COMMAND_LIST_TYPE_DIRECT,     // type
        this->d3d12CommandAllocator,        // pCommandAllocator
        nullptr,    // pInitialState (FIXME: the docs say this must be a valid pipeline state, but the samples use nullptr?)
        __uuidof(ID3D12CommandList),
        (void**)&this->d3d12CommandList);
    o_assert(SUCCEEDED(hr) && this->d3d12CommandList);
}

//------------------------------------------------------------------------------
void
d3d12Renderer::destroyCommandList() {
    if (this->d3d12CommandList) {
        this->d3d12CommandList->Release();
        this->d3d12CommandList = nullptr;
    }
}

//------------------------------------------------------------------------------
void
d3d12Renderer::createFrameSyncObjects() {
    o_assert_dbg(this->d3d12Device);
    o_assert_dbg(nullptr == this->d3d12Fence);
    o_assert_dbg(nullptr == this->fenceEvent);
    
    HRESULT hr = this->d3d12Device->CreateFence(
        0,                                  // InitialValue
        D3D12_FENCE_FLAG_NONE,              // Flags
        __uuidof(ID3D12Fence),
        (void**)&this->d3d12Fence);
    o_assert(SUCCEEDED(hr) && this->d3d12Fence);

    this->fenceEvent = ::CreateEventEx(nullptr, FALSE, FALSE, EVENT_ALL_ACCESS);
    o_assert(nullptr != this->fenceEvent);
}

//------------------------------------------------------------------------------
void
d3d12Renderer::destroyFrameSyncObjects() {
    if (this->fenceEvent) {
        ::CloseHandle(this->fenceEvent);
        this->fenceEvent = nullptr;
    }
    if (this->d3d12Fence) {
        this->d3d12Fence->Release();
        this->d3d12Fence = nullptr;
    }
}

//------------------------------------------------------------------------------
void
d3d12Renderer::createDefaultRenderTargets(int width, int height) {
    o_assert_dbg(nullptr == this->d3d12RTVHeap);
    o_assert_dbg(nullptr == this->d3d12DSVHeap);
    o_assert_dbg(nullptr == this->d3d12DepthStencil);
    o_assert_dbg(this->pointers.displayMgr->dxgiSwapChain);
    HRESULT hr;

    // create a render-target-view heap with NumFrames entries
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc;
    Memory::Clear(&rtvHeapDesc, sizeof(rtvHeapDesc));
    rtvHeapDesc.NumDescriptors = d3d12Config::NumFrames;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    hr = this->d3d12Device->CreateDescriptorHeap(&rtvHeapDesc, __uuidof(ID3D12DescriptorHeap), (void**)&this->d3d12RTVHeap);
    o_assert(SUCCEEDED(hr) && this->d3d12RTVHeap);
    this->rtvDescriptorSize = this->d3d12Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    o_assert(this->rtvDescriptorSize > 0);

    // create NumFrames render-target-views from the render-target-view-heap
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = this->d3d12RTVHeap->GetCPUDescriptorHandleForHeapStart();
    for (int i = 0; i < d3d12Config::NumFrames; i++) {
        o_assert_dbg(nullptr == this->d3d12RenderTargets[i]);
        hr = this->pointers.displayMgr->dxgiSwapChain->GetBuffer(i, __uuidof(ID3D12Resource), (void**)&this->d3d12RenderTargets[i]);
        o_assert_dbg(SUCCEEDED(hr) && this->d3d12RenderTargets[i]);
        this->d3d12Device->CreateRenderTargetView(this->d3d12RenderTargets[i], nullptr, rtvHandle);
        rtvHandle.ptr += this->rtvDescriptorSize;
    }
    this->curBackBufferIndex = this->pointers.displayMgr->dxgiSwapChain->GetCurrentBackBufferIndex();

    // create a single depth-stencil buffer
    if (PixelFormat::None != this->gfxSetup.DepthFormat) {

        D3D12_HEAP_PROPERTIES dsHeapProps;
        Memory::Clear(&dsHeapProps, sizeof(dsHeapProps));
        dsHeapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC dsDesc;
        Memory::Clear(&dsDesc, sizeof(dsDesc));
        dsDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        dsDesc.Alignment = 0;
        dsDesc.Width = width;
        dsDesc.Height = height;
        dsDesc.DepthOrArraySize = 1;
        dsDesc.MipLevels = 0;
        dsDesc.Format = d3d12Types::asRenderTargetFormat(this->gfxSetup.DepthFormat);
        dsDesc.SampleDesc.Count = this->gfxSetup.SampleCount;
        dsDesc.SampleDesc.Quality = 0;
        o_warn("FIXME: d3d12Renderer::createDefaultRenderTargets: Sample Quality!\n");
        dsDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        dsDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

        D3D12_CLEAR_VALUE clearValue;
        Memory::Clear(&clearValue, sizeof(clearValue));
        clearValue.Format = d3d12Types::asRenderTargetFormat(this->gfxSetup.DepthFormat);
        clearValue.DepthStencil.Depth = 1.0f;
        clearValue.DepthStencil.Stencil = 0;

        hr = this->d3d12Device->CreateCommittedResource(
            &dsHeapProps,                       // pHeapProperties
            D3D12_HEAP_FLAG_NONE,               // HeapFlags
            &dsDesc,                            // pResourceDesc,
            D3D12_RESOURCE_STATE_DEPTH_WRITE,   // InitialResourceState
            &clearValue,                        // pOptimizedClearValue
            __uuidof(ID3D12Resource),           // riidResource
            (void**)&this->d3d12DepthStencil);  // ppvResource
        o_assert(SUCCEEDED(hr) && this->d3d12DepthStencil);

        D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc;
        Memory::Clear(&dsvHeapDesc, sizeof(dsvHeapDesc));
        dsvHeapDesc.NumDescriptors = 1;
        dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        hr = this->d3d12Device->CreateDescriptorHeap(&dsvHeapDesc, __uuidof(ID3D12DescriptorHeap), (void**)&this->d3d12DSVHeap);
        o_assert(SUCCEEDED(hr) && this->d3d12DSVHeap);

        D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc;
        Memory::Clear(&dsvDesc, sizeof(dsvDesc));
        dsvDesc.Format = d3d12Types::asRenderTargetFormat(this->gfxSetup.DepthFormat);
        dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        dsvDesc.Flags = D3D12_DSV_FLAG_NONE;

        this->d3d12Device->CreateDepthStencilView(this->d3d12DepthStencil, &dsvDesc, this->d3d12DSVHeap->GetCPUDescriptorHandleForHeapStart());
    }
}

//------------------------------------------------------------------------------
void
d3d12Renderer::destroyDefaultRenderTargets() {

    if (this->d3d12DepthStencil) {
        this->d3d12DepthStencil->Release();
        this->d3d12DepthStencil = nullptr;
    }
    for (auto& rt : this->d3d12RenderTargets) {
        if (rt) {
            rt->Release();
            rt = nullptr;
        }
    }
    if (this->d3d12DSVHeap) {
        this->d3d12DSVHeap->Release();
        this->d3d12DSVHeap = nullptr;
    }
    if (this->d3d12RTVHeap) {
        this->d3d12RTVHeap->Release();
        this->d3d12RTVHeap = nullptr;
    }
}

//------------------------------------------------------------------------------
void
d3d12Renderer::createRootSignature() {
    o_assert_dbg(nullptr == this->d3d12RootSignature);
    o_assert_dbg(this->d3d12Device);
    HRESULT hr;

    // currently, all constant buffers can change between draw calls,
    // so they are set directly in the root signature, textures and
    // samplers can only change between draw states, so they are set via descriptors


    // the root signature describes number of constant buffers,
    // textures and samplers that can be bound to the vertex- and pixel-shader
    StaticArray<D3D12_DESCRIPTOR_RANGE, 2> vsRanges;
    Memory::Clear(&vsRanges[0], sizeof(vsRanges[0]) * vsRanges.Size());
    StaticArray<D3D12_DESCRIPTOR_RANGE, 2> psRanges;
    Memory::Clear(&psRanges[0], sizeof(psRanges[0]) * psRanges.Size());

    vsRanges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    vsRanges[0].NumDescriptors = d3d12Config::MaxNumVSTextures;
    vsRanges[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    vsRanges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
    vsRanges[1].NumDescriptors = d3d12Config::MaxNumVSTextures;
    vsRanges[1].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    psRanges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    psRanges[0].NumDescriptors = d3d12Config::MaxNumPSTextures;
    psRanges[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    psRanges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
    psRanges[1].NumDescriptors = d3d12Config::MaxNumPSTextures;
    psRanges[1].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    StaticArray<D3D12_ROOT_PARAMETER, NumRootParams> rootParams;
    Memory::Clear(&rootParams[0], sizeof(rootParams[0]) * rootParams.Size());

    // vertex shader samplers
    rootParams[VSSamplers].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[VSSamplers].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    rootParams[VSSamplers].DescriptorTable.pDescriptorRanges = &vsRanges[1];
    rootParams[VSSamplers].DescriptorTable.NumDescriptorRanges = 1;

    // pixel shader samplers
    rootParams[PSSamplers].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[PSSamplers].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParams[PSSamplers].DescriptorTable.pDescriptorRanges = &psRanges[1];
    rootParams[PSSamplers].DescriptorTable.NumDescriptorRanges = 1;

    // vertex shader textures
    rootParams[VSTextures].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[VSTextures].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    rootParams[VSTextures].DescriptorTable.pDescriptorRanges = &vsRanges[0];
    rootParams[VSTextures].DescriptorTable.NumDescriptorRanges = 1;

    // pixel shader textures
    rootParams[PSTextures].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[PSTextures].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParams[PSTextures].DescriptorTable.pDescriptorRanges = &psRanges[0];
    rootParams[PSTextures].DescriptorTable.NumDescriptorRanges = 1;

    // vertex shader constant buffers
    rootParams[VSConstantBuffer0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[VSConstantBuffer0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    rootParams[VSConstantBuffer0].Constants.ShaderRegister = 0;
    rootParams[VSConstantBuffer0].Constants.RegisterSpace = 0;
   
    rootParams[VSConstantBuffer1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[VSConstantBuffer1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    rootParams[VSConstantBuffer1].Constants.ShaderRegister = 1;
    rootParams[VSConstantBuffer1].Constants.RegisterSpace = 0;

    // pixel shader constant buffers
    rootParams[PSConstantBuffer0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[PSConstantBuffer0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParams[PSConstantBuffer0].Constants.ShaderRegister = 0;
    rootParams[PSConstantBuffer0].Constants.RegisterSpace = 0;

    rootParams[PSConstantBuffer1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[PSConstantBuffer1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParams[PSConstantBuffer1].Constants.ShaderRegister = 1;
    rootParams[PSConstantBuffer1].Constants.RegisterSpace = 0;

    D3D12_ROOT_SIGNATURE_DESC rootDesc;
    Memory::Clear(&rootDesc, sizeof(rootDesc));
    rootDesc.NumParameters = rootParams.Size();
    rootDesc.pParameters = &rootParams[0];
    rootDesc.NumStaticSamplers = 0;
    rootDesc.pStaticSamplers = nullptr;
    rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    ID3DBlob* sig = nullptr;
    ID3DBlob* error = nullptr;
    hr = D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &error); 
    if (FAILED(hr)) {
        o_assert(error);
        o_error("d3d12Renderer: Failed to create root signature with: %s\n", error->GetBufferPointer());
    }
    hr = this->d3d12Device->CreateRootSignature(
        0,                                  // NodeMask 
        sig->GetBufferPointer(),            // pBlobWithRootSignature
        sig->GetBufferSize(),               // blobLengthInBytes
        __uuidof(ID3D12RootSignature),      // riid
        (void**)&this->d3d12RootSignature); // ppvRootSignature
    o_assert(SUCCEEDED(hr) && this->d3d12RootSignature);
}

//------------------------------------------------------------------------------
void
d3d12Renderer::destroyRootSignature() {
    if (this->d3d12RootSignature) {
        this->d3d12RootSignature->Release();
        this->d3d12RootSignature = nullptr;
    }
}

//------------------------------------------------------------------------------
void
d3d12Renderer::createSamplerDescriptorHeap() {
    o_assert_dbg(nullptr == this->samplerDescHeap);

    this->samplerDescHeap = this->d3d12Allocator.AllocDescriptorHeap(this->d3d12Device,
        D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER,
        d3d12Config::MaxNumSamplers);
    o_assert_dbg(this->samplerDescHeap);
}

//------------------------------------------------------------------------------
void
d3d12Renderer::destroySamplerDescriptorHeap() {
    if (this->samplerDescHeap) {
        this->d3d12Allocator.ReleaseDeferred(this->frameIndex, this->samplerDescHeap);
        this->samplerDescHeap = nullptr;
    }
}

//------------------------------------------------------------------------------
void
d3d12Renderer::createFrameResources(int32 cbSize, int32 maxDrawCallsPerFrame) {
    HRESULT hr;

    this->curFrameRotateIndex = 0;
    this->curConstantBufferOffset = 0;
    for (auto& frameRes : this->d3d12FrameResources) {

        o_assert_dbg(nullptr == frameRes.constantBuffer);
        
        // this is the buffer for shader constants that change between drawcalls,
        // it is placed in its own upload heap, written by the CPU and read by the GPU each frame
        frameRes.constantBuffer = this->d3d12Allocator.AllocUploadBuffer(this->d3d12Device, cbSize);
        o_assert_dbg(frameRes.constantBuffer);
        
        // get the CPU and GPU start address of the constant buffer
        hr = frameRes.constantBuffer->Map(0, nullptr, (void**)&frameRes.cbCpuPtr);
        o_assert(SUCCEEDED(hr));
        frameRes.cbGpuPtr = frameRes.constantBuffer->GetGPUVirtualAddress();
        o_assert_dbg(frameRes.cbGpuPtr);
    }
}

//------------------------------------------------------------------------------
void
d3d12Renderer::destroyFrameResources() {
    for (auto& frameRes : this->d3d12FrameResources) {
        if (frameRes.constantBuffer) {
            frameRes.constantBuffer->Unmap(0, nullptr);
            this->d3d12Allocator.ReleaseDeferred(this->frameIndex, frameRes.constantBuffer);
            frameRes.constantBuffer = nullptr;
            frameRes.cbCpuPtr = nullptr;
            frameRes.cbGpuPtr = 0;
        }
    }
}

//------------------------------------------------------------------------------
bool
d3d12Renderer::isValid() const {
    return this->valid;
}

//------------------------------------------------------------------------------
void
d3d12Renderer::frameSync() {
    // NOTE: this method is called from displayMgr::Present()!
    o_assert_dbg(this->d3d12Fence);
    o_assert_dbg(this->fenceEvent);
    o_assert_dbg(this->d3d12CommandAllocator);
    o_assert_dbg(this->d3d12CommandList);
    HRESULT hr;

    // FIXME: what this actually does is wait for the CURRENT frame to finish
    // that has just been recorded, what we actually need to do is wait for
    // the frame BEFORE to finish, and start recording a new command list
    // while the current one is executing

    // FIXME REMINDER: the d3d12 hello world sample says that waiting for the
    // previous frame to finish is not best practice
    const uint64 fenceValue = this->frameIndex++;
    hr = this->d3d12CommandQueue->Signal(this->d3d12Fence, fenceValue);
    o_assert(SUCCEEDED(hr));

    // wait until previous frame is finished
    if (this->d3d12Fence->GetCompletedValue() < fenceValue) {
        hr = this->d3d12Fence->SetEventOnCompletion(fenceValue, this->fenceEvent);
        ::WaitForSingleObject(this->fenceEvent, INFINITE);
    }

    // reset the command allocator and command list for next frame
    hr = this->d3d12CommandAllocator->Reset();
    o_assert(SUCCEEDED(hr));
    hr = this->d3d12CommandList->Reset(this->d3d12CommandAllocator, nullptr);
    o_assert(SUCCEEDED(hr));

    // lookup new backbuffer index
    this->curBackBufferIndex = this->pointers.displayMgr->dxgiSwapChain->GetCurrentBackBufferIndex();

    // garbage-collect resources
    this->d3d12Allocator.GarbageCollect(this->frameIndex);

    // prepare for next frame
    o_assert_dbg(this->d3d12RootSignature);
    this->d3d12CommandList->SetGraphicsRootSignature(this->d3d12RootSignature);
    this->curConstantBufferOffset = 0;
    if (++this->curFrameRotateIndex >= d3d12Config::NumFrames) {
        this->curFrameRotateIndex = 0;
    }
}

//------------------------------------------------------------------------------
void
d3d12Renderer::commitFrame() {
    o_assert_dbg(this->valid);
    o_assert_dbg(this->d3d12CommandList);

    // transition the default render target back from render target to present state
    const displayMgr* dispMgr = this->pointers.displayMgr;
    D3D12_RESOURCE_BARRIER barrier;
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = this->d3d12RenderTargets[this->curBackBufferIndex];
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    this->d3d12CommandList->ResourceBarrier(1, &barrier);

    // execute the command list, after this, displayMgr::present() is called,
    // which calls Present on the swapchain and waits for the previous
    // frame to finish
    this->d3d12CommandList->Close();
    this->d3d12CommandQueue->ExecuteCommandLists(1, (ID3D12CommandList**) &this->d3d12CommandList);
}

//------------------------------------------------------------------------------
void
d3d12Renderer::resetStateCache() {
    o_warn("d3d12Renderer::resetStateCache()\n");
}

//------------------------------------------------------------------------------
bool
d3d12Renderer::queryFeature(GfxFeature::Code feat) const {
    switch (feat) {
        case GfxFeature::TextureCompressionDXT:
        case GfxFeature::TextureFloat:
        case GfxFeature::Instancing:
        case GfxFeature::OriginTopLeft:
            return true;
        default:
            return false;
    }
}

//------------------------------------------------------------------------------
const DisplayAttrs&
d3d12Renderer::renderTargetAttrs() const {
    o_warn("d3d12Renderer::renderTargetAttrs()\n");
    static DisplayAttrs dummyAttrs;
    return dummyAttrs;
}

//------------------------------------------------------------------------------
void
d3d12Renderer::applyRenderTarget(texture* rt, const ClearState& clearState) {
    o_assert_dbg(this->valid);
    o_assert_dbg(this->d3d12CommandList);
    const displayMgr* dispMgr = this->pointers.displayMgr;

    ID3D12Resource* colorBuffer = nullptr;
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = { 0 };
    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = { 0 };
    D3D12_CPU_DESCRIPTOR_HANDLE* rtvHandlePtr = nullptr;
    D3D12_CPU_DESCRIPTOR_HANDLE* dsvHandlePtr = nullptr; 
    D3D12_RESOURCE_STATES colorStateBefore = (D3D12_RESOURCE_STATES) 0xFFFFFFFF;
    this->invalidateTextureState();
    if (nullptr == rt) {
        this->rtAttrs = dispMgr->GetDisplayAttrs();
        colorBuffer = this->d3d12RenderTargets[this->curBackBufferIndex];
        colorStateBefore = D3D12_RESOURCE_STATE_PRESENT;
        rtvHandle = this->d3d12RTVHeap->GetCPUDescriptorHandleForHeapStart();
        rtvHandle.ptr += this->rtvDescriptorSize * this->curBackBufferIndex;
        rtvHandlePtr = &rtvHandle;
        if (this->d3d12DepthStencil) {
            dsvHandle = this->d3d12DSVHeap->GetCPUDescriptorHandleForHeapStart();
            dsvHandlePtr = &dsvHandle;
        }
    }
    else {
        o_error("FIXME: d3d12Renderer apply offscreen render target!\n");
        // FIXME: the render target could have been a pixel-shader or
        // vertex-shader-texture, d3d12Texture objects must track their current
        // states (hmm does that mean a texture cannot be used as a vertex shader
        // texture and a pixel shader texture at the same time?
    }
    o_assert_dbg(colorBuffer && (colorStateBefore != 0xFFFFFFFF));

    this->curRenderTarget = rt;
    this->rtValid = true;

    // transition color buffer into render target state
    D3D12_RESOURCE_BARRIER barrier;
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = colorBuffer;
    barrier.Transition.StateBefore = colorStateBefore;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    this->d3d12CommandList->ResourceBarrier(1, &barrier);

    // set the render target
    this->d3d12CommandList->OMSetRenderTargets(1, rtvHandlePtr, FALSE, dsvHandlePtr);

    // set viewport and scissor-rect to cover whole screen
    this->applyViewPort(0, 0, this->rtAttrs.FramebufferWidth, this->rtAttrs.FramebufferHeight, true);
    this->applyScissorRect(0, 0, this->rtAttrs.FramebufferWidth, this->rtAttrs.FramebufferHeight, true);

    // perform clear action
    if (clearState.Actions & ClearState::ColorBit) {
        if (rtvHandlePtr) {
            const FLOAT* clearColor = glm::value_ptr(clearState.Color);        
            this->d3d12CommandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
        }
    }
    if (clearState.Actions & ClearState::DepthStencilBits) {
        if (dsvHandlePtr) {
            D3D12_CLEAR_FLAGS d3d12ClearFlags = (D3D12_CLEAR_FLAGS) 0;
            if (clearState.Actions & ClearState::DepthBit) {
                d3d12ClearFlags |= D3D12_CLEAR_FLAG_DEPTH;
            }
            if (clearState.Actions & ClearState::StencilBit) {
                d3d12ClearFlags |= D3D12_CLEAR_FLAG_STENCIL;
            }
            const FLOAT d = clearState.Depth;
            const UINT8 s = clearState.Stencil;
            this->d3d12CommandList->ClearDepthStencilView(dsvHandle, d3d12ClearFlags, d, s, 0, nullptr);
        }
    }
}

//------------------------------------------------------------------------------
void
d3d12Renderer::applyViewPort(int32 x, int32 y, int32 width, int32 height, bool originTopLeft) {
    o_assert_dbg(this->d3d12CommandList);

    D3D12_VIEWPORT vp;
    vp.TopLeftX = (FLOAT)x;
    vp.TopLeftY = (FLOAT)(originTopLeft ? y : (this->rtAttrs.FramebufferHeight - (y + height)));
    vp.Width = (FLOAT)width;
    vp.Height = (FLOAT)height;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    this->d3d12CommandList->RSSetViewports(1, &vp);
}

//------------------------------------------------------------------------------
void
d3d12Renderer::applyScissorRect(int32 x, int32 y, int32 width, int32 height, bool originTopLeft) {
    o_assert_dbg(this->d3d12CommandList);

    D3D12_RECT rect;
    rect.left = x;
    rect.top = originTopLeft ? y : this->rtAttrs.FramebufferHeight - (y + height);
    rect.right = x + width;
    rect.bottom = originTopLeft ? (y + height) : (this->rtAttrs.FramebufferHeight - y);
    this->d3d12CommandList->RSSetScissorRects(1, &rect);
}

//------------------------------------------------------------------------------
void
d3d12Renderer::applyDrawState(drawState* ds) {
    o_assert_dbg(this->d3d12CommandList);

    if (nullptr == ds) {
        // the drawState is pending (dependent resource still loading),
        // invalidate rendering for followup draw calls
        this->curDrawState = nullptr;
        return;
    }
    this->curDrawState = ds;

    o_assert_dbg(ds->d3d12PipelineState);
    o_assert2(ds->Setup.BlendState.ColorFormat == this->rtAttrs.ColorPixelFormat, "ColorFormat in BlendState must match current render target!\n");
    o_assert2(ds->Setup.BlendState.DepthFormat == this->rtAttrs.DepthPixelFormat, "DepthFormat in BlendState must match current render target!\n");
    o_assert2(ds->Setup.RasterizerState.SampleCount == this->rtAttrs.SampleCount, "SampleCount in RasterizerState must match current render target!\n");

    // apply pipeline state (FIXME: only if changed!)
    this->d3d12CommandList->SetPipelineState(ds->d3d12PipelineState);
    
    // apply vertex and index buffers (FIXME: only if changed!)
    D3D12_VERTEX_BUFFER_VIEW vbViews[GfxConfig::MaxNumInputMeshes];
    D3D12_INDEX_BUFFER_VIEW ibView;
    const D3D12_INDEX_BUFFER_VIEW* ibViewPtr = nullptr;
    bool hasIndexBuffer = false;
    for (int i = 0; i < GfxConfig::MaxNumInputMeshes; i++) {
        const mesh* msh = ds->meshes[i];
        if (msh) {
            const mesh::buffer& vb = msh->buffers[mesh::vb];
            o_assert_dbg(vb.d3d12DefaultBuffers[vb.activeSlot]);
            vbViews[i].BufferLocation = vb.d3d12DefaultBuffers[vb.activeSlot]->GetGPUVirtualAddress();
            vbViews[i].SizeInBytes = msh->vertexBufferAttrs.ByteSize();
            vbViews[i].StrideInBytes = msh->vertexBufferAttrs.Layout.ByteSize();
            if ((0 == i) && (IndexType::None != msh->indexBufferAttrs.Type)) {
                const mesh::buffer& ib = msh->buffers[mesh::ib];
                o_assert_dbg(ib.d3d12DefaultBuffers[ib.activeSlot]);
                ibView.BufferLocation = ib.d3d12DefaultBuffers[ib.activeSlot]->GetGPUVirtualAddress();
                ibView.SizeInBytes = msh->indexBufferAttrs.ByteSize();
                ibView.Format = d3d12Types::asIndexType(msh->indexBufferAttrs.Type);
                ibViewPtr = &ibView;
            }
        }
        else {
            vbViews[i].BufferLocation = 0;
            vbViews[i].SizeInBytes = 0;
            vbViews[i].StrideInBytes = 0;
        }
    }
    this->d3d12CommandList->IASetVertexBuffers(0, GfxConfig::MaxNumInputMeshes, vbViews);
    this->d3d12CommandList->IASetIndexBuffer(ibViewPtr);
    this->d3d12CommandList->IASetPrimitiveTopology(ds->meshes[0]->d3d12PrimTopology);
    this->d3d12CommandList->OMSetBlendFactor(glm::value_ptr(ds->Setup.BlendColor));
}

//------------------------------------------------------------------------------
void
d3d12Renderer::applyUniformBlock(int32 blockIndex, int64 layoutHash, const uint8* ptr, int32 byteSize) {
    o_assert_dbg(this->d3d12CommandList);

    if (nullptr == this->curDrawState) {
        return;
    }

    // get the uniform layout for this uniform block
    const shader* shd = this->curDrawState->shd;
    o_assert_dbg(shd);
    const UniformLayout& layout = shd->Setup.UniformBlockLayout(blockIndex);

    // check whether the provided struct is type-compatible with the uniform layout
    o_assert2(layout.TypeHash == layoutHash, "incompatible uniform block!\n");

    // FIXME: textures should be set in applyDrawState, not applyUniformBlock, 
    const ShaderType::Code bindShaderStage = shd->getUniformBlockShaderStage(blockIndex);
    const int32 bindSlotIndex = shd->getUniformBlockBindSlotIndex(blockIndex);
    const uint8* uBufferPtr = nullptr;
    const int numComps = layout.NumComponents();
    for (int compIndex = 0; compIndex < numComps; compIndex++) {
        const auto& comp = layout.ComponentAt(compIndex);
        if (comp.Type == UniformType::Texture) {
            // FIXME: skip texture stuff 
        }
        else {
            // found start of cbuffer data
            uBufferPtr = ptr + layout.ComponentByteOffset(compIndex);
            break;
        }
    }

    // copy uniform data into global constant buffer
    if (nullptr != uBufferPtr) {
        const int32 uniformSize = layout.ByteSizeWithoutTextures();
        o_assert2((this->curConstantBufferOffset + uniformSize) <= this->gfxSetup.GlobalUniformBufferSize, "Global constant buffer exhausted!");
        const auto& frameRes = this->d3d12FrameResources[this->curFrameRotateIndex];
        uint8* dstPtr = frameRes.cbCpuPtr + this->curConstantBufferOffset;
        std::memcpy(dstPtr, uBufferPtr, uniformSize);

        // get the GPU address of current constant buffer location and set
        // the constant buffer location directly in the root signature
        // the root parameter index can be: VSConstantBuffer0, VSConstantBuffer1, 
        // PSConstantBuffer0 or PSConstantBuffer1
        const uint64 cbGpuPtr = frameRes.cbGpuPtr + this->curConstantBufferOffset;
        static const int maxHighFreqCBs = 2;    // FIXME: '2' completely pulled out of ass to keep root sig small-ish
        o_assert_dbg(bindSlotIndex < maxHighFreqCBs);
        UINT rootParamIndex = ShaderType::VertexShader == bindShaderStage ? VSConstantBuffer0 : PSConstantBuffer0;
        rootParamIndex += bindSlotIndex * maxHighFreqCBs;
        this->d3d12CommandList->SetGraphicsRootConstantBufferView(rootParamIndex, cbGpuPtr);

        // advance constant buffer offset (must be multiple of 256)
        this->curConstantBufferOffset = Memory::RoundUp(this->curConstantBufferOffset + uniformSize, 256);
    }
}

//------------------------------------------------------------------------------
void
d3d12Renderer::draw(const PrimitiveGroup& primGroup) {
    o_assert_dbg(this->d3d12CommandList);
    o_assert2_dbg(this->rtValid, "No render target set!\n");
    if (nullptr == this->curDrawState) {
        return;
    }
    const IndexType::Code indexType = this->curDrawState->meshes[0]->indexBufferAttrs.Type;
    if (indexType != IndexType::None) {
        this->d3d12CommandList->DrawIndexedInstanced(
            primGroup.NumElements,  // IndexCountPerInstance
            1,                      // InstanceCount
            primGroup.BaseElement,  // StartIndexLocation
            0,                      // BaseVertexLocation
            0);                     // StartInstanceLocation
    }
    else {
        this->d3d12CommandList->DrawInstanced(
            primGroup.NumElements,  // VertexCountPerInstance
            1,                      // InstanceCount
            primGroup.BaseElement,  // StartVertexLocation
            0);                     // StartInstanceLocation
    }
}

//------------------------------------------------------------------------------
void
d3d12Renderer::draw(int32 primGroupIndex) {
    o_assert_dbg(this->valid);
    if (nullptr == this->curDrawState) {
        return;
    }
    o_assert_dbg(this->curDrawState->meshes[0]);
    if (primGroupIndex >= this->curDrawState->meshes[0]->numPrimGroups) {
        // this may happen if trying to render a placeholder which doesn't
        // have as many materials as the original mesh, anyway, this isn't
        // a serious error
        return;
    }
    const PrimitiveGroup& primGroup = this->curDrawState->meshes[0]->primGroups[primGroupIndex];
    this->draw(primGroup);
}

//------------------------------------------------------------------------------
void
d3d12Renderer::drawInstanced(const PrimitiveGroup& primGroup, int32 numInstances) {
    o_warn("d3d12Renderer::drawInstanced()\n");
}

//------------------------------------------------------------------------------
void
d3d12Renderer::drawInstanced(int32 primGroupIndex, int32 numInstances) {
    o_warn("d3d12Renderer::drawInstanced()\n");
}

//------------------------------------------------------------------------------
void
d3d12Renderer::updateVertices(mesh* msh, const void* data, int32 numBytes) {
    o_warn("d3d12Renderer::updateVertices()\n");
}

//------------------------------------------------------------------------------
void
d3d12Renderer::updateIndices(mesh* msh, const void* data, int32 numBytes) {
    o_warn("d3d12Renderer::updateIndices()\n");
}

//------------------------------------------------------------------------------
void
d3d12Renderer::readPixels(void* buf, int32 bufNumBytes) {
    o_warn("d3d12Renderer::readPixels()\n");
}

//------------------------------------------------------------------------------
void
d3d12Renderer::invalidateMeshState() {
    o_warn("d3d12Renderer::invalidateMeshState()\n");
}

//------------------------------------------------------------------------------
void
d3d12Renderer::invalidateShaderState() {
    o_warn("d3d12Renderer::invalidateShaderState()\n");
}

//------------------------------------------------------------------------------
void
d3d12Renderer::invalidateDrawState() {
    o_warn("d3d12Renderer::invalidateDrawState()\n");
}

//------------------------------------------------------------------------------
void
d3d12Renderer::invalidateTextureState() {
    o_warn("d3d12Renderer::invalidateTextureState()\n");
}

} // namespace _priv
} // namespace Oryol
