#include "interface/mmal/mmal.h"

void buf_get_ptr_stride(void *hnd, uint8_t **ptr, unsigned int *pitch)
{
	MMAL_BUFFER_HEADER_T *mmal_buf = hnd;
	MMAL_BUFFER_HEADER_VIDEO_SPECIFIC_T *videobuf = (MMAL_BUFFER_HEADER_VIDEO_SPECIFIC_T *)mmal_buf->type;

	*ptr = mmal_buf->data + videobuf->offset[0];
	*pitch = videobuf->pitch[0];
}
