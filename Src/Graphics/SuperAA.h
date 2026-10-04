#pragma once

#include "Supermodel.h"
#include "FBO.h"
#include "New3D/GLSLShader.h"

// This class just implements super sampling. Super sampling looks fantastic but is quite expensive.
// 8x and beyond values can start to eat ridiculous amounts of memory / gpu time, for less and less noticable returns
// 4x works and looks great
// values such as 3 are also possible, that works out 9 samples per pixel
// The algorithm is super simple, just add up all samples and divide by the number

class SuperAA
{
public:
	// renderScale < 1 (only when aaValue is 1): render at that fraction of the
	// window size and upscale to the window. upscaleFilter: 0 bilinear,
	// 1 Catmull-Rom (sharper, with anti-ringing).
	SuperAA(int aaValue, CRTcolor CRTcolors, float renderScale = 1.0f, int upscaleFilter = 1);
	~SuperAA();

	void Init(int width, int height);		// width & height are real window dimensions
	void Draw();							// this is a no-op if AA is 1 and CRTcolors 0, since we'll be drawing straight on the back buffer anyway
	void SetOutputTarget(GLuint fbo);		// optional resolved output FBO (0 = window backbuffer)

	GLuint GetTargetID();

private:
	FBO m_fbo;
	GLSLShader m_shader;
	const int m_aa;
	const CRTcolor m_crtcolors;
	const float m_renderScale;
	bool Active() const { return (m_aa > 1) || (m_crtcolors != CRTcolor::None) || (m_renderScale < 1.0f); }
	GLuint m_vao;
	GLuint m_outputTarget;
	int m_width;
	int m_height;
};
