#include "R3DShader.h"
#include "R3DShaderQuads.h"
#include "R3DShaderTriangles.h"
#include "R3DShaderCommon.h"
#include "../GLSLVersion.h"
#include "OSD/Logger.h"
#include <cstring>
#include <regex>

// having 2 sets of shaders to maintain is really less than ideal
// but hopefully not too many breaking changes at this point

namespace New3D {

R3DShader::R3DShader(const Util::Config::Node &config)
	: m_config(config)
{
	m_shaderProgram		= 0;
	m_vertexShader		= 0;
	m_geoShader			= 0;
	m_fragmentShader	= 0;

	Start();	// reset attributes
}

void R3DShader::Start()
{
	m_textured1			= false;
	m_textured2			= false;
	m_textureAlpha		= false;		// use alpha in texture
	m_alphaTest			= false;		// discard fragment based on alpha (ogl does this with fixed function)
	m_lightEnabled		= false;
	m_specularEnabled	= false;
	m_layered			= false;
	m_noLosReturn		= false;
	m_textureInverted	= false;
	m_fixedShading		= false;
	m_smoothShading		= false;
	m_translatorMap		= false;
	m_modelScale		= 1.0f;
	m_nodeAlpha			= 1.0f;
	m_shininess			= 0;
	m_specularValue		= 0;
	m_microTexMinLOD	= 0;
	m_fogIntensity		= 0.0f;
	m_microTexID		= -1;
	m_texturePage		= -1;

	m_baseTexInfo[0]	= -1;
	m_baseTexInfo[1]	= -1;
	m_baseTexInfo[2]	= -1;
	m_baseTexInfo[3]	= -1;

	m_baseTexType		= -1;

	m_transX			= -1;
	m_transY			= -1;
	m_transPage			= -1;

	m_texWrapMode[0]	= 0;
	m_texWrapMode[1]	= 0;

	m_dirtyMesh			= true;			// dirty means all the above are dirty, ie first run
	m_dirtyModel		= true;
	m_dirtyStencil		= true;
}

// ---------------------------------------------------------------------------
// Batched drawing: shader variant
//
// The triangle shaders are reused as they are. Each per-draw uniform becomes a
// plain global of the same name, filled by LoadDrawData() at the top of
// main(): in the vertex shader from the draw record (buffer texture, indexed
// by the instanced inDrawID attribute), in the fragment shader from flat
// varyings the vertex shader passes on. So the shader bodies are unchanged.
//
// Draw record layout (kDrawDataTexels RGBA32UI texels, see PackDrawData):
//   0-3  modelMat columns
//   4    modelScale, nodeAlpha, microTextureMinLOD, fogIntensity   (float bits)
//   5    shininess, specularValue, -, -                            (float bits)
//   6    baseTexInfo (x + model texture offset X, y + offset Y, width, height)
//   7    flags, microTextureID, baseTexType, texturePage
// flags: bit 0 textureEnabled, 1 microTexture, 2 textureInverted,
//   3 textureAlpha, 4 alphaTest, 5 lightEnabled, 6 specularEnabled,
//   7 fixedShading, 8 smoothShading, 9 translatorMap, 10 polyAlpha,
//   16-17 wrap mode U, 18-19 wrap mode V
// ---------------------------------------------------------------------------

namespace {

const char* kBatchedVertexHeader = R"glsl(
uniform usamplerBuffer	drawData;
in uint					inDrawID;
flat out vec4			fsDrawF;		// microTextureMinLOD, fogIntensity, shininess, specularValue
flat out ivec4			fsDrawTex;		// baseTexInfo
flat out ivec4			fsDrawI;		// flags, microTextureID, baseTexType, texturePage
)glsl";

const char* kBatchedVertexLoad = R"glsl(
void LoadDrawData()
{
	int base = int(inDrawID) * 8;
	modelMat = mat4(uintBitsToFloat(texelFetch(drawData, base + 0)),
					uintBitsToFloat(texelFetch(drawData, base + 1)),
					uintBitsToFloat(texelFetch(drawData, base + 2)),
					uintBitsToFloat(texelFetch(drawData, base + 3)));
	vec4 f4 = uintBitsToFloat(texelFetch(drawData, base + 4));
	vec4 f5 = uintBitsToFloat(texelFetch(drawData, base + 5));
	ivec4 i7 = ivec4(texelFetch(drawData, base + 7));
	modelScale		= f4.x;
	nodeAlpha		= f4.y;
	translatorMap	= (i7.x & 0x200) != 0;
	fsDrawF			= vec4(f4.z, f4.w, f5.x, f5.y);
	fsDrawTex		= ivec4(texelFetch(drawData, base + 6));
	fsDrawI			= i7;
}

)glsl";

const char* kBatchedFragmentHeader = R"glsl(
#define R3D_BATCHED
flat in vec4			fsDrawF;
flat in ivec4			fsDrawTex;
flat in ivec4			fsDrawI;
)glsl";

const char* kBatchedFragmentLoad = R"glsl(
void LoadDrawData()
{
	int flags			= fsDrawI.x;
	textureEnabled		= (flags & 0x001) != 0;
	microTexture		= (flags & 0x002) != 0;
	textureInverted		= (flags & 0x004) != 0;
	textureAlpha		= (flags & 0x008) != 0;
	alphaTest			= (flags & 0x010) != 0;
	lightEnabled		= (flags & 0x020) != 0;
	specularEnabled		= (flags & 0x040) != 0;
	fixedShading		= (flags & 0x080) != 0;
	smoothShading		= (flags & 0x100) != 0;
	polyAlpha			= (flags & 0x400) != 0;
	textureWrapMode		= ivec2((flags >> 16) & 3, (flags >> 18) & 3);
	microTextureID		= fsDrawI.y;
	baseTexType			= fsDrawI.z;
	texturePage			= fsDrawI.w;
	baseTexInfo			= fsDrawTex;
	microTextureMinLOD	= fsDrawF.x;
	fogIntensity		= fsDrawF.y;
	shininess			= fsDrawF.z;
	specularValue		= fsDrawF.w;
}

)glsl";

const char* kVertexDrawUniforms[]	= { "modelScale", "nodeAlpha", "modelMat", "translatorMap" };
const char* kFragmentDrawUniforms[]	= { "textureEnabled", "microTexture", "microTextureMinLOD", "microTextureID",
										"baseTexInfo", "baseTexType", "textureInverted", "textureAlpha", "alphaTest",
										"textureWrapMode", "texturePage", "lightEnabled", "specularEnabled",
										"specularValue", "shininess", "fogIntensity", "fixedShading", "smoothShading",
										"polyAlpha" };

// "uniform <type> <name>;" -> "<type> <name>;". False if not found exactly once.
bool UniformToGlobal(std::string& src, const char* name)
{
	std::regex re(std::string("uniform([ \t]+[A-Za-z0-9_]+[ \t]+") + name + "[ \t]*;)");
	auto begin = std::sregex_iterator(src.begin(), src.end(), re);
	if (std::distance(begin, std::sregex_iterator()) != 1) {
		return false;
	}
	src = std::regex_replace(src, re, "$1");
	return true;
}

// Puts `loadFunc` before main() and a call to it as main()'s first statement.
bool InjectLoad(std::string& src, const char* loadFunc)
{
	size_t mainPos = src.find("void main(");
	if (mainPos == std::string::npos || src.find("void main(", mainPos + 1) != std::string::npos) {
		return false;
	}
	size_t brace = src.find('{', mainPos);
	if (brace == std::string::npos) {
		return false;
	}
	src.insert(brace + 1, "\n\tLoadDrawData();\n");
	src.insert(mainPos, loadFunc);
	return true;
}

bool MakeBatchedShaders(const char* vShader, const char* fShader, const char* fCommon,
						std::string& vOut, std::string& fOut, std::string& commonOut)
{
	vOut = vShader;
	for (auto name : kVertexDrawUniforms) {
		if (!UniformToGlobal(vOut, name)) return false;
	}
	if (!InjectLoad(vOut, kBatchedVertexLoad)) return false;
	vOut = kBatchedVertexHeader + vOut;

	fOut = fShader;
	for (auto name : kFragmentDrawUniforms) {
		if (!UniformToGlobal(fOut, name)) return false;
	}
	if (!InjectLoad(fOut, kBatchedFragmentLoad)) return false;
	fOut = kBatchedFragmentHeader + fOut;

	// A per-draw value is not a uniform, so index the sampler array with the
	// branches the GLES build already uses.
	commonOut = fCommon;
	const std::string from = "#ifdef ANDROID";
	const std::string to = "#if defined(ANDROID) || defined(R3D_BATCHED)";
	for (size_t pos = 0; (pos = commonOut.find(from, pos)) != std::string::npos; pos += to.size()) {
		commonOut.replace(pos, from.size(), to);
	}
	return true;
}

} // namespace

void R3DShader::PackDrawData(const Model* model, const Mesh* m, GLuint* out)
{
	auto f2u = [](float f) { GLuint u; std::memcpy(&u, &f, sizeof(u)); return u; };

	std::memcpy(out, model->modelMat, 16 * sizeof(GLuint));
	out[16] = f2u(model->scale);
	out[17] = f2u(model->alpha);
	out[18] = f2u(m->microTextureMinLOD);
	out[19] = f2u(m->fogIntensity);
	out[20] = f2u(m->shininess);
	out[21] = f2u(m->specularValue);
	out[22] = 0;
	out[23] = 0;
	out[24] = (GLuint)(m->x + model->textureOffsetX);
	out[25] = (GLuint)(m->y + model->textureOffsetY);
	out[26] = (GLuint)m->width;
	out[27] = (GLuint)m->height;
	out[28] = (m->textured		? 0x001u : 0u)
			| (m->microTexture	? 0x002u : 0u)
			| (m->inverted		? 0x004u : 0u)
			| (m->textureAlpha	? 0x008u : 0u)
			| (m->alphaTest		? 0x010u : 0u)
			| (m->lighting		? 0x020u : 0u)
			| (m->specular		? 0x040u : 0u)
			| (m->fixedShading	? 0x080u : 0u)
			| (m->smoothShading	? 0x100u : 0u)
			| (m->translatorMap	? 0x200u : 0u)
			| (m->polyAlpha		? 0x400u : 0u)
			| (((GLuint)m->wrapModeU & 3u) << 16)
			| (((GLuint)m->wrapModeV & 3u) << 18);
	out[29] = (GLuint)m->microTextureID;
	out[30] = (GLuint)m->format;
	out[31] = (GLuint)(m->page ^ model->page);
}

bool R3DShader::StencilChanges(const Mesh* m) const
{
	return m_dirtyStencil || m->noLosReturn != m_noLosReturn || m->layered != m_layered;
}

void R3DShader::SetMeshStencil(const Mesh* m)
{
	// Same GL calls, in the same order, as the end of SetMeshUniforms.
	if (m_dirtyStencil || m->noLosReturn != m_noLosReturn) {
		m_noLosReturn = m->noLosReturn;
		glStencilFunc(GL_ALWAYS, m_noLosReturn << 7, 0b10000000);
		glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
		glStencilMask(0b10000000);
	}

	if (m_dirtyStencil || m->layered != m_layered) {
		m_layered = m->layered;
		if (m_layered) {
			glStencilFunc(GL_EQUAL, 0, 0b01111111);
			glStencilOp(GL_KEEP, GL_KEEP, GL_INCR);
			glStencilMask(0b01111111);
		}
		else {
			glStencilFunc(GL_ALWAYS, m_noLosReturn << 7, 0b10000000);
			glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
			glStencilMask(0b10000000);
		}
	}

	m_dirtyStencil = false;
}

bool R3DShader::LoadShader(const char* vertexShader, const char* fragmentShader)
{
#if defined(ANDROID) || defined(CORE_GLES)
	// Geometry shaders are not supported in GLES 3.0
	bool quads = false;
#else
	bool quads = m_config["QuadRendering"].ValueAs<bool>();
#endif

	std::string versionStr = Graphics::GLSLVersion::GetR3D(quads); // highp float for both vertex and fragment

	const char* vShader = vertexShaderR3D;
	const char* gShader = "";
	const char* fShader = fragmentShaderR3D;

	if (quads) {
		vShader = vertexShaderR3DQuads;
		gShader = geometryShaderR3DQuads;
		fShader = fragmentShaderR3DQuads;
	}

	m_batched = false;
#if !defined(ANDROID) && !defined(CORE_GLES)
	m_vertexLocCache.clear();
	if (!quads && !m_forceUnbatched && m_config["New3DBatchedDraws"].ValueAsDefault<int>(1) != 0) {
		GLint major = 0, minor = 0;
		glGetIntegerv(GL_MAJOR_VERSION, &major);
		glGetIntegerv(GL_MINOR_VERSION, &minor);
		std::string bv, bf, bc;
		if (major < 4 || (major == 4 && minor < 3)) {
			InfoLog("New3D: batched draws need OpenGL 4.3 (have %d.%d); using per-mesh uniforms.", major, minor);
		}
		else if (!MakeBatchedShaders(vShader, fShader, fragmentShaderR3DCommon, bv, bf, bc)) {
			ErrorLog("New3D: could not build the batched shader source; using per-mesh uniforms.");
		}
		else if (BuildProgram(bv.c_str(), "", bf.c_str(), false, bc.c_str(), versionStr.c_str())) {
			m_batched = true;
			InfoLog("New3D: batched draw shader built (New3DBatchedDraws = 0 to disable).");
		}
		else {
			ErrorLog("New3D: batched shader failed to build; using per-mesh uniforms.");
			UnloadShader();
		}
	}
#endif

	if (!m_batched) {
		BuildProgram(vShader, gShader, fShader, quads, fragmentShaderR3DCommon, versionStr.c_str());
	}

	GetUniformLocations();

	if (m_batched) {
		// Sampler units are fixed: set them once instead of per mesh.
		glUseProgram(m_shaderProgram);
		glUniform1i(m_locTextureBank[0], 0);
		glUniform1i(m_locTextureBank[1], 1);
		glUniform1i(glGetUniformLocation(m_shaderProgram, "drawData"), kDrawDataUnit);
		glUseProgram(0);
	}

	return true;
}

bool R3DShader::BuildProgram(const char* vShader, const char* gShader, const char* fShader, bool quads, const char* fCommon, const char* versionStr)
{
	m_shaderProgram		= glCreateProgram();
	m_vertexShader		= glCreateShader(GL_VERTEX_SHADER);
	m_fragmentShader	= glCreateShader(GL_FRAGMENT_SHADER);

	const char* vSources[] = { versionStr, vShader };
	const char* fSources[] = { versionStr, fShader, fCommon };

	glShaderSource(m_vertexShader, 2, vSources, nullptr);
	glShaderSource(m_fragmentShader, 3, fSources, nullptr);

	glCompileShader(m_vertexShader);
	glCompileShader(m_fragmentShader);

#if !defined(ANDROID) && !defined(CORE_GLES)
	if (quads) {
		m_geoShader = glCreateShader(GL_GEOMETRY_SHADER);
		glShaderSource(m_geoShader, 1, (const GLchar **)&gShader, nullptr);
		glCompileShader(m_geoShader);
		glAttachShader(m_shaderProgram, m_geoShader);
		PrintShaderResult(m_geoShader);
	}
#endif

	PrintShaderResult(m_vertexShader);
	PrintShaderResult(m_fragmentShader);

	glAttachShader(m_shaderProgram, m_vertexShader);
	glAttachShader(m_shaderProgram, m_fragmentShader);
	glLinkProgram(m_shaderProgram);

	PrintProgramResult(m_shaderProgram);

	GLint linked = GL_FALSE;
	glGetProgramiv(m_shaderProgram, GL_LINK_STATUS, &linked);
	return linked == GL_TRUE;
}

void R3DShader::ProbeBatchedShader()
{
	std::string versionStr = Graphics::GLSLVersion::GetR3D(false);
	std::string bv, bf, bc;
	if (!MakeBatchedShaders(vertexShaderR3D, fragmentShaderR3D, fragmentShaderR3DCommon, bv, bf, bc)) {
		InfoLog("New3D probe: could not build the batched shader source.");
		return;
	}

	auto compile = [](GLenum type, const char* const* src, int n, const char* what) {
		GLuint s = glCreateShader(type);
		glShaderSource(s, n, src, nullptr);
		glCompileShader(s);
		GLint ok = GL_FALSE, len = 0;
		glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
		glGetShaderiv(s, GL_INFO_LOG_LENGTH, &len);
		std::string msg(len > 1 ? len : 1, '\0');
		if (len > 1) glGetShaderInfoLog(s, len, nullptr, &msg[0]);
		InfoLog("New3D probe: batched %s shader %s%s%s", what, ok ? "compiled" : "FAILED to compile",
			len > 1 ? ":\n" : "", len > 1 ? msg.c_str() : "");
		return s;
	};
	const char* vSrc[] = { versionStr.c_str(), bv.c_str() };
	const char* fSrc[] = { versionStr.c_str(), bf.c_str(), bc.c_str() };
	GLuint v = compile(GL_VERTEX_SHADER, vSrc, 2, "vertex");
	GLuint f = compile(GL_FRAGMENT_SHADER, fSrc, 3, "fragment");
	GLuint p = glCreateProgram();
	glAttachShader(p, v);
	glAttachShader(p, f);
	glLinkProgram(p);
	GLint ok = GL_FALSE, len = 0;
	glGetProgramiv(p, GL_LINK_STATUS, &ok);
	glGetProgramiv(p, GL_INFO_LOG_LENGTH, &len);
	std::string msg(len > 1 ? len : 1, '\0');
	if (len > 1) glGetProgramInfoLog(p, len, nullptr, &msg[0]);
	InfoLog("New3D probe: batched program %s (inDrawID at %d)%s%s", ok ? "linked" : "FAILED to link",
		ok ? glGetAttribLocation(p, "inDrawID") : -1, len > 1 ? ":\n" : "", len > 1 ? msg.c_str() : "");
	glDeleteProgram(p);
	glDeleteShader(v);
	glDeleteShader(f);
}

void R3DShader::DisableBatching()
{
	UnloadShader();
	m_forceUnbatched = true;
	LoadShader();
}

void R3DShader::GetUniformLocations()
{
	m_locTextureBank[0]		= glGetUniformLocation(m_shaderProgram, "textureBank[0]");
	m_locTextureBank[1]		= glGetUniformLocation(m_shaderProgram, "textureBank[1]");
	m_locTexturePage		= glGetUniformLocation(m_shaderProgram, "texturePage");
	m_locTexture1Enabled	= glGetUniformLocation(m_shaderProgram, "textureEnabled");
	m_locTexture2Enabled	= glGetUniformLocation(m_shaderProgram, "microTexture");
	m_locTextureAlpha		= glGetUniformLocation(m_shaderProgram, "textureAlpha");
	m_locAlphaTest			= glGetUniformLocation(m_shaderProgram, "alphaTest");
	m_locMicroTexMinLOD		= glGetUniformLocation(m_shaderProgram, "microTextureMinLOD");
	m_locMicroTexID			= glGetUniformLocation(m_shaderProgram, "microTextureID");
	m_locBaseTexInfo		= glGetUniformLocation(m_shaderProgram, "baseTexInfo");
	m_locBaseTexType		= glGetUniformLocation(m_shaderProgram, "baseTexType");
	m_locTextureInverted	= glGetUniformLocation(m_shaderProgram, "textureInverted");
	m_locTexWrapMode		= glGetUniformLocation(m_shaderProgram, "textureWrapMode");
	m_locColourLayer		= glGetUniformLocation(m_shaderProgram, "colourLayer");
	m_locPolyAlpha			= glGetUniformLocation(m_shaderProgram, "polyAlpha");

	m_locFogIntensity		= glGetUniformLocation(m_shaderProgram, "fogIntensity");
	m_locFogDensity			= glGetUniformLocation(m_shaderProgram, "fogDensity");
	m_locFogStart			= glGetUniformLocation(m_shaderProgram, "fogStart");
	m_locFogColour			= glGetUniformLocation(m_shaderProgram, "fogColour");
	m_locFogAttenuation		= glGetUniformLocation(m_shaderProgram, "fogAttenuation");
	m_locFogAmbient			= glGetUniformLocation(m_shaderProgram, "fogAmbient");

	m_locLighting			= glGetUniformLocation(m_shaderProgram, "lighting");
	m_locLightEnabled		= glGetUniformLocation(m_shaderProgram, "lightEnabled");
	m_locSunClamp			= glGetUniformLocation(m_shaderProgram, "sunClamp");
	m_locIntensityClamp		= glGetUniformLocation(m_shaderProgram, "intensityClamp");
	m_locShininess			= glGetUniformLocation(m_shaderProgram, "shininess");
	m_locSpecularValue		= glGetUniformLocation(m_shaderProgram, "specularValue");
	m_locSpecularEnabled	= glGetUniformLocation(m_shaderProgram, "specularEnabled");
	m_locFixedShading		= glGetUniformLocation(m_shaderProgram, "fixedShading");
	m_locSmoothShading		= glGetUniformLocation(m_shaderProgram, "smoothShading");
	m_locTranslatorMap		= glGetUniformLocation(m_shaderProgram, "translatorMap");

	m_locSpotEllipse		= glGetUniformLocation(m_shaderProgram, "spotEllipse");
	m_locSpotRange			= glGetUniformLocation(m_shaderProgram, "spotRange");
	m_locSpotColor			= glGetUniformLocation(m_shaderProgram, "spotColor");
	m_locSpotFogColor		= glGetUniformLocation(m_shaderProgram, "spotFogColor");
	m_locModelScale			= glGetUniformLocation(m_shaderProgram, "modelScale");
	m_locNodeAlpha			= glGetUniformLocation(m_shaderProgram, "nodeAlpha");

	m_locProjMat			= glGetUniformLocation(m_shaderProgram, "projMat");
	m_locModelMat			= glGetUniformLocation(m_shaderProgram, "modelMat");

	m_locHardwareStep		= glGetUniformLocation(m_shaderProgram, "hardwareStep");
	m_locDiscardAlpha		= glGetUniformLocation(m_shaderProgram, "discardAlpha");

	m_locCota				= glGetUniformLocation(m_shaderProgram, "cota");
}

void R3DShader::UnloadShader()
{
	// make sure no shader is bound
	glUseProgram(0);

	if (m_vertexShader) {
		glDeleteShader(m_vertexShader);
		m_vertexShader = 0;
	}

	if (m_geoShader) {
		glDeleteShader(m_geoShader);
		m_geoShader = 0;
	}

	if (m_fragmentShader) {
		glDeleteShader(m_fragmentShader);
		m_fragmentShader = 0;
	}

	if (m_shaderProgram) {
		glDeleteProgram(m_shaderProgram);
		m_shaderProgram = 0;
	}
}

GLint R3DShader::GetVertexAttribPos(const std::string& attrib)
{
	if (m_vertexLocCache.count(attrib)==0) {
		auto pos = glGetAttribLocation(m_shaderProgram, attrib.c_str());
		m_vertexLocCache[attrib] = pos;
	}

	return m_vertexLocCache[attrib];
}

void R3DShader::SetShader(bool enable)
{
	if (enable) {
		glUseProgram(m_shaderProgram);
		Start();
		DiscardAlpha(false);	// need some default
	}
	else {
		glUseProgram(0);
	}
}

void R3DShader::SetMeshUniforms(const Mesh* m)
{
	if (m == nullptr) {
		return;			// sanity check
	}

	if (m_dirtyMesh) {
		glUniform1i(m_locTextureBank[0], 0);
		glUniform1i(m_locTextureBank[1], 1);
	}

	if (m_dirtyMesh || m->textured != m_textured1) {
		glUniform1i(m_locTexture1Enabled, m->textured);
		m_textured1 = m->textured;
	}

	if (m_dirtyMesh || m->microTexture != m_textured2) {
		glUniform1i(m_locTexture2Enabled, m->microTexture);
		m_textured2 = m->microTexture;
	}

	if (m_dirtyMesh || (m->page ^ m_transPage) != m_texturePage) {
		glUniform1i(m_locTexturePage, m->page ^ m_transPage);
		m_texturePage = (m->page ^ m_transPage);
	}

	if (m_dirtyMesh || m->microTextureMinLOD != m_microTexMinLOD) {
		glUniform1f(m_locMicroTexMinLOD, m->microTextureMinLOD);
		m_microTexMinLOD = m->microTextureMinLOD;
	}

	if (m_dirtyMesh || m->microTextureID != m_microTexID) {
		glUniform1i(m_locMicroTexID, m->microTextureID);
		m_microTexID = m->microTextureID;
	}

	if (m_dirtyMesh || (m_baseTexInfo[0] != m->x || m_baseTexInfo[1] != m->y) || m_baseTexInfo[2] != m->width || m_baseTexInfo[3] != m->height) {

		m_baseTexInfo[0] = m->x;
		m_baseTexInfo[1] = m->y;
		m_baseTexInfo[2] = m->width;
		m_baseTexInfo[3] = m->height;

		glUniform4i(m_locBaseTexInfo, (m->x + m_transX), (m->y + m_transY), m->width, m->height);
	}

	if (m_dirtyMesh || m_baseTexType != m->format) {
		m_baseTexType = m->format;
		glUniform1i(m_locBaseTexType,  m_baseTexType);
	}

	if (m_dirtyMesh || m->inverted != m_textureInverted) {
		glUniform1i(m_locTextureInverted, m->inverted);
		m_textureInverted = m->inverted;
	}

	if (m_dirtyMesh || m->alphaTest != m_alphaTest) {
		glUniform1i(m_locAlphaTest, m->alphaTest);
		m_alphaTest = m->alphaTest;
	}

	if (m_dirtyMesh || m->textureAlpha != m_textureAlpha) {
		glUniform1i(m_locTextureAlpha, m->textureAlpha);
		m_textureAlpha = m->textureAlpha;
	}

	if (m_dirtyMesh || m->fogIntensity != m_fogIntensity) {
		glUniform1f(m_locFogIntensity, m->fogIntensity);
		m_fogIntensity = m->fogIntensity;
	}

	if (m_dirtyMesh || m->lighting != m_lightEnabled) {
		glUniform1i(m_locLightEnabled, m->lighting);
		m_lightEnabled = m->lighting;
	}

	if (m_dirtyMesh || m->shininess != m_shininess) {
		glUniform1f(m_locShininess, m->shininess);
		m_shininess = m->shininess;
	}

	if (m_dirtyMesh || m->specular != m_specularEnabled) {
		glUniform1i(m_locSpecularEnabled, m->specular);
		m_specularEnabled = m->specular;
	}

	if (m_dirtyMesh || m->specularValue != m_specularValue) {
		glUniform1f(m_locSpecularValue, m->specularValue);
		m_specularValue = m->specularValue;
	}

	if (m_dirtyMesh || m->fixedShading != m_fixedShading) {
		glUniform1i(m_locFixedShading, m->fixedShading);
		m_fixedShading = m->fixedShading;
	}

	if (m_dirtyMesh || m->smoothShading != m_smoothShading) {
		glUniform1i(m_locSmoothShading, m->smoothShading);
		m_smoothShading = m->smoothShading;
	}

	if (m_dirtyMesh || m->translatorMap != m_translatorMap) {
		glUniform1i(m_locTranslatorMap, m->translatorMap);
		m_translatorMap = m->translatorMap;
	}

	if (m_dirtyMesh || m->polyAlpha != m_polyAlpha) {
		glUniform1i(m_locPolyAlpha, m->polyAlpha);
		m_polyAlpha = m->polyAlpha;
	}

	if (m_dirtyMesh || m->wrapModeU != m_texWrapMode[0] || m->wrapModeV != m_texWrapMode[1]) {
		m_texWrapMode[0] = m->wrapModeU;
		m_texWrapMode[1] = m->wrapModeV;
		glUniform2iv(m_locTexWrapMode, 1, m_texWrapMode);
	}

	if (m_dirtyMesh || m->noLosReturn != m_noLosReturn) {
		m_noLosReturn = m->noLosReturn;
		glStencilFunc(GL_ALWAYS, m_noLosReturn << 7, 0b10000000);
		glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
		glStencilMask(0b10000000);
	}

	if (m_dirtyMesh || m->layered != m_layered) {
		m_layered = m->layered;
		// i think it should just disable z write, but the polys I think must be written first
		if (m_layered) {
			glStencilFunc(GL_EQUAL, 0, 0b01111111);			// basically stencil test passes if the value is zero
			glStencilOp(GL_KEEP, GL_KEEP, GL_INCR); // Increment only when both stencil and depth tests pass.
			glStencilMask(0b01111111);
		}
		else {
			glStencilFunc(GL_ALWAYS, m_noLosReturn << 7, 0b10000000);
			glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
			glStencilMask(0b10000000);
		}
	}

	m_dirtyMesh = false;
}

void R3DShader::SetViewportUniforms(const Viewport *vp)
{
	//didn't bother caching these, they don't get frequently called anyway
	glUniform1f(m_locFogDensity, vp->fogDensity);
	glUniform1f(m_locFogStart, vp->fogStart);
	glUniform3fv(m_locFogColour, 1, vp->fogColour);
	glUniform1f(m_locFogAttenuation, vp->fogAttenuation);
	glUniform1f(m_locFogAmbient, vp->fogAmbient);

	glUniform3fv(m_locLighting, 2, vp->lightingParams);
	glUniform1i(m_locSunClamp, vp->sunClamp);
	glUniform1i(m_locIntensityClamp, vp->intensityClamp);
	glUniform4fv(m_locSpotEllipse, 1, vp->spotEllipse);
	glUniform2fv(m_locSpotRange, 1, vp->spotRange);
	glUniform3fv(m_locSpotColor, 1, vp->spotColor);
	glUniform3fv(m_locSpotFogColor, 1, vp->spotFogColor);

	glUniformMatrix4fv(m_locProjMat, 1, GL_FALSE, vp->projectionMatrix);

	glUniform1i(m_locHardwareStep, vp->hardwareStep);

	glUniform1f(m_locCota, vp->cota);
}

void R3DShader::SetModelStates(const Model* model)
{
	if (m_dirtyModel || model->scale != m_modelScale) {
		glUniform1f(m_locModelScale, model->scale);
		m_modelScale = model->scale;
	}

	if (m_dirtyModel || model->alpha != m_nodeAlpha) {
		glUniform1f(m_locNodeAlpha, model->alpha);
		m_nodeAlpha = model->alpha;
	}

	m_transX = model->textureOffsetX;
	m_transY = model->textureOffsetY;
	m_transPage = model->page;

	// reset texture values
	for (auto& i : m_baseTexInfo) { i = -1; }

	glUniformMatrix4fv(m_locModelMat, 1, GL_FALSE, model->modelMat);

	m_dirtyModel = false;
}

void R3DShader::DiscardAlpha(bool discard)
{
	glUniform1i(m_locDiscardAlpha, discard);
}

void R3DShader::SetLayer(Layer layer)
{
	glUniform1i(m_locColourLayer, (GLint)layer);
}

void R3DShader::PrintShaderResult(GLuint shader)
{
	//===========
	GLint result;
	GLint length;
	//===========

	glGetShaderiv(shader, GL_COMPILE_STATUS, &result);

	if (result == GL_FALSE) {

		glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);

		if (length > 0) {
			std::vector<char> msg(length);
			glGetShaderInfoLog(shader, length, NULL, msg.data());
			ErrorLog("New3D shader failed to compile:\n%s", msg.data());
		}
	}
}

void R3DShader::PrintProgramResult(GLuint program)
{
	//===========
	GLint result;
	//===========

	glGetProgramiv(program, GL_LINK_STATUS, &result);

	if (result == GL_FALSE) {

		GLint maxLength = 0;
		glGetProgramiv(program, GL_INFO_LOG_LENGTH, &maxLength);

		//The maxLength includes the NULL character
		std::vector<GLchar> infoLog(maxLength);
		glGetProgramInfoLog(program, maxLength, &maxLength, infoLog.data());
		ErrorLog("New3D shader failed to link:\n%s", infoLog.data());
	}
}

} // New3D
