#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include "glm/glm.hpp"
#include <d3d11.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include "DirectXTex-main/DirectXTex/DirectXTex.h"
#include "DirectXTK-main/Inc/SpriteBatch.h"
#include "DirectXTK-main/Inc/SpriteFont.h"
#include <DirectXCollision.h>
#include "imgui/imgui.h"
#include "imgui/backends/imgui_impl_glfw.h"
#include "imgui/backends/imgui_impl_dx11.h"
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
#include "zlib-1.3.1/zlib.h"
#include "ImGuizmo.h"
#include "tinyxml2.h"
#include "physx/include/PxPhysics.h"
#include "physx/include/PxPhysicsAPI.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define MAX_POINT_LIGHTS 16

physx::PxFoundation*       gFoundation = nullptr;
physx::PxPhysics*          gPhysics = nullptr;
physx::PxDefaultCpuDispatcher* gDispatcher = nullptr;
physx::PxScene*            gScene = nullptr;
physx::PxMaterial*         gMaterial = nullptr;
physx::PxPvd*              gPvd = nullptr;

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

DirectX::XMMATRIX ortho;

DirectX::XMMATRIX view;

DirectX::XMMATRIX projection;

struct AABB {
    DirectX::XMFLOAT3 min;
    DirectX::XMFLOAT3 max;
};

#define IMVEC2_SUB(a, b) ImVec2((a).x - (b).x, (a).y - (b).y)

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

std::vector<std::unique_ptr<UIElement>> uiElements;

struct UILabel : public UIElement {
    std::string text;
    void Draw() override
    {
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

std::vector<std::string> scripts;

std::vector<Camera> g_cameras;

std::vector<LoadedTexture> g_textures;
std::vector<TextureInfo> g_texture_info;

std::vector<Model> g_models;

std::string projectPath;

std::vector<int> g_texture_ids;

ImVec2 gameSize;

std::vector<Vertex> g_vertices;
std::vector<uint32_t> g_indices;

void InitPhysics()
{
    static physx::PxDefaultErrorCallback gDefaultErrorCallback;
    static physx::PxDefaultAllocator gDefaultAllocatorCallback;

    gFoundation = PxCreateFoundation(PX_PHYSICS_VERSION, gDefaultAllocatorCallback, gDefaultErrorCallback);

    gPvd = physx::PxCreatePvd(*gFoundation);
    physx::PxPvdTransport* transport = physx::PxDefaultPvdSocketTransportCreate("127.0.0.1", 5425, 10);
    gPvd->connect(*transport, physx::PxPvdInstrumentationFlag::eALL);

    gPhysics = PxCreatePhysics(PX_PHYSICS_VERSION, *gFoundation, physx::PxTolerancesScale(), true, gPvd);

    physx::PxSceneDesc sceneDesc(gPhysics->getTolerancesScale());
    sceneDesc.gravity = physx::PxVec3(0.0f, -9.81f, 0.0f);
    gDispatcher = physx::PxDefaultCpuDispatcherCreate(2);
    sceneDesc.cpuDispatcher = gDispatcher;
    sceneDesc.filterShader = physx::PxDefaultSimulationFilterShader;
    gScene = gPhysics->createScene(sceneDesc);

    gMaterial = gPhysics->createMaterial(0.5f, 0.5f, 0.6f); // static friction, dynamic friction, restitution
}

void InitDX(HWND hwnd, GLFWwindow* window)
{
    std::cout << "Checking version" << '\n';
    IMGUI_CHECKVERSION();
    std::cout << "Checking version done" << '\n';
    std::cout << "Creating context" << '\n';
    ImGui::CreateContext();
    std::cout << "Creating context done" << '\n';

    std::cout << "style colors" << '\n';
    ImGui::StyleColorsDark();
    std::cout << "style colors done" << '\n';

    std::cout << "init for opengl" << '\n';
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    std::cout << "init for opengl done" << '\n';

    DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
    swapChainDesc.BufferCount = 1;
    swapChainDesc.BufferDesc.Width = 1920;
    swapChainDesc.BufferDesc.Height = 1080;
    swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.BufferDesc.RefreshRate.Numerator = 60;
    swapChainDesc.BufferDesc.RefreshRate.Denominator = 1;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.OutputWindow = hwnd;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.Windowed = TRUE;

    D3D_FEATURE_LEVEL featureLevel;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &swapChainDesc, &g_swapChain, &g_device, &featureLevel, &g_deviceContext);

    if(FAILED(hr))
    {
        std::cerr << "Failed to create DirectX device and swap chain." << '\n';
        exit(-1);
    }

    std::cout << "init for dx11" << '\n';
    ImGui_ImplDX11_Init(g_device, g_deviceContext);
    std::cout << "init for dx11 done" << '\n';

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
    depthStencilDesc.Width = 1920;
    depthStencilDesc.Height = 1080;
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
    viewport.Width = 1920;
    viewport.Height = 1080;
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
    g_lightBuffer = nullptr;
}

bool ReadFileBinary(const std::string& path, std::vector<char>& outData)
{
    std::ifstream in(path, std::ios::binary);
    if(!in) return false;
    outData.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return true;
}

void CompileShaders()
{
    std::vector<char> vsData;
    std::vector<char> psData;

    if(!ReadFileBinary("vs.cso", vsData) || !ReadFileBinary("ps.cso", psData))
    {
        std::cerr << "Failed to read precompiled shaders\n";
        return;
    }

    HRESULT hr = g_device->CreateVertexShader(vsData.data(), vsData.size(), nullptr, &g_vertexShader);
    if(FAILED(hr)) { std::cerr << "Failed to create vertex shader\n"; return; }

    hr = g_device->CreatePixelShader(psData.data(), psData.size(), nullptr, &g_pixelShader);
    if(FAILED(hr)) { std::cerr << "Failed to create pixel shader\n"; return; }

    D3D11_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };

    hr = g_device->CreateInputLayout(layout, ARRAYSIZE(layout), vsData.data(), vsData.size(), &g_inputLayout);
    if(FAILED(hr)) { std::cerr << "Failed to create input layout\n"; return; }
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

std::string OpenFileDialog(const char* filter, bool save = false)
{
    char filename[MAX_PATH] = "";
    OPENFILENAME ofn = { 0 };
    ofn.lStructSize = sizeof(OPENFILENAME);
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    
    if(save)
    {
        ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
        ofn.lpstrTitle = "Save File";
        if(GetSaveFileName(&ofn))
            return filename;
    }
    else
    {
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
        ofn.lpstrTitle = "Open File";
        if(GetOpenFileName(&ofn))
            return filename;
    }
    
    return "";
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
    model.filePath = path;

    return model;
}

ID3D11ShaderResourceView* g_textureSRV = nullptr;

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

std::string MakeUniqueName(const std::string& baseName, const std::vector<std::string>& existingNames)
{
    std::string uniqueName = baseName;
    int counter = 1;
    while(std::find(existingNames.begin(), existingNames.end(), uniqueName) != existingNames.end())
        uniqueName = baseName + " (" + std::to_string(counter++) + ")";
    return uniqueName;
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

std::string fontPath;

void SaveProject(const std::string& path, const std::vector<Model>& models)
{
    std::ofstream file(path, std::ios::binary);
    if(!file.is_open())
    {
        std::cerr << "Failed to open file for saving: " << path << std::endl;
        return;
    }

    const char* header = "GE3PROJ";
    file.write(header, 7);

    uint32_t version = 1;
    file.write(reinterpret_cast<const char*>(&version), sizeof(version));

    uint32_t modelCount = static_cast<uint32_t>(models.size());
    file.write(reinterpret_cast<const char*>(&modelCount), sizeof(modelCount));

    uint32_t scriptCount = static_cast<uint32_t>(scripts.size());
    file.write(reinterpret_cast<const char*>(&scriptCount), sizeof(scriptCount));

    uint32_t camCount = static_cast<uint32_t>(g_cameras.size());
    file.write(reinterpret_cast<const char*>(&camCount), sizeof(camCount));

    uint32_t uiCount = static_cast<uint32_t>(uiElements.size());
    file.write(reinterpret_cast<const char*>(&uiCount), sizeof(uiCount));

    uint32_t plCount = static_cast<uint32_t>(pointLights.size());
    file.write(reinterpret_cast<const char*>(&plCount), sizeof(plCount));

    for(const auto& elem : uiElements)
    {
        uint32_t elemNameLen = static_cast<uint32_t>(elem->name.length());
        file.write(reinterpret_cast<const char*>(&elemNameLen), sizeof(elemNameLen));
        file.write(elem->name.c_str(), elemNameLen);

        file.write(reinterpret_cast<const char*>(&elem->type), sizeof(UIElementType));
        file.write(reinterpret_cast<const char*>(&elem->x), sizeof(float));
        file.write(reinterpret_cast<const char*>(&elem->y), sizeof(float));
        file.write(reinterpret_cast<const char*>(&elem->color), sizeof(DirectX::XMFLOAT4));

        if(elem->type == UIElementType::Label)
        {
            UILabel* label = dynamic_cast<UILabel*>(elem.get());
            if(label)
            {
                uint32_t textLen = static_cast<uint32_t>(label->text.length());
                file.write(reinterpret_cast<const char*>(&textLen), sizeof(textLen));
                file.write(label->text.c_str(), textLen);
                file.write(reinterpret_cast<const char*>(&label->textScale), sizeof(float));
            }
        }
        else if(elem->type == UIElementType::Button)
        {
            UIButton* button = dynamic_cast<UIButton*>(elem.get());
            if(button)
            {
                uint32_t textLen = static_cast<uint32_t>(button->text.length());
                file.write(reinterpret_cast<const char*>(&textLen), sizeof(textLen));
                file.write(button->text.c_str(), textLen);
                file.write(reinterpret_cast<const char*>(&button->width), sizeof(float));
                file.write(reinterpret_cast<const char*>(&button->height), sizeof(float));
                file.write(reinterpret_cast<const char*>(&button->textScale), sizeof(float));
                file.write(reinterpret_cast<const char*>(&button->hoverColor), sizeof(DirectX::XMFLOAT4));
            }
        }
    }

    for(const Camera& cam : g_cameras)
    {
        uint32_t camNameLen = static_cast<uint32_t>(cam.name.length());
        file.write(reinterpret_cast<const char*>(&camNameLen), sizeof(camNameLen));
        file.write(cam.name.c_str(), camNameLen);

        file.write(reinterpret_cast<const char*>(&cam.position), sizeof(DirectX::XMFLOAT3));

        file.write(reinterpret_cast<const char*>(&cam.rotation), sizeof(DirectX::XMFLOAT2));

        file.write(reinterpret_cast<const char*>(&cam.fov), sizeof(float));

        int isFPSint = cam.isFPS;
        file.write(reinterpret_cast<const char*>(&isFPSint), sizeof(int));
    }

    for(const auto& i : scripts)
    {
        uint32_t scriptNameLen = static_cast<uint32_t>(i.length());
        file.write(reinterpret_cast<const char*>(&scriptNameLen), sizeof(scriptNameLen));
        file.write(i.c_str(), scriptNameLen);
    }

    for(const Model& model : models)
    {
        uint32_t nameLength = static_cast<uint32_t>(model.name.length());
        file.write(reinterpret_cast<const char*>(&nameLength), sizeof(nameLength));
        file.write(model.name.c_str(), nameLength);

        uint32_t pathLength = static_cast<uint32_t>(model.filePath.length());
        file.write(reinterpret_cast<const char*>(&pathLength), sizeof(pathLength));
        file.write(model.filePath.c_str(), pathLength);

        uint32_t idLength = static_cast<uint32_t>(model.id.length());
        file.write(reinterpret_cast<const char*>(&idLength), sizeof(idLength));
        file.write(model.id.c_str(), idLength);

        file.write(reinterpret_cast<const char*>(&model.position), sizeof(DirectX::XMFLOAT3));

        file.write(reinterpret_cast<const char*>(&model.rotation), sizeof(DirectX::XMFLOAT3));

        file.write(reinterpret_cast<const char*>(&model.scale), sizeof(float));

        uint32_t vertexCount = static_cast<uint32_t>(model.vertices.size());
        file.write(reinterpret_cast<const char*>(&vertexCount), sizeof(vertexCount));

        file.write(reinterpret_cast<const char*>(model.vertices.data()), vertexCount * sizeof(Vertex));

        file.write(reinterpret_cast<const char*>(&model.mass), sizeof(float));

        bool isStatic = model.isStatic;
        file.write(reinterpret_cast<const char*>(&isStatic), sizeof(bool));

        bool hasTexture = (model.textureSRV != nullptr);
        file.write(reinterpret_cast<const char*>(&hasTexture), sizeof(bool));

        if(hasTexture)
        {
            std::string textureName, texturePath;
            for(const auto& tex : g_texture_info)
            {
                if(tex.srv == model.textureSRV)
                {
                    textureName = tex.name;
                    texturePath = tex.path;
                    break;
                }
            }

            uint32_t nameLength = static_cast<uint32_t>(textureName.length());
            file.write(reinterpret_cast<const char*>(&nameLength), sizeof(nameLength));
            file.write(textureName.c_str(), nameLength);

            uint32_t pathLength = static_cast<uint32_t>(texturePath.length());
            file.write(reinterpret_cast<const char*>(&pathLength), sizeof(pathLength));
            file.write(texturePath.c_str(), pathLength);
        }
    }

    for(size_t i = 0; i < pointLights.size(); ++i)
    {
        const std::string& name = plNames[i];
        const PointLight& pl = pointLights[i];

        uint32_t nameLen = static_cast<uint32_t>(name.size());
        file.write(reinterpret_cast<const char*>(&nameLen), sizeof(nameLen));
        file.write(name.c_str(), nameLen);

        file.write(reinterpret_cast<const char*>(&pl.position), sizeof(pl.position));

        file.write(reinterpret_cast<const char*>(&pl.range), sizeof(pl.range));

        file.write(reinterpret_cast<const char*>(&pl.color), sizeof(pl.color));

        file.write(reinterpret_cast<const char*>(&pl.intensity), sizeof(pl.intensity));
    }

    uint32_t fontPathLen = static_cast<uint32_t>(fontPath.length());
    file.write(reinterpret_cast<const char*>(&fontPathLen), sizeof(fontPathLen));
    file.write(fontPath.c_str(), fontPathLen);

    file.close();
    std::cout << "Project saved to: " << path << std::endl;
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

void LoadProject(const std::string& path, std::vector<Model>& models, std::vector<Camera>& cameras)
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

    cameras.clear();

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

    uint32_t camCount;
    file.read(reinterpret_cast<char*>(&camCount), sizeof(camCount));

    uint32_t uiCount;
    file.read(reinterpret_cast<char*>(&uiCount), sizeof(uiCount));

    uint32_t plCount;
    file.read(reinterpret_cast<char*>(&plCount), sizeof(plCount));

    for(uint32_t i = 0; i < uiCount; ++i)
    {
        uint32_t elemNameLen;
        file.read(reinterpret_cast<char*>(&elemNameLen), sizeof(elemNameLen));
        std::string name(elemNameLen, '\0');
        file.read(&name[0], elemNameLen);

        UIElementType type;
        file.read(reinterpret_cast<char*>(&type), sizeof(UIElementType));

        float x, y;
        file.read(reinterpret_cast<char*>(&x), sizeof(float));
        file.read(reinterpret_cast<char*>(&y), sizeof(float));

        DirectX::XMFLOAT4 color;
        file.read(reinterpret_cast<char*>(&color), sizeof(DirectX::XMFLOAT4));

        std::unique_ptr<UIElement> newElement;
        if(type == UIElementType::Label)
        {
            newElement = std::make_unique<UILabel>();
            newElement->name = name;
            newElement->x = x;
            newElement->y = y;
            newElement->color = color;

            UILabel* label = dynamic_cast<UILabel*>(newElement.get());
            if(label)
            {
                uint32_t textLen;
                file.read(reinterpret_cast<char*>(&textLen), sizeof(textLen));
                label->text.resize(textLen);
                file.read(&label->text[0], textLen);
                file.read(reinterpret_cast<char*>(&label->textScale), sizeof(float));
            }
        }
        else if(type == UIElementType::Button)
        {
            newElement = std::make_unique<UIButton>();
            newElement->name = name;
            newElement->x = x;
            newElement->y = y;
            newElement->color = color;

            UIButton* button = dynamic_cast<UIButton*>(newElement.get());
            if(button)
            {
                uint32_t textLen;
                file.read(reinterpret_cast<char*>(&textLen), sizeof(textLen));
                button->text.resize(textLen);
                file.read(&button->text[0], textLen);
                file.read(reinterpret_cast<char*>(&button->width), sizeof(float));
                file.read(reinterpret_cast<char*>(&button->height), sizeof(float));
                file.read(reinterpret_cast<char*>(&button->textScale), sizeof(float));
                file.read(reinterpret_cast<char*>(&button->hoverColor), sizeof(DirectX::XMFLOAT4));
            }
        }

        uiElements.push_back(std::move(newElement));
    }

    for(uint32_t i = 0; i < camCount; i++)
    {
        Camera cam;
        uint32_t camNameLen;
        file.read(reinterpret_cast<char*>(&camNameLen), sizeof(camNameLen));
        cam.name.resize(camNameLen);
        file.read(&cam.name[0], camNameLen);

        file.read(reinterpret_cast<char*>(&cam.position), sizeof(DirectX::XMFLOAT3));

        file.read(reinterpret_cast<char*>(&cam.rotation), sizeof(DirectX::XMFLOAT2));

        file.read(reinterpret_cast<char*>(&cam.fov), sizeof(float));

        int isFPSint;
        file.read(reinterpret_cast<char*>(&isFPSint), sizeof(int));
        cam.isFPS = (bool)isFPSint;

        g_cameras.push_back(cam);
    }

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

        uint32_t idLength;
        file.read(reinterpret_cast<char*>(&idLength), sizeof(idLength));
        model.id.resize(idLength);
        file.read(&model.id[0], idLength);

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

        file.read(reinterpret_cast<char*>(&model.mass), sizeof(float));

        file.read(reinterpret_cast<char*>(&model.isStatic), sizeof(bool));

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

    for(uint32_t i = 0; i < plCount; ++i)
    {
        uint32_t nameLen;
        file.read(reinterpret_cast<char*>(&nameLen), sizeof(nameLen));

        std::string name(nameLen, '\0');
        file.read(&name[0], nameLen);

        PointLight pl;
        file.read(reinterpret_cast<char*>(&pl.position), sizeof(pl.position));
        file.read(reinterpret_cast<char*>(&pl.range), sizeof(pl.range));
        file.read(reinterpret_cast<char*>(&pl.color), sizeof(pl.color));
        file.read(reinterpret_cast<char*>(&pl.intensity), sizeof(pl.intensity));

        pointLights.push_back(pl);
        plNames.push_back(name);
    }

    uint32_t fontPathLen;
    file.read(reinterpret_cast<char*>(&fontPathLen), sizeof(fontPathLen));
    fontPath.resize(fontPathLen);
    file.read(&fontPath[0], fontPathLen);

    file.close();
    std::cout << "Project loaded from: " << path << std::endl;
}

void CopyFile(const std::string& src, const std::string& dest)
{
    std::ifstream source(src, std::ios::binary);
    std::ofstream destination(dest, std::ios::binary);
    destination << source.rdbuf();
}

struct AssetEntry {
    std::string pathInRPF;
    std::vector<char> data;
};

bool CompressData(const std::vector<char>& input, std::vector<char>& output)
{
    uLongf outSize = compressBound(input.size());
    output.resize(outSize);
    int res = compress(reinterpret_cast<Bytef*>(output.data()), &outSize, reinterpret_cast<const Bytef*>(input.data()), input.size());
    if (res != Z_OK) return false;
    output.resize(outSize);
    return true;
}

void CompileGame(const std::string& outputPath, const std::vector<Model>& models, const std::vector<std::string>& textures, bool hasScripts = false)
{
    std::filesystem::create_directory(outputPath);

    std::vector<AssetEntry> rpfEntries;

    tinyxml2::XMLDocument doc;
    auto* root = doc.NewElement("Assets");

    for(int i = 0; i < textures.size(); ++i)
    {
        const std::string& texPath = textures[i];
        const TextureInfo& info = g_texture_info[i];

        std::vector<char> data;
        if(ReadFileBinary(texPath, data))
        {
            std::string rpfPath = "textures/" + std::filesystem::path(texPath).filename().string();
            std::string name = std::filesystem::path(texPath).filename().string();
            std::vector<char> textureCompressed;
            CompressData(data, textureCompressed);
            rpfEntries.push_back({ rpfPath, textureCompressed });

            auto* elem = doc.NewElement("Texture");
            elem->SetAttribute("path", rpfPath.c_str());
            elem->SetAttribute("name", name.c_str());
            root->InsertEndChild(elem);

            std::cout << "Exported texture, path: \"" << rpfPath << "\", name: \"" << name << "\"\n";
        }
    }

    for(const auto& model : models)
    {
        std::vector<char> data;
        if(ReadFileBinary(model.filePath, data))
        {
            std::string rpfPath = "models/" + std::filesystem::path(model.filePath).filename().string();
            std::vector<char> modelCompressed;
            CompressData(std::vector<char>(data.begin(), data.end()), modelCompressed);
            rpfEntries.push_back({ rpfPath, modelCompressed });

            auto* elem = doc.NewElement("Model");
            elem->SetAttribute("path", rpfPath.c_str());
            elem->SetAttribute("position", (std::to_string(model.position.x) + "," + std::to_string(model.position.y) + "," + std::to_string(model.position.z)).c_str());
            elem->SetAttribute("rotation", (std::to_string(model.rotation.x) + "," + std::to_string(model.rotation.y) + "," + std::to_string(model.rotation.z)).c_str());
            elem->SetAttribute("scale", std::to_string(model.scale).c_str());
            elem->SetAttribute("id", model.id.c_str());
            elem->SetAttribute("name", model.name.c_str());
            elem->SetAttribute("mass", std::to_string(model.mass).c_str());
            elem->SetAttribute("isStatic", std::to_string(model.isStatic).c_str());
            root->InsertEndChild(elem);
        }
    }

    for(const auto& cam : g_cameras)
    {
        auto* camElem = doc.NewElement("Camera");
        camElem->SetAttribute("position", (std::to_string(cam.position.x) + "," + std::to_string(cam.position.y) + "," + std::to_string(cam.position.z)).c_str());
        camElem->SetAttribute("rotation", (std::to_string(cam.rotation.x) + "," + std::to_string(cam.rotation.y)).c_str());
        camElem->SetAttribute("fov", std::to_string(cam.fov).c_str());
        camElem->SetAttribute("isFPS", std::to_string(cam.isFPS).c_str());
        root->InsertEndChild(camElem);
    }
    
    for(const auto& elem : uiElements)
    {
        auto* uiElem = doc.NewElement("UI");
        uiElem->SetAttribute("type", std::to_string(static_cast<int>(elem->type)).c_str());
        uiElem->SetAttribute("scale", std::to_string(static_cast<UIElement*>(elem.get())->textScale).c_str());

        if(elem->type == UIElementType::Label)
        {
            UILabel* label = static_cast<UILabel*>(elem.get());

            std::string posStr = std::to_string(label->x) + "," + std::to_string(label->y);
            std::string colorStr = std::to_string(label->color.x) + "," + std::to_string(label->color.y) + "," + std::to_string(label->color.z) + "," + std::to_string(label->color.w);

            uiElem->SetAttribute("position", posStr.c_str());
            uiElem->SetAttribute("color", colorStr.c_str());
            uiElem->SetAttribute("name", label->name.c_str());
            uiElem->SetAttribute("text", label->text.c_str());
        }
        else if(elem->type == UIElementType::Button)
        {
            UIButton* button = static_cast<UIButton*>(elem.get());

            std::string posStr = std::to_string(button->x) + "," + std::to_string(button->y);
            std::string colorStr = std::to_string(button->color.x) + "," + std::to_string(button->color.y) + "," + std::to_string(button->color.z) + "," + std::to_string(button->color.w);
            std::string hoverColorStr = std::to_string(button->hoverColor.x) + "," + std::to_string(button->hoverColor.y) + "," + std::to_string(button->hoverColor.z) + "," + std::to_string(button->hoverColor.w);

            uiElem->SetAttribute("position", posStr.c_str());
            uiElem->SetAttribute("color", colorStr.c_str());
            uiElem->SetAttribute("hoverColor", hoverColorStr.c_str());
            uiElem->SetAttribute("name", button->name.c_str());
            uiElem->SetAttribute("text", button->text.c_str());
            uiElem->SetAttribute("width", std::to_string(button->width).c_str());
            uiElem->SetAttribute("height", std::to_string(button->height).c_str());
        }

        root->InsertEndChild(uiElem);
    }

    for(int i = 0; i < pointLights.size(); ++i)
    {
        const std::string& name = plNames[i];
        PointLight& pl = pointLights[i];

        auto* plElem = doc.NewElement("PL");

        plElem->SetAttribute("name", name.c_str());
        plElem->SetAttribute("position", (std::to_string(pl.position.x) + "," + std::to_string(pl.position.y) + "," + std::to_string(pl.position.z)).c_str());
        plElem->SetAttribute("color", (std::to_string(pl.color.x) + "," + std::to_string(pl.color.y) + "," + std::to_string(pl.color.z)).c_str());
        plElem->SetAttribute("range", std::to_string(pl.range).c_str());
        plElem->SetAttribute("intensity", std::to_string(pl.intensity).c_str());

        root->InsertEndChild(plElem);
    }

    // TODO: Add different fonts
    std::vector<char> fontData;
    if(ReadFileBinary(fontPath, fontData))
    {
        auto* fontElem = doc.NewElement("Font");
        std::string rpfFontPath = "fonts/" + std::filesystem::path(fontPath).filename().string();
        rpfEntries.push_back({rpfFontPath, fontData});
        fontElem->SetAttribute("path", rpfFontPath.c_str());
        root->InsertEndChild(fontElem);
    }

    std::vector<char> shaderData;
    if(ReadFileBinary("./vs.cso", shaderData))
    {
        std::vector<char> shaderCompressed;
        CompressData(shaderData, shaderCompressed);
        auto* shaderElem = doc.NewElement("VS");
        std::string vsPath = "shaders/vs.cso";
        rpfEntries.push_back({vsPath, shaderCompressed});
        shaderElem->SetAttribute("path", vsPath.c_str());
        root->InsertEndChild(shaderElem);
    }
    if(ReadFileBinary("./ps.cso", shaderData))
    {
        std::vector<char> shaderCompressed;
        CompressData(shaderData, shaderCompressed);
        auto* shaderElem = doc.NewElement("PS");
        std::string psPath = "shaders/ps.cso";
        rpfEntries.push_back({psPath, shaderCompressed});
        shaderElem->SetAttribute("path", psPath.c_str());
        root->InsertEndChild(shaderElem);
    }

    doc.InsertFirstChild(root);

    tinyxml2::XMLPrinter printer;
    doc.Print(&printer);
    std::string xmlContent = printer.CStr();

    std::vector<char> xmlCompressed;
    CompressData(std::vector<char>(xmlContent.begin(), xmlContent.end()), xmlCompressed);
    rpfEntries.push_back({ "manifest.xml", xmlCompressed });

    std::ofstream rpf(outputPath + "/main.rpf", std::ios::binary);
    uint32_t fileCount = static_cast<uint32_t>(rpfEntries.size());
    rpf.write(reinterpret_cast<const char*>(&fileCount), sizeof(fileCount));

    uint32_t currentOffset = sizeof(uint32_t) + fileCount * (256 + sizeof(uint32_t) * 2);

    struct FileHeader {
        char path[256];
        uint32_t offset;
        uint32_t size;
    };

    std::vector<FileHeader> headers;

    for(const auto& entry : rpfEntries)
    {
        FileHeader hdr{};
        strncpy(hdr.path, entry.pathInRPF.c_str(), 255);
        hdr.offset = currentOffset;
        hdr.size = static_cast<uint32_t>(entry.data.size());
        currentOffset += hdr.size;
        headers.push_back(hdr);
    }

    for(const auto& hdr : headers)
        rpf.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));

    for(const auto& entry : rpfEntries)
        rpf.write(entry.data.data(), entry.data.size());

    rpf.close();

    int scriptCount = 0;

    std::string projectDir = outputPath;
    
    std::string cmd = "mkdir " + projectDir;
    std::system(cmd.c_str());
    
    CopyFile("GameMain.cpp", projectDir + "/GameMain.cpp");

    CopyFile("stb_image.h", projectDir + "/stb_image.h");

    CopyFile("tinyxml2.h", projectDir + "/tinyxml2.h");
    CopyFile("tinyxml2.cpp", projectDir + "/tinyxml2.cpp");
    CopyFile("CMakeListsGame.txt", projectDir + "/CMakeLists.txt");
    
    std::ofstream batchFile(projectDir + "/build.bat");
    batchFile << "@echo off\n";
    if(!std::filesystem::exists(projectDir + "/build"))
        batchFile << "mkdir build\n";
    batchFile << "cd build\n";
    batchFile << "cmake .. -G \"Visual Studio 17 2022\" -A x64\n";
    batchFile << "cmake --build . --config Release\n";
    batchFile << "copy ..\\main.rpf .\\Release\\main.rpf\n";
    batchFile << "echo Game compiled! Output is in ./GameMain.exe\n";
    batchFile << "pause\n";
    batchFile.close();
    
    MessageBoxA(NULL, "Game project created successfully! You can build it using the build.bat file.", "Game Compiler", MB_OK | MB_ICONINFORMATION);
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

// TODO: Implement mouse raycasting

bool RayIntersectsAABB(
    const DirectX::XMVECTOR& rayOrigin,
    const DirectX::XMVECTOR& rayDir,
    const AABB& aabb,
    const DirectX::XMMATRIX& modelMatrix,
    float& outDist)
{
    DirectX::XMFLOAT3 min = aabb.min;
    DirectX::XMFLOAT3 max = aabb.max;

    DirectX::XMVECTOR bounds[2] = {
        DirectX::XMVector3Transform(DirectX::XMLoadFloat3(&min), modelMatrix),
        DirectX::XMVector3Transform(DirectX::XMLoadFloat3(&max), modelMatrix)
    };

    float tMin = 0.0f;
    float tMax = FLT_MAX;

    for (int i = 0; i < 3; i++)
    {
        float rayO = DirectX::XMVectorGetByIndex(rayOrigin, i);
        float rayD = DirectX::XMVectorGetByIndex(rayDir, i);
        float b0 = DirectX::XMVectorGetByIndex(bounds[0], i);
        float b1 = DirectX::XMVectorGetByIndex(bounds[1], i);

        float t1 = (b0 - rayO) / rayD;
        float t2 = (b1 - rayO) / rayD;

        if(t1 > t2) std::swap(t1, t2);

        tMin = std::max(tMin, t1);
        tMax = std::min(tMax, t2);

        if(tMin > tMax) return false;
    }

    outDist = tMin;
    return true;
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

void ShowMainDockspace()
{
    static bool dockspaceOpen = true;
    static bool opt_fullscreen_persistant = true;
    bool opt_fullscreen = opt_fullscreen_persistant;

    ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
    if(opt_fullscreen)
    {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->Pos);
        ImGui::SetNextWindowSize(viewport->Size);
        ImGui::SetNextWindowViewport(viewport->ID);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
        window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
    }

    ImGui::Begin("MainDockspaceWindow", &dockspaceOpen, window_flags);
    if(opt_fullscreen)
        ImGui::PopStyleVar(2);

    ImGuiID dockspace_id = ImGui::GetID("MainDockspace");
    ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);

    ImGui::End();
}

int main()
{
    InitPhysics();
    if(!glfwInit())
    {
        std::cerr << "Failed to init GLFW" << '\n';
        return -1;
    }

    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    GLFWwindow* window = glfwCreateWindow(1920, 1080, "Gica Engine V3", nullptr, nullptr);
    if(!window)
    {
        glfwTerminate();
        std::cerr << "Failed to create window" << '\n';
        return -1;
    }

    glfwSetWindowSizeCallback(window, WindowResizeCallback);
    glfwSetFramebufferSizeCallback(window, WindowResizeCallback);

    glfwMakeContextCurrent(window);

    HWND hwnd = glfwGetWin32Window(window);
    std::cout << "===== Initializing DirectX 11 =====" << '\n';
    InitDX(hwnd, window);
    std::cout << "===== DirectX 11 Intialized =====" << '\n';

    std::cout << "===== Compiling shaders =====" << '\n';
    CompileShaders();
    std::cout << "===== Shaders compiled =====" << '\n';

    float clearColor[4] = { 0.45f, 0.55f, 0.60f, 1.00f };

    int selectedModelIndex = -1;
    int selectedCamIndex = 0;
    int selectedUiIndex = -1;
    int selectedPLIndex = -1;

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

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.IniFilename = "config.ini";

    Camera mainCam;
    mainCam.position = {0.0f, 1.5f, -3.0f};
    mainCam.rotation = {0.0f, 0.0f};
    mainCam.fov = 60.0f;
    mainCam.name = "Main Camera";
    mainCam.isFPS = false;
    g_cameras.push_back(mainCam);

    g_spriteBatch = std::make_unique<DirectX::SpriteBatch>(g_deviceContext);

    while(!glfwWindowShouldClose(window))
    {
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        ImGuizmo::BeginFrame();

        ShowMainDockspace();

        LightBuffer lightData = {};
        //lightData.dirLightDirection = { 0.5f, -1.0f, 0.5f };
        //lightData.dirLightColor = { 1.0f, 1.0f, 1.0f };
        lightData.cameraPosition = g_cameras[selectedCamIndex].position;
        lightData.numPointLights = static_cast<int>(pointLights.size());

        g_deviceContext->ClearRenderTargetView(g_renderTargetView, clearColor);
        g_deviceContext->ClearDepthStencilView(g_depthStencilView, D3D11_CLEAR_DEPTH, 1.0f, 0);

        g_deviceContext->OMSetRenderTargets(1, &g_renderTargetView, g_depthStencilView);

        g_deviceContext->OMSetDepthStencilState(g_depthStencilState, 1);
        g_deviceContext->RSSetState(g_rasterizerState);

        /*D3D11_DEPTH_STENCIL_DESC depthStencilDescTransparent = depthStencilDesc;
        depthStencilDescTransparent.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
        g_device->CreateDepthStencilState(&depthStencilDescTransparent, &g_depthStencilState);*/

        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        ImGui::GetIO().DisplaySize = ImVec2((float)display_w, (float)display_h);

        D3D11_VIEWPORT viewportMain = {};
        viewportMain.Width = static_cast<FLOAT>(display_w);
        viewportMain.Height = static_cast<FLOAT>(display_h);
        viewportMain.MinDepth = 0.0f;
        viewportMain.MaxDepth = 1.0f;
        g_deviceContext->RSSetViewports(1, &viewportMain);

        enum class GizmoTarget { None, Model, Light };
        static GizmoTarget currentTarget = GizmoTarget::Model;

        ImGui::Begin("Game View Controls");
        if(ImGui::ColorEdit3("Color", clearColor))
            ;
        ImGui::RadioButton("Model Gizmo", (int*)&currentTarget, (int)GizmoTarget::Model);
        ImGui::SameLine();
        ImGui::RadioButton("Light Gizmo", (int*)&currentTarget, (int)GizmoTarget::Light);
        ImGui::End();

        ImGui::Begin("UI Controls");
        static DirectX::XMFLOAT2 uiPos = {0.0f, 0.0f};
        static DirectX::XMFLOAT4 uiColor = {1.0f, 1.0f, 1.0f, 1.0f};
        static DirectX::XMFLOAT4 uiHoverColor = {1.0f, 1.0f, 1.0f, 1.0f};
        static DirectX::XMFLOAT2 uiScale = {200.0f, 100.0f};
        static std::string text;
        static int previousUiIndex = -1;
        static float scale = 1.0f;
        if(selectedUiIndex != previousUiIndex && selectedUiIndex >= 0)
        {
            uiPos.x = uiElements[selectedUiIndex]->x;
            uiPos.y = uiElements[selectedUiIndex]->y;
            uiColor = uiElements[selectedUiIndex]->color;
            scale = uiElements[selectedUiIndex]->textScale;
            if(uiElements[selectedUiIndex]->type == UIElementType::Label)
            {
                text = static_cast<UILabel*>(uiElements[selectedUiIndex].get())->text;
            }
            else if(uiElements[selectedUiIndex]->type == UIElementType::Button)
            {
                text = static_cast<UIButton*>(uiElements[selectedUiIndex].get())->text;
                uiScale.x = static_cast<UIButton*>(uiElements[selectedUiIndex].get())->width;
                uiScale.y = static_cast<UIButton*>(uiElements[selectedUiIndex].get())->height;
                uiHoverColor = static_cast<UIButton*>(uiElements[selectedUiIndex].get())->hoverColor;
            }
            previousUiIndex = selectedUiIndex;
        }
        std::vector<const char*> uiNames;
        for(const auto& elem : uiElements)
        {
            const UIElement& element = *elem;
            uiNames.push_back(elem->name.c_str());
        }
        if(ImGui::Combo("Select UI Element", &selectedUiIndex, uiNames.data(), uiNames.size()))
        {
            if(selectedUiIndex >= 0)
            {
                uiPos.x = uiElements[selectedUiIndex]->x;
                uiPos.y = uiElements[selectedUiIndex]->y;
                uiColor = uiElements[selectedUiIndex]->color;
                scale = uiElements[selectedUiIndex]->textScale;
                if(uiElements[selectedUiIndex]->type == UIElementType::Label)
                {
                    text = static_cast<UILabel*>(uiElements[selectedUiIndex].get())->text;
                }
                else if(uiElements[selectedUiIndex]->type == UIElementType::Button)
                {
                    text = static_cast<UIButton*>(uiElements[selectedUiIndex].get())->text;
                    uiScale.x = static_cast<UIButton*>(uiElements[selectedUiIndex].get())->width;
                    uiScale.y = static_cast<UIButton*>(uiElements[selectedUiIndex].get())->height;
                    uiHoverColor = static_cast<UIButton*>(uiElements[selectedUiIndex].get())->hoverColor;
                }
            }
        }
        if(selectedUiIndex >= 0)
        {
            if(ImGui::SliderFloat("Position X", &uiPos.x, -1000.0f, 1000.0f))
            {
                if(uiElements[selectedUiIndex]->type == UIElementType::Label)
                    static_cast<UILabel*>(uiElements[selectedUiIndex].get())->x = uiPos.x;
                else if(uiElements[selectedUiIndex]->type == UIElementType::Button)
                    static_cast<UIButton*>(uiElements[selectedUiIndex].get())->x = uiPos.x;
            }
            if(ImGui::SliderFloat("Position Y", &uiPos.y, -1000.0f, 1000.0f))
            {
                if(uiElements[selectedUiIndex]->type == UIElementType::Label)
                    static_cast<UILabel*>(uiElements[selectedUiIndex].get())->y = uiPos.y;
                else if(uiElements[selectedUiIndex]->type == UIElementType::Button)
                    static_cast<UIButton*>(uiElements[selectedUiIndex].get())->y = uiPos.y;
            }

            if(ImGui::ColorEdit4("Color", &uiColor.x))
            {
                if(uiElements[selectedUiIndex]->type == UIElementType::Label)
                    static_cast<UILabel*>(uiElements[selectedUiIndex].get())->color = uiColor;
                else if(uiElements[selectedUiIndex]->type == UIElementType::Button)
                    static_cast<UIButton*>(uiElements[selectedUiIndex].get())->color = uiColor;
            }

            char buf[256];
            std::strncpy(buf, text.c_str(), sizeof(buf));
            if(ImGui::InputText("Text", buf, sizeof(buf)))
            {
                text = buf;
                if(uiElements[selectedUiIndex]->type == UIElementType::Label)
                    static_cast<UILabel*>(uiElements[selectedUiIndex].get())->text = text;
                if(uiElements[selectedUiIndex]->type == UIElementType::Button)
                    static_cast<UIButton*>(uiElements[selectedUiIndex].get())->text = text;
            }

            if(ImGui::SliderFloat("Text Scale", &scale, 0.0f, 10.0f))
            {
                uiElements[selectedUiIndex]->textScale = scale;
            }

            if(uiElements[selectedUiIndex]->type == UIElementType::Button)
            {
                if(ImGui::SliderFloat("Width", &uiScale.x, 10.0f, 1000.0f))
                {
                    static_cast<UIButton*>(uiElements[selectedUiIndex].get())->width = uiScale.x;
                }
                if(ImGui::SliderFloat("Height", &uiScale.y, 10.0f, 1000.0f))
                {
                    static_cast<UIButton*>(uiElements[selectedUiIndex].get())->height = uiScale.y;
                }
                if(ImGui::ColorEdit4("Hover Color", &uiHoverColor.x))
                {
                    static_cast<UIButton*>(uiElements[selectedUiIndex].get())->hoverColor = uiHoverColor;
                }
            }
        }
        ImGui::End();

        ImGui::Begin("Camera Controls");
        static DirectX::XMFLOAT3 cameraPos = {0.0f, 1.5f, -3.0f};
        static DirectX::XMFLOAT2 cameraRot = {0.0f, 0.0f};
        static float fov = 60.0f;
        static int previousCamIndex = -1;
        static bool isFPS = false;
        if(selectedCamIndex != previousCamIndex && selectedCamIndex >= 0)
        {
            cameraPos = g_cameras[selectedCamIndex].position;
            cameraRot = g_cameras[selectedCamIndex].rotation;
            fov = g_cameras[selectedCamIndex].fov;
            isFPS = g_cameras[selectedCamIndex].isFPS;
            previousCamIndex = selectedCamIndex;
        }
        std::vector<const char*> camNames;
        for(const Camera& cam : g_cameras)
            camNames.push_back(cam.name.c_str());
        if(ImGui::Combo("Select Camera", &selectedCamIndex, camNames.data(), camNames.size()))
        {
            if(selectedCamIndex >= 0)
            {
                cameraPos = g_cameras[selectedCamIndex].position;
                cameraRot = g_cameras[selectedCamIndex].rotation;
                fov = g_cameras[selectedCamIndex].fov;
                isFPS = g_cameras[selectedCamIndex].isFPS;
            }
        }
        if(selectedCamIndex >= 0)
        {
            if(ImGui::DragFloat3("Position", (float*)&cameraPos, 0.001f))
            {
                g_cameras[selectedCamIndex].position = cameraPos;
            }
            if(ImGui::DragFloat("Pitch (X)", &cameraRot.x, 1.0f, -90.0f, 90.0f))
            {
                g_cameras[selectedCamIndex].rotation.x = cameraRot.x;
            }
            if(ImGui::DragFloat("Yaw (Y)", &cameraRot.y, 1.0f, -180.0f, 180.0f))
            {
                g_cameras[selectedCamIndex].rotation.y = cameraRot.y;
            }
            if(ImGui::DragFloat("Field Of View", &fov, 1.0f, 60.0f, 120.0f, "%.3f"))
            {
                g_cameras[selectedCamIndex].fov = fov;
            }
            if(ImGui::Checkbox("Is FPS", &isFPS))
            {
                g_cameras[selectedCamIndex].isFPS = isFPS;
            }
        }
        ImGui::End();

        ImGui::Begin("Model Controls");
        static float modelScale = 0.5f;
        static DirectX::XMFLOAT3 modelPosition = {0.0f, 0.0f, 0.0f};
        static DirectX::XMFLOAT3 modelRotation = {0.0f, 0.0f, 0.0f};
        static int previousModelIndex = -1;
        static bool isStatic = false;
        static float mass = 0.0f;
        if(selectedModelIndex != previousModelIndex && selectedModelIndex >= 0)
        {
            modelPosition = g_models[selectedModelIndex].position;
            modelRotation = g_models[selectedModelIndex].rotation;
            modelScale = g_models[selectedModelIndex].scale;
            previousModelIndex = selectedModelIndex;
            isStatic = g_models[selectedModelIndex].isStatic;
            mass = g_models[selectedModelIndex].mass;
        }
        std::vector<const char*> modelNames;
        for(const Model& model : g_models)
            modelNames.push_back(model.name.c_str());

        if(ImGui::Combo("Select Model", &selectedModelIndex, modelNames.data(), modelNames.size()))
        {
            if(selectedModelIndex >= 0)
            {
                modelPosition = g_models[selectedModelIndex].position;
                modelRotation = g_models[selectedModelIndex].rotation;
                modelScale = g_models[selectedModelIndex].scale;
                isStatic = g_models[selectedModelIndex].isStatic;
                mass = g_models[selectedModelIndex].mass;
            }
        }

        if(selectedModelIndex >= 0)
        {
            modelPosition = g_models[selectedModelIndex].position;
            modelRotation = g_models[selectedModelIndex].rotation;
            modelScale = g_models[selectedModelIndex].scale;
            isStatic = g_models[selectedModelIndex].isStatic;
            mass = g_models[selectedModelIndex].mass;
            if(ImGui::DragFloat3("Position", (float*)&modelPosition, 0.001f))
            {
                g_models[selectedModelIndex].position = modelPosition;
            }
            if(ImGui::DragFloat3("Rotation", (float*)&modelRotation, 1.0f, -180.0f, 180.0f))
            {
                g_models[selectedModelIndex].rotation = modelRotation;
            }
            if(ImGui::InputFloat("Scale", &modelScale, 0.1f, 1.0f, "%.3f"))
            {
                g_models[selectedModelIndex].scale = modelScale;
            }
            if(ImGui::DragFloat("Mass", &mass, 0.0f))
            {
                g_models[selectedModelIndex].mass = mass;
            }
            if(ImGui::Checkbox("Is Static", &isStatic))
            {
                g_models[selectedModelIndex].isStatic = isStatic;
            }
        }
        ImGui::End();

        ImGui::Begin("Light Controls");
        static float range = 15.0f;
        static float intensity = 20.0f;
        static DirectX::XMFLOAT3 position = {0, 0, 0};
        static DirectX::XMFLOAT3 color = {1, 1, 1};
        static int previousPLIndex = -1;
        if(selectedPLIndex != previousPLIndex && selectedPLIndex >= 0)
        {
            range = pointLights[selectedPLIndex].range;
            intensity = pointLights[selectedPLIndex].intensity;
            position = pointLights[selectedPLIndex].position;
            color = pointLights[selectedPLIndex].color;
        }
        std::vector<const char*> itemsPL;
        for(const auto& str : plNames)
            itemsPL.push_back(str.c_str());
        if(ImGui::Combo("Select Point Light", &selectedPLIndex, itemsPL.data(), (int)itemsPL.size())) // TODO: Next implement spot lights :)
        {
            if(selectedPLIndex >= 0)
            {
                range = pointLights[selectedPLIndex].range;
                intensity = pointLights[selectedPLIndex].intensity;
                position = pointLights[selectedPLIndex].position;
                color = pointLights[selectedPLIndex].color;
            }
        }

        if(selectedPLIndex >= 0)
        {
            range = pointLights[selectedPLIndex].range;
            intensity = pointLights[selectedPLIndex].intensity;
            position = pointLights[selectedPLIndex].position;
            color = pointLights[selectedPLIndex].color;
            if(ImGui::InputFloat3("Position", (float*)&position, "%.3f"))
            {
                pointLights[selectedPLIndex].position = position;
            }
            if(ImGui::ColorEdit3("Color", (float*)&color))
            {
                pointLights[selectedPLIndex].color = color;
            }
            if(ImGui::SliderFloat("Range", &range, 0.01f, 50.0f, "%.3f"))
            {
                pointLights[selectedPLIndex].range = range;
            }
            if(ImGui::SliderFloat("Intensity", &intensity, 0.01f, 50.0f, "%.3f"))
            {
                pointLights[selectedPLIndex].intensity = intensity;
            }
        }
        ImGui::End();

        ImGui::Begin("Asset Browser");

        if(ImGui::CollapsingHeader("Models"))
        {
            static bool showRenamePopup = false;
            static int renameIndex = -1;
            static char renameBuffer[128] = {};

            for(size_t i = 0; i < g_models.size(); ++i)
            {
                const Model& model = g_models[i];
                ImGui::Selectable(model.name.c_str(), selectedModelIndex == static_cast<int>(i));
                if(ImGui::IsItemClicked())
                {
                    selectedModelIndex = static_cast<int>(i);
                }
                if(ImGui::BeginPopupContextItem())
                {
                    if(ImGui::MenuItem("Rename"))
                    {
                        showRenamePopup = true;
                        renameIndex = static_cast<int>(i);
                        strncpy(renameBuffer, model.name.c_str(), sizeof(renameBuffer));
                    }
                    if(ImGui::MenuItem("Delete"))
                    {
                        g_models.erase(g_models.begin() + i);
                        if(selectedModelIndex == static_cast<int>(i))
                            selectedModelIndex = -1;
                        ImGui::EndPopup();
                        break;
                    }
                    ImGui::EndPopup();
                }
            }

            if(showRenamePopup)
            {
                ImGui::OpenPopup("Rename Model");
                showRenamePopup = false;
            }

            if(ImGui::BeginPopupModal("Rename Model", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::InputText("New Name", renameBuffer, IM_ARRAYSIZE(renameBuffer));

                if(ImGui::Button("OK"))
                {
                    if(renameIndex >= 0 && renameIndex < g_models.size())
                    {
                        g_models[renameIndex].name = renameBuffer;
                    }
                    ImGui::CloseCurrentPopup();
                }

                ImGui::SameLine();
                if(ImGui::Button("Cancel"))
                {
                    ImGui::CloseCurrentPopup();
                }

                ImGui::EndPopup();
            }
        }

        if(ImGui::CollapsingHeader("Textures"))
        {
            static bool showRenamePopup = false;
            static int renameIndex = -1;
            static char renameBuffer[128] = {};

            for(size_t i = 0; i < g_textures.size(); ++i)
            {
                const std::string& name = g_textures[i].name;
                ImGui::Selectable(name.c_str());
                if(ImGui::BeginPopupContextItem())
                {
                    if(ImGui::MenuItem("Rename"))
                    {
                        showRenamePopup = true;
                        renameIndex = static_cast<int>(i);
                        strncpy(renameBuffer, name.c_str(), sizeof(renameBuffer));
                    }
                    if(ImGui::MenuItem("Delete"))
                    {
                        g_textures.erase(g_textures.begin() + i);
                        if(selectedModelIndex == static_cast<int>(i))
                            selectedModelIndex = -1;
                        ImGui::EndPopup();
                        break;
                    }
                    ImGui::EndPopup();
                }
            }

            if(showRenamePopup)
            {
                ImGui::OpenPopup("Rename Texture");
                showRenamePopup = false;
            }

            if(ImGui::BeginPopupModal("Rename Texture", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::InputText("New Name", renameBuffer, IM_ARRAYSIZE(renameBuffer));

                if(ImGui::Button("OK"))
                {
                    if(renameIndex >= 0 && renameIndex < g_textures.size())
                    {
                        g_textures[renameIndex].name = renameBuffer;
                        g_texture_info[renameIndex].name = renameBuffer;
                    }
                    ImGui::CloseCurrentPopup();
                }

                ImGui::SameLine();
                if(ImGui::Button("Cancel"))
                {
                    ImGui::CloseCurrentPopup();
                }

                ImGui::EndPopup();
            }
        }

        if(ImGui::CollapsingHeader("Lights"))
        {
            if(ImGui::TreeNode("Point Lights"))
            {
                static bool showRenamePopup = false;
                static int renameIndex = -1;
                static char renameBuffer[128] = {};

                for(size_t i = 0; i < pointLights.size(); ++i)
                {
                    const std::string& name = plNames[i];
                    ImGui::Selectable(name.c_str());
                    if(ImGui::BeginPopupContextItem())
                    {
                        if(ImGui::MenuItem("Rename"))
                        {
                            showRenamePopup = true;
                            renameIndex = static_cast<int>(i);
                            strncpy(renameBuffer, name.c_str(), sizeof(renameBuffer));
                        }
                        if(ImGui::MenuItem("Delete"))
                        {
                            pointLights.erase(pointLights.begin() + i);
                            if(selectedPLIndex == static_cast<int>(i))
                                selectedPLIndex = -1;
                            ImGui::EndPopup();
                            break;
                        }
                        ImGui::EndPopup();
                    }
                }

                if(showRenamePopup)
                {
                    ImGui::OpenPopup("Rename Point Light");
                    showRenamePopup = false;
                }

                if(ImGui::BeginPopupModal("Rename Point Light", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
                {
                    ImGui::InputText("New Name", renameBuffer, IM_ARRAYSIZE(renameBuffer));

                    if(ImGui::Button("OK"))
                    {
                        if(renameIndex >= 0 && renameIndex < pointLights.size())
                        {
                            plNames[renameIndex] = renameBuffer;
                        }
                        ImGui::CloseCurrentPopup();
                    }

                    ImGui::SameLine();
                    if(ImGui::Button("Cancel"))
                    {
                        ImGui::CloseCurrentPopup();
                    }

                    ImGui::EndPopup();
                }
                ImGui::TreePop();
            }
        }

        if(ImGui::CollapsingHeader("UI"))
        {
            if(ImGui::TreeNode("Labels"))
            {
                static bool showRenamePopup = false;
                static int renameIndex = -1;
                static char renameBuffer[128] = {};

                for(size_t i = 0; i < uiElements.size(); ++i)
                {
                    if(static_cast<UIElement*>(uiElements[i].get())->type != UIElementType::Label)
                        continue;
                    const std::string& name = static_cast<UILabel*>(uiElements[i].get())->name;
                    ImGui::Selectable(name.c_str());
                    if(ImGui::IsItemClicked())
                    {
                        selectedUiIndex = static_cast<int>(i);
                    }
                    if(ImGui::BeginPopupContextItem())
                    {
                        if(ImGui::MenuItem("Rename"))
                        {
                            showRenamePopup = true;
                            renameIndex = static_cast<int>(i);
                            strncpy(renameBuffer, name.c_str(), sizeof(renameBuffer));
                        }
                        if(ImGui::MenuItem("Delete"))
                        {
                            uiElements.erase(uiElements.begin() + i);
                            if(selectedUiIndex == static_cast<int>(i))
                                selectedUiIndex = -1;
                            ImGui::EndPopup();
                            break;
                        }
                        ImGui::EndPopup();
                    }
                }

                if(showRenamePopup)
                {
                    ImGui::OpenPopup("Rename Label");
                    showRenamePopup = false;
                }

                if(ImGui::BeginPopupModal("Rename Label", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
                {
                    ImGui::InputText("New Name", renameBuffer, IM_ARRAYSIZE(renameBuffer));

                    if(ImGui::Button("OK"))
                    {
                        if(renameIndex >= 0 && renameIndex < uiElements.size())
                        {
                            static_cast<UILabel*>(uiElements[renameIndex].get())->name = renameBuffer;
                        }
                        ImGui::CloseCurrentPopup();
                    }

                    ImGui::SameLine();
                    if(ImGui::Button("Cancel"))
                    {
                        ImGui::CloseCurrentPopup();
                    }

                    ImGui::EndPopup();
                }
                ImGui::TreePop();
            }
            if(ImGui::TreeNode("Buttons"))
            {
                static bool showRenamePopup = false;
                static int renameIndex = -1;
                static char renameBuffer[128] = {};

                for(size_t i = 0; i < uiElements.size(); ++i)
                {
                    if(static_cast<UIElement*>(uiElements[i].get())->type != UIElementType::Button)
                        continue;
                    const std::string& name = static_cast<UIButton*>(uiElements[i].get())->name;
                    ImGui::Selectable(name.c_str());
                    if(ImGui::IsItemClicked())
                    {
                        selectedUiIndex = static_cast<int>(i);
                    }
                    if(ImGui::BeginPopupContextItem())
                    {
                        if(ImGui::MenuItem("Rename"))
                        {
                            showRenamePopup = true;
                            renameIndex = static_cast<int>(i);
                            strncpy(renameBuffer, name.c_str(), sizeof(renameBuffer));
                        }
                        if(ImGui::MenuItem("Delete"))
                        {
                            uiElements.erase(uiElements.begin() + i);
                            if(selectedUiIndex == static_cast<int>(i))
                                selectedUiIndex = -1;
                            ImGui::EndPopup();
                            break;
                        }
                        ImGui::EndPopup();
                    }
                }

                if(showRenamePopup)
                {
                    ImGui::OpenPopup("Rename Button");
                    showRenamePopup = false;
                }

                if(ImGui::BeginPopupModal("Rename Button", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
                {
                    ImGui::InputText("New Name", renameBuffer, IM_ARRAYSIZE(renameBuffer));

                    if(ImGui::Button("OK"))
                    {
                        if(renameIndex >= 0 && renameIndex < uiElements.size())
                        {
                            static_cast<UIButton*>(uiElements[renameIndex].get())->name = renameBuffer;
                        }
                        ImGui::CloseCurrentPopup();
                    }

                    ImGui::SameLine();
                    if(ImGui::Button("Cancel"))
                    {
                        ImGui::CloseCurrentPopup();
                    }

                    ImGui::EndPopup();
                }
                ImGui::TreePop();
            }
        }

        if(ImGui::CollapsingHeader("Cameras"))
        {
            static bool showRenamePopup = false;
            static int renameIndex = -1;
            static char renameBuffer[128] = {};

            for(size_t i = 0; i < g_cameras.size(); ++i)
            {
                const Camera& cam = g_cameras[i];
                ImGui::Selectable(cam.name.c_str(), selectedCamIndex == static_cast<int>(i));
                if(ImGui::IsItemClicked())
                {
                    selectedCamIndex = static_cast<int>(i);
                }
                if(ImGui::BeginPopupContextItem())
                {
                    if(ImGui::MenuItem("Rename"))
                    {
                        showRenamePopup = true;
                        renameIndex = static_cast<int>(i);
                        strncpy(renameBuffer, cam.name.c_str(), sizeof(renameBuffer));
                    }
                    if(ImGui::MenuItem("Delete"))
                    {
                        g_cameras.erase(g_cameras.begin() + i);
                        if(selectedCamIndex == static_cast<int>(i))
                            selectedCamIndex = -1;
                        ImGui::EndPopup();
                        break;
                    }
                    ImGui::EndPopup();
                }
            }

            if(showRenamePopup)
            {
                ImGui::OpenPopup("Rename Camera");
                showRenamePopup = false;
            }

            if(ImGui::BeginPopupModal("Rename Camera", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::InputText("New Name", renameBuffer, IM_ARRAYSIZE(renameBuffer));

                if(ImGui::Button("OK"))
                {
                    if(renameIndex >= 0 && renameIndex < g_cameras.size())
                    {
                        g_cameras[renameIndex].name = renameBuffer;
                    }
                    ImGui::CloseCurrentPopup();
                }

                ImGui::SameLine();
                if(ImGui::Button("Cancel"))
                {
                    ImGui::CloseCurrentPopup();
                }

                ImGui::EndPopup();
            }
        }

        if(ImGui::CollapsingHeader("Scripts"))
        {
            static bool showRenamePopup = false;
            static int renameIndex = -1;
            static char renameBuffer[128] = {};

            for(int i = 0; i < scripts.size(); ++i)
            {
                const std::string& name = scripts[i];
                ImGui::Selectable(name.c_str());
                if(ImGui::BeginPopupContextItem())
                {
                    if(ImGui::MenuItem("Rename"))
                    {
                        showRenamePopup = true;
                        renameIndex = static_cast<int>(i);
                        strncpy(renameBuffer, name.c_str(), sizeof(renameBuffer));
                    }
                    if(ImGui::MenuItem("Delete"))
                    {
                        scripts.erase(scripts.begin() + i);
                        if(selectedModelIndex == static_cast<int>(i))
                            selectedModelIndex = -1;
                        ImGui::EndPopup();
                        break;
                    }
                    ImGui::EndPopup();
                }
            }

            if(showRenamePopup)
            {
                ImGui::OpenPopup("Rename Script");
                showRenamePopup = false;
            }

            if(ImGui::BeginPopupModal("Rename Script", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::InputText("New Name", renameBuffer, IM_ARRAYSIZE(renameBuffer));

                if(ImGui::Button("OK"))
                {
                    if(renameIndex >= 0 && renameIndex < scripts.size())
                    {
                        std::string cmd = "ren " + scripts[renameIndex] + " " + renameBuffer;
                        std::system(cmd.c_str());
                        scripts[renameIndex] = renameBuffer;
                    }
                    ImGui::CloseCurrentPopup();
                }

                ImGui::SameLine();
                if(ImGui::Button("Cancel"))
                {
                    ImGui::CloseCurrentPopup();
                }

                ImGui::EndPopup();
            }
        }

        ImGui::End();

        if(ImGui::BeginMainMenuBar())
        {
            if(ImGui::BeginMenu("File"))
            {
                if(ImGui::BeginMenu("New"))
                {
                    if(ImGui::MenuItem("Script"))
                    {
                        std::system("copy NUL script.cpp");
                        scripts.push_back("script.cpp");
                    }
                    if(ImGui::MenuItem("Camera"))
                    {
                        std::string baseName = "Camera";
                        std::vector<std::string> existingCamNames;
                        for(const auto& cam : g_cameras)
                            existingCamNames.push_back(cam.name);
                        std::string cameraName = MakeUniqueName(baseName, existingCamNames);
                        Camera cam;
                        cam.position = {0.0f, 0.0f, 0.0f};
                        cam.rotation = {0.0f, 0.0f};
                        cam.fov = 60.0f;
                        cam.name = cameraName;
                        cam.isFPS = false;

                        g_cameras.push_back(cam);
                    }
                    if(ImGui::BeginMenu("UI"))
                    {
                        if(ImGui::MenuItem("Label"))
                        {
                            auto label = std::make_unique<UILabel>();
                            label->x = 0;
                            std::cout << "Label X: " << label->x << '\n';
                            label->y = 0;
                            label->color = {1.0f, 1.0f, 1.0f, 1.0f};
                            label->text = "New label";
                            std::string baseName = "Label";
                            std::vector<std::string> existingUiNames;
                            for(const auto& elem : uiElements)
                                existingUiNames.push_back(static_cast<UILabel*>(elem.get())->name);
                            std::string uiName = MakeUniqueName(baseName, existingUiNames);
                            label->name = uiName;
                            uiElements.push_back(std::move(label));
                        }
                        if(ImGui::MenuItem("Button"))
                        {
                            auto button = std::make_unique<UIButton>();
                            button->x = 0;
                            button->y = 0;
                            button->width = 200;
                            button->height = 100;
                            button->color = {1.0f, 1.0f, 1.0f, 1.0f};
                            button->text = "New button";
                            std::string baseName = "Button";
                            std::vector<std::string> existingUiNames;
                            for(const auto& elem : uiElements)
                                existingUiNames.push_back(static_cast<UIButton*>(elem.get())->name);
                            std::string uiName = MakeUniqueName(baseName, existingUiNames);
                            button->name = uiName;
                            uiElements.push_back(std::move(button));
                        }
                        ImGui::EndMenu();
                    }
                    if(ImGui::BeginMenu("Light"))
                    {
                        if(ImGui::MenuItem("Point Light"))
                        {
                            PointLight pointLight;
                            pointLight.position = {0, 0, 0};
                            pointLight.color = {1, 1, 1};
                            pointLight.intensity = 5.0f;
                            pointLight.range = 5.0f;
                            std::vector<std::string> existingPLNames;
                            std::string baseName = "Point Light";
                            for(const std::string& name : plNames)
                                existingPLNames.push_back(name);
                            std::string plName = MakeUniqueName(baseName, existingPLNames);
                            plNames.push_back(plName);
                            pointLights.push_back(pointLight);
                            selectedPLIndex = static_cast<int>(pointLights.size()) - 1;
                        }
                        ImGui::EndMenu();
                    }
                    ImGui::EndMenu();
                }
                if(ImGui::BeginMenu("Open"))
                {
                    if(ImGui::MenuItem("Model"))
                    {
                        std::string path = OpenFileDialog("OBJ Files\0*.obj\0");
                        if(!path.empty())
                        {
                            Model newModel = LoadOBJModel(path);
                            if(newModel.valid)
                            {
                                std::string baseName = std::filesystem::path(path).filename().string();
                                std::vector<std::string> existingModelNames;
                                for(const auto& model : g_models)
                                    existingModelNames.push_back(model.name);
                                newModel.name = MakeUniqueName(baseName, existingModelNames);

                                newModel.scale = modelScale;
                                newModel.position = modelPosition;
                                newModel.rotation = modelRotation;
                                newModel.textureSRV = nullptr;
                                newModel.isStatic = false;
                                newModel.mass = 1.0f;
                                float width = (newModel.localBounds.max.x - newModel.localBounds.min.x) * newModel.scale;
                                float height = (newModel.localBounds.max.y - newModel.localBounds.min.y) * newModel.scale;
                                float depth = (newModel.localBounds.max.z - newModel.localBounds.min.z) * newModel.scale;

                                physx::PxVec3 halfExtents(width / 2.0f, height / 2.0f, depth / 2.0f);
                                physx::PxBoxGeometry boxGeometry(halfExtents);

                                physx::PxTransform transform(
                                        physx::PxVec3(newModel.position.x, newModel.position.y, newModel.position.z),
                                        physx::PxQuat(physx::PxIdentity) // or you can use Euler->Quat if needed
                                        );

                                // Create rigid body
                                physx::PxRigidDynamic* actor = gPhysics->createRigidDynamic(transform);
                                physx::PxShape* shape = gPhysics->createShape(boxGeometry, *gMaterial);
                                actor->attachShape(*shape);

                                physx::PxRigidBodyExt::updateMassAndInertia(*actor, newModel.mass);
                                gScene->addActor(*actor);

                                newModel.rigidBody = actor; // store pointer in your model struct
                                g_models.push_back(newModel);
                                selectedModelIndex = static_cast<int>(g_models.size()) - 1;
                            }
                        }
                    }
                    /*if(ImGui::MenuItem("Texture"))
                    {
                        std::string path = OpenFileDialog("Image Files\0*.png;*.jpg;*.jpeg;*.bmp\0");
                        if(!path.empty() && selectedModelIndex >= 0)
                        {
                            ID3D11ShaderResourceView* newTexture = LoadTextureIfNotLoaded(path);
                            if(newTexture)
                            {
                                g_models[selectedModelIndex].textureSRV = newTexture;
                                std::string baseName = std::filesystem::path(path).filename().string();
                                std::vector<std::string> existingTextureNames;
                                for(const auto& tex : g_textures)
                                    existingTextureNames.push_back(tex.name);
                                std::string textureName = MakeUniqueName(baseName, existingTextureNames);
                                g_models[selectedModelIndex].id = textureName;
                                g_textures.push_back({ textureName, newTexture });

                                TextureInfo textureInfo;
                                textureInfo.name = textureName;
                                textureInfo.path = path;
                                textureInfo.srv = newTexture;
                                g_texture_info.push_back(textureInfo);
                            }
                        }
                    }*/
                    if(ImGui::MenuItem("Texture"))
                    {
                        std::string path = OpenFileDialog("Image Files\0*.png;*.jpg;*.jpeg;*.bmp\0");
                        if(!path.empty() && selectedModelIndex >= 0)
                        {
                            ID3D11ShaderResourceView* newTexture = LoadTextureIfNotLoaded(path);
                            if(newTexture)
                            {
                                g_models[selectedModelIndex].textureSRV = newTexture;

                                auto it = std::find_if(g_texture_info.begin(), g_texture_info.end(),
                                        [&](const TextureInfo& info) { return info.path == path; });

                                if(it != g_texture_info.end())
                                {
                                    g_models[selectedModelIndex].id = it->name;
                                }
                                else
                                {
                                    std::string baseName = std::filesystem::path(path).filename().string();
                                    std::vector<std::string> existingTextureNames;
                                    for(const auto& tex : g_textures)
                                        existingTextureNames.push_back(tex.name);

                                    std::string textureName = MakeUniqueName(baseName, existingTextureNames);
                                    g_models[selectedModelIndex].id = textureName;

                                    g_textures.push_back({ textureName, newTexture });

                                    TextureInfo textureInfo;
                                    textureInfo.name = textureName;
                                    textureInfo.path = path;
                                    textureInfo.srv = newTexture;
                                    g_texture_info.push_back(textureInfo);
                                }
                            }
                        }
                    }
                    if(ImGui::MenuItem("Project"))
                    {
                        projectPath = OpenFileDialog("Project files\0*.ge3proj\0");
                        if(!projectPath.empty())
                        {
                            LoadProject(projectPath, g_models, g_cameras);
                            if(fontPath.length() > 0)
                            {
                                std::wstring wfontPath = std::wstring(fontPath.begin(), fontPath.end());
                                g_spriteFont = std::make_unique<DirectX::SpriteFont>(g_device, wfontPath.c_str());
                            }
                            previousCamIndex = -1;
                            selectedPLIndex = -1;
                            if(!g_models.empty())
                                selectedModelIndex = 0;
                            if(!g_cameras.empty())
                                selectedCamIndex = 0;
                            if(!pointLights.empty())
                                selectedPLIndex = 0;
                        }
                    }
                    if(ImGui::MenuItem("Font"))
                    {
                        fontPath = OpenFileDialog("SpriteFont files\0*.spritefont\0");
                        if(!fontPath.empty())
                        {
                            std::wstring wfontPath = std::wstring(fontPath.begin(), fontPath.end());
                            g_spriteFont = std::make_unique<DirectX::SpriteFont>(g_device, wfontPath.c_str());
                        }
                    }
                    ImGui::EndMenu();
                }
                if(ImGui::BeginMenu("Save"))
                {
                    if(ImGui::MenuItem("Project"))
                    {
                        std::string path = OpenFileDialog("Project files\0*.ge3proj\0", true);
                        if(!path.empty())
                        {
                            if(path.find(".ge3proj") == std::string::npos)
                                path += ".ge3proj";
                            SaveProject(path, g_models);
                            projectPath = path;
                        }
                    }
                    ImGui::EndMenu();
                }
                ImGui::EndMenu();
            }
            if(ImGui::BeginMenu("Edit"))
            {
                if(selectedModelIndex >= 0 && ImGui::MenuItem("Delete Model", "Del"))
                {
                    g_models.erase(g_models.begin() + selectedModelIndex);
                    if(g_models.empty())
                        selectedModelIndex = -1;
                    else if(selectedModelIndex >= static_cast<int>(g_models.size()))
                        selectedModelIndex = static_cast<int>(g_models.size()) - 1;
                }
                ImGui::EndMenu();
            }

            ImGui::Separator();

            if(ImGui::MenuItem("Compile Game"))
            {
                char folder[MAX_PATH] = "";
                BROWSEINFO bi = { 0 };
                bi.lpszTitle = "Select folder for game output";
                LPITEMIDLIST pidl = SHBrowseForFolder(&bi);
                if(pidl != NULL)
                {
                    SHGetPathFromIDList(pidl, folder);
                    CoTaskMemFree(pidl);
                    if(strlen(folder) > 0)
                    {
                        std::vector<std::string> texturePaths;
                        for(const auto& tex : g_texture_info)
                            texturePaths.push_back(tex.path);
                        CompileGame(folder, g_models, texturePaths);
                    }
                }
            }

            ImGui::EndMainMenuBar();
        }

        ImGui::Begin("Game View"); 
        gameSize = ImGui::GetContentRegionAvail();

        static ID3D11Texture2D* rtTex = nullptr;
        static ID3D11RenderTargetView* rtRTV = nullptr;
        static ID3D11ShaderResourceView* rtSRV = nullptr;
        static ID3D11Texture2D* rtDepthTex = nullptr;
        static ID3D11DepthStencilView* rtDSV = nullptr;
        static UINT lastWidth = 0;
        static UINT lastHeight = 0;

        if(!rtTex || lastWidth != (UINT)gameSize.x || lastHeight != (UINT)gameSize.y)
        {
            lastWidth = (UINT)gameSize.x;
            lastHeight = (UINT)gameSize.y;

            if(rtTex) { rtTex->Release(); rtTex = nullptr; }
            if(rtRTV) { rtRTV->Release(); rtRTV = nullptr; }
            if(rtSRV) { rtSRV->Release(); rtSRV = nullptr; }
            if(rtDepthTex) { rtDepthTex->Release(); rtDepthTex = nullptr; }
            if(rtDSV) { rtDSV->Release(); rtDSV = nullptr; }

            if(lastWidth > 0 && lastHeight > 0)
            {
                D3D11_TEXTURE2D_DESC desc = {};
                desc.Width = (UINT)gameSize.x;
                desc.Height = (UINT)gameSize.y;
                desc.MipLevels = 1;
                desc.ArraySize = 1;
                desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                desc.SampleDesc.Count = 1;
                desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
                desc.Usage = D3D11_USAGE_DEFAULT;

                HRESULT hr = g_device->CreateTexture2D(&desc, nullptr, &rtTex);
                if(SUCCEEDED(hr))
                {
                    hr = g_device->CreateRenderTargetView(rtTex, nullptr, &rtRTV);
                    if(SUCCEEDED(hr))
                    {
                        hr = g_device->CreateShaderResourceView(rtTex, nullptr, &rtSRV);
                    }
                }

                D3D11_TEXTURE2D_DESC depthDesc = {};
                depthDesc.Width = (UINT)gameSize.x;
                depthDesc.Height = (UINT)gameSize.y;
                depthDesc.MipLevels = 1;
                depthDesc.ArraySize = 1;
                depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
                depthDesc.SampleDesc.Count = 1;
                depthDesc.Usage = D3D11_USAGE_DEFAULT;
                depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

                hr = g_device->CreateTexture2D(&depthDesc, nullptr, &rtDepthTex);
                if(SUCCEEDED(hr))
                {
                    hr = g_device->CreateDepthStencilView(rtDepthTex, nullptr, &rtDSV);
                }
            }
        }

        if(rtRTV && rtDSV && lastWidth > 0 && lastHeight > 0)
        {
            g_deviceContext->OMSetRenderTargets(1, &rtRTV, rtDSV);
            g_deviceContext->ClearRenderTargetView(rtRTV, clearColor);
            g_deviceContext->ClearDepthStencilView(rtDSV, D3D11_CLEAR_DEPTH, 1.0f, 0);

            g_deviceContext->OMSetRenderTargets(1, &rtRTV, g_depthStencilView);
            D3D11_VIEWPORT viewportRT = {};
            viewportRT.Width = gameSize.x;
            viewportRT.Height = gameSize.y;
            viewportRT.MinDepth = 0.0f;
            viewportRT.MaxDepth = 1.0f;
            g_deviceContext->RSSetViewports(1, &viewportRT);

            g_deviceContext->ClearRenderTargetView(rtRTV, clearColor);

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

                g_deviceContext->PSSetConstantBuffers(1, 1, &g_lightBuffer);
                g_deviceContext->PSSetShaderResources(1, 1, &g_pointLightSRV);
                g_deviceContext->IASetInputLayout(g_inputLayout);
                g_deviceContext->OMSetDepthStencilState(g_depthStencilState, 1);
                g_deviceContext->RSSetState(g_rasterizerState);

                if(model.textureSRV)
                {
                    g_deviceContext->PSSetShaderResources(0, 1, &model.textureSRV);
                }
                else
                {
                    ID3D11ShaderResourceView* nullTexture = nullptr;
                    g_deviceContext->PSSetShaderResources(0, 1, &nullTexture);
                }

                g_deviceContext->IASetVertexBuffers(0, 1, &model.vertexBuffer, &stride, &offset);

                DirectX::XMMATRIX RotationMatrix = DirectX::XMMatrixRotationRollPitchYaw(
                        DirectX::XMConvertToRadians(g_cameras[selectedCamIndex].rotation.x),
                        DirectX::XMConvertToRadians(g_cameras[selectedCamIndex].rotation.y),
                        0.0f
                        );

                DirectX::XMVECTOR forward = DirectX::XMVector3Transform(DirectX::XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), RotationMatrix);
                DirectX::XMVECTOR cameraTarget = DirectX::XMVectorAdd(DirectX::XMLoadFloat3(&g_cameras[selectedCamIndex].position), forward);

                DirectX::XMVECTOR up = DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

                view = DirectX::XMMatrixLookAtLH(
                        DirectX::XMLoadFloat3(&g_cameras[selectedCamIndex].position),
                        cameraTarget,
                        up
                        );

                projection = DirectX::XMMatrixPerspectiveFovLH(
                        DirectX::XMConvertToRadians(g_cameras[selectedCamIndex].fov),
                        gameSize.x / gameSize.y,
                        0.1f,
                        100.0f
                        );

                DirectX::XMMATRIX world =
                    DirectX::XMMatrixScaling(model.scale, model.scale, model.scale) *
                    DirectX::XMMatrixRotationRollPitchYaw(DirectX::XMConvertToRadians(model.rotation.x), DirectX::XMConvertToRadians(model.rotation.y), DirectX::XMConvertToRadians(model.rotation.z)) *
                    DirectX::XMMatrixTranslation(model.position.x, model.position.y, model.position.z);

                ImVec2 windowPos = ImGui::GetWindowPos();
                ImVec2 imagePos = ImGui::GetCursorScreenPos();
                ImGuizmo::SetRect(imagePos.x, imagePos.y, gameSize.x, gameSize.y);
                ImGuizmo::SetDrawlist();

                D3D11_MAPPED_SUBRESOURCE mappedResource;
                HRESULT hr = g_deviceContext->Map(g_matrixBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
                if(SUCCEEDED(hr))
                {
                    MatrixBuffer* dataPtr = (MatrixBuffer*)mappedResource.pData;
                    dataPtr->world = DirectX::XMMatrixTranspose(world);
                    dataPtr->view = DirectX::XMMatrixTranspose(view);
                    dataPtr->projection = DirectX::XMMatrixTranspose(projection);
                    g_deviceContext->Unmap(g_matrixBuffer, 0);
                }

                g_deviceContext->VSSetConstantBuffers(0, 1, &g_matrixBuffer);
                g_deviceContext->Draw(model.vertices.size(), 0);
            }

            ortho = DirectX::XMMatrixOrthographicOffCenterLH(
                    0.0f, gameSize.x,
                    gameSize.y, 0.0f,
                    0.0f, 1.0f);

            double mx, my;
            glfwGetCursorPos(window, &mx, &my);
            bool clicked = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;

            for(auto& element : uiElements)
            {
                element->Draw();
                /*if(element->type == UIElementType::Button)
                    element->Update(mx, my, clicked);*/
            }

            g_deviceContext->OMSetRenderTargets(1, &g_renderTargetView, g_depthStencilView);
            g_deviceContext->RSSetViewports(1, &viewportMain);
            g_deviceContext->OMSetDepthStencilState(g_depthStencilState, 1);
            g_deviceContext->RSSetState(g_rasterizerState);
        }

        if(rtSRV)
            ImGui::Image((ImTextureID)rtSRV, gameSize);
        if(ImGui::IsItemHovered())
        {
            ImVec2 imagePos = ImGui::GetItemRectMin();
            ImVec2 mousePos = ImGui::GetMousePos();    // screen-space
            ImVec2 localMouse = IMVEC2_SUB(mousePos, imagePos);   // relative to game view

            float mouseX = localMouse.x / gameSize.x;
            float mouseY = localMouse.y / gameSize.y;
            float ndcX = mouseX * 2.0f - 1.0f;
            float ndcY = 1.0f - mouseY * 2.0f;

            DirectX::XMMATRIX proj = projection;
            DirectX::XMMATRIX viewM = view;
            DirectX::XMMATRIX invViewProj = DirectX::XMMatrixInverse(nullptr, viewM * proj);

            DirectX::XMVECTOR nearPoint = DirectX::XMVectorSet(ndcX, ndcY, 0.0f, 1.0f);
            DirectX::XMVECTOR farPoint = DirectX::XMVectorSet(ndcX, ndcY, 1.0f, 1.0f);

            nearPoint = DirectX::XMVector3TransformCoord(nearPoint, invViewProj);
            farPoint = DirectX::XMVector3TransformCoord(farPoint, invViewProj);

            DirectX::XMVECTOR rayDir = DirectX::XMVector3Normalize(DirectX::XMVectorSubtract(farPoint, nearPoint));
            DirectX::XMVECTOR rayOrigin = nearPoint;

            if(ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGuizmo::IsOver() && !ImGuizmo::IsUsing())
            {
                float closestDist = FLT_MAX;
                int hitIndex = -1;

                for(int i = 0; i < g_models.size(); ++i)
                {
                    const Model& model = g_models[i];
                    DirectX::XMMATRIX modelMatrix =
                        DirectX::XMMatrixScaling(model.scale, model.scale, model.scale) *
                        DirectX::XMMatrixRotationRollPitchYaw(
                                DirectX::XMConvertToRadians(model.rotation.x),
                                DirectX::XMConvertToRadians(model.rotation.y),
                                DirectX::XMConvertToRadians(model.rotation.z)) *
                        DirectX::XMMatrixTranslation(model.position.x, model.position.y, model.position.z);

                    float hitDist = 0.0f;
                    if(RayIntersectsAABB(rayOrigin, rayDir, model.localBounds, modelMatrix, hitDist))
                    {
                        if(hitDist < closestDist)
                        {
                            closestDist = hitDist;
                            hitIndex = i;
                        }
                    }
                }

                if(hitIndex >= 0)
                {
                    selectedModelIndex = hitIndex;
                    std::cout << "Selected: " << g_models[hitIndex].name << std::endl;
                }
            }

            ImGuizmo::SetDrawlist();
            ImGuizmo::SetRect(imagePos.x, imagePos.y, gameSize.x, gameSize.y);
            ImGuizmo::Enable(true);

            if(selectedModelIndex >= 0 && selectedModelIndex < g_models.size() && currentTarget == GizmoTarget::Model)
            {
                Model& model = g_models[selectedModelIndex];

                DirectX::XMMATRIX scaleMatrix = DirectX::XMMatrixScaling(model.scale, model.scale, model.scale);
                DirectX::XMMATRIX rotationMatrix = DirectX::XMMatrixRotationRollPitchYaw(
                        DirectX::XMConvertToRadians(model.rotation.x),
                        DirectX::XMConvertToRadians(model.rotation.y),
                        DirectX::XMConvertToRadians(model.rotation.z)
                        );
                DirectX::XMMATRIX translationMatrix = DirectX::XMMatrixTranslation(
                        model.position.x, model.position.y, model.position.z
                        );

                ImGuizmo::OPERATION operation = ImGuizmo::TRANSLATE;
                ImGuizmo::MODE mode = ImGuizmo::LOCAL;

                DirectX::XMMATRIX modelMatrixXM = scaleMatrix * rotationMatrix * translationMatrix;

                DirectX::XMFLOAT4X4 viewMatrix, projMatrix, modelMatrix;
                XMStoreFloat4x4(&viewMatrix, view);
                XMStoreFloat4x4(&projMatrix, projection);
                XMStoreFloat4x4(&modelMatrix, modelMatrixXM);

                if(ImGuizmo::Manipulate(reinterpret_cast<const float*>(&viewMatrix), reinterpret_cast<const float*>(&projMatrix), operation, mode, reinterpret_cast<float*>(&modelMatrix)))
                {
                    float translation[3], rotation[3], scale[3];
                    ImGuizmo::DecomposeMatrixToComponents(reinterpret_cast<float*>(&modelMatrix), translation, rotation, scale);

                    model.position = { translation[0], translation[1], translation[2] };
                }
            }
            if(selectedPLIndex >= 0 && selectedPLIndex < pointLights.size() && currentTarget == GizmoTarget::Light)
            {
                PointLight& pl = pointLights[selectedPLIndex];

                DirectX::XMMATRIX translationMatrix = DirectX::XMMatrixTranslation(
                        pl.position.x, pl.position.y, pl.position.z
                        );

                DirectX::XMMATRIX lightMatrix = DirectX::XMMatrixTranslation(
                        pl.position.x,
                        pl.position.y,
                        pl.position.z
                        );

                ImGuizmo::OPERATION operation = ImGuizmo::TRANSLATE;
                ImGuizmo::MODE mode = ImGuizmo::WORLD;

                DirectX::XMFLOAT4X4 viewMatrix, projMatrix, plMatrix;
                XMStoreFloat4x4(&viewMatrix, view);
                XMStoreFloat4x4(&projMatrix, projection);
                XMStoreFloat4x4(&plMatrix, lightMatrix);

                if(ImGuizmo::Manipulate(reinterpret_cast<const float*>(&viewMatrix), reinterpret_cast<const float*>(&projMatrix), operation, mode, reinterpret_cast<float*>(&plMatrix)))
                {
                    float translation[3], rotation[3], scale[3];
                    ImGuizmo::DecomposeMatrixToComponents(reinterpret_cast<float*>(&plMatrix), translation, rotation, scale);

                    pl.position = { translation[0], translation[1], translation[2] };
                }
            }
        }
        ImGui::End();

        ImGui::Render();
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        g_swapChain->Present(1, 0);

        glfwPollEvents();
    }

    for(auto& model : g_models)
    {
        if(model.vertexBuffer) model.vertexBuffer->Release();
    }
    g_models.clear();

    CleanDX();

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
