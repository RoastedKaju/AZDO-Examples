	#version 460 core

	layout (location = 0) in vec3 aPos;
	layout (location = 1) in vec3 aColor;

	layout (location = 0) out vec3 vColor;

	layout (std140, binding = 0)
	uniform FrameData
	{
		mat4 uMVP;
		bool uShowWireFrame;
	};

	void main()
	{
		vColor = aColor;
		gl_Position = uMVP * vec4(aPos, 1.0);
	}