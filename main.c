#include <atto/gl.h>
#include "rpicamtex.h"
#include <atto/app.h>

#include <math.h>
#include <stdlib.h>

static const char shader_vertex[] =
	"attribute vec2 av2_pos;"
	"varying vec2 vv2_pos;"
	"void main() { vv2_pos = av2_pos; gl_Position = vec4(av2_pos, 0., 1.); }"
;

static const char shader_fragment_show[] =
	"#extension GL_OES_EGL_image_external: require\n"
	"uniform samplerExternalOES us2_texture;"
	//"uniform sampler2D us2_texture;"
	"uniform float uf_time;"
	"varying vec2 vv2_pos;"
	"void main() {"
		//"float a = 5. * atan(vv2_pos.x, vv2_pos.y);"
		//"float r = .1 * sin(.4 * uf_time + length(vv2_pos)*3.);"
		//"gl_FragColor = texture2D(us2_texture, gl_FragCoord.xy);"// + r*vec2(sin(a),cos(a)));"
		//"gl_FragColor = vec4(vv2_pos, 0., 0.);"
		"gl_FragColor = texture2D(us2_texture, vv2_pos*.5 + .5);"
	"}"
;

static const float screenquad[] = {
	1.f, -1.f,
	1.f, 1.f,
	-1.f, -1.f,
	-1.f, 1.f,
};

static struct {
	AGLProgramUniform uni[2];
	AGLAttribute shattr[1];
	
	AGLDrawSource show;
	AGLDrawMerge merge;
	AGLDrawTarget screen;
	
	AGLTexture camtex;
} g;

static void init(void) {
	g.show.program = aGLProgramCreateSimple(shader_vertex, shader_fragment_show);
	if (g.show.program <= 0) {
		aAppDebugPrintf("shader error: %s", a_gl_error);
		/* \fixme add fatal */
	}

	g.camtex = aGLTextureCreate();

	g.uni[0].name = "uf_time";
	g.uni[0].type = AGLAT_Float;
	g.uni[0].count = 1;

	g.shattr[0].name = "av2_pos";
	g.shattr[0].buffer = 0;
	g.shattr[0].size = 2;
	g.shattr[0].type = GL_FLOAT;
	g.shattr[0].normalized = GL_FALSE;
	g.shattr[0].stride = 0;
	g.shattr[0].ptr = screenquad;

	/*
	g.uni[1].name = "us2_texture";
	g.uni[1].type = AGLAT_Texture;
	g.uni[1].value.texture = &g.camtex;
	g.uni[1].count = 1;
	*/

	g.show.primitive.mode = GL_TRIANGLE_STRIP;
	g.show.primitive.count = 4;
	g.show.primitive.first = 0;
	g.show.primitive.index.buffer = 0;
	g.show.primitive.index.data.ptr = 0;
	g.show.primitive.index.type = 0;
	
	g.show.attribs.p = g.shattr;
	g.show.attribs.n = sizeof g.shattr / sizeof *g.shattr;

	g.show.uniforms.p = g.uni;
	g.show.uniforms.n = 1;//sizeof g.uni / sizeof *g.uni;
	aGLUniformLocate(g.show.program, (AGLProgramUniform*)g.show.uniforms.p, g.show.uniforms.n);

	g.show.primitive.cull_mode = AGLCM_Disable;
	g.show.primitive.front_face = AGLFF_CounterClockwise;

	g.screen.framebuffer = 0;

	g.merge.blend.enable = 0;
	g.merge.depth.mode = AGLDM_Disabled;
}


static void resize(ATimeUs timestamp, unsigned int old_w, unsigned int old_h) {
	AGLTextureUploadData data;
	(void)(timestamp); (void)(old_w); (void)(old_h);

	data.format = AGLTF_U8_RGBA;
	data.x = data.y = 0;
	data.width = a_app_state->width;
	data.height = a_app_state->height;
	
	{
		unsigned char *pixels = malloc(data.width * data.height * 4);
		for (int x = 0; x < data.width; ++x) {
			for (int y = 0; y < data.height; ++y) {
				const unsigned mul = x * y;
				unsigned char *p = pixels + (x + y * data.width) * 4;
				p[0] = (mul << 2);
				p[1] = (mul >> 4) & 0xfc;
				p[2] = (mul >> 10) & 0xfc;
				p[3] = 0;
			}
		}
		data.pixels = pixels;
		//aGLTextureUpload(&g.camtex, &data);
		free(pixels);
		data.pixels = NULL;
	}

	g.screen.viewport.x = 0;
	g.screen.viewport.y = 0;
	g.screen.viewport.w = a_app_state->width;
	g.screen.viewport.h = a_app_state->height;
}

static void paint(ATimeUs timestamp, float dt) {
	float t = timestamp * 1e-6f;
	(void)(dt);

	AGLClearParams clear;
	clear.r = sinf(t*.1f);
	clear.g = sinf(t*.2f);
	clear.b = sinf(t*.3f);
	clear.depth = 1;
	clear.bits = AGLCB_Everything;

	aGLClear(&clear, &g.screen);

	rctUpdate(g.camtex._.name);

	g.uni[0].value.pf = &t;
	//aGLDraw(&g.draw, &g.merge, &g.fb);
	aGLDraw(&g.show, &g.merge, &g.screen);
}

static void keyPress(ATimeUs timestamp, AKey key, int pressed) {
	(void)(timestamp); (void)(pressed);
	if (key == AK_Esc) {
		rctDestroy();
		aAppTerminate(0);
	}
}

void attoAppInit(struct AAppProctable *proctable) {
	aGLInit();
	init();

	rctInit(1280, 720);

	proctable->resize = resize;
	proctable->paint = paint;
	proctable->key = keyPress;
}
