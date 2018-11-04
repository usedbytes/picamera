package picamera

// #cgo LDFLAGS: -L/opt/vc/lib/ -lmmal_core -lmmal_util -lmmal_vc_client -lvcos -lbcm_host -lvcsm -lvchiq_arm
// #cgo CFLAGS: -I/opt/vc/include
//
// #include "camera.h"
import "C"

import (
	"fmt"
	"image"
	"math"
)

type Camera struct {
	c *C.struct_camera

	srcW, srcH uint
	outW, outH uint
	crop Rectangle
	fps uint

	enabled bool
	rot int
	hflip, vflip bool
}

type Point struct {
	X, Y float64
}

type Rectangle struct {
	Min, Max Point
}

func (r Rectangle) Dx() float64 {
	return r.Max.X - r.Min.X
}

func (r Rectangle) Dy() float64 {
	return r.Max.Y - r.Min.Y
}

func (r Rectangle) In(s Rectangle) bool {
	return r.Min.X >= s.Min.X &&
	       r.Max.X <= s.Max.X &&
	       r.Min.Y >= s.Min.Y &&
	       r.Max.X <= s.Max.Y
}

func Rect(x1, y1, x2, y2 float64) Rectangle {
	return Rectangle{
		Min: Point{
			X: math.Min(x1, x2),
			Y: math.Min(y1, y2),
		},
		Max: Point{
			X: math.Max(x1, x2),
			Y: math.Max(y1, y2),
		},
	}
}

var frameSizes []image.Point = []image.Point{
	image.Point{ 640, 480 },
	image.Point{ 1926, 972 },
	image.Point{ 2592, 1944 },
}

func NewCamera(width, height, fps uint) *Camera {
	c := &Camera{
		outW: width,
		outH: height,
		crop: Rect(0, 0, 1.0, 1.0),
		fps: fps,
	}

	var frameSize image.Point
	for _, frameSize = range frameSizes {
		if width <= uint(frameSize.X) || height <= uint(frameSize.Y) {
			break
		}
	}

	c.srcW, c.srcH = uint(frameSize.X), uint(frameSize.Y)

	c.c = C.camera_init(C.uint32_t(c.srcW), C.uint32_t(c.srcH), C.uint(c.fps))
	if c.c == nil {
		return nil
	}

	C.camera_set_out_size(c.c, C.uint32_t(c.outW), C.uint32_t(c.outH))

	return c
}

var errs map[int]string = map[int]string {
	C.MMAL_SUCCESS:   "Success",
	C.MMAL_ENOMEM:    "Out of memory",
	C.MMAL_ENOSPC:    "Out of resources",
	C.MMAL_EINVAL:    "Invalid argument",
	C.MMAL_ENOSYS:    "Function not implemented",
	C.MMAL_ENOENT:    "No such file or directory",
	C.MMAL_ENXIO:     "No such device or address",
	C.MMAL_EIO:       "I/O error",
	C.MMAL_ESPIPE:    "Illegal seek",
	C.MMAL_ECORRUPT:  "Data is corrupt",
	C.MMAL_ENOTREADY: "Not ready",
	C.MMAL_ECONFIG:   "Incorrect configuration",
	C.MMAL_EISCONN:   "Port already connected",
	C.MMAL_ENOTCONN:  "Port not connected",
	C.MMAL_EAGAIN:    "Resource temporarily unavailable",
	C.MMAL_EFAULT:    "Bad address",
}
func getError(ret C.int) error {
	str, ok := errs[int(ret)]
	if !ok {
		return fmt.Errorf("Unknown error")
	}
	return fmt.Errorf(str)
}

func (c *Camera) SetFrameSize(width, height uint) error {
	if c.enabled {
		return fmt.Errorf("Can't set frame size when enabled")
	}

	ret := C.camera_set_frame_size(c.c, C.uint32_t(width), C.uint32_t(height))
	if ret != C.MMAL_SUCCESS {
		return getError(ret)
	}
	c.srcW, c.srcH = width, height

	return nil
}

func (c *Camera) GetFrameSize() (uint, uint) {
	return c.srcW, c.srcW
}

func (c *Camera) SetOutSize(width, height uint) error {
	if c.enabled {
		return fmt.Errorf("Can't set output size when enabled")
	}

	ret := C.camera_set_out_size(c.c, C.uint32_t(width), C.uint32_t(height))
	if ret != C.MMAL_SUCCESS {
		return getError(ret)
	}
	c.outW, c.outH = width, height

	return nil
}

func (c *Camera) GetOutSize() (uint, uint) {
	return c.outW, c.outH
}

func (c *Camera) SetFPS(fps uint) error {
	if c.enabled {
		return fmt.Errorf("Can't set FPS when enabled")
	}

	ret := C.camera_set_fps(c.c, C.uint(fps))
	if ret != C.MMAL_SUCCESS {
		return getError(ret)
	}
	c.fps = fps

	return nil
}

func (c *Camera) GetFPS() uint {
	return c.fps
}

func (c *Camera) SetCrop(crop Rectangle) error {
	if !crop.In(Rect(0.0, 0.0, 1.0, 1.0)) {
		return fmt.Errorf("Crop rectangle must be inside (0,0)-(1,1)")
	}

	if crop.Dx() <= 0 || crop.Dy() <= 0 {
		return fmt.Errorf("Empty crop rect")
	}

	ret := C.camera_set_crop(c.c, C.double(crop.Min.X), C.double(crop.Min.Y), C.double(crop.Dx()), C.double(crop.Dy()))
	if ret != C.MMAL_SUCCESS {
		return getError(ret)
	}
	c.crop = crop

	return nil
}

func boolToCint(b bool) C.int {
	if b {
		return 1
	}
	return 0
}

func (c *Camera) SetTransform(rot int, hflip, vflip bool) error {

	ret := C.camera_set_transform(c.c, C.int(rot), boolToCint(hflip), boolToCint(vflip))
	if ret != C.MMAL_SUCCESS {
		return getError(ret)
	}
	c.rot, c.hflip, c.vflip = rot, hflip, vflip

	return nil
}

func (c *Camera) GetTransform() (int, bool, bool) {
	return c.rot, c.hflip, c.vflip
}

func (c *Camera) Enabled() bool {
	return c.enabled
}

func (c *Camera) Enable() {
	if !c.enabled {
		C.camera_enable(c.c)
		c.enabled = true
	}
}

func (c *Camera) Disable() {
	if c.enabled {
		C.camera_disable(c.c)
		c.enabled = false
	}
}

func (c *Camera) GetFrame() (*Frame, error) {
	if !c.enabled {
		return nil, fmt.Errorf("Camera not enabled")
	}

	buf := C.camera_dequeue_buffer(c.c)
	if buf == nil {
		return nil, fmt.Errorf("Couldn't dequeue frame")
	}

	return newFrame(c, buf), nil
}

func (c *Camera) Close() {
	C.camera_exit(c.c)
	c.c = nil
}
