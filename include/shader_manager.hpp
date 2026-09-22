#include <glad/gl.h>
#include <string>
#include <iostream>

namespace ShaderManager
{
	static GLuint CompileShader(GLenum type, const char* inSource)
	{
		GLuint shader = glCreateShader(type);
		glShaderSource(shader, 1, &inSource, nullptr);
		glCompileShader(shader);

		GLint status = 0;
		glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
		if (!status)
		{
			GLint logLength = 0;
			glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);
			std::string log(logLength, '\0');
			glGetShaderInfoLog(shader, logLength, nullptr, log.data());
			std::cerr << "Shader compile error (" << (type == GL_VERTEX_SHADER ? "vertex" : "fragment") << "):\n" << log << std::endl;
			glDeleteShader(shader);
			return 0;
		}

		return shader;
	}

	static inline GLuint CreateShaderProgram(const std::string& inVertexSource, const std::string& inFragmentSource)
	{
		GLuint vs = CompileShader(GL_VERTEX_SHADER, inVertexSource.c_str());
		GLuint fs = CompileShader(GL_FRAGMENT_SHADER, inFragmentSource.c_str());

		GLuint program = glCreateProgram();
		glAttachShader(program, vs);
		glAttachShader(program, fs);
		glLinkProgram(program);

		GLint status = 0;
		glGetProgramiv(program, GL_LINK_STATUS, &status);
		if (!status)
		{
			GLint logLength = 0;
			glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLength);
			std::string log(logLength, '\0');
			glGetProgramInfoLog(program, logLength, nullptr, log.data());
			std::cerr << "Program link error:\n" << log << std::endl;
			return 0;
		}

		glDeleteShader(vs);
		glDeleteShader(fs);
		return program;
	}
}