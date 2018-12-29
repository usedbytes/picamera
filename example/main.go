package main

import (
	"fmt"
	"image"
	"image/png"
	"os"
	"time"

	"github.com/usedbytes/picamera"
)

func writePNG(img image.Image, filename string) {
	file, err := os.Create(filename)
	if err != nil {
		panic(err)
	}
	defer file.Close()

	err = png.Encode(file, img)
	if err != nil {
		panic(err)
	}
}

func main() {
	fmt.Println("Hello camera")

	camera := picamera.NewCamera(320, 320, 30)
	if camera == nil {
		panic("Couldn't open camera")
	}

	camera.SetTransform(0, true, true)
	camera.SetCrop(picamera.Rect(0, 0.0, 1.0, 1.0))
	camera.SetFormat(picamera.FORMAT_RGBA)

	camera.Enable()
	for i := 0; i < 10; i++ {
		frame, err := camera.GetFrame(1 * time.Second)
		if err != nil {
			fmt.Println(err);
			break
		}
		fname := fmt.Sprintf("out_%d.png", i)
		writePNG(frame, fname)

		// frame.Release() is very important! If you don't release
		// your frames, GetFrame() will stall after 3 frames.
		frame.Release()
	}

	camera.Close()
}
