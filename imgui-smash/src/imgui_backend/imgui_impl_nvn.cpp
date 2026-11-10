#include "imgui_impl_nvn.hpp"
#include "DrawSlots.h"
#include "helpers.h"
#include "imgui_hid_mappings.h"
#include "logger/Logger.hpp"
#include <cmath>

#include "nn/hid.h"

#include "helpers/InputHelper.h"
#include "helpers/memoryHelper.h"
#include "MemoryPoolMaker.h"
#include "command_memory_reserve.hpp"
#include "font_setup.hpp"
#include "imgui_backend_config.h"
#include "imgui_smash_font_stats.h"
#include "shared_fonts.hpp"

#include <atomic>
#include <cstdint>

#include "imgui_shader.h"

#define UBOSIZE 0x1000

typedef float Matrix44f[4][4];
static_assert(sizeof(Matrix44f) == 64, "projection matrix update is 64 bytes");

// orthographic matrix used for shader
static Matrix44f projMatrix = {};

// orthographic projection matrix calc from glm
void orthoRH_ZO(Matrix44f &Result, float left, float right, float bottom, float top, float zNear, float zFar) {

  Result[0][0] = static_cast<float>(2) / (right - left);
  Result[0][1] = 0.0f;
  Result[0][2] = 0.0f;
  Result[0][3] = 0.0f;

  Result[1][0] = 0.0f;
  Result[1][1] = static_cast<float>(2) / (top - bottom);
  Result[1][2] = 0.0f;
  Result[1][3] = 0.0f;

  Result[2][0] = 0.0f;
  Result[2][1] = 0.0f;
  Result[2][2] = -static_cast<float>(1) / (zFar - zNear);
  Result[2][3] = 0.0f;

  Result[3][0] = -(right + left) / (right - left);
  Result[3][1] = -(top + bottom) / (top - bottom);
  Result[3][2] = -zNear / (zFar - zNear);
  Result[3][3] = 1.0f;
}

namespace ImguiNvnBackend {
  void loadIni();
  void saveIni();

  MemoryBuffer *checkedMemoryBuffer(size_t size) {
    void *storage = ImGui::MemAlloc(sizeof(MemoryBuffer));
    if (storage == nullptr) {
      return nullptr;
    }
    return new (ImNewWrapper(), storage) MemoryBuffer(size);
  }

  MemoryBuffer *checkedMemoryBuffer(size_t size, nvn::MemoryPoolFlags flags) {
    void *storage = ImGui::MemAlloc(sizeof(MemoryBuffer));
    if (storage == nullptr) {
      return nullptr;
    }
    return new (ImNewWrapper(), storage) MemoryBuffer(size, flags);
  }

  MemoryBuffer *checkedMemoryBuffer(size_t size, void *bytes, nvn::MemoryPoolFlags flags) {
    void *storage = ImGui::MemAlloc(sizeof(MemoryBuffer));
    if (storage == nullptr) {
      return nullptr;
    }
    return new (ImNewWrapper(), storage) MemoryBuffer(size, bytes, flags);
  }

  // doesnt get used anymore really, as back when it was needed i had a simplified shader to test with, but now I just test with the actual imgui shader
  void initTestShader() {

    auto bd = getBackendData();
    bd->testShaderBinary = ImguiShaderCompiler::CompileShader("test");

    bd->testShaderBuffer = checkedMemoryBuffer(bd->testShaderBinary.size, bd->testShaderBinary.ptr,
                                               nvn::MemoryPoolFlags::CPU_UNCACHED |
                                               nvn::MemoryPoolFlags::GPU_CACHED |
                                               nvn::MemoryPoolFlags::SHADER_CODE);

    SMASH_ASSERT(bd->testShaderBuffer != nullptr && bd->testShaderBuffer->IsBufferReady(),
                 "Shader Buffer was not ready! unable to continue.");

    BinaryHeader offsetData = BinaryHeader((u32 *) bd->testShaderBinary.ptr);

    nvn::BufferAddress addr = bd->testShaderBuffer->GetBufferAddress();

    nvn::ShaderData &vertShaderData = bd->testShaderDatas[0];
    vertShaderData.data = addr + offsetData.mVertexDataOffset;
    vertShaderData.control = bd->testShaderBinary.ptr + offsetData.mVertexControlOffset;

    nvn::ShaderData &fragShaderData = bd->testShaderDatas[1];
    fragShaderData.data = addr + offsetData.mFragmentDataOffset;
    fragShaderData.control = bd->testShaderBinary.ptr + offsetData.mFragmentControlOffset;

    SMASH_ASSERT(bd->testShader.Initialize(bd->device), "Unable to Init Program!");
    SMASH_ASSERT(bd->testShader.SetShaders(2, bd->testShaderDatas), "Unable to Set Shaders!");

    Logger::log("Test Shader Setup.\n");

  }

  // neat tool to cycle through all loaded textures in a texture pool
  int texIDSelector() {
    {

      static int curId = 256;
      static int downCounter = 0;
      static int upCounter = 0;

      if (InputHelper::isButtonPress(nn::hid::NpadButton::Left)) {
        curId--;
        Logger::log("ID: %d\n", curId);
      } else if (InputHelper::isButtonHold(nn::hid::NpadButton::Left)) {

        downCounter++;
        if (downCounter > 30) {
          curId--;
          Logger::log("ID: %d\n", curId);
        }
      } else {
        downCounter = 0;
      }

      if (InputHelper::isButtonPress(nn::hid::NpadButton::Right)) {
        curId++;
        Logger::log("ID: %d\n", curId);
      } else if (InputHelper::isButtonHold(nn::hid::NpadButton::Right)) {

        upCounter++;
        if (upCounter > 30) {
          curId++;
          Logger::log("ID: %d\n", curId);
        }
      } else {
        upCounter = 0;
      }

      /* fun values with bd->device->GetTextureHandle(curId, 256):
       * 282 = Window Texture
       * 393 = Some sort of render pass (shadow?) nvm it just seems to be the first occurrence of many more textures like it
       * 257 = debug font texture
       */

      return curId;
    }
  }

  // places ImDrawVerts, starting at startIndex, that use the x,y,width, and height values to define vertex coords
  void createQuad(ImDrawVert *verts, int startIndex, int x, int y, int width, int height, ImU32 col) {

    float minXVal = x;
    float maxXVal = x + width;
    float minYVal = y; // 400
    float maxYVal = y + height; // 400

    // top left
    ImDrawVert p1 = {
        .pos = ImVec2(minXVal, minYVal),
        .uv = ImVec2(0.0f, 0.0f),
        .col = col
    };
    // top right
    ImDrawVert p2 = {
        .pos = ImVec2(minXVal, maxYVal),
        .uv = ImVec2(0.0f, 1.0f),
        .col = col
    };
    // bottom left
    ImDrawVert p3 = {
        .pos = ImVec2(maxXVal, minYVal),
        .uv = ImVec2(1.0f, 0.0f),
        .col = col
    };
    // bottom right
    ImDrawVert p4 = {
        .pos = ImVec2(maxXVal, maxYVal),
        .uv = ImVec2(1.0f, 1.0f),
        .col = col
    };

    verts[startIndex] = p4;
    verts[startIndex + 1] = p2;
    verts[startIndex + 2] = p1;

    verts[startIndex + 3] = p1;
    verts[startIndex + 4] = p3;
    verts[startIndex + 5] = p4;
  }

  // this function is mainly what I used to debug the rendering of ImGui, so code is a bit messier
  void renderTestShader(ImDrawData *drawData) {

    auto bd = getBackendData();
    auto io = ImGui::GetIO();

    constexpr int triVertCount = 3;
    constexpr int quadVertCount = triVertCount * 2;

    int quadCount = 1; // modify to reflect how many quads need to be drawn per frame

    int pointCount = quadVertCount * quadCount;

    size_t totalVtxSize = pointCount * sizeof(ImDrawVert);
    if (!bd->vtxBuffer || bd->vtxBuffer->GetPoolSize() < totalVtxSize) {
      if (bd->vtxBuffer) {
        bd->vtxBuffer->Finalize();
        IM_FREE(bd->vtxBuffer);
      }
      bd->vtxBuffer = checkedMemoryBuffer(totalVtxSize);
      Logger::log("(Re)sized Vertex Buffer to Size: %d\n", totalVtxSize);
    }

    if (bd->vtxBuffer == nullptr || !bd->vtxBuffer->IsBufferReady()) {
      Logger::log("Cannot Draw Data! Buffers are not Ready.\n");
      return;
    }

    ImDrawVert *verts = (ImDrawVert *) bd->vtxBuffer->GetMemPtr();

    float scale = 3.0f;

    float imageX = 1 * scale; // bd->fontTexture.GetWidth();
    float imageY = 1 * scale; // bd->fontTexture.GetHeight();

    createQuad(verts, 0, (io.DisplaySize.x / 2) - (imageX), (io.DisplaySize.y / 2) - (imageY), imageX, imageY,
               IM_COL32_WHITE);

    bd->cmdBuf->BeginRecording();
    bd->cmdBuf->BindProgram(&bd->shaderProgram, nvn::ShaderStageBits::VERTEX | nvn::ShaderStageBits::FRAGMENT);

    bd->cmdBuf->BindUniformBuffer(nvn::ShaderStage::VERTEX, 0, *bd->uniformMemory, UBOSIZE);
    bd->cmdBuf->UpdateUniformBuffer(*bd->uniformMemory, UBOSIZE, 0, sizeof(projMatrix), &projMatrix);

    bd->cmdBuf->BindVertexBuffer(0, (*bd->vtxBuffer), bd->vtxBuffer->GetPoolSize());

    setRenderStates();

//        bd->cmdBuf->BindTexture(nvn::ShaderStage::FRAGMENT, 0, bd->fontTexHandle);

    bd->cmdBuf->DrawArrays(nvn::DrawPrimitive::TRIANGLES, 0, pointCount);

    auto handle = bd->cmdBuf->EndRecording();
    bd->queue->SubmitCommands(1, &handle);
  }

// backend impl

  NvnBackendData *getBackendData() {
    NvnBackendData *result = ImGui::GetCurrentContext() ? (NvnBackendData *) ImGui::GetIO().BackendRendererUserData
                                                        : nullptr;
    SMASH_ASSERT(result, "Backend has not been initialized!");
    return result;
  }

  bool createShaders() {

    auto bd = getBackendData();

    if (!IMGUI_SMASH_FORCE_PRECOMPILED_SHADERS && ImguiShaderCompiler::CheckIsValidVersion(bd->device)) {
      Logger::log("GLSLC compiler can be used!\n");

      ImguiShaderCompiler::InitializeCompiler();

      bd->imguiShaderBinary = ImguiShaderCompiler::CompileShader("imgui");

    } else {
      Logger::log("Unable to compile shaders at runtime. falling back to pre-compiled shaders.\n");

      void *buf = Mem::Allocate(romfs_ShaderData_imgui_bin_len);
      SMASH_ASSERT(buf, "Failed to Allocate Buffer! File Size: %d", romfs_ShaderData_imgui_bin_len);
      memcpy(buf, romfs_ShaderData_imgui_bin, romfs_ShaderData_imgui_bin_len);

      Logger::log("Loaded shader\n");

      bd->imguiShaderBinary.size = romfs_ShaderData_imgui_bin_len;
      bd->imguiShaderBinary.ptr = (u8 *) buf;
    }

    if (bd->imguiShaderBinary.size > 0) {
      return true;
    }

    return false;
  }

  namespace {

  // Backend storage of one ImGui texture. TexID points at `handle`, which renderDrawData binds per draw command.
  struct NvnTexture {
    nvn::Texture texture;
    nvn::MemoryPool pool;
    void *storage = nullptr;
    size_t poolBytes = 0;
    nvn::TextureHandle handle = 0;
    int descriptor = -1;
    bool poolLive = false;
    bool textureLive = false;
    bool retiring = false;
    uint64_t retireAfter = 0;
  };

  std::atomic<uint32_t> g_atlasWidth{0};
  std::atomic<uint32_t> g_atlasHeight{0};
  std::atomic<uint32_t> g_atlasBytesPerPixel{0};
  std::atomic<uint32_t> g_texturesLive{0};
  std::atomic<uint64_t> g_pixelBytes{0};
  std::atomic<uint64_t> g_poolBytes{0};
  std::atomic<uint64_t> g_creates{0};
  std::atomic<uint64_t> g_updates{0};
  std::atomic<uint64_t> g_destroys{0};

  void releaseTexture(NvnBackendData *bd, NvnTexture *owned) {
    if (owned->textureLive) {
      owned->texture.Finalize();
    }
    if (owned->poolLive) {
      owned->pool.Finalize();
    }
    if (owned->storage != nullptr) {
      Mem::Deallocate(owned->storage);
    }
    bd->textureIds.release(owned->descriptor);
    IM_DELETE(owned);
  }

  bool createTexture(NvnBackendData *bd, ImTextureData *tex) {
    const bool alpha = tex->Format == ImTextureFormat_Alpha8;
    if ((!alpha && tex->Format != ImTextureFormat_RGBA32) || tex->Width <= 0 || tex->Height <= 0) {
      return false;
    }
    void *memory = ImGui::MemAlloc(sizeof(NvnTexture));
    if (memory == nullptr) {
      return false;
    }
    auto *owned = new (ImNewWrapper(), memory) NvnTexture();
    nvn::TextureBuilder builder;
    builder.SetDefaults()
        .SetDevice(bd->device)
        .SetTarget(nvn::TextureTarget::TARGET_2D)
        .SetFormat(alpha ? nvn::Format::R8 : nvn::Format::RGBA8)
        .SetSize2D(tex->Width, tex->Height);
    if (alpha) {
      // Coverage in R; sampled as white with that alpha, exactly what the RGBA32 atlas held.
      builder.SetSwizzle(nvn::TextureSwizzle::ONE, nvn::TextureSwizzle::ONE, nvn::TextureSwizzle::ONE,
                         nvn::TextureSwizzle::R);
    }
    int pageQuery = 0;
    bd->device->GetInteger(nvn::DeviceInfo::MEMORY_POOL_PAGE_SIZE, &pageQuery);
    const int page = device_alignment(pageQuery, 0x1000);
    owned->poolBytes = texturePoolBytes(builder.GetStorageSize(), builder.GetStorageAlignment(),
                                        static_cast<size_t>(page > 0 ? page : 0x1000));
    owned->storage = owned->poolBytes != 0 ? Mem::AllocateAlign(static_cast<size_t>(page), owned->poolBytes) : nullptr;
    if (owned->storage == nullptr) {
      releaseTexture(bd, owned);
      return false;
    }
    nvn::MemoryPoolBuilder poolBuilder;
    poolBuilder.SetDefaults()
        .SetDevice(bd->device)
        .SetFlags(nvn::MemoryPoolFlags::CPU_UNCACHED | nvn::MemoryPoolFlags::GPU_CACHED)
        .SetStorage(owned->storage, owned->poolBytes);
    owned->poolLive = owned->pool.Initialize(&poolBuilder);
    if (!owned->poolLive) {
      releaseTexture(bd, owned);
      return false;
    }
    builder.SetStorage(&owned->pool, 0);
    owned->textureLive = owned->texture.Initialize(&builder);
    owned->descriptor = owned->textureLive ? bd->textureIds.acquire() : -1;
    if (owned->descriptor < 0) {
      releaseTexture(bd, owned);
      return false;
    }
    bd->texPool.RegisterTexture(owned->descriptor, &owned->texture, nullptr);
    owned->handle = bd->device->GetTextureHandle(owned->descriptor, bd->samplerId);
    tex->SetTexID(static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(&owned->handle)));
    tex->BackendUserData = owned;
    g_creates.fetch_add(1, std::memory_order_relaxed);
    g_texturesLive.fetch_add(1, std::memory_order_relaxed);
    g_poolBytes.fetch_add(owned->poolBytes, std::memory_order_relaxed);
    g_pixelBytes.fetch_add(static_cast<uint64_t>(tex->GetSizeInBytes()), std::memory_order_relaxed);
    g_atlasWidth.store(static_cast<uint32_t>(tex->Width), std::memory_order_relaxed);
    g_atlasHeight.store(static_cast<uint32_t>(tex->Height), std::memory_order_relaxed);
    g_atlasBytesPerPixel.store(static_cast<uint32_t>(tex->BytesPerPixel), std::memory_order_relaxed);
    return true;
  }

  // Writes texels ImGui changed. ImGui only writes regions no earlier draw sampled, so submissions still in flight
  // keep reading what they read; the next recording invalidates the texture cache before it draws.
  void uploadRect(NvnTexture *owned, ImTextureData *tex, int x, int y, int w, int h) {
    if (w <= 0 || h <= 0) {
      return;
    }
    nvn::CopyRegion region = {.xoffset = x, .yoffset = y, .zoffset = 0, .width = w, .height = h, .depth = 1};
    owned->texture.WriteTexelsStrided(nullptr, &region, tex->GetPixelsAt(x, y), tex->GetPitch(), 0);
    owned->texture.FlushTexels(nullptr, &region);
    g_updates.fetch_add(1, std::memory_order_relaxed);
  }

  void destroyTexture(NvnBackendData *bd, ImTextureData *tex) {
    if (auto *owned = static_cast<NvnTexture *>(tex->BackendUserData)) {
      g_texturesLive.fetch_sub(1, std::memory_order_relaxed);
      g_poolBytes.fetch_sub(owned->poolBytes, std::memory_order_relaxed);
      g_pixelBytes.fetch_sub(static_cast<uint64_t>(tex->GetSizeInBytes()), std::memory_order_relaxed);
      g_destroys.fetch_add(1, std::memory_order_relaxed);
      releaseTexture(bd, owned);
    }
    tex->SetTexID(ImTextureID_Invalid);
    tex->BackendUserData = nullptr;
    tex->SetStatus(ImTextureStatus_Destroyed);
  }

  }  // namespace

  bool updateTexture(ImTextureData *tex) {
    auto bd = getBackendData();
    if (tex->Status == ImTextureStatus_WantCreate) {
      if (!createTexture(bd, tex)) {
        Logger::log("Texture create failed: %dx%d format %d.\n", tex->Width, tex->Height, (int) tex->Format);
        return false;
      }
      uploadRect(static_cast<NvnTexture *>(tex->BackendUserData), tex, 0, 0, tex->Width, tex->Height);
      tex->SetStatus(ImTextureStatus_OK);
      bd->texturesTouched = true;
    } else if (tex->Status == ImTextureStatus_WantUpdates) {
      auto *owned = static_cast<NvnTexture *>(tex->BackendUserData);
      if (owned != nullptr) {
        for (const ImTextureRect &rect : tex->Updates) {
          uploadRect(owned, tex, rect.x, rect.y, rect.w, rect.h);
        }
      }
      tex->SetStatus(ImTextureStatus_OK);
      bd->texturesTouched = true;
    } else if (tex->Status == ImTextureStatus_WantDestroy) {
      // ImGui no longer draws with it; wait until every submission recorded before now has completed.
      auto *owned = static_cast<NvnTexture *>(tex->BackendUserData);
      if (owned != nullptr && !owned->retiring) {
        owned->retiring = true;
        owned->retireAfter = ImguiDrawSlots::lastSerial();
      }
      if (owned == nullptr || ImguiDrawSlots::completedThrough(owned->retireAfter)) {
        destroyTexture(bd, tex);
      }
    }
    return true;
  }

  bool setupTextures() {

    Logger::log("Setting up ImGui texture pools.\n");

    auto bd = getBackendData();

    // init sampler and texture pools

    int sampDescSize = 0;
    bd->device->GetInteger(nvn::DeviceInfo::SAMPLER_DESCRIPTOR_SIZE, &sampDescSize);
    int texDescSize = 0;
    bd->device->GetInteger(nvn::DeviceInfo::TEXTURE_DESCRIPTOR_SIZE, &texDescSize);

    int sampMemPoolSize = sampDescSize * MaxSampDescriptors;
    int texMemPoolSize = texDescSize * MaxTexDescriptors;
    int totalPoolSize = ALIGN_UP(sampMemPoolSize + texMemPoolSize, 0x1000);
    if (!MemoryPoolMaker::createPool(&bd->sampTexMemPool, totalPoolSize)) {
      Logger::log("Failed to Create Texture/Sampler Memory Pool!\n");
      return false;
    }

    if (!bd->samplerPool.Initialize(&bd->sampTexMemPool, 0, MaxSampDescriptors)) {
      Logger::log("Failed to Create Sampler Pool!\n");
      return false;
    }

    if (!bd->texPool.Initialize(&bd->sampTexMemPool, sampMemPoolSize, MaxTexDescriptors)) {
      Logger::log("Failed to Create Texture Pool!\n");
      return false;
    }

    // The textures themselves come from ImGui's requests in renderDrawData (updateTexture).

    bd->samplerBuilder.SetDefaults()
        .SetDevice(bd->device)
        .SetMinMagFilter(nvn::MinFilter::LINEAR, nvn::MagFilter::LINEAR)
        .SetWrapMode(nvn::WrapMode::CLAMP, nvn::WrapMode::CLAMP, nvn::WrapMode::CLAMP);

    if (!bd->fontSampler.Initialize(&bd->samplerBuilder)) {
      Logger::log("Failed to Init Font Sampler!\n");
      return false;
    }

    bd->samplerId = 257;
    bd->samplerPool.RegisterSampler(bd->samplerId, &bd->fontSampler);

    Logger::log("Finished.\n");

    return true;
  }

  bool setupShaders(u8 *shaderBinary, ulong binarySize) {

    Logger::log("Setting up ImGui Shaders.\n");

    auto bd = getBackendData();
    Logger::log("shaderProgram: 0x%p  - device: 0x%p ", &(bd->shaderProgram), &(bd->device));
    if (!bd->shaderProgram.Initialize(bd->device)) {
      Logger::log("Failed to Initialize Shader Program!");
      return false;
    }

    bd->shaderMemory = checkedMemoryBuffer(binarySize, shaderBinary, nvn::MemoryPoolFlags::CPU_UNCACHED |
                                                                     nvn::MemoryPoolFlags::GPU_CACHED |
                                                                     nvn::MemoryPoolFlags::SHADER_CODE);

    if (bd->shaderMemory == nullptr || !bd->shaderMemory->IsBufferReady()) {
      Logger::log("Shader Memory Pool not Ready! Unable to continue.\n");
      return false;
    }

    BinaryHeader offsetData = BinaryHeader((u32 *) shaderBinary);

    nvn::BufferAddress addr = bd->shaderMemory->GetBufferAddress();

    nvn::ShaderData &vertShaderData = bd->shaderDatas[0];
    vertShaderData.data = addr + offsetData.mVertexDataOffset;
    vertShaderData.control = shaderBinary + offsetData.mVertexControlOffset;

    nvn::ShaderData &fragShaderData = bd->shaderDatas[1];
    fragShaderData.data = addr + offsetData.mFragmentDataOffset;
    fragShaderData.control = shaderBinary + offsetData.mFragmentControlOffset;

    if (!bd->shaderProgram.SetShaders(2, bd->shaderDatas)) {
      Logger::log("Failed to Set shader data for program.\n");
      return false;
    }

    bd->shaderProgram.SetDebugLabel("ImGuiShader");

    // Uniform Block Object Memory Setup

    bd->uniformMemory = checkedMemoryBuffer(UBOSIZE);

    if (bd->uniformMemory == nullptr || !bd->uniformMemory->IsBufferReady()) {
      Logger::log("Uniform Memory Pool not Ready! Unable to continue.\n");
      return false;
    }

    // setup vertex attrib & stream

    bd->attribStates[0].SetDefaults().SetFormat(nvn::Format::RG32F, offsetof(ImDrawVert, pos)); // pos
    bd->attribStates[1].SetDefaults().SetFormat(nvn::Format::RG32F, offsetof(ImDrawVert, uv)); // uv
    bd->attribStates[2].SetDefaults().SetFormat(nvn::Format::RGBA8, offsetof(ImDrawVert, col)); // color

    bd->streamState.SetDefaults().SetStride(sizeof(ImDrawVert));

    Logger::log("Finished.\n");

    return true;
  }

  void InitBackend(const NvnBackendInitInfo &initInfo) {
    ImGuiIO &io = ImGui::GetIO();
    SMASH_ASSERT(!io.BackendRendererUserData, "Already Initialized Imgui Backend!");

    io.BackendPlatformName = "Switch";
    io.BackendRendererName = "imgui_impl_nvn";
    io.IniFilename = nullptr;
    io.MouseDrawCursor = true;
    io.ConfigFlags |= ImGuiConfigFlags_IsTouchScreen;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
    // Glyphs are rasterized when first drawn; renderDrawData honours ImGui's texture requests.
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    ImGuiPlatformIO &platformIo = ImGui::GetPlatformIO();
    platformIo.Renderer_TextureMaxWidth = ImguiFontSetup::kDefaultLimits.texMaxWidth;
    platformIo.Renderer_TextureMaxHeight = ImguiFontSetup::kDefaultLimits.texMaxHeight;

    io.DisplaySize = ImVec2(IMGUI_SMASH_VIEWPORT_WIDTH, IMGUI_SMASH_VIEWPORT_HEIGHT); // default size

    // calculate ortho matrix
    orthoRH_ZO(projMatrix, 0.0f, io.DisplaySize.x, io.DisplaySize.y, 0.0f, -1.0f, 1.0f);

    auto *bd = IM_NEW(NvnBackendData)();
    io.BackendRendererUserData = (void *) bd;

    bd->device = initInfo.device;
    bd->queue = initInfo.queue;
    bd->cmdBuf = initInfo.cmdBuf;
    bd->recordingCmd = nullptr;
    bd->isInitialized = false;

#if IMGUI_SMASH_LOAD_DEFAULT_FONT
    {
      ImguiFontSetup::SharedFont shared[ImguiFontSetup::kMaxSharedFonts];
      const int count = ImguiSharedFonts::query(shared, ImguiFontSetup::kMaxSharedFonts);
      if (ImguiFontSetup::setup(io.Fonts, shared, count) == nullptr) {
        Logger::log("Failed to add the overlay font!\n");
      }
      const ImguiFontSetup::Stats fonts = ImguiFontSetup::stats();
      Logger::log("Fonts: %d shared merged, %d rejected, fallback U+%04X.\n", fonts.sharedSources,
                  fonts.rejectedSources, fonts.fallbackChar);
    }
#endif

    if (createShaders()) {
      Logger::log("Shader Binaries Loaded! Setting up Render Data.\n");

      if (bd->isUseTestShader)
        initTestShader();

        // Setup Shaders crashes
        // Setup Font crashes
      if (setupShaders(bd->imguiShaderBinary.ptr, bd->imguiShaderBinary.size)) {
        if(setupTextures()) {
            Logger::log("Rendering Setup!\n");
    
            bd->isInitialized = true;
        } else {
            Logger::log("Failed to Setup Textures!\n");
            return;
        }
      } else {
        Logger::log("Failed to Setup Shaders Data!\n");
        return;
    }
    Logger::log("Setup Render Data!\n");
    }
  }

  void ShutdownBackend() {
    auto *bd = getBackendData();
    // Textures still read by a pending submission are left allocated rather than freed under the GPU.
    if (ImguiDrawSlots::completedThrough(ImguiDrawSlots::lastSerial())) {
      for (ImTextureData *tex : ImGui::GetPlatformIO().Textures) {
        if (tex->BackendUserData != nullptr) {
          destroyTexture(bd, tex);
        }
      }
    }
    ImguiDrawSlots::shutdown(bd->device);
    
    if (bd->vtxBuffer) {
        bd->vtxBuffer->Finalize();
        IM_FREE(bd->vtxBuffer);
    }

    if (bd->idxBuffer) {
        bd->idxBuffer->Finalize();
        IM_FREE(bd->idxBuffer);
    }
  }

  bool updateTouch(ImGuiIO &io) {
    ImVec2 pos(0, 0);
    if (!InputHelper::getTouchCoords(&pos.x, &pos.y)) {
      if (InputHelper::isTouchRelease()) {
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
      }
      return false;
    }

    io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);

    pos.x = (pos.x / (float) IMGUI_SMASH_VIEWPORT_WIDTH) * io.DisplaySize.x;
    pos.y = (pos.y / (float) IMGUI_SMASH_VIEWPORT_HEIGHT) * io.DisplaySize.y;
    io.AddMousePosEvent(pos.x, pos.y);

    if (InputHelper::isTouchPress()) {
      io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    }
    return true;
  }

  void updateMouse(ImGuiIO &io) {
    ImVec2 mousePos(0, 0);
    InputHelper::getMouseCoords(&mousePos.x, &mousePos.y);

    io.AddMouseSourceEvent(ImGuiMouseSource_Mouse);

    // Workaround from https://github.com/Amethyst-szs/smo-lunakit
    mousePos = ImVec2((mousePos.x / (float) IMGUI_SMASH_VIEWPORT_WIDTH) * io.DisplaySize.x,
                      (mousePos.y / (float) IMGUI_SMASH_VIEWPORT_HEIGHT) * io.DisplaySize.y);
    io.AddMousePosEvent(mousePos.x, mousePos.y);

    ImVec2 scrollDelta(0, 0);
    InputHelper::getScrollDelta(&scrollDelta.x, &scrollDelta.y);

    if (scrollDelta.x != 0.0f)
      io.AddMouseWheelEvent(0.0f, scrollDelta.x > 0.0f ? 0.5f : -0.5f);

    for (auto [im_k, nx_k]: mouse_mapping) {
      if (InputHelper::isMousePress((nn::hid::MouseButton) nx_k))
        io.AddMouseButtonEvent((ImGuiMouseButton) im_k, true);
      else if (InputHelper::isMouseRelease((nn::hid::MouseButton) nx_k))
        io.AddMouseButtonEvent((ImGuiMouseButton) im_k, false);
    }
  }

  void updateKeyboard(ImGuiIO &io) {
    for (auto [im_k, nx_k]: key_mapping) {
      if (InputHelper::isKeyPress((nn::hid::KeyboardKey) nx_k)) {
        io.AddKeyEvent((ImGuiKey) im_k, true);
      } else if (InputHelper::isKeyRelease((nn::hid::KeyboardKey) nx_k)) {
        io.AddKeyEvent((ImGuiKey) im_k, false);
      }
    }
  }

  void updateGamepad(ImGuiIO &io) {
    for (auto [im_k, nx_k]: npad_mapping) {
      if (InputHelper::isButtonPress((nn::hid::NpadButton) nx_k))
        io.AddKeyEvent((ImGuiKey) im_k, true);
      else if (InputHelper::isButtonRelease((nn::hid::NpadButton) nx_k))
        io.AddKeyEvent((ImGuiKey) im_k, false);
    }

    io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickLeft, InputHelper::getLeftStickLeft() > 0.8f, InputHelper::getLeftStickLeft());
    io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickRight, InputHelper::getLeftStickRight() > 0.8f, InputHelper::getLeftStickRight());
    io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickUp, InputHelper::getLeftStickUp() > 0.8f, InputHelper::getLeftStickUp());
    io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickDown, InputHelper::getLeftStickDown() > 0.8f, InputHelper::getLeftStickDown());

    io.AddKeyAnalogEvent(ImGuiKey_GamepadRStickLeft, InputHelper::getRightStickLeft() > 0.8f, InputHelper::getRightStickLeft());
    io.AddKeyAnalogEvent(ImGuiKey_GamepadRStickRight, InputHelper::getRightStickRight() > 0.8f, InputHelper::getRightStickRight());
    io.AddKeyAnalogEvent(ImGuiKey_GamepadRStickUp, InputHelper::getRightStickUp() > 0.8f, InputHelper::getRightStickUp());
    io.AddKeyAnalogEvent(ImGuiKey_GamepadRStickDown, InputHelper::getRightStickDown() > 0.8f, InputHelper::getRightStickDown());
  }

  void updateInput() {

    ImGuiIO &io = ImGui::GetIO();

#if IMGUI_SMASH_INPUT_TOUCH
    if (updateTouch(io)) {
      // Handled touch screen input, disable other inputs for this frame.
      return;
    }
#endif

#if IMGUI_SMASH_INPUT_KBM
    updateKeyboard(io);
    updateMouse(io);
#endif

#if IMGUI_SMASH_INPUT_PAD
    if (InputHelper::isInputToggled()) {
      updateGamepad(io);
    }
#endif

  }

  void newFrame() {
    ImGuiIO &io = ImGui::GetIO();
    auto *bd = getBackendData();

    nn::TimeSpan curTick = nn::os::GetSystemTick().ToTimeSpan();
    nn::TimeSpan prevTick(bd->lastTick);
    io.DeltaTime = fabsf((float) (curTick - prevTick).GetNanoSeconds() / 1e9f);

    bd->lastTick = curTick;

    InputHelper::updatePadState(); // update input helper

    updateInput(); // update backend inputs
  }

  void setRenderStates() {

    auto bd = getBackendData();
    nvn::CommandBuffer *cmd = bd->recordingCmd != nullptr ? bd->recordingCmd : bd->cmdBuf;

    nvn::PolygonState polyState;
    polyState.SetDefaults();
    polyState.SetPolygonMode(nvn::PolygonMode::FILL);
    polyState.SetCullFace(nvn::Face::NONE);
    polyState.SetFrontFace(nvn::FrontFace::CCW);
    cmd->BindPolygonState(&polyState);

    nvn::ColorState colorState;
    colorState.SetDefaults();
    colorState.SetLogicOp(nvn::LogicOp::COPY);
    colorState.SetAlphaTest(nvn::AlphaFunc::ALWAYS);
    for (int i = 0; i < 8; ++i) {
      colorState.SetBlendEnable(i, true);
    }
    cmd->BindColorState(&colorState);

    nvn::BlendState blendState;
    blendState.SetDefaults();
    blendState.SetBlendFunc(nvn::BlendFunc::SRC_ALPHA, nvn::BlendFunc::ONE_MINUS_SRC_ALPHA, nvn::BlendFunc::ONE,
                            nvn::BlendFunc::ZERO);
    blendState.SetBlendEquation(nvn::BlendEquation::ADD, nvn::BlendEquation::ADD);
    cmd->BindBlendState(&blendState);

    cmd->BindVertexAttribState(3, bd->attribStates);
    cmd->BindVertexStreamState(1, &bd->streamState);

    cmd->SetTexturePool(&bd->texPool);
    cmd->SetSamplerPool(&bd->samplerPool);
  }

  nvn::CommandHandle renderDrawData(ImDrawData *drawData) {

    // we dont need to process any data if it isnt valid
    if (!drawData->Valid) {
        Logger::log("Draw Data was Invalid! Skipping Render.");
      return 0;
    }
    // if we dont have any command lists to draw, we can stop here
    if (drawData->CmdListsCount == 0) {
        Logger::log("Command List was Empty! Skipping Render.\n");
      return 0;
    }

    // get both the main backend data and IO from ImGui
    auto bd = getBackendData();
    ImGuiIO &io = ImGui::GetIO();

    // if something went wrong during backend setup, don't try to render anything
    if (!bd->isInitialized) {
      Logger::log("Backend Data was not fully initialized!\n");
      return 0;
    }
    
    if(!bd->cmdBuf) {
        Logger::log("Backend Command Buffer not set!\n");
        return 0;
    }

    // disable imgui rendering if we are using the test shader code
    if (bd->isUseTestShader) {
      renderTestShader(drawData);
      return 0;
    }

    // Texture requests first. A draw command naming a texture that was never created would trip ImGui's
    // GetTexID assert, so a failed create skips this frame; ImGui repeats the request next frame.
    if (drawData->Textures != nullptr) {
      bool created = true;
      for (ImTextureData *tex : *drawData->Textures) {
        if (tex->Status != ImTextureStatus_OK && !updateTexture(tex)) {
          created = false;
        }
      }
      if (!created) {
        return 0;
      }
    }

    size_t totalVtxSize = static_cast<size_t>(drawData->TotalVtxCount) * sizeof(ImDrawVert);
    size_t totalIdxSize = static_cast<size_t>(drawData->TotalIdxCount) * sizeof(ImDrawIdx);
    const int slot = ImguiDrawSlots::begin(bd->device, totalVtxSize, totalIdxSize);
    if (slot < 0) {
      return 0;
    }
    nvn::CommandBuffer *cmd = ImguiDrawSlots::commandBuffer(slot);
    MemoryBuffer *vtx = ImguiDrawSlots::vertex(slot);
    MemoryBuffer *idx = ImguiDrawSlots::index(slot);
    MemoryBuffer *uniform = ImguiDrawSlots::uniform(slot);
    if (cmd == nullptr || vtx == nullptr || idx == nullptr || uniform == nullptr || !vtx->IsBufferReady() ||
        !idx->IsBufferReady() || !uniform->IsBufferReady()) {
      ImguiDrawSlots::discardUnsubmitted(slot, bd->device);
      return 0;
    }

    orthoRH_ZO(projMatrix, 0.0f, io.DisplaySize.x, io.DisplaySize.y, 0.0f, -1.0f, 1.0f);

    bd->recordingCmd = cmd;
    cmd->BeginRecording();

    // Setup is one uninterruptible group. Its largest immediate update is the
    // 64-byte projection matrix. Later checks stop further groups after overflow.
    if (!ImguiDrawSlots::overflowed(slot)) {
      if (bd->texturesTouched) {
        // CPU-written texels and newly registered descriptors become visible to this recording's draws.
        cmd->Barrier(nvn::BarrierBits(nvn::BarrierBits::INVALIDATE_TEXTURE) |
                     nvn::BarrierBits::INVALIDATE_TEXTURE_DESCRIPTOR);
      }
      cmd->BindProgram(&bd->shaderProgram, nvn::ShaderStageBits::VERTEX | nvn::ShaderStageBits::FRAGMENT);
      cmd->BindUniformBuffer(nvn::ShaderStage::VERTEX, 0, *uniform, UBOSIZE);
      cmd->UpdateUniformBuffer(*uniform, UBOSIZE, 0, sizeof(projMatrix), &projMatrix);
      setRenderStates();
    }

    size_t vtxOffset = 0, idxOffset = 0;
    nvn::TextureHandle boundTextureHandle = 0;

    for (int i = 0; i < drawData->CmdListsCount && !ImguiDrawSlots::overflowed(slot); i++) {
      auto cmdList = drawData->CmdLists[i];
      size_t vtxSize = static_cast<size_t>(cmdList->VtxBuffer.Size) * sizeof(ImDrawVert);
      size_t idxSize = static_cast<size_t>(cmdList->IdxBuffer.Size) * sizeof(ImDrawIdx);

      cmd->BindVertexBuffer(0, (*vtx) + vtxOffset, vtxSize);
      if (vtxSize > 0) {
        memcpy(vtx->GetMemPtr() + vtxOffset, cmdList->VtxBuffer.Data, vtxSize);
      }
      if (idxSize > 0) {
        memcpy(idx->GetMemPtr() + idxOffset, cmdList->IdxBuffer.Data, idxSize);
      }
      if (ImguiDrawSlots::overflowed(slot)) {
        break;
      }

      for (auto draw : cmdList->CmdBuffer) {
        ImVec4 clipRect = ImVec4(draw.ClipRect.x, draw.ClipRect.y, draw.ClipRect.z, draw.ClipRect.w);
        ImVec2 clip_min(clipRect.x, clipRect.y);
        ImVec2 clip_max(clipRect.z, clipRect.w);
        ImVec2 clip_size(clip_max.x - clip_min.x, clip_max.y - clip_min.y);

        if (currentTextureIndex != -1) {
          cmd->SetRenderTargets(1, (nvn::Texture *const *)(&nvnTextures[currentTextureIndex]), 0, 0, 0);
        }
        cmd->SetViewport(0, 0, static_cast<int>(io.DisplaySize.x), static_cast<int>(io.DisplaySize.y));
        cmd->SetScissor(static_cast<int>(clip_min.x), static_cast<int>(clip_min.y), static_cast<int>(clip_size.x),
                        static_cast<int>(clip_size.y));

        nvn::TextureHandle TexID = *(nvn::TextureHandle *)draw.GetTexID();
        if (boundTextureHandle != TexID) {
          boundTextureHandle = TexID;
          cmd->BindTexture(nvn::ShaderStage::FRAGMENT, 0, TexID);
        }
        cmd->DrawElementsBaseVertex(nvn::DrawPrimitive::TRIANGLES, nvn::IndexType::UNSIGNED_SHORT, draw.ElemCount,
                                    (*idx) + (draw.IdxOffset * sizeof(ImDrawIdx)) + idxOffset, draw.VtxOffset);
        if (ImguiDrawSlots::overflowed(slot)) {
          break;
        }
      }

      vtxOffset += vtxSize;
      idxOffset += idxSize;
    }

    bd->resetData = false;
    nvn::CommandHandle handle = cmd->EndRecording();
    bd->recordingCmd = nullptr;
    ImguiDrawSlots::complete(slot, bd->device, handle);
    if (ImguiDrawSlots::active() != slot) {
      return 0;
    }
    bd->texturesTouched = false;
    return handle;
  }
}

extern "C" int imgui_smash_font_stats(ImguiSmashFontStats *out) {
  if (out == nullptr) {
    return 0;
  }
  const ImguiFontSetup::Stats fonts = ImguiFontSetup::stats();
  *out = {};
  out->version = IMGUI_SMASH_FONT_STATS_VERSION;
  out->size = static_cast<uint32_t>(sizeof(ImguiSmashFontStats));
  out->sharedSources = static_cast<uint32_t>(fonts.sharedSources);
  out->rejectedSources = static_cast<uint32_t>(fonts.rejectedSources);
  out->glyphsAdmitted = static_cast<uint32_t>(fonts.admitted);
  out->glyphRefusals = static_cast<uint32_t>(fonts.refusals);
  out->fallbackChar = static_cast<uint32_t>(fonts.fallbackChar);
  out->atlasWidth = ImguiNvnBackend::g_atlasWidth.load(std::memory_order_relaxed);
  out->atlasHeight = ImguiNvnBackend::g_atlasHeight.load(std::memory_order_relaxed);
  out->atlasBytesPerPixel = ImguiNvnBackend::g_atlasBytesPerPixel.load(std::memory_order_relaxed);
  out->texturesLive = ImguiNvnBackend::g_texturesLive.load(std::memory_order_relaxed);
  out->pixelBytes = ImguiNvnBackend::g_pixelBytes.load(std::memory_order_relaxed);
  out->poolBytes = ImguiNvnBackend::g_poolBytes.load(std::memory_order_relaxed);
  out->creates = ImguiNvnBackend::g_creates.load(std::memory_order_relaxed);
  out->updates = ImguiNvnBackend::g_updates.load(std::memory_order_relaxed);
  out->destroys = ImguiNvnBackend::g_destroys.load(std::memory_order_relaxed);
  return 1;
}