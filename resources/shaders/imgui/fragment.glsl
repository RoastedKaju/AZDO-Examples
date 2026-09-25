	#version 460 core
	
	layout (location = 0) in vec3 vColor;
	layout (location = 0) out vec4 FragColor;

	layout (std140, binding = 0)
	uniform FrameData
	{
		mat4 uMVP;
		bool uShowWireFrame;
	};

	layout(std430, binding = 0)
	buffer ObjectData
	{
		vec3 wireframeColor;
	} objectData;

	void main()
	{
		if(uShowWireFrame)
		{
			FragColor = vec4(objectData.wireframeColor, 1.0);
		}
		else
		{
			FragColor = vec4(vColor, 1.0);
		}
	}