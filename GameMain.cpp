#define GLFW_EXPOSE_NATIVE_WIN32
#include "../GLFW/glfw3.h"
#include "../GLFW/glfw3native.h"
#include "../glm/glm.hpp"
#include <d3d11.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include "../DirectXTex-main/DirectXTex/DirectXTex.h"
#include "../DirectXTK-main/Inc/SpriteBatch.h"
#include "../DirectXTK-main/Inc/SpriteFont.h"
#include <DirectXCollision.h>
#include "../imgui/imgui.h"
#include "../imgui/backends/imgui_impl_glfw.h"
#include "../imgui/backends/imgui_impl_dx11.h"
#include <iostream>
#include <vector>
#include <cstring>
#include <fstream>
#include <sstream>
#include <conio.h>
#include <array>
#include <thread>
#include <filesystem>
#include <algorithm>
#include <shlobj.h>
#include <combaseapi.h>
#include <cstdlib>
#include <limits>
#include "../zlib-1.3.1/zlib.h"
#include "../ImGuizmo.h"
#include "../tinyxml2.h"
#include "../physx/include/PxPhysics.h"
#include "../physx/include/PxPhysicsAPI.h"
#include "../physx/include/cooking/PxCooking.h"
#include "../physx/include/characterkinematic/PxControllerManager.h"
#include "../physx/include/characterkinematic/PxCapsuleController.h"
#include <chrono>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define MAX_POINT_LIGHTS 16

physx::PxFoundation*       gFoundation = nullptr;
physx::PxPhysics*          gPhysics = nullptr;
physx::PxDefaultCpuDispatcher* gDispatcher = nullptr;
physx::PxScene*            gScene = nullptr;
physx::PxMaterial*         gMaterial = nullptr;
physx::PxPvd*              gPvd = nullptr;
physx::PxControllerManager* gControllerManager = nullptr;
physx::PxController* gPlayerController = nullptr;
physx::PxRigidDynamic* gCharacterActor = nullptr;

ID3D11Device* g_device = nullptr;
ID3D11DeviceContext* g_deviceContext = nullptr;
IDXGISwapChain* g_swapChain = nullptr;
ID3D11RenderTargetView* g_renderTargetView = nullptr;
ID3D11DepthStencilView* g_depthStencilView = nullptr;
ID3D11DepthStencilState* g_depthStencilState = nullptr;
ID3D11ShaderResourceView* g_texture = nullptr;
ID3D11RasterizerState* g_rasterizerState = nullptr;

ID3D11InputLayout* g_inputLayout = nullptr;
ID3D11Buffer* g_vertexBuffer = nullptr;
ID3D11VertexShader* g_vertexShader = nullptr;
ID3D11PixelShader* g_pixelShader = nullptr;
ID3D11Buffer* g_matrixBuffer = nullptr;
ID3D11Buffer* g_lightBuffer = nullptr;
ID3D11Buffer* g_pointLightBuffer = nullptr;
ID3D11ShaderResourceView* g_pointLightSRV = nullptr;

DirectX::XMMATRIX view;

DirectX::XMMATRIX projection;

std::unique_ptr<DirectX::SpriteBatch> g_spriteBatch;
std::unique_ptr<DirectX::SpriteFont> g_spriteFont;

struct PointLight
{
    DirectX::XMFLOAT3 position;
    float range;
    DirectX::XMFLOAT3 color;
    float intensity;
};

std::vector<PointLight> pointLights;
std::vector<std::string> plNames;

struct LightBuffer
{
    DirectX::XMFLOAT3 dirLightDirection;
    float pad1;
    DirectX::XMFLOAT3 dirLightColor;
    float pad2;
    DirectX::XMFLOAT3 cameraPosition;
    float pad3;
    int numPointLights;
    float pad4[3]; // Padding to align to 16 bytes
};

struct Vertex {
    DirectX::XMFLOAT3 position;
    DirectX::XMFLOAT3 normal;
    DirectX::XMFLOAT2 texCoord;
};

struct MatrixBuffer {
    DirectX::XMMATRIX world;
    DirectX::XMMATRIX view;
    DirectX::XMMATRIX projection;
};

struct AABB {
    DirectX::XMFLOAT3 min;
    DirectX::XMFLOAT3 max;
};

struct Model
{
    std::string name;
    std::string filePath;
    std::vector<Vertex> vertices;
    ID3D11Buffer* vertexBuffer = nullptr;
    DirectX::XMFLOAT3 position = {0, 0, 0};
    DirectX::XMFLOAT3 rotation = {0, 0, 0};
    float scale = 1.0f;
    bool valid = false;
    ID3D11ShaderResourceView* textureSRV = nullptr;
    std::string id;
    AABB localBounds;
    AABB worldBounds;
    physx::PxRigidDynamic* rigidBody = nullptr;
    bool isStatic = false;
    float mass = 0.0f;
    DirectX::XMMATRIX worldMatrix = DirectX::XMMatrixIdentity();
};

struct LoadedTexture {
    std::string name;
    ID3D11ShaderResourceView* srv;
};

struct TextureInfo {
    std::string name;
    std::string path;
    ID3D11ShaderResourceView* srv;
};

struct RPFEntry {
    std::string path;
    std::vector<char> data;
};

struct Camera {
    DirectX::XMFLOAT3 position;
    DirectX::XMFLOAT2 rotation;
    float fov;
    std::string name;
    bool isFPS;
};

enum class UIElementType {
    None = 0,
    Label = 1,
    Button = 2,
    // ...
};

struct UIElement {
    float x, y, width, height;
    DirectX::XMFLOAT4 color = {1.0f, 1.0f, 1.0f, 1.0f};
    std::string name;
    float textScale = 1.0f;
    UIElementType type = UIElementType::None;
    virtual void Draw() = 0;
    virtual void Update(float mouseX, float mouseY, bool clicked) {}
};

std::wstring WriteToTempFile(const void* data, size_t size)
{
    char tempPath[MAX_PATH];
    GetTempPathA(MAX_PATH, tempPath);
    std::string tempFileName = std::string(tempPath) + "temp_font.spritefont";

    std::ofstream tempFile(tempFileName, std::ios::binary);
    if(tempFile.is_open())
    {
        tempFile.write(reinterpret_cast<const char*>(data), size);
        tempFile.close();
    }
    else
    {
        std::cerr << "Failed to write to temporary file" << std::endl;
        return L"";
    }

    return std::wstring(tempFileName.begin(), tempFileName.end());
}

std::string fontPath;
std::string vsPath;
std::string psPath;

std::vector<char> vsData;
std::vector<char> psData;

std::vector<std::unique_ptr<UIElement>> uiElements;

struct UILabel : public UIElement {
    std::string text;
    float x, y;
    DirectX::XMFLOAT4 color = {1.0f, 1.0f, 1.0f, 1.0f};
    void Draw() override
    {
        if(!g_spriteFont || !g_spriteBatch) return;

        g_spriteBatch->Begin();

        g_spriteFont->DrawString(
                g_spriteBatch.get(),
                std::wstring(text.begin(), text.end()).c_str(),
                DirectX::XMFLOAT2(x, y),
                DirectX::XMVECTOR{color.x, color.y, color.z, color.w},
                0.0f,
                DirectX::XMFLOAT2(0.0f, 0.0f),
                textScale
                );

        g_spriteBatch->End();
    }
    UILabel() {
        type = UIElementType::Label;
    }
};

struct UIButton : public UIElement {
    std::string text;
    float x, y;
    float width, height;
    DirectX::XMFLOAT4 color = {1.0f, 1.0f, 1.0f, 1.0f};
    DirectX::XMFLOAT4 hoverColor = {0.0f, 0.8f, 0.2f, 1.0f};
    std::function<void()> onClick;

    bool hovered = false;
    bool wasPressed = false;

    void Update(float mx, float my, bool clicked) override
    {
        hovered = (mx >= x && mx <= x + width &&
                   my >= y && my <= y + height);

        if(hovered && clicked && !wasPressed)
        {
            if(onClick) onClick();
            wasPressed = true;
        }
        if(!clicked)
        {
            wasPressed = false;
        }
    }

    void Draw() override
    {
        if(!g_spriteBatch || !g_spriteFont) return;

        const auto& col = hovered ? hoverColor : color;

        g_spriteBatch->Begin();
        g_spriteFont->DrawString(
            g_spriteBatch.get(),
            std::wstring(text.begin(), text.end()).c_str(),
            DirectX::XMFLOAT2(x, y),
            DirectX::XMVECTOR{col.x, col.y, col.z, col.w},
            0.0f,
            DirectX::XMFLOAT2(0.0f, 0.0f),
            textScale
        );
        g_spriteBatch->End();
    }
    UIButton() {
        type = UIElementType::Button;
    }
};

std::vector<LoadedTexture> g_textures;
std::vector<TextureInfo> g_texture_info;

std::unordered_map<std::string, std::vector<char>> g_rpfAssets;

std::vector<Model> g_models;

std::vector<Camera> g_cameras;

std::vector<std::string> scripts;

ImVec2 gameSize;

std::vector<Vertex> g_vertices;
std::vector<uint32_t> g_indices;

void InitPhysics()
{
    static physx::PxDefaultErrorCallback gDefaultErrorCallback;
    static physx::PxDefaultAllocator gDefaultAllocatorCallback;

    gFoundation = PxCreateFoundation(PX_PHYSICS_VERSION, gDefaultAllocatorCallback, gDefaultErrorCallback);

    gPvd = physx::PxCreatePvd(*gFoundation);
    physx::PxPvdTransport* mTransport = physx::PxDefaultPvdSocketTransportCreate("localhost", 5425, 10000);
    if(mTransport == NULL)
        return;
    physx::PxPvdInstrumentationFlags mPvdFlags = physx::PxPvdInstrumentationFlag::eALL;
    physx::PxPvd* mPvd = physx::PxCreatePvd(*gFoundation);
    mPvd->connect(*mTransport, mPvdFlags);

    gPhysics = PxCreatePhysics(PX_PHYSICS_VERSION, *gFoundation, physx::PxTolerancesScale(), true, gPvd);

    physx::PxSceneDesc sceneDesc(gPhysics->getTolerancesScale());
    sceneDesc.gravity = physx::PxVec3(0.0f, -9.81f, 0.0f);
    gDispatcher = physx::PxDefaultCpuDispatcherCreate(2);
    sceneDesc.cpuDispatcher = gDispatcher;
    sceneDesc.filterShader = physx::PxDefaultSimulationFilterShader;

    sceneDesc.flags |= physx::PxSceneFlag::eENABLE_ACTIVE_ACTORS;
    sceneDesc.flags |= physx::PxSceneFlag::eENABLE_CCD;

    gScene = gPhysics->createScene(sceneDesc);

    physx::PxPvdSceneClient* pvdClient = gScene->getScenePvdClient();
    if (pvdClient)
    {
        pvdClient->setScenePvdFlag(physx::PxPvdSceneFlag::eTRANSMIT_CONSTRAINTS, true);
        pvdClient->setScenePvdFlag(physx::PxPvdSceneFlag::eTRANSMIT_CONTACTS, true);
        pvdClient->setScenePvdFlag(physx::PxPvdSceneFlag::eTRANSMIT_SCENEQUERIES, true);
    }

    gMaterial = gPhysics->createMaterial(0.5f, 0.5f, 0.6f);
}

void InitDX(HWND hwnd, GLFWwindow* window)
{
    DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
    swapChainDesc.BufferCount = 1;
    swapChainDesc.BufferDesc.Width = 800;
    swapChainDesc.BufferDesc.Height = 600;
    swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.BufferDesc.RefreshRate.Numerator = 60;
    swapChainDesc.BufferDesc.RefreshRate.Denominator = 1;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.OutputWindow = hwnd;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.Windowed = TRUE;

    D3D_FEATURE_LEVEL featureLevel;
    D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
        D3D_FEATURE_LEVEL_9_3,
        D3D_FEATURE_LEVEL_9_1,
    };
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, featureLevels, ARRAYSIZE(featureLevels), D3D11_SDK_VERSION, &swapChainDesc, &g_swapChain, &g_device, &featureLevel, &g_deviceContext);

    if(FAILED(hr))
    {
        std::cerr << "Failed to create DirectX device and swap chain." << '\n';
        exit(-1);
    }

    ID3D11Texture2D* pBackBuffer = nullptr;
    hr = g_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBackBuffer);
    if(FAILED(hr))
    {
        std::cerr << "Failed to get back buffer" << '\n';
        exit(-1);
    }

    hr = g_device->CreateRenderTargetView(pBackBuffer, nullptr, &g_renderTargetView);
    pBackBuffer->Release();
    if(FAILED(hr))
    {
        std::cerr << "Failed to create render target view" << '\n';
        exit(-1);
    }

    D3D11_TEXTURE2D_DESC depthStencilDesc = {};
    depthStencilDesc.Width = 800;
    depthStencilDesc.Height = 600;
    depthStencilDesc.MipLevels = 1;
    depthStencilDesc.ArraySize = 1;
    depthStencilDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthStencilDesc.SampleDesc.Count = 1;
    depthStencilDesc.Usage = D3D11_USAGE_DEFAULT;
    depthStencilDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

    ID3D11Texture2D* pDepthStencilBuffer = nullptr;
    hr = g_device->CreateTexture2D(&depthStencilDesc, nullptr, &pDepthStencilBuffer);
    if(FAILED(hr))
    {
        std::cerr << "Failed to create depth stencil buffer" << '\n';
        exit(-1);
    }

    hr = g_device->CreateDepthStencilView(pDepthStencilBuffer, nullptr, &g_depthStencilView);
    pDepthStencilBuffer->Release();
    if(FAILED(hr))
    {
        std::cerr << "Failed to create depth stencil view" << '\n';
        exit(-1);
    }

    D3D11_BUFFER_DESC lightBufferDesc = {};
    lightBufferDesc.Usage = D3D11_USAGE_DYNAMIC;
    lightBufferDesc.ByteWidth = sizeof(LightBuffer);
    lightBufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    lightBufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    hr = g_device->CreateBuffer(&lightBufferDesc, nullptr, &g_lightBuffer);
    if(FAILED(hr))
    {
        std::cerr << "Failed to create light buffer!\n";
    }

    D3D11_BUFFER_DESC lightDesc = {};
    lightDesc.Usage = D3D11_USAGE_DYNAMIC;
    lightDesc.ByteWidth = sizeof(PointLight) * MAX_POINT_LIGHTS;
    lightDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    lightDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    lightDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    lightDesc.StructureByteStride = sizeof(PointLight);

    g_device->CreateBuffer(&lightDesc, nullptr, &g_pointLightBuffer);

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    srvDesc.Buffer.FirstElement = 0;
    srvDesc.Buffer.NumElements = MAX_POINT_LIGHTS;
    hr = g_device->CreateShaderResourceView(g_pointLightBuffer, &srvDesc, &g_pointLightSRV);

    g_deviceContext->OMSetRenderTargets(1, &g_renderTargetView, g_depthStencilView);

    D3D11_VIEWPORT viewport = {};
    viewport.Width = 800;
    viewport.Height = 600;
    g_deviceContext->RSSetViewports(1, &viewport);
}

void CleanDX()
{
    if(g_texture) g_texture->Release();
    
    if(g_inputLayout) g_inputLayout->Release();
    if(g_vertexBuffer) g_vertexBuffer->Release();
    if(g_vertexShader) g_vertexShader->Release();
    if(g_pixelShader) g_pixelShader->Release();
    if(g_matrixBuffer) g_matrixBuffer->Release();
    
    if(g_deviceContext) g_deviceContext->ClearState();
    if(g_renderTargetView) g_renderTargetView->Release();
    if(g_depthStencilView) g_depthStencilView->Release();
    
    if(g_swapChain) g_swapChain->Release();
    if(g_deviceContext) g_deviceContext->Release();
    if(g_device) g_device->Release();
    if(g_lightBuffer) g_lightBuffer->Release();
    
    g_texture = nullptr;
    g_inputLayout = nullptr;
    g_vertexBuffer = nullptr;
    g_vertexShader = nullptr;
    g_pixelShader = nullptr;
    g_matrixBuffer = nullptr;
    g_renderTargetView = nullptr;
    g_depthStencilView = nullptr;
    g_swapChain = nullptr;
    g_deviceContext = nullptr;
    g_device = nullptr;
}

void CompileShaders()
{
    HRESULT hr = g_device->CreateVertexShader(vsData.data(), vsData.size(), nullptr, &g_vertexShader);
    if(FAILED(hr)) { std::cerr << "Failed to create vertex shader. HRESULT: " << std::hex << hr << '\n'; return; }

    hr = g_device->CreatePixelShader(psData.data(), psData.size(), nullptr, &g_pixelShader);
    if(FAILED(hr)) { std::cerr << "Failed to create pixel shader\n"; return; }

    D3D11_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };

    hr = g_device->CreateInputLayout(layout, ARRAYSIZE(layout), vsData.data(), vsData.size(), &g_inputLayout);
    if(FAILED(hr))
    {
        std::cerr << "Failed to create input layout" << std::endl;
    }
    else
    {
        std::cout << "Created input layout" << '\n';
    }

    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.Usage = D3D11_USAGE_DYNAMIC;
    cbDesc.ByteWidth = sizeof(MatrixBuffer);
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    hr = g_device->CreateBuffer(&cbDesc, nullptr, &g_matrixBuffer);
    if(FAILED(hr))
    {
        std::cerr << "Failed to create constant buffer" << std::endl;
    }
    else
    {
        std::cout << "Created matrix buffer" << '\n';
    }
    
    ID3D11SamplerState* samplerState;
    D3D11_SAMPLER_DESC samplerDesc = {};
    samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    samplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    samplerDesc.MinLOD = 0;
    samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
    
    hr = g_device->CreateSamplerState(&samplerDesc, &samplerState);
    if(FAILED(hr))
    {
        std::cerr << "Failed to create sampler state" << std::endl;
    }
    else
    {
        g_deviceContext->PSSetSamplers(0, 1, &samplerState);
        samplerState->Release();
        std::cout << "Created and set sampler state" << '\n';
    }
}

ID3D11ShaderResourceView* g_textureSRV = nullptr;

ID3D11ShaderResourceView* LoadTextureFromMemory(const void* data, size_t size)
{
    int width, height, channels;
    unsigned char* imageData = stbi_load_from_memory(
        reinterpret_cast<const unsigned char*>(data),
        static_cast<int>(size),
        &width, &height, &channels,
        STBI_rgb_alpha
    );

    if(!imageData)
    {
        std::cerr << "Failed to load texture from memory\n";
        return nullptr;
    }

    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = width;
    texDesc.Height = height;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = imageData;
    initData.SysMemPitch = width * 4;

    ID3D11Texture2D* texture = nullptr;
    HRESULT hr = g_device->CreateTexture2D(&texDesc, &initData, &texture);
    if(FAILED(hr))
    {
        std::cerr << "Failed to create texture from memory\n";
        stbi_image_free(imageData);
        return nullptr;
    }

    ID3D11ShaderResourceView* textureSRV = nullptr;
    hr = g_device->CreateShaderResourceView(texture, nullptr, &textureSRV);
    texture->Release();
    stbi_image_free(imageData);

    if(FAILED(hr))
    {
        std::cerr << "Failed to create shader resource view from memory\n";
        return nullptr;
    }

    return textureSRV;
}

ID3D11ShaderResourceView* LoadTexture(const std::string& path)
{
    int width, height, channels;
    unsigned char* imageData = stbi_load(path.c_str(), &width, &height, &channels, STBI_rgb_alpha);
    if(!imageData)
    {
        std::cerr << "Failed to load image: " << path << '\n';
        return nullptr;
    }

    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = width;
    texDesc.Height = height;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    texDesc.CPUAccessFlags = 0;

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = imageData;
    initData.SysMemPitch = width * 4;
    initData.SysMemSlicePitch = width * height * 4;

    ID3D11Texture2D* texture = nullptr;
    HRESULT hr = g_device->CreateTexture2D(&texDesc, &initData, &texture);
    if(FAILED(hr))
    {
        std::cerr << "Failed to create texture from image: " << path << '\n';
        stbi_image_free(imageData);
        return nullptr;
    }

    ID3D11ShaderResourceView* textureSRV = nullptr;
    hr = g_device->CreateShaderResourceView(texture, nullptr, &textureSRV);
    texture->Release();

    stbi_image_free(imageData);

    if(FAILED(hr))
    {
        std::cerr << "Failed to create shader resource view: " << path << '\n';
        return nullptr;
    }

    return textureSRV;
}

void ResizeSwapChain(int width, int height)
{
    if(width <= 0 || height <= 0 || !g_device || !g_swapChain)
        return;

    if(g_renderTargetView) g_renderTargetView->Release();
    if(g_depthStencilView) g_depthStencilView->Release();
    
    HRESULT hr = g_swapChain->ResizeBuffers(1, width, height, DXGI_FORMAT_R8G8B8A8_UNORM, 0);
    if(FAILED(hr))
    {
        std::cerr << "Failed to resize swap chain buffers!" << std::endl;
        return;
    }
    
    ID3D11Texture2D* pBackBuffer = nullptr;
    hr = g_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBackBuffer);
    if(FAILED(hr))
    {
        std::cerr << "Failed to get back buffer after resize" << std::endl;
        return;
    }
    
    hr = g_device->CreateRenderTargetView(pBackBuffer, nullptr, &g_renderTargetView);
    pBackBuffer->Release();
    if(FAILED(hr))
    {
        std::cerr << "Failed to create render target view after resize" << std::endl;
        return;
    }
    
    D3D11_TEXTURE2D_DESC depthStencilDesc = {};
    depthStencilDesc.Width = width;
    depthStencilDesc.Height = height;
    depthStencilDesc.MipLevels = 1;
    depthStencilDesc.ArraySize = 1;
    depthStencilDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthStencilDesc.SampleDesc.Count = 1;
    depthStencilDesc.Usage = D3D11_USAGE_DEFAULT;
    depthStencilDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    
    ID3D11Texture2D* pDepthStencilBuffer = nullptr;
    hr = g_device->CreateTexture2D(&depthStencilDesc, nullptr, &pDepthStencilBuffer);
    if(FAILED(hr))
    {
        std::cerr << "Failed to create depth stencil buffer after resize" << std::endl;
        return;
    }
    
    hr = g_device->CreateDepthStencilView(pDepthStencilBuffer, nullptr, &g_depthStencilView);
    pDepthStencilBuffer->Release();
    if(FAILED(hr))
    {
        std::cerr << "Failed to create depth stencil view after resize" << std::endl;
        return;
    }
    
    g_deviceContext->OMSetRenderTargets(1, &g_renderTargetView, g_depthStencilView);
    
    D3D11_VIEWPORT viewport = {};
    viewport.Width = static_cast<float>(width);
    viewport.Height = static_cast<float>(height);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    g_deviceContext->RSSetViewports(1, &viewport);
}

void WindowResizeCallback(GLFWwindow* window, int width, int height)
{
    if(width > 0 && height > 0)
        ResizeSwapChain(width, height);
}

std::string MakeUniqueName(const std::string& baseName, const std::vector<std::string>& existingNames)
{
    std::string uniqueName = baseName;
    int counter = 1;
    while(std::find(existingNames.begin(), existingNames.end(), uniqueName) != existingNames.end())
        uniqueName = baseName + " (" + std::to_string(counter++) + ")";
    return uniqueName;
}

Model LoadOBJModel(const std::string& path, float scale = 1.0f)
{
    Model model;

    std::cout << "Loading OBJ: " << path << std::endl;
    std::ifstream file(path);
    if(!file.is_open())
    {
        std::cerr << "Failed to open file: " << path << std::endl;
        return model;
    }

    model.vertices.clear();

    std::vector<DirectX::XMFLOAT3> positions;
    std::vector<DirectX::XMFLOAT3> normals;
    std::vector<DirectX::XMFLOAT2> texcoords;

    positions.push_back({0, 0, 0});
    normals.push_back({0, 0, 1});
    texcoords.push_back({0, 0});

    std::string line;
    while(std::getline(file, line))
    {
        std::istringstream iss(line);
        std::string type;
        iss >> type;

        if(type == "v")
        {
            float x, y, z;
            iss >> x >> y >> z;
            positions.emplace_back(x, y, z);
        }
        else if(type == "vn")
        {
            float nx, ny, nz;
            iss >> nx >> ny >> nz;
            normals.emplace_back(nx, ny, nz);
        }
        else if(type == "vt")
        {
            float u, v;
            iss >> u >> v;
            texcoords.emplace_back(u, 1.0f - v);
        }
        else if(type == "f")
        {
            std::string vertexData;
            std::vector<std::string> faceVertices;
            
            while(iss >> vertexData)
            {
                faceVertices.push_back(vertexData);
            }
            
            for(size_t i = 2; i < faceVertices.size(); ++i)
            {
                std::array<std::string, 3> triangleVerts = {
                    faceVertices[0], faceVertices[i-1], faceVertices[i]
                };
                
                for(const auto& vert : triangleVerts)
                {
                    Vertex vtx;
                    vtx.position = {0, 0, 0};
                    vtx.normal = {0, 0, 1};
                    vtx.texCoord = {0, 0};
                    
                    size_t slash1 = vert.find('/');
                    if(slash1 != std::string::npos)
                    {
                        int posIndex = std::stoi(vert.substr(0, slash1));
                        if(posIndex < 0) 
                            posIndex = positions.size() + posIndex;
                        else 
                            posIndex = posIndex;
                            
                        if(posIndex > 0 && posIndex < positions.size())
                        {
                            vtx.position = positions[posIndex];
                        }
                        
                        size_t slash2 = vert.find('/', slash1 + 1);
                        
                        if(slash2 > slash1 + 1)
                        {
                            int texIndex = std::stoi(vert.substr(slash1 + 1, slash2 - slash1 - 1));
                            if(texIndex < 0) 
                                texIndex = texcoords.size() + texIndex;
                                
                            if(texIndex > 0 && texIndex < texcoords.size())
                            {
                                vtx.texCoord = texcoords[texIndex];
                            }
                        }
                        
                        if(slash2 != std::string::npos && slash2 + 1 < vert.length())
                        {
                            int normIndex = std::stoi(vert.substr(slash2 + 1));
                            if(normIndex < 0) 
                                normIndex = normals.size() + normIndex;
                                
                            if(normIndex > 0 && normIndex < normals.size())
                            {
                                vtx.normal = normals[normIndex];
                            }
                        }
                    }
                    else
                    {
                        int posIndex = std::stoi(vert);
                        if(posIndex < 0) 
                            posIndex = positions.size() + posIndex;
                            
                        if(posIndex > 0 && posIndex < (int)positions.size())
                        {
                            DirectX::XMFLOAT3 pos = positions[posIndex];
                            vtx.position = { pos.x * scale, pos.y * scale, pos.z * scale };
                        }
                    }
                    
                    model.vertices.push_back(vtx);
                }
            }
        }
    }
    
    if(model.vertices.empty())
    {   
        std::cerr << "No vertices loaded from OBJ file: " << path << std::endl;
        return model;
    }
    
    if(normals.size() <= 1)
    {
        std::cout << "No normals in OBJ file, calculating face normals..." << std::endl;
        for(size_t i = 0; i < model.vertices.size(); i += 3)
        {
            if(i + 2 < model.vertices.size())
            {
                DirectX::XMVECTOR v0 = DirectX::XMLoadFloat3(&model.vertices[i].position);
                DirectX::XMVECTOR v1 = DirectX::XMLoadFloat3(&model.vertices[i+1].position);
                DirectX::XMVECTOR v2 = DirectX::XMLoadFloat3(&model.vertices[i+2].position);
                
                DirectX::XMVECTOR edge1 = DirectX::XMVectorSubtract(v1, v0);
                DirectX::XMVECTOR edge2 = DirectX::XMVectorSubtract(v2, v0);
                
                DirectX::XMVECTOR normal = DirectX::XMVector3Normalize(
                    DirectX::XMVector3Cross(edge1, edge2)
                );
                
                DirectX::XMFLOAT3 normalFloat3;
                DirectX::XMStoreFloat3(&normalFloat3, normal);
                
                model.vertices[i].normal = normalFloat3;
                model.vertices[i+1].normal = normalFloat3;
                model.vertices[i+2].normal = normalFloat3;
            }
        }
    }
    
    std::cout << "Loaded " << model.vertices.size() << " vertices from OBJ file" << std::endl;
    
    if(model.vertexBuffer)
    {
        model.vertexBuffer->Release();
        model.vertexBuffer = nullptr;
    }

    D3D11_BUFFER_DESC vbDesc = {};
    vbDesc.Usage = D3D11_USAGE_DEFAULT;
    vbDesc.ByteWidth = sizeof(Vertex) * model.vertices.size();
    vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = model.vertices.data();

    HRESULT hr = g_device->CreateBuffer(&vbDesc, &initData, &model.vertexBuffer);
    if(FAILED(hr))
    {
        std::cerr << "Failed to create vertex buffer! HRESULT: " << hr << std::endl;
    }
    else
    {
        std::cout << "Successfully created vertex buffer" << std::endl;
    }

    model.valid = true;

    return model;
}

ID3D11ShaderResourceView* LoadTextureIfNotLoaded(const std::string& path)
{
    for(const auto& info : g_texture_info)
    {
        if(info.path == path)
            return info.srv;
    }

    ID3D11ShaderResourceView* srv = LoadTexture(path);
    if(!srv)
        return nullptr;

    std::string fileName = std::filesystem::path(path).filename().string();
    std::vector<std::string> existingTextureNames;
    for(const auto& tex : g_textures)
        existingTextureNames.push_back(tex.name);
    std::string uniqueName = MakeUniqueName(fileName, existingTextureNames);

    //g_texture_info.push_back({ uniqueName, path, srv });
    //g_textures.push_back({ uniqueName, srv });

    return srv;
}

void AddTextureNoDupe(const std::string& name, ID3D11ShaderResourceView* srv)
{
    for(const auto& t : g_textures)
    {
        if(t.name == name && t.srv == srv)
            return;
    }
    g_textures.push_back({ name, srv });
}

void LoadProject(const std::string& path, std::vector<Model>& models)
{
    std::ifstream file(path, std::ios::binary);
    if(!file.is_open())
    {
        std::cerr << "Failed to open file for loading: " << path << std::endl;
        return;
    }

    for(auto& model : models)
    {
        if(model.vertexBuffer) model.vertexBuffer->Release();
    }
    models.clear();

    char header[8] = {0};
    file.read(header, 7);
    if(strcmp(header, "GE3PROJ") != 0)
    {
        std::cerr << "Invalid project file format" << std::endl;
        file.close();
        return;
    }

    uint32_t version;
    file.read(reinterpret_cast<char*>(&version), sizeof(version));
    if(version != 1)
    {
        std::cerr << "Unsupported project file version" << std::endl;
        file.close();
        return;
    }

    uint32_t modelCount;
    file.read(reinterpret_cast<char*>(&modelCount), sizeof(modelCount));

    uint32_t scriptCount;
    file.read(reinterpret_cast<char*>(&scriptCount), sizeof(scriptCount));

    for(uint32_t i = 0; i < scriptCount; i++)
    {
        std::string script;
        uint32_t scriptNameLen;
        file.read(reinterpret_cast<char*>(&scriptNameLen), sizeof(scriptNameLen));
        script.resize(scriptNameLen);
        file.read(&script[0], scriptNameLen);
        scripts.push_back(script);
    }

    for(uint32_t i = 0; i < modelCount; i++)
    {
        Model model;
        model.valid = true;

        uint32_t nameLength;
        file.read(reinterpret_cast<char*>(&nameLength), sizeof(nameLength));
        model.name.resize(nameLength);
        file.read(&model.name[0], nameLength);

        uint32_t pathLength;
        file.read(reinterpret_cast<char*>(&pathLength), sizeof(pathLength));
        model.filePath.resize(pathLength);
        file.read(&model.filePath[0], pathLength);

        file.read(reinterpret_cast<char*>(&model.position), sizeof(DirectX::XMFLOAT3));

        file.read(reinterpret_cast<char*>(&model.rotation), sizeof(DirectX::XMFLOAT3));

        file.read(reinterpret_cast<char*>(&model.scale), sizeof(float));

        uint32_t vertexCount;
        file.read(reinterpret_cast<char*>(&vertexCount), sizeof(vertexCount));

        model.vertices.resize(vertexCount);
        file.read(reinterpret_cast<char*>(model.vertices.data()), vertexCount * sizeof(Vertex));

        D3D11_BUFFER_DESC vbDesc = {};
        vbDesc.Usage = D3D11_USAGE_DEFAULT;
        vbDesc.ByteWidth = sizeof(Vertex) * model.vertices.size();
        vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

        D3D11_SUBRESOURCE_DATA initData = {};
        initData.pSysMem = model.vertices.data();

        HRESULT hr = g_device->CreateBuffer(&vbDesc, &initData, &model.vertexBuffer);
        if(FAILED(hr))
        {
            std::cerr << "Failed to create vertex buffer! HRESULT: " << hr << std::endl;
            model.valid = false;
        }

        bool hasTexture;
        file.read(reinterpret_cast<char*>(&hasTexture), sizeof(bool));

        if(hasTexture)
        {
            uint32_t textureNameLength;
            file.read(reinterpret_cast<char*>(&textureNameLength), sizeof(textureNameLength));
            std::string textureName(textureNameLength, '\0');
            file.read(&textureName[0], textureNameLength);

            uint32_t texturePathLength;
            file.read(reinterpret_cast<char*>(&texturePathLength), sizeof(texturePathLength));
            std::string texturePath(texturePathLength, '\0');
            file.read(&texturePath[0], texturePathLength);

            std::cout << "Loading texture '" << textureName << "' from path: " << texturePath << '\n';

            bool found = false;
            for(const auto& tex : g_texture_info)
            {
                if(tex.path == texturePath)
                {
                    model.textureSRV = tex.srv;
                    found = true;
                    break;
                }
            }

            if(!found)
            {
                std::cout << "Texture not found in memory. Trying to load from path: " << texturePath << '\n';
                ID3D11ShaderResourceView* srv = LoadTextureIfNotLoaded(texturePath);
                if(srv)
                {
                    model.textureSRV = srv;
                    g_texture_info.push_back({ textureName, texturePath, srv });
                    AddTextureNoDupe(textureName, srv);
                }
                else
                {
                    std::cerr << "Failed to load texture from path: " << texturePath << '\n';
                }
            }
        }
        models.push_back(model);
    }

    file.close();
    std::cout << "Project loaded from: " << path << std::endl;
}

bool DecompressData(const std::vector<char>& input, std::vector<char>& output, size_t expectedSize = 4096)
{
    output.resize(expectedSize);
    uLongf outLen = expectedSize;
    int res = uncompress(reinterpret_cast<Bytef*>(output.data()), &outLen,
                         reinterpret_cast<const Bytef*>(input.data()), input.size());
    if(res == Z_BUF_ERROR)
    {
        return DecompressData(input, output, expectedSize * 2);
    }
    if (res != Z_OK) return false;
    output.resize(outLen);
    return true;
}

std::unordered_map<std::string, std::vector<char>> LoadRPF(const std::string& filePath)
{
    std::unordered_map<std::string, std::vector<char>> fileMap;
    std::ifstream in(filePath, std::ios::binary);
    if(!in)
    {
        std::cerr << "Failed to open RPF: " << filePath << std::endl;
        return fileMap;
    }

    uint32_t fileCount;
    in.read(reinterpret_cast<char*>(&fileCount), sizeof(fileCount));

    struct FileHeader {
        char path[256];
        uint32_t offset;
        uint32_t size;
    };

    std::vector<FileHeader> headers(fileCount);
    for(uint32_t i = 0; i < fileCount; ++i)
    {
        in.read(reinterpret_cast<char*>(&headers[i]), sizeof(FileHeader));
    }

    for(const auto& hdr : headers)
    {
        std::vector<char> data(hdr.size);
        in.seekg(hdr.offset);
        in.read(data.data(), hdr.size);
        fileMap[std::string(hdr.path)] = std::move(data);
    }

    return fileMap;
}

Model LoadOBJModelFromMemory(const std::vector<char>& data, float scale = 0.5f, DirectX::XMFLOAT3 position = {0.0f, 0.0f, 0.0f}, DirectX::XMFLOAT3 rotation = {0.0f, 0.0f, 0.0f})
{
    Model model;
    
    model.vertices.clear();

    std::vector<DirectX::XMFLOAT3> positions;
    std::vector<DirectX::XMFLOAT3> normals;
    std::vector<DirectX::XMFLOAT2> texcoords;

    positions.push_back({0, 0, 0});
    normals.push_back({0, 0, 1});
    texcoords.push_back({0, 0});
    
    std::string line;
    std::istringstream stream(std::string(data.begin(), data.end()));  // Treat raw bytes as a string stream

    while(std::getline(stream, line)) 
    {
        std::istringstream iss(line);
        std::string type;
        iss >> type;

        if(type == "v")
        {
            float x, y, z;
            iss >> x >> y >> z;
            positions.emplace_back(x, y, z);
        }
        else if(type == "vn")
        {
            float nx, ny, nz;
            iss >> nx >> ny >> nz;
            normals.emplace_back(nx, ny, nz);
        }
        else if(type == "vt")
        {
            float u, v;
            iss >> u >> v;
            texcoords.emplace_back(u, 1.0f - v);
        }
        else if(type == "f")
        {
            std::string vertexData;
            std::vector<std::string> faceVertices;
            
            while(iss >> vertexData)
            {
                faceVertices.push_back(vertexData);
            }
            
            for(size_t i = 2; i < faceVertices.size(); ++i)
            {
                std::array<std::string, 3> triangleVerts = {
                    faceVertices[0], faceVertices[i-1], faceVertices[i]
                };
                
                for(const auto& vert : triangleVerts)
                {
                    Vertex vtx;
                    vtx.position = {0, 0, 0};
                    vtx.normal = {0, 0, 1};
                    vtx.texCoord = {0, 0};
                    
                    size_t slash1 = vert.find('/');
                    if(slash1 != std::string::npos)
                    {
                        int posIndex = std::stoi(vert.substr(0, slash1));
                        if(posIndex < 0) 
                            posIndex = positions.size() + posIndex;
                        else 
                            posIndex = posIndex;
                            

                        if(posIndex > 0 && posIndex < positions.size())
                        {
                            vtx.position = positions[posIndex];
                        }
                        
                        size_t slash2 = vert.find('/', slash1 + 1);
                        
                        if(slash2 > slash1 + 1)
                        {
                            int texIndex = std::stoi(vert.substr(slash1 + 1, slash2 - slash1 - 1));
                            if(texIndex < 0) 
                                texIndex = texcoords.size() + texIndex;
                                  
                            if(texIndex > 0 && texIndex < texcoords.size())
                            {
                                vtx.texCoord = texcoords[texIndex];
                            }
                        }
                        
                        if(slash2 != std::string::npos && slash2 + 1 < vert.length())
                        {
                            int normIndex = std::stoi(vert.substr(slash2 + 1));
                            if(normIndex < 0) 
                                normIndex = normals.size() + normIndex;
                                
                            if(normIndex > 0 && normIndex < normals.size())
                            {
                                vtx.normal = normals[normIndex];
                            }
                        }
                    }
                    else
                    {
                        int posIndex = std::stoi(vert);
                        if(posIndex < 0) 
                            posIndex = positions.size() + posIndex;
                            
                        if(posIndex > 0 && posIndex < (int)positions.size())
                        {
                            DirectX::XMFLOAT3 pos = positions[posIndex];
                            vtx.position = { pos.x * scale, pos.y * scale, pos.z * scale };
                        }
                    }
                    
                    model.vertices.push_back(vtx);
                }
            }
        }
    }
    
    if(model.vertices.empty())
    {   
        std::cerr << "No vertices loaded from OBJ data" << std::endl;
        return model;
    }
    
    if(normals.size() <= 1)
    {
        for(size_t i = 0; i < model.vertices.size(); i += 3)
        {
            if(i + 2 < model.vertices.size())
            {
                DirectX::XMVECTOR v0 = DirectX::XMLoadFloat3(&model.vertices[i].position);
                DirectX::XMVECTOR v1 = DirectX::XMLoadFloat3(&model.vertices[i+1].position);
                DirectX::XMVECTOR v2 = DirectX::XMLoadFloat3(&model.vertices[i+2].position);
                
                DirectX::XMVECTOR edge1 = DirectX::XMVectorSubtract(v1, v0);
                DirectX::XMVECTOR edge2 = DirectX::XMVectorSubtract(v2, v0);
                
                DirectX::XMVECTOR normal = DirectX::XMVector3Normalize(
                    DirectX::XMVector3Cross(edge1, edge2)
                );
                
                DirectX::XMFLOAT3 normalFloat3;
                DirectX::XMStoreFloat3(&normalFloat3, normal);
                
                model.vertices[i].normal = normalFloat3;
                model.vertices[i+1].normal = normalFloat3;
                model.vertices[i+2].normal = normalFloat3;
            }
        }
    }
    
    DirectX::XMFLOAT3 min = model.vertices[0].position;
    DirectX::XMFLOAT3 max = model.vertices[0].position;

    for(const Vertex& v : model.vertices)
    {
        const auto& pos = v.position;
        min.x = std::min(min.x, pos.x);
        min.y = std::min(min.y, pos.y);
        min.z = std::min(min.z, pos.z);

        max.x = std::max(max.x, pos.x);
        max.y = std::max(max.y, pos.y);
        max.z = std::max(max.z, pos.z);
    }

    model.localBounds = { min, max };
    
    if(model.vertexBuffer)
    {
        model.vertexBuffer->Release();
        model.vertexBuffer = nullptr;
    }

    D3D11_BUFFER_DESC vbDesc = {};
    vbDesc.Usage = D3D11_USAGE_DEFAULT;
    vbDesc.ByteWidth = sizeof(Vertex) * model.vertices.size();
    vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = model.vertices.data();

    HRESULT hr = g_device->CreateBuffer(&vbDesc, &initData, &model.vertexBuffer);
    if(FAILED(hr))
    {
        std::cerr << "Failed to create vertex buffer! HRESULT: " << hr << std::endl;
    }
    else
    {
        //std::cout << "Successfully created vertex buffer" << std::endl;
    }

    model.valid = true;
    model.rotation = rotation;
    model.scale = scale;
    model.position = position;

    return model;
}

void ComputeLocalBounds(Model& model)
{
    if(model.vertices.empty()) return;

    DirectX::XMFLOAT3 min = model.vertices[0].position;
    DirectX::XMFLOAT3 max = model.vertices[0].position;

    for(const Vertex& v : model.vertices)
    {
        const auto& pos = v.position;
        min.x = std::min(min.x, pos.x);
        min.y = std::min(min.y, pos.y);
        min.z = std::min(min.z, pos.z);

        max.x = std::max(max.x, pos.x);
        max.y = std::max(max.y, pos.y);
        max.z = std::max(max.z, pos.z);
    }

    model.localBounds = { min, max };
}

ID3D11ShaderResourceView* LoadTextureFromMemoryIfNotLoaded(const std::string& name, const void* data, size_t size)
{
    for(const auto& tex : g_textures)
    {
        if (tex.name == name)
            return tex.srv;
    }

    ID3D11ShaderResourceView* srv = LoadTextureFromMemory(data, size);
    if(!srv)
        return nullptr;

    g_textures.push_back({ name, srv });
    return srv;
}

void LoadAssetsFromRPF(const std::unordered_map<std::string, std::vector<char>>& rpfFiles)
{
    std::cout << "[DEBUG]: Entered LoadAssetsFromRPF\n";
    auto it = rpfFiles.find("manifest.xml");
    if(it == rpfFiles.end())
    {
        std::cerr << "No manifest.xml in RPF!" << std::endl;
        return;
    }

    std::vector<char> xmlDecompressed;
    if(!DecompressData(it->second, xmlDecompressed))
    {
        std::cerr << "Failed to decompress manifest.xml!" << std::endl;
        return;
    }

    /* debug
    {
        std::ofstream debugOut("debug_manifest.xml", std::ios::binary);
        if(debugOut.is_open())
        {
            debugOut.write(xmlDecompressed.data(), xmlDecompressed.size());
            std::cout << "Extracted manifest.xml to debug_manifest.xml\n";
        }
        else
        {
            std::cerr << "Failed to open debug_manifest.xml for writing\n";
        }
    }*/

    tinyxml2::XMLDocument doc;
    if(doc.Parse(xmlDecompressed.data(), xmlDecompressed.size()) != tinyxml2::XML_SUCCESS)
    {
        std::cerr << "Failed to parse XML manifest!" << std::endl;
        return;
    }

    std::cout << "[DEBUG]: XML decompressed and parsed\n";

    /* Debug: extract all files from the RPF to disk
    {
        std::filesystem::path outputDir = "debug_extracted";
        std::filesystem::create_directories(outputDir);

        for(const auto& [path, data] : rpfFiles)
        {
            std::filesystem::path outPath = outputDir / path;
            std::filesystem::create_directories(outPath.parent_path());

            std::ofstream outFile(outPath, std::ios::binary);
            if(!outFile)
            {
                std::cerr << "Failed to write " << outPath << '\n';
                continue;
            }

            std::vector<char> decompressed;
            if(!DecompressData(data, decompressed))
                std::cerr << "Failed to decompress\n";
            outFile.write(decompressed.data(), decompressed.size());
            std::cout << "Extracted " << path << " to " << outPath << '\n';
        }
    }*/

    auto* root = doc.FirstChildElement("Assets");
    if(!root) return;

    for(auto* texElem = root->FirstChildElement("Texture"); texElem; texElem = texElem->NextSiblingElement("Texture"))
    {
        const char* path = texElem->Attribute("path");
        if(path && rpfFiles.count(path))
        {
            std::string name = std::filesystem::path(path).filename().string();
            const std::vector<char>& textureData = rpfFiles.at(path);
            std::vector<char> decompressedTexture;
            if(DecompressData(textureData, decompressedTexture))
            {
                ID3D11ShaderResourceView* srv = LoadTextureFromMemoryIfNotLoaded(name, decompressedTexture.data(), decompressedTexture.size());

                if(srv)
                {
                    std::cout << "[DEBUG]: Loaded texture " << name << '\n';
                    g_textures.push_back({ name, srv });
                }
            }
            else
            {
                std::cout << "Failed to decompress texture.\n";
            }
        }
    }

    std::cout << "[DEBUG]: Loaded all textures\n";

    for(auto* modelElem = root->FirstChildElement("Model"); modelElem; modelElem = modelElem->NextSiblingElement("Model"))
    {
        std::cout << "[DEBUG]: Entered model loading loop\n";
        DirectX::XMFLOAT3 position, rotation;
        float scale;
        float mass;
        int isStatic;
        const char* path = modelElem->Attribute("path");
        if(path && rpfFiles.count(path))
        {
            const char* positionStr = modelElem->Attribute("position");
            const char* rotationStr = modelElem->Attribute("rotation");
            const char* scaleStr = modelElem->Attribute("scale");
            const char* id = modelElem->Attribute("id");
            const char* name = modelElem->Attribute("name");
            const char* massStr = modelElem->Attribute("mass");
            const char* isStaticStr = modelElem->Attribute("isStatic");

            if(positionStr && rotationStr && scaleStr && id && name && massStr && isStaticStr)
            {
                sscanf(positionStr, "%f,%f,%f", &position.x, &position.y, &position.z);
                sscanf(rotationStr, "%f,%f,%f", &rotation.x, &rotation.y, &rotation.z);
                sscanf(scaleStr, "%f", &scale);
                sscanf(massStr, "%f", &mass);
                sscanf(isStaticStr, "%d", &isStatic);

                std::cout << "[DEBUG]: Got all values of Model from XML\n";

                std::vector<char> decompressedModel;
                if(DecompressData(rpfFiles.at(path), decompressedModel))
                {
                    std::cout << "[DEBUG]: Model decompressed\n";
                    Model m = LoadOBJModelFromMemory(decompressedModel, scale, position, rotation);
                    std::cout << "[DEBUG]: Model loaded from memory\n";

                    if(name)
                        m.name = name;

                    if(id)
                    {
                        m.id = id;
                        for(const auto& tex : g_textures)
                        {
                            if(tex.name == id)
                            {
                                m.textureSRV = tex.srv;
                                std::cout << "Texture " << id << " assigned to model " << m.name << '\n';
                                break;
                            }
                        }
                    }
                    m.mass = mass;
                    m.isStatic = (bool)isStatic;

                    ComputeLocalBounds(m);
                    std::cout << "[DEBUG]: Local bounds computed\n";

                    float width = (m.localBounds.max.x - m.localBounds.min.x) * m.scale;
                    float height = (m.localBounds.max.y - m.localBounds.min.y) * m.scale;
                    float depth = (m.localBounds.max.z - m.localBounds.min.z) * m.scale;

                    physx::PxVec3 halfExtents(width / 2.0f, height / 2.0f, depth / 2.0f);
                    physx::PxBoxGeometry boxGeometry(halfExtents);

                    float pitch = DirectX::XMConvertToRadians(m.rotation.x);
                    float yaw   = DirectX::XMConvertToRadians(m.rotation.y);
                    float roll  = DirectX::XMConvertToRadians(m.rotation.z);
                    //DirectX::XMMATRIX rotationMatrix = DirectX::XMMatrixRotationRollPitchYaw(roll, pitch, yaw);

                    std::vector<physx::PxVec3> convexVerts;
                    for(const Vertex& v : m.vertices)
                    {
                        convexVerts.emplace_back(v.position.x * m.scale, v.position.y * m.scale, v.position.z * m.scale);
                    }

                    std::cout << "[DEBUG]: Convex vertices constructed\n";

                    if(convexVerts.size() < 4)
                    {
                        std::cerr << "Too few vertices to create a convex mesh: " << convexVerts.size() << "\n";
                        return;
                    }

                    for(const auto& vert : convexVerts)
                    {
                        if(!physx::PxIsFinite(vert.x) || !physx::PxIsFinite(vert.y) || !physx::PxIsFinite(vert.z))
                        {
                            std::cerr << "Invalid vertex detected: " << vert.x << ", " << vert.y << ", " << vert.z << "\n";
                            return;
                        }
                    }

                    physx::PxBounds3 bounds = physx::PxBounds3::empty();
                    for(const auto& vert : convexVerts)
                    {
                        bounds.include(vert);
                    }
                    physx::PxVec3 size = bounds.getExtents();
                    if(size.magnitude() < 0.001f || size.magnitude() > 1000.0f)
                    {
                        std::cerr << "Mesh size out of reasonable bounds: " << size.magnitude() << "\n";
                        return;
                    }

                    gMaterial = gPhysics->createMaterial(0.5f, 0.5f, 0.3f);

                    physx::PxConvexMeshDesc convexDesc;
                    convexDesc.points.count = static_cast<physx::PxU32>(convexVerts.size());
                    convexDesc.points.stride = sizeof(physx::PxVec3);
                    convexDesc.points.data = convexVerts.data();
                    convexDesc.flags = physx::PxConvexFlag::eCOMPUTE_CONVEX;

                    physx::PxDefaultMemoryOutputStream stream;
                    physx::PxTolerancesScale scale;
                    physx::PxCookingParams cookingParams(scale);
                    cookingParams.meshPreprocessParams |= physx::PxMeshPreprocessingFlag::eDISABLE_CLEAN_MESH;
                    cookingParams.meshPreprocessParams |= physx::PxMeshPreprocessingFlag::eDISABLE_ACTIVE_EDGES_PRECOMPUTE;

                    if(!PxCookConvexMesh(cookingParams, convexDesc, stream))
                    {
                        std::cerr << "Failed to cook convex mesh\n";
                        return;
                    }

                    std::cout << "[DEBUG]: Convex mesh cooked\n";

                    physx::PxDefaultMemoryInputData input(stream.getData(), stream.getSize());
                    physx::PxConvexMesh* convexMesh = gPhysics->createConvexMesh(input);
                    if(!convexMesh)
                    {
                        std::cerr << "Failed to create convex mesh\n";
                        return;
                    }

                    std::cout << "[DEBUG]: Convex mesh created\n";

                    DirectX::XMVECTOR q = DirectX::XMQuaternionRotationRollPitchYaw(
                            DirectX::XMConvertToRadians(m.rotation.x),
                            DirectX::XMConvertToRadians(m.rotation.y),
                            DirectX::XMConvertToRadians(m.rotation.z)
                            );
                    DirectX::XMFLOAT4 qf;
                    DirectX::XMStoreFloat4(&qf, q);
                    physx::PxQuat pxQuat(qf.x, qf.y, qf.z, qf.w);

                    physx::PxConvexMeshGeometry convexGeom(convexMesh);
                    physx::PxTransform transform(
                            physx::PxVec3(m.position.x, m.position.y, m.position.z),
                            pxQuat
                            );

                    if(!convexGeom.isValid())
                    {
                        std::cerr << "convexGeom is invalid!\n";
                        return;
                    }

                    physx::PxRigidDynamic* actor = gPhysics->createRigidDynamic(transform);
                    if(!actor)
                    {
                        std::cerr << "actor is invalid\n";
                        return;
                    }
                    physx::PxPvdSceneClient* pvdClient = gScene->getScenePvdClient();
                    if(pvdClient)
                    {
                        pvdClient->setScenePvdFlag(physx::PxPvdSceneFlag::eTRANSMIT_CONSTRAINTS, true);
                        pvdClient->setScenePvdFlag(physx::PxPvdSceneFlag::eTRANSMIT_CONTACTS, true);
                        pvdClient->setScenePvdFlag(physx::PxPvdSceneFlag::eTRANSMIT_SCENEQUERIES, true);
                    }
                    physx::PxShape* shape = gPhysics->createShape(convexGeom, *gMaterial, true);
                    if(!shape->isExclusive())
                    {
                        std::cerr << "Shape is not exclusive, may be shared or reused improperly\n";
                    }
                    if(!shape)
                    {
                        std::cerr << "Failed to create shape\n";
                        return;
                    }
                    std::cout << "[DEBUG]: Shape created\n";
                    actor->attachShape(*shape);

                    std::cout << "[DEBUG]: Shape attached\n";

                    if(m.mass > 0.0f && actor->getNbShapes() > 0)
                    {
                        bool result = physx::PxRigidBodyExt::updateMassAndInertia(*actor, m.mass);
                        if (!result)
                        {
                            std::cerr << "[ERROR]: updateMassAndInertia failed.\n";
                            return;
                        }
                        else
                        {
                            std::cout << "[DEBUG]: Mass and inertia updated\n";
                        }
                    }
                    else
                    {
                        std::cerr << "[ERROR]: Invalid mass or no shapes on actor.\n";
                        return;
                    }
                    actor->setMass(m.mass);

                    if(!actor->isReleasable())
                    {
                        std::cerr << "[ERROR]: Actor is not releasable\n";
                        return;
                    }
                    std::cout << "[DEBUG]: Actor is releasable\n";

                    std::cout << "[DEBUG]: Trying to add actor to scene\n";
                    gScene->addActor(*actor);
                    std::cout << "[DEBUG]: Actor added to scene\n";

                    std::cout << "[DEBUG]: Updated mass and intertia, actor woken up, added actor to scene\n";

                    m.rigidBody = actor;

                    g_models.push_back(m);

                    std::cout << "Model: " << m.name << "\n";
                    std::cout << "  Dimensions: " << width << ", " << height << ", " << depth << "\n";
                    std::cout << "  Mass: " << actor->getMass() << "\n";
                    std::cout << "  Initial Position: " << m.position.x << ", " << m.position.y << ", " << m.position.z << "\n";
                    physx::PxTransform worldPose = actor->getGlobalPose();
                    physx::PxTransform shapePose = shape->getLocalPose();
                    std::cout << "  Actor global pose: pos(" << worldPose.p.x << "," << worldPose.p.y << "," << worldPose.p.z << ") rot(" << worldPose.q.x << "," << worldPose.q.y << "," << worldPose.q.z << "," << worldPose.q.w << ")\n";
                    std::cout << "  Shape local pose: rot(" << shapePose.q.x << "," << shapePose.q.y << "," << shapePose.q.z << "," << shapePose.q.w << ")\n";
                }
                else
                {
                    std::cout << "Failed to decompress model.\n";
                }
            }
        }
    }

    for(auto* camElem = root->FirstChildElement("Camera"); camElem; camElem = camElem->NextSiblingElement("Camera"))
    {
        DirectX::XMFLOAT3 position;
        DirectX::XMFLOAT2 rotation;
        float fov;
        int isFPS;
        Camera cam;
        const char* camPosStr = camElem->Attribute("position");
        const char* camRotStr = camElem->Attribute("rotation");
        const char* camFovStr = camElem->Attribute("fov");
        const char* camFPSStr = camElem->Attribute("isFPS");

        if(camPosStr && camRotStr && camFovStr && camFPSStr)
        {
            sscanf(camPosStr, "%f,%f,%f", &position.x, &position.y, &position.z);
            sscanf(camRotStr, "%f,%f", &rotation.x, &rotation.y);
            sscanf(camFovStr, "%f", &fov);
            sscanf(camFPSStr, "%d", &isFPS);
            cam.position = position;
            cam.rotation = rotation;
            cam.fov = fov;
            cam.isFPS = static_cast<bool>(isFPS);
            g_cameras.push_back(cam);
        }
    }

    for(auto* uiElem = root->FirstChildElement("UI"); uiElem; uiElem = uiElem->NextSiblingElement("UI"))
    {
        const char* typeStr = uiElem->Attribute("type");
        const char* position = uiElem->Attribute("position");
        const char* uiColor = uiElem->Attribute("color");
        const char* uiName = uiElem->Attribute("name");
        const char* uiText = uiElem->Attribute("text");
        const char* scaleStr = uiElem->Attribute("scale");

        if(!typeStr || !position || !uiColor || !uiName || !uiText || !scaleStr)
            continue;

        int typeInt = 0;
        sscanf(typeStr, "%d", &typeInt);
        UIElementType uiType = static_cast<UIElementType>(typeInt);

        if(uiType == UIElementType::Label)
        {
            auto label = std::make_unique<UILabel>();

            sscanf(position, "%f,%f", &label->x, &label->y);
            sscanf(uiColor, "%f,%f,%f,%f", &label->color.x, &label->color.y, &label->color.z, &label->color.w);
            sscanf(scaleStr, "%f", &label->textScale);
            label->name = uiName;
            label->text = uiText;

            uiElements.push_back(std::move(label));
        }
        else if(uiType == UIElementType::Button)
        {
            const char* widthStr = uiElem->Attribute("width");
            const char* heightStr = uiElem->Attribute("width");
            const char* hoverColorStr = uiElem->Attribute("hoverColor");

            if(!widthStr || !heightStr || !hoverColorStr)
                continue;

            auto button = std::make_unique<UIButton>();

            sscanf(position, "%f,%f", &button->x, &button->y);
            sscanf(uiColor, "%f,%f,%f,%f", &button->color.x, &button->color.y, &button->color.z, &button->color.w);
            sscanf(scaleStr, "%f", &button->textScale);
            sscanf(hoverColorStr, "%f,%f,%f,%f", &button->hoverColor.x, &button->hoverColor.y, &button->hoverColor.z, &button->hoverColor.w);
            sscanf(widthStr, "%f", &button->width);
            sscanf(heightStr, "%f", &button->height);
            button->name = uiName;
            button->text = uiText;

            uiElements.push_back(std::move(button));
        }
        else
        {
            std::cout << "Unsupported UI element type: " << typeInt << '\n';
            continue;
        }
    }

    for(auto* plElem = root->FirstChildElement("PL"); plElem; plElem = plElem->NextSiblingElement("PL"))
    {
        const char* nameStr = plElem->Attribute("name");
        const char* posStr = plElem->Attribute("position");
        const char* colorStr = plElem->Attribute("color");
        const char* rangeStr = plElem->Attribute("range");
        const char* intensityStr = plElem->Attribute("intensity");
        
        PointLight pl;

        if(nameStr && posStr && colorStr && rangeStr && intensityStr)
        {
            plNames.push_back(nameStr);
            sscanf(posStr, "%f,%f,%f", &pl.position.x, &pl.position.y, &pl.position.z);
            sscanf(colorStr, "%f,%f,%f", &pl.color.x, &pl.color.y, &pl.color.z);
            sscanf(rangeStr, "%f", &pl.range);
            sscanf(intensityStr, "%f", &pl.intensity);
            pointLights.push_back(pl);
        }
    }

    auto* fontElem = root->FirstChildElement("Font");
    if(fontElem)
    {
        const char* path = fontElem->Attribute("path");
        if(path && rpfFiles.count(path))
        {
            fontPath = path;
            const std::vector<char>& fontData = rpfFiles.at(path);
            if(fontPath.length() > 0)
            {
                std::wstring tempFontPath = WriteToTempFile(fontData.data(), fontData.size());
                g_spriteFont = std::make_unique<DirectX::SpriteFont>(g_device, tempFontPath.c_str());
                if(!g_spriteFont)
                {
                    std::cerr << "Failed to load font from memory!" << std::endl;
                }
            }
        }
    }

    auto* vsElem = root->FirstChildElement("VS");
    if(vsElem)
    {
        const char* path = vsElem->Attribute("path");
        if(path && rpfFiles.count(path))
        {
            vsPath = path;
            const std::vector<char>& vsRPFData = rpfFiles.at(path);
            std::vector<char> vsDecompressed;
            if(vsPath.length() > 0 && vsRPFData.size() > 0)
            {
                if(DecompressData(vsRPFData, vsDecompressed))
                    vsData = std::move(vsDecompressed);
            }
        }
    }
    auto* psElem = root->FirstChildElement("PS");
    if(psElem)
    {
        const char* path = psElem->Attribute("path");
        if(path && rpfFiles.count(path))
        {
            psPath = path;
            const std::vector<char>& psRPFData = rpfFiles.at(path);
            std::vector<char> psDecompressed;
            if(psPath.length() > 0 && psRPFData.size() > 0)
            {
                if(DecompressData(psRPFData, psDecompressed))
                    psData = std::move(psDecompressed);
            }
        }
    }
}

void UpdateWorldBounds(Model& m)
{
    float s = m.scale;
    const auto& lb = m.localBounds;

    m.worldBounds.min = {
      m.position.x + lb.min.x * s,
      m.position.y + lb.min.y * s,
      m.position.z + lb.min.z * s
    };
    m.worldBounds.max = {
      m.position.x + lb.max.x * s,
      m.position.y + lb.max.y * s,
      m.position.z + lb.max.z * s
    };
}


bool AABBvsAABB(const AABB& a, const AABB& b)
{
    return (a.min.x <= b.max.x && a.max.x >= b.min.x) &&
           (a.min.y <= b.max.y && a.max.y >= b.min.y) &&
           (a.min.z <= b.max.z && a.max.z >= b.min.z);
}

int getModel(std::string name)
{
    for(int i = 0; i < g_models.size(); ++i)
    {
        if(g_models[i].name == name)
            return i;
    }
    return -1;
}

int getUIElement(std::string name)
{
    for(int i = 0; i < uiElements.size(); ++i)
    {
        if(uiElements[i]->name == name)
            return i;
    }
    return -1;
}

int getPointLight(std::string name)
{
    for(int i = 0; i < plNames.size(); ++i)
    {
        if(plNames[i] == name)
            return i;
    }
    return -1;
}

GLFWwindow* window;

float lastMouseX = 0.0f, lastMouseY = 0.0f;
bool firstMouse = true;

// if FPS on a camera is enabled
void UpdateCameraLook(Camera& cam)
{
    double xpos, ypos;
    glfwGetCursorPos(window, &xpos, &ypos);

    if(firstMouse)
    {
        lastMouseX = xpos;
        lastMouseY = ypos;
        firstMouse = false;
    }

    float sensitivity = 0.1f;
    float dx = static_cast<float>(xpos - lastMouseX) * sensitivity;
    float dy = static_cast<float>(ypos - lastMouseY) * sensitivity;

    lastMouseX = xpos;
    lastMouseY = ypos;

    cam.rotation.y += dx;
    cam.rotation.x += dy;

    cam.rotation.x = std::clamp(cam.rotation.x, -89.0f, 89.0f);
}

// if FPS on a camera is enabled
void UpdateCameraMovement(Camera& cam, float deltaTime)
{
    float speed = 5.0f;
    DirectX::XMVECTOR pos = DirectX::XMLoadFloat3(&cam.position);

    DirectX::XMMATRIX rotMatrix = DirectX::XMMatrixRotationRollPitchYaw(
        DirectX::XMConvertToRadians(cam.rotation.x),
        DirectX::XMConvertToRadians(cam.rotation.y),
        0.0f);

    DirectX::XMVECTOR fullForward = XMVector3TransformCoord(
            DirectX::XMVectorSet(0, 0, 1, 0), rotMatrix);

    DirectX::XMFLOAT3 fwd;
    DirectX::XMStoreFloat3(&fwd, fullForward);
    fwd.y = 0.0f;
    DirectX::XMVECTOR flatForward = DirectX::XMVector3Normalize(DirectX::XMLoadFloat3(&fwd));
    DirectX::XMVECTOR right = DirectX::XMVector3TransformCoord(
        DirectX::XMVectorSet(1, 0, 0, 0), rotMatrix);

    if(glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
        pos = DirectX::XMVectorAdd(pos, DirectX::XMVectorScale(flatForward, speed * deltaTime));
    if(glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
        pos = DirectX::XMVectorSubtract(pos, DirectX::XMVectorScale(flatForward, speed * deltaTime));
    if(glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
        pos = DirectX::XMVectorSubtract(pos, DirectX::XMVectorScale(right, speed * deltaTime));
    if(glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
        pos = DirectX::XMVectorAdd(pos, DirectX::XMVectorScale(right, speed * deltaTime));
    if(glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        exit(0);

    DirectX::XMStoreFloat3(&cam.position, pos);
}

void UpdateCharacterController(float deltaTime)
{
    physx::PxVec3 moveDir(0.0f);

    float speed = 0.1f;

    static bool isJumping = false;

    float yaw = DirectX::XMConvertToRadians(g_cameras[0].rotation.y);

    physx::PxVec3 forwardDir(sinf(yaw), 0.0f, cosf(yaw));
    physx::PxVec3 rightDir(forwardDir.z, 0.0f, -forwardDir.x);

    physx::PxVec3 inputDir(0.0f);
    if(glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
        inputDir += forwardDir;
    if(glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
        inputDir -= forwardDir;
    if(glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
        inputDir -= rightDir;
    if(glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
        inputDir += rightDir;

    if(inputDir.magnitudeSquared() > 0)
        inputDir = inputDir.getNormalized();

    static physx::PxVec3 velocity(0.0f);
    const float gravity = 9.81f / 1000.0f;
    velocity.y -= gravity * deltaTime;

    physx::PxVec3 finalMove = (inputDir * speed) + velocity;

    //physx::PxTransform t = gCharacterActor->getGlobalPose();

    physx::PxControllerCollisionFlags collisionFlags = gPlayerController->move(finalMove * deltaTime, 0.001f, deltaTime, physx::PxControllerFilters());
    //physx::PxVec3 newPos = t.p + finalMove * deltaTime;
    //gCharacterActor->setKinematicTarget(physx::PxTransform(newPos));

    if(collisionFlags & physx::PxControllerCollisionFlag::eCOLLISION_DOWN)
    {
        isJumping = false;
        velocity.y = 0.0f;
    }
    if(glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS && !isJumping)
        velocity.y = 0.1f;
}

float GetDeltaTime()
{
    static double lastTime = glfwGetTime();
    double currentTime = glfwGetTime();
    float delta = static_cast<float>(currentTime - lastTime);
    lastTime = currentTime;
    return delta;
}

void key_callback(GLFWwindow *window, int key, int scancode, int action, int mods)
{
    // Your code here
}

int activeCam = 0;

void OnInit()
{
    using namespace physx;

    PxTransform groundPose = PxTransform(PxQuat(PxHalfPi, PxVec3(0, 0, 1))); // Rotate Z axis into Y

    PxRigidStatic* ground = gPhysics->createRigidStatic(groundPose);
    if(!ground)
    {
        std::cerr << "[ERROR]: Failed to create ground\n";
        return;
    }

    PxShape* shape = PxRigidActorExt::createExclusiveShape(*ground, PxPlaneGeometry(), *gMaterial);
    if(!shape)
    {
        std::cerr << "[ERROR]: Failed to create shape for ground\n";
        return;
    }

    gScene->addActor(*ground);
    std::cout << "[DEBUG]: Ground added to scene\n";

    if(g_cameras[0].isFPS)
    {
        gControllerManager = PxCreateControllerManager(*gScene);

        PxCapsuleControllerDesc desc;
        desc.height = 1.5f;
        desc.radius = 0.3f;
        desc.position = PxExtendedVec3(g_cameras[0].position.x, 2, g_cameras[0].position.z);
        desc.material = gMaterial;
        desc.stepOffset = 0.5f;
        desc.slopeLimit = 45.0f;
        desc.contactOffset = 0.1f;
        desc.upDirection = PxVec3(0, 1, 0);
        desc.climbingMode = PxCapsuleClimbingMode::eCONSTRAINED;

        gPlayerController = gControllerManager->createController(desc);
    }
    
    // Your code
}

double GetTimeSeconds()
{
    using namespace std::chrono;
    static auto start = high_resolution_clock::now();
    auto now = high_resolution_clock::now();
    return duration<double>(now - start).count();
}

void OnUpdate()
{
    /*for(auto& model : g_models)
        UpdateWorldBounds(model);*/

    /* Check collision
    for(int i = 0; i < g_models.size(); ++i)
    {
        for(int j = i + 1; j < g_models.size(); ++j)
        {
            if(AABBvsAABB(g_models[i].worldBounds, g_models[j].worldBounds))
            {
                std::cout << "Collision: " << g_models[i].name << " and " << g_models[j].name << "\n";
            }
        }
    }*/

    float deltaTime = GetDeltaTime();
    float fps = 1.0f / std::max(deltaTime, 0.0001f);

    //std::cout << "\rFPS: " << fps << std::flush;

    /*gScene->simulate(deltaTime);
    gScene->fetchResults(true);*/

    static double accumulator = 0.0;
    static double lastTime = GetTimeSeconds();

    double currentTime = GetTimeSeconds();
    double frameTime = currentTime - lastTime;
    lastTime = currentTime;

    frameTime = std::min(frameTime, 0.1);

    accumulator += frameTime;

    const double fixedDeltaTime = 1.0 / 60.0;

    while(accumulator >= fixedDeltaTime)
    {
        gScene->simulate(fixedDeltaTime);
        gScene->fetchResults(true);
        accumulator -= fixedDeltaTime;
    }
     
    for(auto& model : g_models)
    {
        if(model.rigidBody && !model.isStatic)
        {
            physx::PxTransform transform = model.rigidBody->getGlobalPose();

            DirectX::XMFLOAT3 pos(transform.p.x, transform.p.y, transform.p.z);
            DirectX::XMFLOAT4 rot(transform.q.x, transform.q.y, transform.q.z, transform.q.w);

            model.worldMatrix = DirectX::XMMatrixAffineTransformation(
                    DirectX::XMVectorSet(model.scale, model.scale, model.scale, 0),
                    DirectX::XMVectorZero(),
                    DirectX::XMLoadFloat4(&rot),
                    DirectX::XMLoadFloat3(&pos)
                    );
        }
        else
        {
            DirectX::XMVECTOR scaleVec = DirectX::XMVectorSet(model.scale, model.scale, model.scale, 0);

            float pitch = DirectX::XMConvertToRadians(model.rotation.x);
            float yaw   = DirectX::XMConvertToRadians(model.rotation.y);
            float roll  = DirectX::XMConvertToRadians(model.rotation.z);
            DirectX::XMVECTOR q = DirectX::XMQuaternionRotationRollPitchYaw(pitch, yaw, roll);

            DirectX::XMVECTOR posVec = DirectX::XMLoadFloat3(&model.position);
            model.worldMatrix = DirectX::XMMatrixAffineTransformation(
                    scaleVec,
                    DirectX::XMVectorZero(),
                    q,
                    posVec
                    );
        }
    }

    if(g_cameras[0].isFPS)
    {
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        UpdateCameraLook(g_cameras[0]);
        UpdateCharacterController(fixedDeltaTime);

        physx::PxExtendedVec3 pos = gPlayerController->getPosition();
        g_cameras[0].position = { static_cast<float>(pos.x), static_cast<float>(pos.y + 0.6f), static_cast<float>(pos.z) };
    }

    // Your code
}

int main()
{
    bool use_vsync = false;
    InitPhysics();
    std::cout << "PhysX physics initialized\n";

    auto rpfFiles = LoadRPF("main.rpf");

    if(!glfwInit())
    {
        std::cerr << "Failed to init GLFW" << '\n';
        return -1;
    }

    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    window = glfwCreateWindow(800, 600, "Gica Engine V3 Game", nullptr, nullptr);
    if(!window)
    {
        glfwTerminate();
        std::cerr << "Failed to create window" << '\n';
        return -1;
    }

    glfwSetWindowSizeCallback(window, WindowResizeCallback);
    glfwSetKeyCallback(window, key_callback);
    glfwSetFramebufferSizeCallback(window, WindowResizeCallback);

    glfwMakeContextCurrent(window);

    HWND hwnd = glfwGetWin32Window(window);
    std::cout << "===== Initializing DirectX 11 =====" << '\n';
    InitDX(hwnd, window);
    std::cout << "===== DirectX 11 Intialized =====" << '\n';

    std::cout << "===== Loading assets from RPF =====" << '\n';
    LoadAssetsFromRPF(rpfFiles);
    std::cout << "===== Assets loaded from RPF =====" << '\n';

    std::cout << "===== Compiling shaders =====" << '\n';
    CompileShaders();
    std::cout << "===== Shaders compiled =====" << '\n';

    /*for(Model& model : g_models)
        ComputeLocalBounds(model);*/

    float clearColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};

    D3D11_DEPTH_STENCIL_DESC depthStencilDesc = {};
    depthStencilDesc.DepthEnable = TRUE;
    depthStencilDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    depthStencilDesc.DepthFunc = D3D11_COMPARISON_LESS;

    depthStencilDesc.StencilEnable = FALSE;
    depthStencilDesc.StencilReadMask = D3D11_DEFAULT_STENCIL_READ_MASK;
    depthStencilDesc.StencilWriteMask = D3D11_DEFAULT_STENCIL_WRITE_MASK;

    g_device->CreateDepthStencilState(&depthStencilDesc, &g_depthStencilState);

    D3D11_RASTERIZER_DESC rasterizerDesc = {};
    rasterizerDesc.CullMode = D3D11_CULL_BACK;
    rasterizerDesc.FillMode = D3D11_FILL_SOLID;
    rasterizerDesc.FrontCounterClockwise = FALSE;
    rasterizerDesc.DepthBias = 0;
    rasterizerDesc.SlopeScaledDepthBias = 0;
    rasterizerDesc.DepthClipEnable = TRUE;
    rasterizerDesc.ScissorEnable = FALSE;
    rasterizerDesc.MultisampleEnable = FALSE;
    rasterizerDesc.AntialiasedLineEnable = FALSE;

    g_device->CreateRasterizerState(&rasterizerDesc, &g_rasterizerState);

    g_spriteBatch = std::make_unique<DirectX::SpriteBatch>(g_deviceContext);

    OnInit();

    while(!glfwWindowShouldClose(window))
    {
        OnUpdate();
        int width, height;
        glfwGetFramebufferSize(window, &width, &height);
        float aspect = static_cast<float>(width) / static_cast<float>(height);

        LightBuffer lightData = {};
        //lightData.dirLightDirection = { 0.5f, -1.0f, 0.5f };
        //lightData.dirLightColor = { 1.0f, 1.0f, 1.0f };
        lightData.cameraPosition = g_cameras[activeCam].position;
        lightData.numPointLights = (int)pointLights.size();

        g_deviceContext->ClearRenderTargetView(g_renderTargetView, clearColor);
        g_deviceContext->ClearDepthStencilView(g_depthStencilView, D3D11_CLEAR_DEPTH, 1.0f, 0);

        g_deviceContext->OMSetRenderTargets(1, &g_renderTargetView, g_depthStencilView);
        g_deviceContext->OMSetDepthStencilState(g_depthStencilState, 1);
        g_deviceContext->RSSetState(g_rasterizerState);

        D3D11_VIEWPORT viewport = {};
        viewport.Width = static_cast<float>(width);
        viewport.Height = static_cast<float>(height);
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;
        g_deviceContext->RSSetViewports(1, &viewport);

        g_deviceContext->IASetInputLayout(g_inputLayout);
        g_deviceContext->VSSetShader(g_vertexShader, nullptr, 0);
        g_deviceContext->PSSetShader(g_pixelShader, nullptr, 0);

        for(const Model& model : g_models)
        {
            if(!model.valid || model.vertexBuffer == nullptr)
                continue;
            UINT stride = sizeof(Vertex);
            UINT offset = 0;

            g_deviceContext->IASetInputLayout(g_inputLayout);
            g_deviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            g_deviceContext->VSSetShader(g_vertexShader, nullptr, 0);
            g_deviceContext->PSSetShader(g_pixelShader, nullptr, 0);
            g_deviceContext->UpdateSubresource(g_matrixBuffer, 0, nullptr, &model.worldMatrix, 0, 0);

            D3D11_MAPPED_SUBRESOURCE mapped;
            if(SUCCEEDED(g_deviceContext->Map(g_lightBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
            {
                memcpy(mapped.pData, &lightData, sizeof(LightBuffer));
                g_deviceContext->Unmap(g_lightBuffer, 0);
            }

            if(!pointLights.empty())
            {
                if(SUCCEEDED(g_deviceContext->Map(g_pointLightBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
                {
                    memcpy(mapped.pData, pointLights.data(), sizeof(PointLight) * pointLights.size());
                    g_deviceContext->Unmap(g_pointLightBuffer, 0);
                }
            }

            g_deviceContext->IASetInputLayout(g_inputLayout);
            g_deviceContext->OMSetDepthStencilState(g_depthStencilState, 1);
            g_deviceContext->RSSetState(g_rasterizerState);
            g_deviceContext->PSSetConstantBuffers(1, 1, &g_lightBuffer);

            ID3D11ShaderResourceView* texSRV = model.textureSRV ? model.textureSRV : nullptr;
            g_deviceContext->PSSetShaderResources(0, 1, &texSRV);

            g_deviceContext->PSSetShaderResources(1, 1, &g_pointLightSRV);

            g_deviceContext->IASetVertexBuffers(0, 1, &model.vertexBuffer, &stride, &offset);

            DirectX::XMMATRIX RotationMatrix = DirectX::XMMatrixRotationRollPitchYaw(
                    DirectX::XMConvertToRadians(g_cameras[activeCam].rotation.x),
                    DirectX::XMConvertToRadians(g_cameras[activeCam].rotation.y),
                    0.0f
                    );

            DirectX::XMVECTOR forward = DirectX::XMVector3Transform(DirectX::XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), RotationMatrix);
            DirectX::XMVECTOR cameraTarget = DirectX::XMVectorAdd(DirectX::XMLoadFloat3(&g_cameras[activeCam].position), forward);

            DirectX::XMVECTOR up = DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

            view = DirectX::XMMatrixLookAtLH(
                    DirectX::XMLoadFloat3(&g_cameras[activeCam].position),
                    cameraTarget,
                    up
                    );

            projection = DirectX::XMMatrixPerspectiveFovLH(
                    DirectX::XMConvertToRadians(g_cameras[activeCam].fov),
                    aspect,
                    0.1f,
                    100.0f
                    );

            DirectX::XMMATRIX world =
                DirectX::XMMatrixScaling(model.scale, model.scale, model.scale) *
                /*DirectX::XMMatrixRotationRollPitchYaw(DirectX::XMConvertToRadians(model.rotation.x), DirectX::XMConvertToRadians(model.rotation.y), DirectX::XMConvertToRadians(model.rotation.z)) **/
                DirectX::XMMatrixTranslation(model.position.x, model.position.y, model.position.z);

            D3D11_MAPPED_SUBRESOURCE mappedResource;
            HRESULT hr = g_deviceContext->Map(g_matrixBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
            if(SUCCEEDED(hr))
            {
                MatrixBuffer* dataPtr = (MatrixBuffer*)mappedResource.pData;
                dataPtr->world = DirectX::XMMatrixTranspose(model.worldMatrix);
                dataPtr->view = DirectX::XMMatrixTranspose(view);
                dataPtr->projection = DirectX::XMMatrixTranspose(projection);
                g_deviceContext->Unmap(g_matrixBuffer, 0);
            }

            g_deviceContext->VSSetConstantBuffers(0, 1, &g_matrixBuffer);
            g_deviceContext->Draw(model.vertices.size(), 0);
        }

        DirectX::XMMATRIX ortho = DirectX::XMMatrixOrthographicOffCenterLH(
                0.0f, width,
                height, 0.0f,
                0.0f, 1.0f);

        double mx, my;
        glfwGetCursorPos(window, &mx, &my);
        bool clicked = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;

        for(auto& element : uiElements)
        {
            element->Draw();
            if(element->type == UIElementType::Button)
              element->Update(mx, my, clicked);
        }

        g_deviceContext->OMSetRenderTargets(1, &g_renderTargetView, g_depthStencilView);
        g_deviceContext->RSSetViewports(1, &viewport);
        g_deviceContext->OMSetDepthStencilState(g_depthStencilState, 1);
        g_deviceContext->RSSetState(g_rasterizerState);

        g_swapChain->Present(use_vsync ? 1 : 0, 0);

        glfwPollEvents();
    }
    for(auto& model : g_models)
    {
        if(model.vertexBuffer) model.vertexBuffer->Release();
    }
    g_models.clear();

    CleanDX();

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
