#include <iostream>
#include <vector>
#include <string>
#include <tuple>
#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <common.hpp>
#include <window_manager.hpp>
#include <file_manager.hpp>
#include <shader_manager.hpp>

constexpr int TRANSMITTANCE_W = 256;
constexpr int TRANSMITTANCE_H = 64;
constexpr int SCATTERING_R_SIZE = 32;
constexpr int SCATTERING_MU_SIZE = 128;
constexpr int SCATTERING_MU_S_SIZE = 32;
constexpr int SCATTERING_NU_SIZE = 8;
constexpr int SCATTERING_W = SCATTERING_NU_SIZE * SCATTERING_MU_S_SIZE; // 256
constexpr int IRRADIANCE_W = 64;
constexpr int IRRADIANCE_H = 16;

// must match AtmosphereUBO::radii.x
constexpr double PLANET_BOTTOM_RADIUS = 6360000.0;

// atmosphere
struct alignas(16) AtmosphereUBO
{
    // w = scale height
    glm::vec4 rayleigh_scattering{5.802e-6f, 13.558e-6f, 33.1e-6f, 8000.0f};
    glm::vec4 mie_scattering{3.996e-6f, 3.996e-6f, 3.996e-6f, 1200.0f};
    glm::vec4 mie_extinction{4.440e-6f, 4.440e-6f, 4.440e-6f, 0.8f};
    glm::vec4 absorption_extinction{0.650e-6f, 1.881e-6f, 0.085e-6f, 25000.0f};
    glm::vec4 solar_irradiance{1.474f, 1.8504f, 1.91198f, 0.004675f};
    glm::vec4 radii{6360000.0f, 6420000.0f, -0.2079f, 15000.0f};
    glm::vec4 ground_albedo{0.1f, 0.1f, 0.1f, 0.0f};
};
static_assert(sizeof(AtmosphereUBO) == 7 * 16, "UBO layout mismatch");

// UBO at binding 1
struct FrameData
{
    glm::vec4 camera_pos_exposure; // xyz = position relative to planet center (m), w = exposure
    glm::vec4 cam_right;           // scaled by aspect * tan(fov/2)
    glm::vec4 cam_up;              // scaled by tan(fov/2)
    glm::vec4 cam_forward;
    glm::vec4 sun_dir;
};

class FrameRing
{
public:
    static constexpr int SLOTS = 3;

    void Create()
    {
        GLint align = 256;
        glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &align);
        stride = (sizeof(FrameData) + align - 1) / align * align;

        const GLbitfield flags = GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;
        glCreateBuffers(1, &buffer);
        glNamedBufferStorage(buffer, stride * SLOTS, nullptr, flags);
        mapped = static_cast<uint8_t *>(glMapNamedBufferRange(buffer, 0, stride * SLOTS, flags));
    }

    void Upload(const FrameData &data)
    {
        index = (index + 1) % SLOTS;
        if (fences[index])
        {
            glClientWaitSync(fences[index], GL_SYNC_FLUSH_COMMANDS_BIT, 1'000'000'000);
            glDeleteSync(fences[index]);
            fences[index] = nullptr;
        }
        std::memcpy(mapped + index * stride, &data, sizeof(FrameData));
        glBindBufferRange(GL_UNIFORM_BUFFER, 1, buffer, index * stride, sizeof(FrameData));
    }

    void EndFrame() { fences[index] = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0); }

private:
    GLuint buffer = 0;
    uint8_t *mapped = nullptr;
    GLsizeiptr stride = 0;
    GLsync fences[SLOTS] = {};
    int index = 0;
};

static std::string Compose(const std::string &common, const std::string &pass)
{
    return "#version 460 core\n" + common + "\n#line 1\n" + pass;
}

static GLuint BuildProgram(const std::vector<std::pair<GLenum, std::string>> &stages)
{
    GLuint program = glCreateProgram();
    std::vector<GLuint> shaders;

    for (const auto &[type, source] : stages)
    {
        GLuint shader = glCreateShader(type);
        const char *src = source.c_str();
        glShaderSource(shader, 1, &src, nullptr);
        glCompileShader(shader);

        GLint ok = 0;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
        if (!ok)
        {
            char log[4096];
            glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
            std::cerr << "[shader] compile error: " << log << "\n";
        }
        glAttachShader(program, shader);
        shaders.push_back(shader);
    }

    glLinkProgram(program);
    GLint ok = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        char log[4096];
        glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        std::cerr << "[shader] link error: " << log << "\n";
    }

    for (GLuint s : shaders)
    {
        glDetachShader(program, s);
        glDeleteShader(s);
    }
    return program;
}

static GLuint CreateTexture2D(GLenum format, int w, int h)
{
    GLuint t;
    glCreateTextures(GL_TEXTURE_2D, 1, &t);
    glTextureStorage2D(t, 1, format, w, h);
    glTextureParameteri(t, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(t, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(t, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(t, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return t;
}

static GLuint CreateTexture3D(GLenum format, int w, int h, int d)
{
    GLuint t;
    glCreateTextures(GL_TEXTURE_3D, 1, &t);
    glTextureStorage3D(t, 1, format, w, h, d);
    glTextureParameteri(t, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(t, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(t, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(t, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTextureParameteri(t, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    return t;
}

int main()
{
    WindowManager::Window window{};
    window.Create("Sky", 800, 600);
    window.EnableOutputDebug();

    // atmosphere UBO, immutable
    AtmosphereUBO atmosphereUBO{};
    GLuint ubo;
    glCreateBuffers(1, &ubo);
    glNamedBufferStorage(ubo, sizeof(AtmosphereUBO), &atmosphereUBO, 0);
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, ubo);

    // per-frame ring buffer
    FrameRing frameRing;
    frameRing.Create();

    // tables
    GLuint transmittanceTex = CreateTexture2D(GL_RGBA32F, TRANSMITTANCE_W, TRANSMITTANCE_H);
    GLuint deltaIrradianceTex = CreateTexture2D(GL_RGBA32F, IRRADIANCE_W, IRRADIANCE_H);
    GLuint deltaRayleighTex = CreateTexture3D(GL_RGBA32F, SCATTERING_W, SCATTERING_MU_SIZE, SCATTERING_R_SIZE);
    GLuint deltaMieTex = CreateTexture3D(GL_RGBA32F, SCATTERING_W, SCATTERING_MU_SIZE, SCATTERING_R_SIZE);
    GLuint scatteringTex = CreateTexture3D(GL_RGBA32F, SCATTERING_W, SCATTERING_MU_SIZE, SCATTERING_R_SIZE);

    const std::string common = FileManager::ReadTextFile("../../../resources/shaders/sky/atmosphere_common.glsl");
    auto computeProgram = [&](const char *path)
    {
        return BuildProgram({{GL_COMPUTE_SHADER, Compose(common, FileManager::ReadTextFile(path))}});
    };
    // build program
    GLuint transmittanceCS = computeProgram("../../../resources/shaders/sky/transmittance_comp.glsl");
    GLuint directIrradianceCS = computeProgram("../../../resources/shaders/sky/direct_irradiance_comp.glsl");
    GLuint singleScatteringCS = computeProgram("../../../resources/shaders/sky/single_scattering_comp.glsl");
    GLuint debugProgram = BuildProgram({{GL_VERTEX_SHADER, FileManager::ReadTextFile("../../../resources/shaders/sky/fullscreen_vertex.glsl")}, {GL_FRAGMENT_SHADER, FileManager::ReadTextFile("../../../resources/shaders/sky/debug_lut_frag.glsl")}});
    GLuint skyProgram = BuildProgram({{GL_VERTEX_SHADER, FileManager::ReadTextFile("../../../resources/shaders/sky/fullscreen_vertex.glsl")}, {GL_FRAGMENT_SHADER, Compose(common, FileManager::ReadTextFile("../../../resources/shaders/sky/sky_frag.glsl"))}});

    
    // empty VAO
    GLuint vao;
    glCreateVertexArrays(1, &vao);

    // one-time precomputation
    constexpr GLbitfield BARRIER = GL_TEXTURE_FETCH_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT;
    // transmittance
    glUseProgram(transmittanceCS);
    glBindImageTexture(0, transmittanceTex, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);
    glDispatchCompute(TRANSMITTANCE_W / 8, TRANSMITTANCE_H / 8, 1);
    glMemoryBarrier(BARRIER);
    // everything below samples transmittance
    glBindTextureUnit(0, transmittanceTex);
    // direct irradiance
    glUseProgram(directIrradianceCS);
    glBindImageTexture(0, deltaIrradianceTex, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);
    glDispatchCompute(IRRADIANCE_W / 8, IRRADIANCE_H / 8, 1);
    glMemoryBarrier(BARRIER);
    // single scattering
    glUseProgram(singleScatteringCS);
    glBindImageTexture(0, deltaRayleighTex, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA32F);
    glBindImageTexture(1, deltaMieTex, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA32F);
    glBindImageTexture(2, scatteringTex, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA32F);
    glDispatchCompute(SCATTERING_W / 8, SCATTERING_MU_SIZE / 8, SCATTERING_R_SIZE);
    glMemoryBarrier(BARRIER);

    // camera
    GLFWwindow *handle = window.GetHandle();
    glm::dvec3 camPos{0.0, 100.0, 0.0};

    float yaw = 0.0f, pitch = 0.15f;
    float sunAzimuth = 0.0f, sunElevation = 0.6f; // radians
    float exposure = 10.0f;
    const float fovY = glm::radians(70.0f);

    int mode = 3; // 1/2/3 = debug tables, 4 = sky
    bool looking = false;
    double lastX = 0.0, lastY = 0.0;
    double lastTime = glfwGetTime();

    while (!window.ShouldClose())
    {
        if (window.ProcessEvents() == false)
        {
            continue;
        }

        double now = glfwGetTime();
        float dt = float(now - lastTime);
        lastTime = now;

        auto down = [&](int key)
        { return glfwGetKey(handle, key) == GLFW_PRESS; };

        if (down(GLFW_KEY_1))
            mode = 0;
        if (down(GLFW_KEY_2))
            mode = 1;
        if (down(GLFW_KEY_3))
            mode = 2;
        if (down(GLFW_KEY_4))
            mode = 3;

        // mouse look while the right button is held
        bool rmb = glfwGetMouseButton(handle, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
        if (rmb && !looking)
        {
            glfwSetInputMode(handle, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            glfwGetCursorPos(handle, &lastX, &lastY);
            looking = true;
        }
        else if (!rmb && looking)
        {
            glfwSetInputMode(handle, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            looking = false;
        }
        if (looking)
        {
            double x, y;
            glfwGetCursorPos(handle, &x, &y);
            yaw += float(x - lastX) * 0.0025f;
            pitch -= float(y - lastY) * 0.0025f;
            pitch = std::clamp(pitch, -1.55f, 1.55f);
            lastX = x;
            lastY = y;
        }

        glm::vec3 forward{std::sin(yaw) * std::cos(pitch), std::sin(pitch), -std::cos(yaw) * std::cos(pitch)};
        glm::vec3 right{std::cos(yaw), 0.0f, std::sin(yaw)};
        glm::vec3 up = glm::cross(right, forward);

        // fly: WASD + Q/E, Shift = boost. Speed scales with altitude so the whole atmosphere is reachable.
        double speed = std::max(10.0, camPos.y * 0.5) * (down(GLFW_KEY_LEFT_SHIFT) ? 10.0 : 1.0);
        glm::dvec3 move{0.0};
        if (down(GLFW_KEY_W))
            move += glm::dvec3(forward);
        if (down(GLFW_KEY_S))
            move -= glm::dvec3(forward);
        if (down(GLFW_KEY_D))
            move += glm::dvec3(right);
        if (down(GLFW_KEY_A))
            move -= glm::dvec3(right);
        if (down(GLFW_KEY_E))
            move += glm::dvec3(0, 1, 0);
        if (down(GLFW_KEY_Q))
            move -= glm::dvec3(0, 1, 0);
        camPos += move * speed * double(dt);
        camPos.y = std::max(camPos.y, 10.0); // stay above the surface (no ground rendered yet)

        // sun: arrow keys; exposure: Z/X
        if (down(GLFW_KEY_UP))
            sunElevation += 0.5f * dt;
        if (down(GLFW_KEY_DOWN))
            sunElevation -= 0.5f * dt;
        if (down(GLFW_KEY_RIGHT))
            sunAzimuth += 0.5f * dt;
        if (down(GLFW_KEY_LEFT))
            sunAzimuth -= 0.5f * dt;
        if (down(GLFW_KEY_X))
            exposure *= std::exp(1.0f * dt);
        if (down(GLFW_KEY_Z))
            exposure *= std::exp(-1.0f * dt);

        int fbw, fbh;
        glfwGetFramebufferSize(handle, &fbw, &fbh);
        glViewport(0, 0, fbw, fbh);

        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        if (mode == 3)
        {
            float aspect = float(fbw) / float(std::max(fbh, 1));
            float tanHalf = std::tan(fovY * 0.5f);

            FrameData fd{};
            glm::dvec3 fromCenter = camPos + glm::dvec3(0.0, PLANET_BOTTOM_RADIUS, 0.0);
            fd.camera_pos_exposure = glm::vec4(glm::vec3(fromCenter), exposure);
            fd.cam_right = glm::vec4(right * (aspect * tanHalf), 0.0f);
            fd.cam_up = glm::vec4(up * tanHalf, 0.0f);
            fd.cam_forward = glm::vec4(forward, 0.0f);
            fd.sun_dir = glm::vec4(std::cos(sunElevation) * std::sin(sunAzimuth), std::sin(sunElevation),
                                   -std::cos(sunElevation) * std::cos(sunAzimuth), 0.0f);

            frameRing.Upload(fd);
            glUseProgram(skyProgram);
            glBindTextureUnit(0, transmittanceTex);
            glBindTextureUnit(2, scatteringTex);
            glBindVertexArray(vao);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            frameRing.EndFrame();
        }
        else
        {
            // debug views; in the scattering view the cursor's x picks the altitude layer
            double cx, cy;
            int ww, wh;
            glfwGetCursorPos(handle, &cx, &cy);
            glfwGetWindowSize(handle, &ww, &wh);
            int layer = std::clamp(int(cx / std::max(ww, 1) * SCATTERING_R_SIZE), 0, SCATTERING_R_SIZE - 1);

            glUseProgram(debugProgram);
            glProgramUniform1i(debugProgram, 0, mode);
            glProgramUniform1i(debugProgram, 1, layer);
            glBindTextureUnit(0, transmittanceTex);
            glBindTextureUnit(1, deltaIrradianceTex);
            glBindTextureUnit(2, scatteringTex);
            glBindVertexArray(vao);
            glDrawArrays(GL_TRIANGLES, 0, 3);
        }

        window.SwapBuffers();
    }

    return EXIT_SUCCESS;
}