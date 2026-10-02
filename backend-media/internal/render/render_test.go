package render

import (
	"bytes"
	"context"
	"errors"
	"image"
	"image/color"
	"image/jpeg"
	"image/png"
	"math"
	"runtime"
	"testing"
	"time"

	"github.com/serogaq/TeleBezel/backend-media/internal/tbi"
)

func encode(t testing.TB, img image.Image) []byte {
	t.Helper()
	var buffer bytes.Buffer
	if err := jpeg.Encode(&buffer, img, &jpeg.Options{Quality: 88}); err != nil {
		t.Fatal(err)
	}
	return buffer.Bytes()
}

func landscape(width, height int) *image.RGBA {
	img := image.NewRGBA(image.Rect(0, 0, width, height))
	for y := 0; y < height; y++ {
		for x := 0; x < width; x++ {
			sky := float64(y) / float64(height)
			img.Set(x, y, color.RGBA{uint8(80 + 120*sky), uint8(140 + 60*math.Sin(float64(x)/37)), uint8(230 - 150*sky), 255})
		}
	}
	return img
}

func screenshot(width, height int) *image.RGBA {
	img := image.NewRGBA(image.Rect(0, 0, width, height))
	for y := 0; y < height; y++ {
		for x := 0; x < width; x++ {
			ink := y%40 > 24 && y%40 < 30 && x%60 < 50
			if ink {
				img.Set(x, y, color.RGBA{20, 20, 20, 255})
			} else {
				img.Set(x, y, color.RGBA{250, 250, 250, 255})
			}
		}
	}
	return img
}

var (
	emery  = Spec{Width: 200, Height: 228, Shape: tbi.ShapeRect, Budget: 28672, Formats: []int{4, 2}}
	gabbro = Spec{Width: 260, Height: 260, Shape: tbi.ShapeRound, Budget: 21504, Formats: []int{4, 2}}
)

func check(t *testing.T, name string, result Result, spec Spec) {
	t.Helper()
	header, packed, err := tbi.Decode(result.Data)
	if err != nil {
		t.Fatalf("%s: %v", name, err)
	}
	if len(packed) > spec.Budget {
		t.Fatalf("%s: %d bytes over the %d budget", name, len(packed), spec.Budget)
	}
	if header.Width > spec.Width || header.Height > spec.Height || header.CanvasWidth != spec.Width || header.Colors > 16 {
		t.Fatalf("%s: %+v does not fit %+v", name, header, spec)
	}
}

func TestRendersCommonShapesForBothWatches(t *testing.T) {
	cases := []struct {
		name  string
		input []byte
		class string
	}{
		{"photo", encode(t, landscape(1280, 960)), "photo"},
		{"portrait", encode(t, landscape(960, 1280)), "photo"},
		{"panorama", encode(t, landscape(2400, 600)), "photo"},
		{"small", encode(t, landscape(90, 67)), "photo"},
		{"screenshot", encode(t, screenshot(1080, 2340)), "graphic"},
		{"grey", encode(t, image.NewGray(image.Rect(0, 0, 320, 240))), "graphic"},
	}
	for _, item := range cases {
		for _, spec := range []Spec{emery, gabbro} {
			result, err := Render(context.Background(), item.input, spec)
			if err != nil {
				t.Fatalf("%s: %v", item.name, err)
			}
			check(t, item.name, result, spec)
			if result.Class != item.class {
				t.Fatalf("%s classified as %s", item.name, result.Class)
			}
		}
	}
	photo, _ := Render(context.Background(), encode(t, landscape(1280, 960)), emery)
	if photo.Width != 200 || photo.Height != 150 || photo.Bits != 4 {
		t.Fatalf("a landscape photo fills the emery width in colour, got %dx%d p%d", photo.Width, photo.Height, photo.Bits)
	}
	portrait, _ := Render(context.Background(), encode(t, landscape(960, 1280)), Spec{Width: 260, Height: 260, Shape: tbi.ShapeRound, Budget: 28672, Formats: []int{4, 2}})
	if portrait.Height != 260 || portrait.Bits != 4 {
		t.Fatalf("a portrait photo fills the gabbro height, got %dx%d p%d", portrait.Width, portrait.Height, portrait.Bits)
	}
	small, _ := Render(context.Background(), encode(t, landscape(90, 67)), emery)
	if small.Width != 200 {
		t.Fatalf("a small thumbnail is scaled up to the screen, got %d", small.Width)
	}
}

func TestBudgetKeepsPhotosInColourAndTextSharp(t *testing.T) {
	tight := gabbro
	tight.Budget = 20480
	photo, err := Render(context.Background(), encode(t, landscape(1280, 960)), tight)
	if err != nil {
		t.Fatal(err)
	}
	check(t, "photo", photo, tight)
	if photo.Bits != 4 || photo.Width < 195 {
		t.Fatalf("a photo keeps colour and gives up a little size, got %dx%d p%d", photo.Width, photo.Height, photo.Bits)
	}
	text, err := Render(context.Background(), encode(t, screenshot(1080, 1400)), Spec{Width: 260, Height: 260, Shape: tbi.ShapeRound, Budget: 12288, Formats: []int{4, 2}})
	if err != nil {
		t.Fatal(err)
	}
	if text.Bits != 2 || text.Height != 260 {
		t.Fatalf("a screenshot keeps its size and gives up colours, got %dx%d p%d", text.Width, text.Height, text.Bits)
	}
	if _, err := Render(context.Background(), encode(t, landscape(1280, 960)), Spec{Width: 260, Height: 260, Shape: tbi.ShapeRound, Budget: 2048, Formats: []int{4}}); err != nil {
		t.Fatalf("a tiny budget still renders a smaller image: %v", err)
	}
}

func TestRejectsUntrustedInputSafely(t *testing.T) {
	photo := encode(t, landscape(64, 48))
	var pngBytes bytes.Buffer
	_ = png.Encode(&pngBytes, landscape(8, 8))
	huge := append([]byte(nil), photo...)
	for index := 0; index+8 < len(huge); index++ {
		if huge[index] == 0xFF && huge[index+1] == 0xC0 {
			huge[index+5], huge[index+6], huge[index+7], huge[index+8] = 0x7F, 0xFF, 0x7F, 0xFF
			break
		}
	}
	cases := map[string][]byte{
		"empty":     nil,
		"png":       pngBytes.Bytes(),
		"truncated": photo[:len(photo)/2],
		"garbage":   []byte("definitely not an image"),
		"huge":      huge,
	}
	for name, input := range cases {
		if _, err := Render(context.Background(), input, emery); err == nil {
			t.Fatalf("%s was accepted", name)
		}
	}
	if _, err := Render(context.Background(), huge, emery); !errors.Is(err, ErrTooLarge) {
		t.Fatalf("oversized dimensions are refused before decoding, got %v", err)
	}
	if _, err := Render(context.Background(), make([]byte, MaxInputBytes+1), emery); !errors.Is(err, ErrTooLarge) {
		t.Fatalf("an oversized body is refused, got %v", err)
	}
	cancelled, cancel := context.WithCancel(context.Background())
	cancel()
	if _, err := Render(cancelled, encode(t, landscape(640, 480)), emery); err == nil {
		t.Fatal("a cancelled render finished")
	}
}

func TestLargeSourcesStayWithinTheirMemory(t *testing.T) {
	input := encode(t, screenshot(3000, 3000))
	runtime.GC()
	var before, after runtime.MemStats
	runtime.ReadMemStats(&before)
	result, err := Render(context.Background(), input, gabbro)
	runtime.ReadMemStats(&after)
	if err != nil {
		t.Fatal(err)
	}
	check(t, "large screenshot", result, gabbro)
	if result.Class != "graphic" {
		t.Fatalf("a reduced screenshot was classified as %s", result.Class)
	}
	if allocated := after.TotalAlloc - before.TotalAlloc; allocated > 48<<20 {
		t.Fatalf("a 9 megapixel render allocated %d MiB", allocated>>20)
	}
	wide := append([]byte(nil), input...)
	for index := 0; index+8 < len(wide); index++ {
		if wide[index] == 0xFF && wide[index+1] == 0xC0 {
			wide[index+5], wide[index+6], wide[index+7], wide[index+8] = 0x0F, 0xA0, 0x13, 0x88
			break
		}
	}
	if _, err := Render(context.Background(), wide, gabbro); !errors.Is(err, ErrTooLarge) {
		t.Fatalf("5000x4000 is over the pixel limit, got %v", err)
	}
}

func TestRenderingIsDeterministic(t *testing.T) {
	input := encode(t, landscape(800, 600))
	first, _ := Render(context.Background(), input, gabbro)
	second, _ := Render(context.Background(), input, gabbro)
	other, _ := Render(context.Background(), input, emery)
	if !bytes.Equal(first.Data, second.Data) || first.Tag != second.Tag || first.Tag == other.Tag {
		t.Fatal("the same input and spec must give the same bytes and tag, another spec another tag")
	}
}

func TestParseSpecIsStrict(t *testing.T) {
	good := []struct {
		width, height, budget int
		shape, formats        string
	}{{200, 228, 28672, "rect", "p4,p2"}, {260, 260, 2048, "round", "p2"}}
	for _, item := range good {
		if _, err := ParseSpec(item.width, item.height, item.shape, item.formats, item.budget); err != nil {
			t.Fatalf("%+v: %v", item, err)
		}
	}
	bad := []struct {
		width, height, budget int
		shape, formats        string
	}{{300, 228, 28672, "rect", "p4"}, {200, 228, 99999, "rect", "p4"}, {200, 228, 28672, "oval", "p4"}, {200, 228, 28672, "rect", "p8"},
		{200, 228, 28672, "rect", "p4,p4"}, {200, 228, 28672, "rect", ""}, {8, 228, 28672, "rect", "p4"}}
	for _, item := range bad {
		if _, err := ParseSpec(item.width, item.height, item.shape, item.formats, item.budget); err == nil {
			t.Fatalf("%+v was accepted", item)
		}
	}
}

func BenchmarkRenderPhotoForGabbro(b *testing.B) {
	input := encode(b, landscape(800, 800))
	spec := Spec{Width: 260, Height: 260, Shape: tbi.ShapeRound, Budget: 28672, Formats: []int{4, 2}}
	b.ResetTimer()
	started := time.Now()
	for index := 0; index < b.N; index++ {
		if _, err := Render(context.Background(), input, spec); err != nil {
			b.Fatal(err)
		}
	}
	b.ReportMetric(float64(time.Since(started).Milliseconds())/float64(b.N), "ms/render")
}

func FuzzRender(f *testing.F) {
	f.Add(encode(f, landscape(32, 24)))
	f.Add([]byte{0xFF, 0xD8, 0xFF})
	f.Fuzz(func(t *testing.T, input []byte) {
		result, err := Render(context.Background(), input, emery)
		if err == nil {
			check(t, "fuzz", result, emery)
		}
	})
}
