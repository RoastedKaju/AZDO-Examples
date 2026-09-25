#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <window_manager.hpp>
#include <file_manager.hpp>
#include <shader_manager.hpp>

static bool sDrawWireFrame = true;
static float sWireFrameColor[3] = { 0.0f, 0.0f, 0.0f };

// Vertex
struct Vertex
{
	glm::vec3 position;
	glm::vec3 color;
};

Vertex vertices[] = {
	{ { -0.5f, -0.5f,  0.5f }, { 0.5f, 0.0f, 0.0f } }, // 0: Bottom-Left-Front
	{ {  0.5f, -0.5f,  0.5f }, { 0.0f, 1.0f, 0.0f } }, // 1: Bottom-Right-Front
	{ {  0.5f,  0.5f,  0.5f }, { 0.0f, 0.0f, 1.0f } }, // 2: Top-Right-Front
	{ { -0.5f,  0.5f,  0.5f }, { 1.0f, 0.5f, 0.0f } }, // 3: Top-Left-Front
	{ { -0.5f, -0.5f, -0.5f }, { 1.0f, 0.0f, 1.0f } }, // 4: Bottom-Left-Back
	{ {  0.5f, -0.5f, -0.5f }, { 0.0f, 0.5f, 1.0f } }, // 5: Bottom-Right-Back
	{ {  0.5f,  0.5f, -0.5f }, { 1.0f, 1.0f, 1.0f } }, // 6: Top-Right-Back
	{ { -0.5f,  0.5f, -0.5f }, { 1.0f, 1.0f, 0.0f } }  // 7: Top-Left-Back
};

GLuint indices[] = {
	// Front
	0, 1, 2,  2, 3, 0,
	// Right
	1, 5, 6,  6, 2, 1,
	// Back
	5, 4, 7,  7, 6, 5,
	// Left
	4, 0, 3,  3, 7, 4,
	// Top
	3, 2, 6,  6, 7, 3,
	// Bottom
	4, 5, 1,  1, 0, 4
};

// Per-Frame Data Must match with shader
struct FrameData
{
	glm::mat4 mvp;
	GLuint showWireframe; // Since shader uses std140, use 4 bytes for a boolean value
};

int main()
{
	WindowManager::Window window{};
	window.Create("ImGUI", 800, 600);
	window.EnableOutputDebug();

	// Enable depth testing
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS);
	// Culling
	glEnable(GL_CULL_FACE);
	glCullFace(GL_BACK);
	glFrontFace(GL_CCW);

	// DSA buffers
	GLuint VBO, EBO, UBO, SSBO;

	glCreateBuffers(1, &VBO);
	glNamedBufferStorage(VBO, sizeof(vertices), vertices, GL_DYNAMIC_STORAGE_BIT);

	glCreateBuffers(1, &EBO);
	glNamedBufferStorage(EBO, sizeof(indices), indices, GL_DYNAMIC_STORAGE_BIT);

	glCreateBuffers(1, &UBO);
	glNamedBufferStorage(UBO, sizeof(FrameData), nullptr, GL_DYNAMIC_STORAGE_BIT);
	glBindBufferBase(GL_UNIFORM_BUFFER, 0, UBO);

	glCreateBuffers(1, &SSBO);
	glNamedBufferStorage(SSBO, sizeof(glm::vec3), nullptr, GL_DYNAMIC_STORAGE_BIT);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, SSBO);

	// VAO
	GLuint VAO;
	glCreateVertexArrays(1, &VAO);

	glEnableVertexArrayAttrib(VAO, 0);
	glVertexArrayAttribFormat(VAO, 0, 3, GL_FLOAT, GL_FALSE, offsetof(Vertex, position));
	glVertexArrayAttribBinding(VAO, 0, 0);

	glEnableVertexArrayAttrib(VAO, 1);
	glVertexArrayAttribFormat(VAO, 1, 3, GL_FLOAT, GL_FALSE, offsetof(Vertex, color));
	glVertexArrayAttribBinding(VAO, 1, 0);

	glVertexArrayVertexBuffer(VAO, 0, VBO, 0, sizeof(Vertex)); // attach VBO to VAO
	glVertexArrayElementBuffer(VAO, EBO); // attach EBO to VAO

	// load shaders
	const std::string vs = FileManager::ReadTextFile("../../../resources/shaders/imgui/vertex.glsl");
	const std::string fs = FileManager::ReadTextFile("../../../resources/shaders/imgui/fragment.glsl");
	GLuint shaderProgram = ShaderManager::CreateShaderProgram(vs, fs);
	if (!shaderProgram)
	{
		return EXIT_FAILURE;
	}

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable; // Enable docking
	io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
	ImGui_ImplGlfw_InitForOpenGL(window.GetHandle(), true);
	ImGui_ImplOpenGL3_Init("#version 460");

	while (!window.ShouldClose())
	{
		if (!window.ProcessEvents())
		{
			continue;
		}

		float time = (float)glfwGetTime();

		const Extent2D windowSize = window.GetSize();
		glm::mat4 model = glm::rotate(glm::mat4(1.0f), time * glm::radians(50.0f), glm::vec3(0.5f, 1.0f, 0.0f));
		glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 0.0f, 3.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
		glm::mat4 proj = glm::perspective(glm::radians(45.0f), (float)windowSize.width / (float)windowSize.height, 0.1f, 100.0f);
		glm::mat4 mvp = proj * view * model;

		// Push Frame-Data to UBO
		FrameData frameData{ .mvp = mvp, .showWireframe = false };
		glNamedBufferSubData(UBO, 0, sizeof(FrameData), &frameData);

		// Clear
		glClearColor(0.1f, 0.12f, 0.1f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		glUseProgram(shaderProgram);
		glBindVertexArray(VAO);
		glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, nullptr);

		// Render wireframe
		if (sDrawWireFrame)
		{
			// Push Frame-Data to UBO with wireframe enabled
			frameData.showWireframe = true;
			glNamedBufferSubData(UBO, 0, sizeof(FrameData), &frameData);
			// Push Color value to SSBO
			glm::vec3 wireframeColor = glm::vec3(sWireFrameColor[0], sWireFrameColor[1], sWireFrameColor[2]);
			glNamedBufferSubData(SSBO, 0, sizeof(glm::vec3), &wireframeColor);
			// Change polygon to lines
			glEnable(GL_POLYGON_OFFSET_LINE);
			glPolygonOffset(-1.0f, -1.0f);
			glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
			glLineWidth(2.0f);
			glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, nullptr);
			// Restore default state
			glDisable(GL_POLYGON_OFFSET_LINE);
			glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
		}

		// ImGUI Layer
		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();

		// Dockspace on entire viewport with centeral node transparent
		ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);

		ImGui::Begin("ImGUI Window");
		ImGui::Checkbox("Show Wireframe", &sDrawWireFrame);
		ImGui::Text("Wireframe Color");
		ImGui::ColorEdit3("##WireframeColor", sWireFrameColor);
		ImGui::End();

		ImGui::Render();
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());


		ImGuiIO& io = ImGui::GetIO();
		if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
		{
			// This is needed because when you drag the windows out of ImGUI dynamically creates OS-level windows
			ImGui::UpdatePlatformWindows();
			ImGui::RenderPlatformWindowsDefault();
			glfwMakeContextCurrent(window.GetHandle()); // Restore the context back to GLFW window
		}

		window.SwapBuffers();
	}

	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();

	glDeleteProgram(shaderProgram);
	glDeleteVertexArrays(1, &VAO);
	glDeleteBuffers(1, &VBO);
	glDeleteBuffers(1, &EBO);
	glDeleteBuffers(1, &UBO);
	glDeleteBuffers(1, &SSBO);

	window.Destroy();
	return EXIT_SUCCESS;
}