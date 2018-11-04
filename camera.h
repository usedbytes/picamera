/* Copyright 2017-2018 Brian Starkey <stark3y@gmail.com> */

#include "cameracontrol.h"

struct camera;
struct camera_buffer {
	/* Opaque handle - don't touch! */
	void *hnd;
};

struct camera_config {
	int source_w, source_h;
	PARAM_FLOAT_RECT_T crop_rect;
	int fps;

	int out_w, out_h;
};

/* Wait for a frame to be available, and return it */
struct camera_buffer *camera_dequeue_buffer(struct camera *camera);
/* Return a buffer once it's finished with */
void camera_queue_buffer(struct camera *camera, struct camera_buffer *buf);

struct camera *camera_init(uint32_t width, uint32_t height, unsigned int fps);
void camera_exit(struct camera *camera);

int camera_enable(struct camera *camera);
int camera_disable(struct camera *camera);

int camera_set_frame_size(struct camera *camera, uint32_t width, uint32_t height);
int camera_set_fps(struct camera *camera, unsigned int fps);
int camera_set_out_size(struct camera *camera, uint32_t width, uint32_t height);
int camera_set_crop(struct camera *camera, double top, double left, double width, double height);
int camera_set_transform(struct camera *camera, int rot, int hflip, int vflip);
