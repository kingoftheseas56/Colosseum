package avatar

import (
	"bytes"
	"image"
	"image/color"
	"image/png"
	"testing"
)

func TestValidateAcceptsPNGWithinBounds(t *testing.T) {
	imageData := image.NewRGBA(image.Rect(0, 0, 64, 64))
	imageData.Set(0, 0, color.RGBA{R: 255, A: 255})

	var buffer bytes.Buffer
	if err := png.Encode(&buffer, imageData); err != nil {
		t.Fatalf("png.Encode() error = %v", err)
	}

	contentType, extension, err := Validate(buffer.Bytes())
	if err != nil {
		t.Fatalf("Validate() error = %v", err)
	}
	if contentType != "image/png" {
		t.Fatalf("content type = %q, want image/png", contentType)
	}
	if extension != ".png" {
		t.Fatalf("extension = %q, want .png", extension)
	}
}

func TestValidateRejectsTinyImage(t *testing.T) {
	imageData := image.NewRGBA(image.Rect(0, 0, 1, 1))
	var buffer bytes.Buffer
	if err := png.Encode(&buffer, imageData); err != nil {
		t.Fatalf("png.Encode() error = %v", err)
	}
	if _, _, err := Validate(buffer.Bytes()); err == nil {
		t.Fatal("Validate() accepted a 1x1 image")
	}
}

func TestValidateRejectsNonImage(t *testing.T) {
	if _, _, err := Validate([]byte("not an image")); err == nil {
		t.Fatal("Validate() accepted non-image input")
	}
}
