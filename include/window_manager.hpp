#pragma once

#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <common.hpp>

namespace WindowManager
{
	static inline void DefaultFrameBufferSizeCallback(GLFWwindow *inWindow, int inWidth, int inHeight);
	static inline void GLAPIENTRY DebugOutput(GLenum inSource, GLenum inType, GLuint inID, GLenum inSeverity, GLsizei inLength, const GLchar *inMessage, const void *inUserParam);

	class Window
	{
	public:
		bool Create(const char *inTitle, int inWidth, int inHeight)
		{
			mWidth = inWidth;
			mHeight = inHeight;

			// Initialize GLFW
			if (!glfwInit())
			{
				std::cerr << "Failed to initialize GLFW.\n";
				return false;
			}
			glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
			glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
			glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
			glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);

			pWindow = glfwCreateWindow(mWidth, mHeight, inTitle, nullptr, nullptr);
			if (!pWindow)
			{
				std::cerr << "Failed to create window.\n";
				glfwTerminate();
				return false;
			}
			// Make current window as context
			glfwMakeContextCurrent(pWindow);
			glfwSwapInterval(1);
			SetFramebufferSizeCallback(DefaultFrameBufferSizeCallback);

			// Initialize GLAD
			int version = gladLoadGL(glfwGetProcAddress);
			if (!version)
			{
				std::cerr << "Failed to initialize GLAD.\n";
				return false;
			}
			// Check extensions
			if (!GLAD_GL_ARB_bindless_texture)
			{
				std::cerr << "'GL_ARB_bindless_texture' not supported.\n";
				return false;
			}
			if (!GLAD_GL_ARB_gpu_shader_int64)
			{
				std::cerr << "'GL_ARB_gpu_shader_int64' not supported.\n";
				return false;
			}
			// Add this class as user pointer
			glfwSetWindowUserPointer(pWindow, this);

			glfwGetFramebufferSize(pWindow, &mWidth, &mHeight);
			glViewport(0, 0, mWidth, mHeight);

			return true;
		}

		void SetFramebufferSizeCallback(GLFWframebuffersizefun inCallback)
		{
			glfwSetFramebufferSizeCallback(pWindow, inCallback);
		}

		void EnableOutputDebug()
		{
			// Enable OpenGL debug output
			glEnable(GL_DEBUG_OUTPUT);
			glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
			glDebugMessageCallback(DebugOutput, nullptr);
		}

		bool ShouldClose() const
		{
			return glfwWindowShouldClose(pWindow);
		}

		bool ProcessEvents()
		{
			glfwPollEvents();

			if (mWidth == 0 || mHeight == 0)
			{
				glfwWaitEvents();
				return false;
			}

			if (mResized)
			{
				mResized = false;
				glfwGetFramebufferSize(pWindow, &mWidth, &mHeight);
				glViewport(0, 0, mWidth, mHeight);
			}

			return true;
		}

		void SwapBuffers() const { glfwSwapBuffers(pWindow); };

		void Destroy()
		{
			glfwDestroyWindow(pWindow);
			glfwTerminate();
		}

		// Getters
		GLFWwindow *GetHandle() const { return pWindow; }
		Extent2D GetSize() const { return {mWidth, mHeight}; }

		// Setters
		void SetSize(const int inWidth, const int inHeight)
		{
			mWidth = inWidth;
			mHeight = inHeight;
		}
		void SetResized(const bool inResized) { mResized = inResized; }

	private:
		int mWidth{800};
		int mHeight{600};
		bool mResized{false};

		GLFWwindow *pWindow{nullptr};
	};

	static inline void DefaultFrameBufferSizeCallback(GLFWwindow *inWindow, int inWidth, int inHeight)
	{
		if (Window *owningWindow = static_cast<Window *>(glfwGetWindowUserPointer(inWindow)))
		{
			owningWindow->SetSize(inWidth, inHeight);
			owningWindow->SetResized(true);
		}
	}

	static inline void GLAPIENTRY DebugOutput(GLenum inSource, GLenum inType, GLuint inID, GLenum inSeverity, GLsizei inLength, const GLchar *inMessage, const void *inUserParam)
	{
		// Ignore non-significant error codes
		if (inID == 131169 || inID == 131185 || inID == 131218 || inID == 131204)
			return;
		std::cerr << "GL Debug [" << inID << "] " << inMessage << '\n';
	}
}