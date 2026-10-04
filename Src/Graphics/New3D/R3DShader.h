#ifndef _R3DSHADER_H_
#define _R3DSHADER_H_

#include <GL/glew.h>
#include "Util/NewConfig.h"
#include "Model.h"
#include <map>
#include <string>

namespace New3D {

class R3DShader
{
public:
	R3DShader(const Util::Config::Node &config);

	bool	LoadShader			(const char* vertexShader = nullptr, const char* fragmentShader = nullptr);
	void	UnloadShader		();
	void	SetMeshUniforms		(const Mesh* m);
	void	SetModelStates		(const Model* model);
	void	SetViewportUniforms	(const Viewport *vp);
	void	Start				();
	void	SetShader			(bool enable = true);
	GLint	GetVertexAttribPos	(const std::string& attrib);
	void	DiscardAlpha		(bool discard);				// use to remove alpha from texture alpha only polys for 1st pass
	void	SetLayer			(Layer layer);

	// Batched drawing (New3DBatchedDraws = 1): the per-mesh and per-model
	// values come from a buffer texture, one record per draw, picked by an
	// instanced draw ID attribute (baseInstance), instead of uniforms. Runs of
	// meshes then go out in one glMultiDrawArraysIndirect.
	static constexpr int kDrawDataTexels	= 8;			// RGBA32UI texels per draw record
	static constexpr int kDrawDataUnit		= 2;			// texture unit of the draw record buffer texture
	bool	IsBatched			() const { return m_batched; }
	void	DisableBatching		();							// rebuild the normal (per-mesh uniform) program
	void	ProbeBatchedShader	();							// diagnostics only: build the batched program, log the result, delete it
	static void PackDrawData	(const Model* model, const Mesh* mesh, GLuint* out);	// writes kDrawDataTexels * 4 words
	bool	StencilChanges		(const Mesh* m) const;		// would SetMeshStencil change GL state?
	void	SetMeshStencil		(const Mesh* m);			// the stencil part of SetMeshUniforms

private:

	void PrintShaderResult(GLuint shader);
	void PrintProgramResult(GLuint program);
	bool BuildProgram(const char* vShader, const char* gShader, const char* fShader, bool quads, const char* fCommon, const char* versionStr);
	void GetUniformLocations();

	bool m_batched = false;
	bool m_forceUnbatched = false;
	bool m_dirtyStencil = true;

	// run-time config
	const Util::Config::Node &m_config;

	// shader IDs
	GLuint m_shaderProgram;
	GLuint m_vertexShader;
	GLuint m_geoShader;
	GLuint m_fragmentShader;

	// mesh uniform locations
	GLint m_locTextureBank[2];		// 2 banks
	GLint m_locTexture1Enabled;		// base texture
	GLint m_locTexture2Enabled;		// micro texture
	GLint m_locTexturePage;
	GLint m_locTextureAlpha;
	GLint m_locAlphaTest;
	GLint m_locMicroTexMinLOD;
	GLint m_locMicroTexID;
	GLint m_locBaseTexInfo;
	GLint m_locBaseTexType;
	GLint m_locTextureInverted;
	GLint m_locTexWrapMode;
	GLint m_locTranslatorMap;
	GLint m_locColourLayer;
	GLint m_locPolyAlpha;

	// cached mesh values
	bool	m_textured1;
	bool	m_textured2;
	bool	m_textureAlpha;		// use alpha in texture
	bool	m_alphaTest;		// discard fragment based on alpha (ogl does this with fixed function)
	float	m_fogIntensity;
	bool	m_lightEnabled;
	float	m_shininess;
	float	m_specularValue;
	bool	m_specularEnabled;
	bool	m_fixedShading;
	bool	m_smoothShading;
	bool	m_translatorMap;
	bool	m_polyAlpha;
	int		m_texturePage;

	bool	m_layered;
	bool	m_noLosReturn;
	float	m_microTexMinLOD;
	int		m_microTexID;
	int		m_baseTexInfo[4];
	int		m_baseTexType;
	int		m_texWrapMode[2];
	bool	m_textureInverted;

	// cached model values
	float	m_modelScale;
	float	m_nodeAlpha;
	int		m_transX;
	int		m_transY;
	int		m_transPage;

	// are our cache values dirty
	bool	m_dirtyMesh;
	bool	m_dirtyModel;

	// viewport uniform locations
	GLint m_locFogIntensity;
	GLint m_locFogDensity;
	GLint m_locFogStart;
	GLint m_locFogColour;
	GLint m_locFogAttenuation;
	GLint m_locFogAmbient;
	GLint m_locProjMat;
	GLint m_locCota;

	// lighting / other
	GLint m_locLighting;
	GLint m_locLightEnabled;
	GLint m_locSunClamp;
	GLint m_locIntensityClamp;
	GLint m_locShininess;
	GLint m_locSpecularValue;
	GLint m_locSpecularEnabled;
	GLint m_locFixedShading;
	GLint m_locSmoothShading;

	GLint m_locSpotEllipse;
	GLint m_locSpotRange;
	GLint m_locSpotColor;
	GLint m_locSpotFogColor;

	// model uniforms
	GLint m_locModelScale;
	GLint m_locNodeAlpha;
	GLint m_locModelMat;

	// global uniforms
	GLint m_locHardwareStep;
	GLint m_locDiscardAlpha;

	// vertex attribute position cache
	std::map<std::string, GLint> m_vertexLocCache;

};


} // New3D

#endif