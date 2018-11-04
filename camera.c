/*
 * Copyright 2017-2018 Brian Starkey <stark3y@gmail.com>
 *
 * Portions based on the Raspberry Pi Userland examples:
 * Copyright (c) 2013, Broadcom Europe Ltd
 * Copyright (c) 2013, James Hughes
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the copyright holder nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <stdio.h>
#include <stdbool.h>

#include "interface/mmal/mmal.h"
#include "interface/mmal/mmal_logging.h"
#include "interface/mmal/mmal_buffer.h"
#include "interface/mmal/util/mmal_util.h"
#include "interface/mmal/util/mmal_util_params.h"
#include "interface/mmal/util/mmal_default_components.h"
#include "interface/mmal/util/mmal_connection.h"

#include "cameracontrol.h"
#include "camera.h"

#define MMAL_CAMERA_PREVIEW_PORT 0
#define MMAL_CAMERA_VIDEO_PORT 1
#define MMAL_CAMERA_CAPTURE_PORT 2

#define NATIVE_WIDTH 640
#define NATIVE_HEIGHT 480

static void port_cleanup(MMAL_PORT_T *port)
{
	if (!port)
		return;

	if (port->is_enabled)
		mmal_port_disable(port);
}

static void component_cleanup(MMAL_COMPONENT_T *component)
{
	int i;

	if (!component)
		return;

	for (i = 0; i < component->input_num; i++)
		port_cleanup(component->input[i]);

	for (i = 0; i < component->output_num; i++)
		port_cleanup(component->output[i]);

	port_cleanup(component->control);

	mmal_component_destroy(component);
}

struct buffer_pool {
	MMAL_PORT_T *port;
	MMAL_POOL_T *free_pool;
	MMAL_QUEUE_T *ready_queue;
};

static void buffer_pool_cleanup(struct buffer_pool *pool)
{
	if (!pool)
		return;

	if (pool->ready_queue)
		mmal_queue_destroy(pool->ready_queue);

	if (pool->free_pool)
		mmal_pool_destroy(pool->free_pool);

	free(pool);
}

static struct buffer_pool *buffer_pool_create(MMAL_PORT_T *port)
{
	struct buffer_pool *pool = calloc(1, sizeof(*pool));
	if (!pool)
		return NULL;

	port->buffer_num = 5;
	port->buffer_size = port->buffer_size_recommended;
	pool->free_pool = mmal_port_pool_create(port, port->buffer_num, port->buffer_size);
	if (!pool->free_pool) {
		fprintf(stderr, "Couldn't create buffer pool\n");
		goto fail;
	}

	pool->ready_queue = mmal_queue_create();
	if (!pool->ready_queue) {
		fprintf(stderr, "Couldn't create ready queue\n");
		goto fail;
	}

	return pool;

fail:
	buffer_pool_cleanup(pool);
	return NULL;
}

struct camera {
	MMAL_COMPONENT_T *component;
	MMAL_COMPONENT_T *isp;
	MMAL_PORT_T *port;
	struct buffer_pool *pool;

	MMAL_ES_FORMAT_T *output_format;
	MMAL_ES_FORMAT_T *intermediate_format;
	RASPICAM_CAMERA_PARAMETERS parameters;
};

struct camera_buffer *camera_dequeue_buffer(struct camera *camera)
{
	struct camera_buffer *buf = malloc(sizeof(*buf));
	if (!buf)
		return buf;

	buf->hnd = mmal_queue_timedwait(camera->pool->ready_queue, 1000);
	if (!buf->hnd) {
		fprintf(stderr, "Couldn't dequeue buffer\n");
		free(buf);
		return NULL;
	}

	return buf;
}

void camera_queue_buffer(struct camera *camera, struct camera_buffer *buf)
{
	MMAL_BUFFER_HEADER_T *free_buf = NULL;

	mmal_buffer_header_release(buf->hnd);
	free(buf);

	while ((free_buf = mmal_queue_get(camera->pool->free_pool->queue))) {
		MMAL_STATUS_T ret = mmal_port_send_buffer(camera->port, free_buf);
		if (ret != MMAL_SUCCESS)
			fprintf(stderr, "Couldn't queue free buffer: %d\n", ret);
	}
}

static void camera_frame_callback(MMAL_PORT_T *port, MMAL_BUFFER_HEADER_T *buf)
{
	struct camera *camera = (struct camera *)port->userdata;

	mmal_queue_put(camera->pool->ready_queue, buf);
}

static void camera_control_callback(MMAL_PORT_T *port, MMAL_BUFFER_HEADER_T *buf) {
	/* Don't do anything with control buffers... */
	mmal_buffer_header_release(buf);
}

void camera_exit(struct camera *camera)
{
	camera_disable(camera);

	mmal_format_free(camera->output_format);
	mmal_format_free(camera->intermediate_format);

	free(camera);
}

int camera_enable(struct camera *camera)
{
	MMAL_BUFFER_HEADER_T *buf;
	MMAL_STATUS_T ret;
	MMAL_PORT_T *ports[2];
	int i, iret;


	ret = mmal_component_create("vc.ril.isp", &camera->isp);
	if (ret != MMAL_SUCCESS)
	{
		fprintf(stderr, "Failed to create ISP component\n");
		goto fail;
	}

	ret = mmal_component_create(MMAL_COMPONENT_DEFAULT_CAMERA, &camera->component);
	if (ret != MMAL_SUCCESS)
	{
		fprintf(stderr, "Failed to create component\n");
		goto fail;
	}

	if (camera->component->output_num != 3) {
		fprintf(stderr, "Unexpected number of output ports: %d\n", camera->component->output_num);
		goto fail;
	}

	ret = mmal_port_enable(camera->component->control, camera_control_callback);
	if (ret != MMAL_SUCCESS) {
		fprintf(stderr, "Enabling control port failed: %d\n", ret);
		goto fail;
	}

	MMAL_PARAMETER_CAMERA_CONFIG_T camera_config = {
		.hdr = { MMAL_PARAMETER_CAMERA_CONFIG, sizeof(camera_config) },
		.max_stills_w = 0,
		.max_stills_h = 0,
		.stills_yuv422 = 0,
		.stills_yuv422 = 0,
		.one_shot_stills = 1,
		.max_preview_video_w = NATIVE_WIDTH,
		.max_preview_video_h = NATIVE_HEIGHT,
		.num_preview_video_frames = 3,
		.stills_capture_circular_buffer_height = 0,
		.fast_preview_resume = 0,
		.use_stc_timestamp = MMAL_PARAM_TIMESTAMP_MODE_RESET_STC
	};

	ret = mmal_port_parameter_set(camera->component->control, &camera_config.hdr);
	if (ret != MMAL_SUCCESS) {
		fprintf(stderr, "Configuring camera parameters failed: %d\n", ret);
		goto fail;
	}

	iret = raspicamcontrol_set_all_parameters(camera->component, &camera->parameters);
	if (iret != 0) {
		fprintf(stderr, "Setting raspicam defaults failed: %d\n", iret);
		goto fail;
	}

	if (camera->output_format->es->video.width != camera->intermediate_format->es->video.width ||
	    camera->output_format->es->video.height != camera->intermediate_format->es->video.height) {
		camera->port = camera->isp->output[0];

		mmal_format_full_copy(camera->component->output[0]->format, camera->intermediate_format);
		ret = mmal_port_format_commit(camera->component->output[0]);
		if (ret != MMAL_SUCCESS)
		{
			fprintf(stderr, "Couldn't set camera output port format: %d\n", ret);
			goto fail;
		}

		mmal_format_full_copy(camera->isp->input[0]->format, camera->intermediate_format);
		ret = mmal_port_format_commit(camera->isp->input[0]);
		if (ret != MMAL_SUCCESS)
		{
			fprintf(stderr, "Couldn't set isp input port format: %d\n", ret);
			goto fail;
		}

		ret = mmal_port_parameter_set_boolean(camera->isp->input[0], MMAL_PARAMETER_ZERO_COPY, MMAL_TRUE);
		if (ret != MMAL_SUCCESS)
		{
			fprintf(stderr, "Couldn't set zero-copy on isp port: %d\n", ret);
			goto fail;
		}

		ret = mmal_port_connect(camera->component->output[0], camera->isp->input[0]);
		if (ret != MMAL_SUCCESS)
		{
			fprintf(stderr, "Couldn't connect ports: %d\n", ret);
			goto fail;
		}
	} else {
		camera->port = camera->component->output[0];
	}
	camera->port->userdata = (void *)camera;

	mmal_format_full_copy(camera->port->format, camera->output_format);
	ret = mmal_port_format_commit(camera->port);
	if (ret != MMAL_SUCCESS)
	{
		fprintf(stderr, "Couldn't set output port format: %d\n", ret);
		goto fail;
	}

	for (i = 1; i < camera->component->output_num; i++) {
		mmal_format_full_copy(camera->component->output[i]->format, camera->component->output[0]->format);
		ret = mmal_port_format_commit(camera->component->output[i]);
		if (ret != MMAL_SUCCESS)
		{
			fprintf(stderr, "Couldn't copy format to port %d: %d\n", i, ret);
			goto fail;
		}
	}

	camera->pool = buffer_pool_create(camera->port);
	if (!camera->pool) {
		fprintf(stderr, "Couldn't create buffer pool\n");
		goto fail;
	}

	ret = mmal_component_enable(camera->isp);
	if (ret != MMAL_SUCCESS )
	{
		fprintf(stderr, "Couldn't enable isp component: %d\n", ret);
		goto fail;
	}

	ret = mmal_component_enable(camera->component);
	if (ret != MMAL_SUCCESS )
	{
		fprintf(stderr, "Couldn't enable component: %d\n", ret);
		goto fail;
	}

	ret = mmal_port_enable(camera->port, camera_frame_callback);
	if (ret != MMAL_SUCCESS)
	{
		fprintf(stderr, "Couldn't enable camera port: %d\n", ret);
		goto fail;
	}

	if (!camera->component->output[0]->is_enabled) {
		ret = mmal_port_enable(camera->component->output[0], NULL);
		if (ret != MMAL_SUCCESS)
		{
			fprintf(stderr, "Couldn't enable camera output port: %d\n", ret);
			goto fail;
		}
	}

	i = 0;
	while ((buf = mmal_queue_get(camera->pool->free_pool->queue))) {
		ret = mmal_port_send_buffer(camera->port, buf);
		if (ret != MMAL_SUCCESS) {
			fprintf(stderr, "Couldn't queue free buffer: %d\n", ret);
			goto fail;
		}
		i++;
	}
	if (i != camera->port->buffer_num)
		fprintf(stderr, "Queued an unexpected number of buffers (%d)\n", i);

	return 0;

fail:
	return -1;
}

int camera_disable(struct camera *camera)
{
	if (camera->port->is_enabled)
		mmal_port_disable(camera->port);

	buffer_pool_cleanup(camera->pool);
	component_cleanup(camera->isp);
	component_cleanup(camera->component);

	return 0;
}

struct camera *camera_init(uint32_t width, uint32_t height, unsigned int fps)
{
	int ret;
	struct camera *camera = calloc(1, sizeof(*camera));

	if (!camera)
		return NULL;

	camera->output_format = mmal_format_alloc();
	camera->intermediate_format = mmal_format_alloc();

	camera->output_format->type = MMAL_ES_TYPE_VIDEO;
	camera->output_format->encoding = MMAL_ENCODING_I420;
	camera->output_format->encoding_variant = MMAL_ENCODING_I420;
	camera->output_format->es->video.width = width;
	camera->output_format->es->video.height = height;
	camera->output_format->es->video.crop.x = 0;
	camera->output_format->es->video.crop.y = 0;
	camera->output_format->es->video.crop.width = width;
	camera->output_format->es->video.crop.height = height;
	camera->output_format->es->video.frame_rate.num = fps;
	camera->output_format->es->video.frame_rate.den = 1;

	mmal_format_full_copy(camera->intermediate_format, camera->output_format);

	camera->intermediate_format->encoding = MMAL_ENCODING_OPAQUE;
	camera->intermediate_format->encoding_variant = MMAL_ENCODING_I420;

	raspicamcontrol_set_defaults(&camera->parameters);

	return camera;
}

int camera_set_fps(struct camera *camera, unsigned int fps)
{
	camera->output_format->es->video.frame_rate.num = fps;
	camera->intermediate_format->es->video.frame_rate.num = fps;

	return 0;
}

int camera_set_frame_size(struct camera *camera, uint32_t width, uint32_t height)
{
	camera->intermediate_format->es->video.width = width;
	camera->intermediate_format->es->video.height = height;
	camera->intermediate_format->es->video.crop.width = width;
	camera->intermediate_format->es->video.crop.height = height;

	return 0;
}

int camera_set_out_size(struct camera *camera, uint32_t width, uint32_t height)
{
	camera->output_format->es->video.width = width;
	camera->output_format->es->video.height = height;
	camera->output_format->es->video.crop.width = width;
	camera->output_format->es->video.crop.height = height;

	return 0;
}

int camera_set_crop(struct camera *camera, double left, double top, double width, double height)
{
	camera->parameters.roi = (PARAM_FLOAT_RECT_T){ .x = left, .y = top, .w = width, .h = height };

	if (camera->port && camera->port->is_enabled) {
		return raspicamcontrol_set_ROI(camera->component, camera->parameters.roi);
	}

	return 0;
}

int camera_set_transform(struct camera *camera, int rot, int hflip, int vflip)
{
	camera->parameters.hflip = hflip;
	camera->parameters.vflip = vflip;
	camera->parameters.rotation = rot;

	if (camera->port && camera->port->is_enabled) {
		int ret;

		ret = raspicamcontrol_set_flips(camera->component, camera->parameters.hflip, camera->parameters.vflip);
		if (ret != MMAL_SUCCESS) {
			return ret;
		}

		ret = raspicamcontrol_set_rotation(camera->component, camera->parameters.rotation);
		if (ret != MMAL_SUCCESS) {
			return ret;
		}
	}

	return 0;
}
