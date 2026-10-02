// Command media-samples renders sample images the way the watch shows them
// and writes the Go encoder's contract vectors for the watch decoder tests.
package main

import (
	"bytes"
	"context"
	"encoding/json"
	"flag"
	"fmt"
	"hash/crc32"
	"image"
	"image/color"
	"image/draw"
	"image/jpeg"
	"image/png"
	"math"
	"os"
	"path/filepath"
	"sort"
	"strings"

	"github.com/serogaq/TeleBezel/backend-media/internal/render"
	"github.com/serogaq/TeleBezel/backend-media/internal/tbi"
)

type watch struct {
	name string
	spec render.Spec
}

var watches = []watch{
	{"emery", render.Spec{Width: 200, Height: 228, Shape: tbi.ShapeRect, Budget: 28672, Formats: []int{4, 2}}},
	{"gabbro", render.Spec{Width: 260, Height: 260, Shape: tbi.ShapeRound, Budget: 21504, Formats: []int{4, 2}}},
}

func pebble(value byte) color.RGBA {
	level := func(bits byte) uint8 { return bits * 85 }
	return color.RGBA{level(value >> 4 & 3), level(value >> 2 & 3), level(value & 3), 255}
}

// screen draws a TBI stream on a black screen exactly as the watch does.
func screen(stream []byte) (*image.RGBA, error) {
	header, packed, err := tbi.Decode(stream)
	if err != nil {
		return nil, err
	}
	out := image.NewRGBA(image.Rect(0, 0, header.CanvasWidth, header.CanvasHeight))
	draw.Draw(out, out.Bounds(), &image.Uniform{color.Black}, image.Point{}, draw.Src)
	left := (header.CanvasWidth - header.Width) / 2
	top := (header.CanvasHeight - header.Height) / 2
	offset := 0
	for y := 0; y < header.Height; y++ {
		row := tbi.RowAt(header.Bits, header.Shape, header.Width, header.Height, header.CanvasWidth, header.CanvasHeight, y)
		for index := 0; index < row.Count; index++ {
			bit := index * header.Bits
			value := packed[offset+bit>>3] >> (8 - header.Bits - bit&7) & (1<<header.Bits - 1)
			out.Set(left+row.X+index, top+y, pebble(header.Palette[value]))
		}
		offset += row.Bytes
	}
	if header.Shape == tbi.ShapeRound {
		radius := float64(header.CanvasWidth) / 2
		for y := 0; y < header.CanvasHeight; y++ {
			for x := 0; x < header.CanvasWidth; x++ {
				if math.Hypot(float64(x)+0.5-radius, float64(y)+0.5-radius) > radius {
					out.Set(x, y, color.RGBA{128, 128, 128, 255})
				}
			}
		}
	}
	return out, nil
}

type vector struct {
	Name         string `json:"name"`
	Bits         int    `json:"bits"`
	Shape        int    `json:"shape"`
	Width        int    `json:"width"`
	Height       int    `json:"height"`
	CanvasWidth  int    `json:"canvasWidth"`
	CanvasHeight int    `json:"canvasHeight"`
	Tag          uint32 `json:"tag"`
	Size         int    `json:"size"`
	CRC          uint32 `json:"crc"`
}

func synthetic() []byte {
	img := image.NewRGBA(image.Rect(0, 0, 640, 480))
	for y := 0; y < 480; y++ {
		for x := 0; x < 640; x++ {
			img.Set(x, y, color.RGBA{uint8(x * 255 / 640), uint8(y * 255 / 480), uint8((x + y) % 256), 255})
		}
	}
	var buffer bytes.Buffer
	_ = jpeg.Encode(&buffer, img, &jpeg.Options{Quality: 90})
	return buffer.Bytes()
}

func vectors(directory string) error {
	var manifest []vector
	input := synthetic()
	for _, item := range watches {
		for _, bits := range []int{4, 2} {
			spec := item.spec
			spec.Formats = []int{bits}
			spec.Budget = 28672
			result, err := render.Render(context.Background(), input, spec)
			if err != nil {
				return err
			}
			header, packed, err := tbi.Decode(result.Data)
			if err != nil {
				return err
			}
			name := fmt.Sprintf("go_%s_p%d", item.name, bits)
			if err := os.WriteFile(filepath.Join(directory, name+".tbi"), result.Data, 0o644); err != nil {
				return err
			}
			manifest = append(manifest, vector{name, header.Bits, header.Shape, header.Width, header.Height, header.CanvasWidth,
				header.CanvasHeight, header.Tag, len(packed), crc32.ChecksumIEEE(packed)})
		}
	}
	encoded, _ := json.MarshalIndent(manifest, "", "  ")
	return os.WriteFile(filepath.Join(directory, "manifest-go.json"), append(encoded, '\n'), 0o644)
}

func sheet(inputs []string, output string) error {
	sort.Strings(inputs)
	const cell = 280
	out := image.NewRGBA(image.Rect(0, 0, cell*len(watches), cell*len(inputs)))
	draw.Draw(out, out.Bounds(), &image.Uniform{color.RGBA{64, 64, 64, 255}}, image.Point{}, draw.Src)
	for row, path := range inputs {
		data, err := os.ReadFile(path)
		if err != nil {
			return err
		}
		for column, item := range watches {
			result, err := render.Render(context.Background(), data, item.spec)
			if err != nil {
				fmt.Fprintf(os.Stderr, "%s on %s: %v\n", filepath.Base(path), item.name, err)
				continue
			}
			picture, err := screen(result.Data)
			if err != nil {
				return err
			}
			fmt.Printf("%s %s: %dx%d p%d %s %d bytes\n", filepath.Base(path), item.name, result.Width, result.Height, result.Bits, result.Class, len(result.Data))
			draw.Draw(out, picture.Bounds().Add(image.Pt(column*cell+10, row*cell+10)), picture, image.Point{}, draw.Src)
		}
	}
	file, err := os.Create(output)
	if err != nil {
		return err
	}
	defer file.Close()
	return png.Encode(file, out)
}

func main() {
	contracts := flag.String("vectors", "", "write the Go contract vectors into this directory")
	output := flag.String("sheet", "", "write a contact sheet of the given JPEG files to this PNG")
	flag.Parse()
	if *contracts != "" {
		if err := vectors(*contracts); err != nil {
			fmt.Fprintln(os.Stderr, err)
			os.Exit(1)
		}
	}
	if *output != "" {
		var inputs []string
		for _, argument := range flag.Args() {
			if strings.HasSuffix(strings.ToLower(argument), ".jpg") || strings.HasSuffix(strings.ToLower(argument), ".jpeg") {
				inputs = append(inputs, argument)
			}
		}
		if err := sheet(inputs, *output); err != nil {
			fmt.Fprintln(os.Stderr, err)
			os.Exit(1)
		}
	}
}
