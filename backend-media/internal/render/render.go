// Package render turns an untrusted JPEG into a TBI1 image for the watch.
package render

import (
	"bytes"
	"context"
	"crypto/sha256"
	"encoding/binary"
	"errors"
	"fmt"
	"image"
	"image/color"
	"image/jpeg"
	"math"
	"sort"
	"strings"

	"github.com/serogaq/TeleBezel/backend-media/internal/tbi"
)

// PipelineVersion changes whenever the same input would render differently,
// so caches keyed by it never return an older rendition.
const PipelineVersion = "1"

const (
	MaxInputBytes = 5 << 20
	MaxSide       = 8192
	MaxPixels     = 16_000_000
	MaxBudget     = 28672
	MinBudget     = 2048
	MaxCanvas     = 260
)

var (
	ErrUnsupported = errors.New("media.unsupported")
	ErrInvalid     = errors.New("request.invalid")
	ErrTooLarge    = errors.New("media.too_large")
)

// Spec describes the watch screen and memory the image must fit.
type Spec struct {
	Width   int
	Height  int
	Shape   int
	Budget  int
	Formats []int
}

// ParseSpec validates the query the API forwards; anything unexpected is
// refused instead of clamped, so a caller cannot probe unusual code paths.
func ParseSpec(width, height int, shape, formats string, budget int) (Spec, error) {
	spec := Spec{Width: width, Height: height, Budget: budget}
	if width < 16 || width > MaxCanvas || height < 16 || height > MaxCanvas || budget < MinBudget || budget > MaxBudget {
		return spec, ErrInvalid
	}
	switch shape {
	case "rect":
		spec.Shape = tbi.ShapeRect
	case "round":
		spec.Shape = tbi.ShapeRound
	default:
		return spec, ErrInvalid
	}
	seen := map[int]bool{}
	for _, name := range strings.Split(formats, ",") {
		bits := map[string]int{"p4": 4, "p2": 2, "p1": 1}[name]
		if bits == 0 || seen[bits] {
			return spec, ErrInvalid
		}
		seen[bits] = true
		spec.Formats = append(spec.Formats, bits)
	}
	sort.Sort(sort.Reverse(sort.IntSlice(spec.Formats)))
	return spec, nil
}

// Result is the encoded image and what was chosen for it.
type Result struct {
	Data   []byte
	Width  int
	Height int
	Bits   int
	Class  string
	Tag    uint32
}

// Render decodes, fits, quantises and encodes. It never panics on bad input:
// a decoder failure of any kind becomes ErrUnsupported.
func Render(ctx context.Context, input []byte, spec Spec) (result Result, err error) {
	defer func() {
		if recovered := recover(); recovered != nil {
			result, err = Result{}, ErrUnsupported
		}
	}()
	if len(input) == 0 || len(input) > MaxInputBytes {
		return result, ErrTooLarge
	}
	config, format, err := image.DecodeConfig(bytes.NewReader(input))
	if err != nil || format != "jpeg" {
		return result, ErrUnsupported
	}
	if config.Width <= 0 || config.Height <= 0 || config.Width > MaxSide || config.Height > MaxSide ||
		config.Width*config.Height > MaxPixels {
		return result, ErrTooLarge
	}
	decoded, err := jpeg.Decode(bytes.NewReader(input))
	if err != nil {
		return result, ErrUnsupported
	}
	if err := ctx.Err(); err != nil {
		return result, err
	}
	width, height := fit(config.Width, config.Height, spec.Width, spec.Height)
	source, class := linear(decoded)
	plans := plan(spec, class)
	digest := sha256.Sum256(append(append([]byte(PipelineVersion+"|"), input...), []byte(fmt.Sprintf("|%+v", spec))...))
	tag := binary.LittleEndian.Uint32(digest[:4]) | 1
	for _, choice := range plans {
		w := int(math.Max(1, math.Floor(float64(width)*choice.scale)))
		h := int(math.Max(1, math.Floor(float64(height)*choice.scale)))
		if tbi.Size(choice.bits, spec.Shape, w, h, spec.Width, spec.Height) > spec.Budget {
			continue
		}
		if err := ctx.Err(); err != nil {
			return result, err
		}
		pixels := resample(source, w, h)
		palette := choosePalette(pixels, 1<<choice.bits, class == "graphic")
		indices := quantize(pixels, w, h, palette, class == "photo")
		data, err := tbi.Encode(&tbi.Image{Bits: choice.bits, Shape: spec.Shape, Width: w, Height: h, CanvasWidth: spec.Width,
			CanvasHeight: spec.Height, Tag: tag, Palette: pebbleBytes(palette), Pixels: indices})
		if err != nil {
			return result, err
		}
		return Result{Data: data, Width: w, Height: h, Bits: choice.bits, Class: class, Tag: tag}, nil
	}
	return result, ErrTooLarge
}

type choice struct {
	bits  int
	scale float64
}

// plan orders the candidate renditions. Photos keep colour and give up a
// little size first; text and diagrams keep their size and give up colours.
func plan(spec Spec, class string) []choice {
	allowed := map[int]bool{}
	for _, bits := range spec.Formats {
		allowed[bits] = true
	}
	var plans []choice
	add := func(bits int, from, to float64) {
		if !allowed[bits] {
			return
		}
		for scale := from; scale >= to-1e-9; scale -= 0.05 {
			plans = append(plans, choice{bits, scale})
		}
	}
	if class == "photo" {
		add(4, 1, 0.75)
		add(2, 1, 0.75)
		add(4, 0.7, 0.1)
		add(2, 0.7, 0.1)
	} else {
		add(4, 1, 1)
		add(2, 1, 0.75)
		add(4, 0.95, 0.75)
		add(2, 0.7, 0.1)
		add(4, 0.7, 0.1)
	}
	add(1, 1, 0.1)
	return plans
}

func fit(width, height, boxWidth, boxHeight int) (int, int) {
	scale := math.Min(float64(boxWidth)/float64(width), float64(boxHeight)/float64(height))
	w := int(math.Max(1, math.Min(float64(boxWidth), math.Floor(float64(width)*scale))))
	h := int(math.Max(1, math.Min(float64(boxHeight), math.Floor(float64(height)*scale))))
	return w, h
}

type plane struct {
	width, height int
	rgb           []float32
}

var toLinear = func() (table [256]float32) {
	for index := range table {
		value := float64(index) / 255
		if value <= 0.04045 {
			table[index] = float32(value / 12.92)
		} else {
			table[index] = float32(math.Pow((value+0.055)/1.055, 2.4))
		}
	}
	return table
}()

func toSRGB(value float32) float32 {
	if value <= 0 {
		return 0
	}
	if value >= 1 {
		return 1
	}
	if value <= 0.0031308 {
		return value * 12.92
	}
	return float32(1.055*math.Pow(float64(value), 1/2.4) - 0.055)
}

const planeLimit = 2_000_000

var shade = func() (table [256]uint32) {
	for index := range table {
		table[index] = uint32(toSRGB(toLinear[index]) * 63)
	}
	return table
}()

func pixels(img image.Image) func(x, y int) (uint8, uint8, uint8) {
	bounds := img.Bounds()
	switch source := img.(type) {
	case *image.YCbCr:
		return func(x, y int) (uint8, uint8, uint8) {
			yi := source.YOffset(bounds.Min.X+x, bounds.Min.Y+y)
			ci := source.COffset(bounds.Min.X+x, bounds.Min.Y+y)
			return color.YCbCrToRGB(source.Y[yi], source.Cb[ci], source.Cr[ci])
		}
	case *image.Gray:
		return func(x, y int) (uint8, uint8, uint8) {
			value := source.Pix[source.PixOffset(bounds.Min.X+x, bounds.Min.Y+y)]
			return value, value, value
		}
	default:
		return func(x, y int) (uint8, uint8, uint8) {
			r, g, b, _ := img.At(bounds.Min.X+x, bounds.Min.Y+y).RGBA()
			return uint8(r >> 8), uint8(g >> 8), uint8(b >> 8)
		}
	}
}

// linear converts once into linear light so averaging does not darken edges.
func linear(img image.Image) (plane, string) {
	bounds := img.Bounds()
	width, height := bounds.Dx(), bounds.Dy()
	factor := 1
	for ((width+factor-1)/factor)*((height+factor-1)/factor) > planeLimit {
		factor++
	}
	result := plane{width: (width + factor - 1) / factor, height: (height + factor - 1) / factor}
	result.rgb = make([]float32, result.width*result.height*3)
	at := pixels(img)
	step := max(1, int(math.Sqrt(float64(width*height)/40000)))
	colours := map[uint32]bool{}
	flat, total := 0, 0
	for y := 0; y < height; y++ {
		row := (y / factor) * result.width
		sampled := y%step == 0
		var previous uint32
		for x := 0; x < width; x++ {
			r, g, b := at(x, y)
			offset := (row + x/factor) * 3
			result.rgb[offset] += toLinear[r]
			result.rgb[offset+1] += toLinear[g]
			result.rgb[offset+2] += toLinear[b]
			if sampled && x%step == 0 {
				key := shade[r]<<12 | shade[g]<<6 | shade[b]
				colours[key] = true
				if x > 0 && key == previous {
					flat++
				}
				previous = key
				total++
			}
		}
	}
	if factor > 1 {
		for y := 0; y < result.height; y++ {
			rows := min(factor, height-y*factor)
			for x := 0; x < result.width; x++ {
				count := float32(rows * min(factor, width-x*factor))
				offset := (y*result.width + x) * 3
				result.rgb[offset] /= count
				result.rgb[offset+1] /= count
				result.rgb[offset+2] /= count
			}
		}
	}
	if total > 0 && float64(flat)/float64(total) > 0.6 && len(colours) < 512 {
		return result, "graphic"
	}
	return result, "photo"
}

// resample averages the source area behind each target pixel (downscaling)
// or interpolates bilinearly (upscaling), and returns sRGB values in 0..1.
func resample(source plane, width, height int) []float32 {
	out := make([]float32, width*height*3)
	scaleX := float64(source.width) / float64(width)
	scaleY := float64(source.height) / float64(height)
	for y := 0; y < height; y++ {
		for x := 0; x < width; x++ {
			var sum [3]float64
			if scaleX >= 1 && scaleY >= 1 {
				x0, x1 := int(float64(x)*scaleX), int(math.Ceil(float64(x+1)*scaleX))
				y0, y1 := int(float64(y)*scaleY), int(math.Ceil(float64(y+1)*scaleY))
				x1, y1 = min(x1, source.width), min(y1, source.height)
				count := 0
				for sy := y0; sy < y1; sy++ {
					row := sy * source.width * 3
					for sx := x0; sx < x1; sx++ {
						offset := row + sx*3
						sum[0] += float64(source.rgb[offset])
						sum[1] += float64(source.rgb[offset+1])
						sum[2] += float64(source.rgb[offset+2])
						count++
					}
				}
				for channel := range sum {
					sum[channel] /= float64(max(count, 1))
				}
			} else {
				fx := math.Max(0, (float64(x)+0.5)*scaleX-0.5)
				fy := math.Max(0, (float64(y)+0.5)*scaleY-0.5)
				x0, y0 := int(fx), int(fy)
				x1, y1 := min(x0+1, source.width-1), min(y0+1, source.height-1)
				ax, ay := fx-float64(x0), fy-float64(y0)
				for channel := 0; channel < 3; channel++ {
					at := func(px, py int) float64 { return float64(source.rgb[(py*source.width+px)*3+channel]) }
					top := at(x0, y0)*(1-ax) + at(x1, y0)*ax
					bottom := at(x0, y1)*(1-ax) + at(x1, y1)*ax
					sum[channel] = top*(1-ay) + bottom*ay
				}
			}
			offset := (y*width + x) * 3
			for channel := 0; channel < 3; channel++ {
				out[offset+channel] = toSRGB(float32(sum[channel]))
			}
		}
	}
	return out
}

type rgb [3]float32

// The watch displays 64 colours: two bits for each channel.
var pebbleColours = func() (colours [64]rgb) {
	levels := [4]float32{0, 85.0 / 255, 170.0 / 255, 1}
	for index := range colours {
		colours[index] = rgb{levels[index>>4&3], levels[index>>2&3], levels[index&3]}
	}
	return colours
}()

func distance(a, b rgb) float32 {
	dr, dg, db := a[0]-b[0], a[1]-b[1], a[2]-b[2]
	mean := (a[0] + b[0]) / 2
	return (2+mean)*dr*dr + 4*dg*dg + (3-mean)*db*db
}

func nearest(value rgb, palette []int) (int, float32) {
	best, bestDistance := 0, float32(math.MaxFloat32)
	for index, colour := range palette {
		if d := distance(value, pebbleColours[colour]); d < bestDistance {
			best, bestDistance = index, d
		}
	}
	return best, bestDistance
}

var all = func() (indices []int) {
	for index := 0; index < 64; index++ {
		indices = append(indices, index)
	}
	return indices
}()

// choosePalette picks up to `size` of the 64 watch colours with a few rounds
// of k-means whose centres are snapped back to displayable colours.
func choosePalette(pixels []float32, size int, exact bool) []int {
	counts := make([]int, 64)
	for offset := 0; offset < len(pixels); offset += 3 {
		index, _ := nearest(rgb{pixels[offset], pixels[offset+1], pixels[offset+2]}, all)
		counts[index]++
	}
	var used []int
	for index, count := range counts {
		if count > 0 {
			used = append(used, index)
		}
	}
	sort.SliceStable(used, func(a, b int) bool { return counts[used[a]] > counts[used[b]] })
	if len(used) <= size {
		return used
	}
	palette := append([]int(nil), used[:size]...)
	if exact {
		return palette
	}
	for round := 0; round < 6; round++ {
		sums := make([][4]float64, size)
		for offset := 0; offset < len(pixels); offset += 3 {
			value := rgb{pixels[offset], pixels[offset+1], pixels[offset+2]}
			index, _ := nearest(value, palette)
			sums[index][0] += float64(value[0])
			sums[index][1] += float64(value[1])
			sums[index][2] += float64(value[2])
			sums[index][3]++
		}
		taken := map[int]bool{}
		next := make([]int, 0, size)
		for index := range palette {
			if sums[index][3] == 0 {
				continue
			}
			centre := rgb{float32(sums[index][0] / sums[index][3]), float32(sums[index][1] / sums[index][3]), float32(sums[index][2] / sums[index][3])}
			var candidates []int
			for _, colour := range all {
				if !taken[colour] {
					candidates = append(candidates, colour)
				}
			}
			chosen, _ := nearest(centre, candidates)
			taken[candidates[chosen]] = true
			next = append(next, candidates[chosen])
		}
		for _, colour := range used {
			if len(next) >= size {
				break
			}
			if !taken[colour] {
				taken[colour] = true
				next = append(next, colour)
			}
		}
		palette = next
	}
	return palette
}

// quantize maps pixels to palette indices; photos use serpentine
// Floyd-Steinberg with damped error, text and diagrams stay undithered.
func quantize(pixels []float32, width, height int, palette []int, dither bool) []uint8 {
	const damping = 0.8
	out := make([]uint8, width*height)
	work := append([]float32(nil), pixels...)
	for y := 0; y < height; y++ {
		forward := y%2 == 0
		for step := 0; step < width; step++ {
			x := step
			if !forward {
				x = width - 1 - step
			}
			offset := (y*width + x) * 3
			value := rgb{work[offset], work[offset+1], work[offset+2]}
			index, _ := nearest(value, palette)
			out[y*width+x] = uint8(index)
			if !dither {
				continue
			}
			chosen := pebbleColours[palette[index]]
			spread := func(dx, dy int, weight float32) {
				nx, ny := x+dx, y+dy
				if !forward {
					nx = x - dx
				}
				if nx < 0 || nx >= width || ny >= height {
					return
				}
				target := (ny*width + nx) * 3
				for channel := 0; channel < 3; channel++ {
					work[target+channel] += (value[channel] - chosen[channel]) * weight * damping
				}
			}
			spread(1, 0, 7.0/16)
			spread(-1, 1, 3.0/16)
			spread(0, 1, 5.0/16)
			spread(1, 1, 1.0/16)
		}
	}
	return out
}

func pebbleBytes(palette []int) []byte {
	out := make([]byte, len(palette))
	for index, colour := range palette {
		out[index] = byte(0xC0 | colour)
	}
	return out
}
