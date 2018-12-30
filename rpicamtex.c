#include <atto/gl.h>
#include <atto/app.h>

#include "interface/mmal/mmal.h"
#include "interface/mmal/mmal_parameters_camera.h"
#include "interface/mmal/util/mmal_default_components.h"
#include "interface/mmal/util/mmal_util.h"
#include "interface/mmal/mmal_buffer.h"
#include "interface/mmal/util/mmal_util_params.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>

#define CAMERA_PREVIEW_PORT 0
#define CAMERA_VIDEO_PORT 1
#define CAMERA_STILL_PORT 2

extern EGLDisplay a_app_egl_display;

#define LOG(...) aAppDebugPrintf(__VA_ARGS__)

static struct {
	int max_w, max_h;
} info;

#define MMCHK(f) \
{ \
	const MMAL_STATUS_T status = f; \
	if (MMAL_SUCCESS != status) { \
		aAppDebugPrintf(#f " failed: %d", status); \
		aAppTerminate(status); \
	} \
}

static void enumCameras() {
	MMAL_COMPONENT_T *camera_info;
	MMCHK(mmal_component_create(MMAL_COMPONENT_DEFAULT_CAMERA_INFO, &camera_info));

	MMAL_PARAMETER_CAMERA_INFO_T param;
	param.hdr.id = MMAL_PARAMETER_CAMERA_INFO;
	param.hdr.size = sizeof(param);
	MMCHK(mmal_port_parameter_get(camera_info->control, &param.hdr));

	if (param.num_cameras < 1) {
		aAppDebugPrintf("Not enough cameras");
		aAppTerminate(1);
	}

	info.max_w = param.cameras[0].max_width;
	info.max_h = param.cameras[0].max_height;

	aAppDebugPrintf("camera info: cams=%u flashes=%u", param.num_cameras, param.num_flashes);
	for (uint32_t i = 0; i < param.num_cameras; ++i) {
		const MMAL_PARAMETER_CAMERA_INFO_CAMERA_T *c = param.cameras + i;
		aAppDebugPrintf("\tcam %u: name=%s port_id=%u max=%ux%u lens=%d ", i,
			c->camera_name, c->port_id, c->max_width, c->max_height, c->lens_present);
	}
	for (uint32_t i = 0; i < param.num_flashes; ++i) {
		const MMAL_PARAMETER_CAMERA_INFO_FLASH_T *f = param.flashes + i;
		aAppDebugPrintf("\tflash %u: type=%d", i, f->flash_type);
	}

	mmal_component_destroy(camera_info);
}

static struct {
	MMAL_COMPONENT_T *camera;
	MMAL_PORT_T *preview_port;
	MMAL_POOL_T *pool;
	MMAL_QUEUE_T *queue;
	MMAL_BUFFER_HEADER_T *prev_buffer;
	EGLImageKHR egl_image;
} rt;

static void callbackCameraControl(MMAL_PORT_T *port, MMAL_BUFFER_HEADER_T *hdr) {
	aAppDebugPrintf("%s: port=%p, hdr=%p, cmd=%u", __FUNCTION__, port, hdr, hdr->cmd);
	mmal_buffer_header_release(hdr);
}

static void callbackPreviewOutput(MMAL_PORT_T *port, MMAL_BUFFER_HEADER_T *hdr) {
	(void)port;
	if (hdr->length == 0 || hdr->data == NULL) {
		aAppDebugPrintf("%s: empty buffer received", __FUNCTION__);
		mmal_buffer_header_release(hdr);
	} else {
		//LOG("%s: hdr=%p, cmd=%u, length=%u", __FUNCTION__, hdr, hdr->cmd, hdr->length);
	}
	mmal_queue_put(rt.queue, hdr);
}

void rctInit(int w, int h) {
	(void)w; (void)h;

	enumCameras();

	w = w > info.max_w ? info.max_w : w;
	h = h > info.max_h ? info.max_h : h;

	LOG("%d %d", w, h);

	MMCHK(mmal_component_create(MMAL_COMPONENT_DEFAULT_CAMERA, &rt.camera));

	{
		MMAL_PARAMETER_INT32_T camera_num = {{MMAL_PARAMETER_CAMERA_NUM, sizeof(camera_num)}, 0};
		MMCHK(mmal_port_parameter_set(rt.camera->control, &camera_num.hdr));
	}

	if (rt.camera->output_num < 1) {
		aAppDebugPrintf("No camera output ports");
		aAppTerminate(1);
	}

	rt.preview_port = rt.camera->output[CAMERA_PREVIEW_PORT];

	MMCHK(mmal_port_enable(rt.camera->control, callbackCameraControl));

	{
		MMAL_PARAMETER_CAMERA_CONFIG_T cam_config = {
			{MMAL_PARAMETER_CAMERA_CONFIG, sizeof(cam_config)},
			.max_stills_w = w, .max_stills_h = h,
			.stills_yuv422 = 0,
			.one_shot_stills = 1,
			.max_preview_video_w = w,
			.max_preview_video_h = h,
			.num_preview_video_frames = 3,
			.stills_capture_circular_buffer_height = 0,
			.fast_preview_resume = 0,
			.use_stc_timestamp = MMAL_PARAM_TIMESTAMP_MODE_RESET_STC
		};

		MMCHK(mmal_port_parameter_set(rt.camera->control, &cam_config.hdr));
	}

	{
		MMAL_ES_FORMAT_T *const format = rt.preview_port->format;

		format->encoding = MMAL_ENCODING_OPAQUE;
		format->encoding_variant = MMAL_ENCODING_I420;
		format->es->video.width = w;
		format->es->video.height = h;
		format->es->video.crop.x = 0;
		format->es->video.crop.y = 0;
		format->es->video.crop.width = w;
		format->es->video.crop.height = h;
		format->es->video.frame_rate.num = 0;
		format->es->video.frame_rate.den = 1;

		MMCHK(mmal_port_format_commit(rt.preview_port));

		// needed?
		mmal_format_full_copy(rt.camera->output[CAMERA_VIDEO_PORT]->format, format);
		MMCHK(mmal_port_format_commit(rt.camera->output[CAMERA_VIDEO_PORT]));

		mmal_format_full_copy(rt.camera->output[CAMERA_STILL_PORT]->format, format);
		MMCHK(mmal_port_format_commit(rt.camera->output[CAMERA_STILL_PORT]));
	}

	MMCHK(mmal_component_enable(rt.camera));

	MMCHK(mmal_port_parameter_set_boolean(rt.preview_port, MMAL_PARAMETER_ZERO_COPY, MMAL_TRUE));
	MMCHK(mmal_port_format_commit(rt.preview_port));

	rt.preview_port->buffer_num = rt.preview_port->buffer_num_recommended; 
	rt.preview_port->buffer_size = rt.preview_port->buffer_size_recommended; 

	rt.pool = mmal_port_pool_create(rt.preview_port, rt.preview_port->buffer_num, rt.preview_port->buffer_size);
	if (!rt.pool) {
		aAppDebugPrintf("Error creating pool");
		aAppTerminate(1);
	}

	rt.queue = mmal_queue_create();
	if (!rt.queue) {
		aAppDebugPrintf("Error creating queue");
		aAppTerminate(1);
	}

	MMCHK(mmal_port_enable(rt.preview_port, callbackPreviewOutput));
}

#define GLCHK(f) do{\
		f; \
		const int glerror = glGetError(); \
		if (glerror != GL_NO_ERROR) { \
			LOG(#f " returned %d", glerror); \
			aAppTerminate(2); \
		} \
	}while(0)

static void updateTexture(EGLenum target, EGLClientBuffer buf, GLuint texture, EGLImageKHR *egl_image)
{
	glGetError();
	//LOG("tex = %u", texture);
	//glBindTexture(GL_TEXTURE_2D, 0);
	GLCHK(glBindTexture(GL_TEXTURE_EXTERNAL_OES, texture));
	if (*egl_image != EGL_NO_IMAGE_KHR)
	{
		/* Discard the EGL image for the preview frame */
		//LOG("destroy egl_image=%p", *egl_image);
		eglDestroyImageKHR(a_app_egl_display, *egl_image);
		*egl_image = EGL_NO_IMAGE_KHR;
	}

	*egl_image = eglCreateImageKHR(a_app_egl_display, EGL_NO_CONTEXT, target, buf, NULL);
	//LOG("egl_image = %p", *egl_image);
	GLCHK(glEGLImageTargetTexture2DOES(GL_TEXTURE_EXTERNAL_OES, *egl_image));
}

void rctUpdate(GLuint tex) {
	MMAL_BUFFER_HEADER_T *hdr;
       	while ((hdr = mmal_queue_get(rt.pool->queue)) != NULL) {
		//LOG("pool->queue: hdr=%p, cmd=%u, length=%u", hdr, hdr->cmd, hdr->length);
		const MMAL_STATUS_T status = mmal_port_send_buffer(rt.preview_port, hdr);
		if (MMAL_SUCCESS != status)
			aAppDebugPrintf("Error sending buffer to port: %d", status);
	}

	MMAL_BUFFER_HEADER_T *last = NULL;
	while ((hdr = mmal_queue_get(rt.queue)) != NULL) {
		if (last)
			mmal_buffer_header_release(last);
		last = hdr;
	}

	if (last) {
		updateTexture(EGL_IMAGE_BRCM_MULTIMEDIA, (EGLClientBuffer)last->data, tex, &rt.egl_image);
		if (rt.prev_buffer)
			mmal_buffer_header_release(rt.prev_buffer);
		rt.prev_buffer = last;
	}
}

void rctDestroy() {
   	if (rt.egl_image != EGL_NO_IMAGE_KHR)
      		eglDestroyImageKHR(a_app_egl_display, rt.egl_image);

	mmal_port_disable(rt.preview_port);
		
	if (rt.prev_buffer)
		mmal_buffer_header_release(rt.prev_buffer);

	{
		MMAL_BUFFER_HEADER_T *hdr;
		while ((hdr = mmal_queue_get(rt.queue)) != NULL)
			mmal_buffer_header_release(hdr);
	}

	mmal_queue_destroy(rt.queue);
	mmal_pool_destroy(rt.pool);

	mmal_component_disable(rt.camera);
	mmal_component_destroy(rt.camera);
}
