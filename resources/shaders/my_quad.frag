#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) out vec4 color;

layout (binding = 0) uniform sampler2D colorTex;

layout (location = 0 ) in VS_OUT
{
	vec2 texCoord;
} surf;


#define hMargin 5
#define  vMargin 5
#define  winSize (2 * hMargin + 1)*(2 * vMargin + 1)

vec2 Limit(vec2 v)
{
	return vec2(max(0, min(1, v.x)), max(0, min(1, v.y)));
}

void main()
{
	ivec2 texSize = textureSize(colorTex, 0);
	vec4 colorArray[winSize];
	vec4 result;
	vec2 kerCoord;
	int counter = 0;

	for(kerCoord.x = -hMargin; kerCoord.x <= hMargin; kerCoord.x++)
	{
		for(kerCoord.y = -vMargin; kerCoord.y <= vMargin; kerCoord.y++)
		{
			vec2 curCoord = Limit(surf.texCoord + kerCoord/texSize);
			colorArray[counter++] = textureLod(colorTex, curCoord, 0);
		}
	}

	for(int i = 0; i < winSize; i++)
	{
		for(int j = 0; j < winSize - i - 1; j++)
		{
			float t;

			if(colorArray[j].r > colorArray[j + 1].r)
			{
				t = colorArray[j].r;
				colorArray[j].r = colorArray[j + 1].r;
				colorArray[j + 1].r = t;
			}

			if(colorArray[j].g > colorArray[j + 1].g)
			{
				t = colorArray[j].g;
				colorArray[j].g = colorArray[j + 1].g;
				colorArray[j + 1].g = t;
			}

			if(colorArray[j].b > colorArray[j + 1].b)
			{
				t = colorArray[j].b;
				colorArray[j].b = colorArray[j + 1].b;
				colorArray[j + 1].b = t;
			}

			if(colorArray[j].a > colorArray[j + 1].a)
			{
				t = colorArray[j].a;
				colorArray[j].a = colorArray[j + 1].a;
				colorArray[j + 1].a = t;
			}
		}
	}

	color = colorArray[(2 * vMargin + 1) * hMargin + vMargin + 1];
}