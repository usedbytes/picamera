package picamera

// #cgo LDFLAGS: -L/opt/vc/lib/ -lmmal_core -lmmal_util -lmmal_vc_client -lvcos -lbcm_host -lvcsm -lvchiq_arm
// #cgo CFLAGS: -I/opt/vc/include
//
// #include "camera.h"
// void buf_get_ptr_stride(void *hnd, uint8_t **ptr, unsigned int *pitch);
import "C"

import (
	"fmt"
	"image"
	"unsafe"
)

type Frame struct {
	image.Gray

	camera *Camera
	c *C.struct_camera_buffer
}

func newFrame(c *Camera, buf *C.struct_camera_buffer) *Frame {

	fmt.Println("Frame", buf.hnd)

	var cPtr *C.uint8_t
	var cPitch C.uint
	C.buf_get_ptr_stride(buf.hnd, &cPtr, &cPitch)

	length := c.outH * uint(cPitch)

	var sl = struct {
		addr uintptr
		len  int
		cap  int
	}{uintptr(unsafe.Pointer(cPtr)), int(length), int(length)}

	// Use unsafe to turn sl into a []byte.
	b := *(*[]byte)(unsafe.Pointer(&sl))

	return &Frame{
		Gray: image.Gray{
			Pix: b,
			Stride: int(cPitch),
			Rect: image.Rect(0, 0, int(c.outW), int(c.outH)),
		},
		camera: c,
		c: buf,
	}
}

func (f *Frame) Release() {
	C.camera_queue_buffer(f.camera.c, f.c)
}
