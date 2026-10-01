package tbi

import (
	"bytes"
	"encoding/json"
	"hash/crc32"
	"math/rand"
	"os"
	"path/filepath"
	"testing"
)

const contracts = "../../../tests/contracts/media"

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

func manifests(t *testing.T) []vector {
	t.Helper()
	if _, err := os.Stat(contracts); os.IsNotExist(err) && os.Getenv("TELEBEZEL_REQUIRE_CONTRACTS") == "" {
		t.Skip("the shared contract vectors live outside this build context")
	}
	files, err := filepath.Glob(filepath.Join(contracts, "manifest*.json"))
	if err != nil || len(files) == 0 {
		t.Fatalf("no media contract manifests: %v", err)
	}
	var vectors []vector
	for _, file := range files {
		raw, err := os.ReadFile(file)
		if err != nil {
			t.Fatal(err)
		}
		var list []vector
		if err := json.Unmarshal(raw, &list); err != nil {
			t.Fatal(err)
		}
		vectors = append(vectors, list...)
	}
	return vectors
}

func TestContractVectorsDecodeLikeTheWatch(t *testing.T) {
	for _, item := range manifests(t) {
		stream, err := os.ReadFile(filepath.Join(contracts, item.Name+".tbi"))
		if err != nil {
			t.Fatal(err)
		}
		header, packed, err := Decode(stream)
		if err != nil {
			t.Fatalf("%s: %v", item.Name, err)
		}
		if header.Width != item.Width || header.Height != item.Height || header.Tag != item.Tag || header.Bits != item.Bits ||
			header.Shape != item.Shape || len(packed) != item.Size || crc32.ChecksumIEEE(packed) != item.CRC {
			t.Fatalf("%s: decoded %+v with %d bytes", item.Name, header, len(packed))
		}
	}
}

func TestRoundGeometryMatchesTheWatch(t *testing.T) {
	if row := RowAt(4, ShapeRound, 260, 260, 260, 260, 130); row.X != 0 || row.Count != 260 {
		t.Fatalf("middle row %+v", row)
	}
	if row := RowAt(4, ShapeRound, 260, 260, 260, 260, 0); row.Count <= 0 || row.Count >= 60 || row.X <= 100 {
		t.Fatalf("top row %+v", row)
	}
	if row := RowAt(4, ShapeRect, 120, 80, 200, 228, 3); row.X != 0 || row.Count != 120 || row.Bytes != 60 {
		t.Fatalf("rect row %+v", row)
	}
	if Size(4, ShapeRound, 260, 260, 260, 260) >= Size(4, ShapeRect, 260, 260, 260, 260) {
		t.Fatal("a round canvas must store fewer bytes than its square")
	}
}

func TestEncodeDecodeRoundTrip(t *testing.T) {
	random := rand.New(rand.NewSource(7))
	for _, shape := range []int{ShapeRect, ShapeRound} {
		for _, bits := range []int{1, 2, 4} {
			for _, size := range [][2]int{{1, 1}, {13, 7}, {200, 228}, {260, 146}, {180, 260}} {
				image := &Image{Bits: bits, Shape: shape, Width: size[0], Height: size[1], CanvasWidth: 260, CanvasHeight: 260, Tag: 9,
					Palette: []byte{0xC0, 0xFF, 0xC3, 0xF0}[:min(4, 1<<bits)], Pixels: make([]uint8, size[0]*size[1])}
				for index := range image.Pixels {
					if random.Intn(3) == 0 {
						image.Pixels[index] = uint8(random.Intn(len(image.Palette)))
					} else if index > 0 {
						image.Pixels[index] = image.Pixels[index-1]
					}
				}
				stream, err := Encode(image)
				if err != nil {
					t.Fatal(err)
				}
				_, packed, err := Decode(stream)
				if err != nil {
					t.Fatalf("shape %d bits %d size %v: %v", shape, bits, size, err)
				}
				if !bytes.Equal(packed, Pack(image)) {
					t.Fatalf("shape %d bits %d size %v: pixels differ", shape, bits, size)
				}
			}
		}
	}
}

func TestEncodeRejectsInvalidImages(t *testing.T) {
	cases := []*Image{
		{Bits: 3, Width: 1, Height: 1, CanvasWidth: 1, CanvasHeight: 1, Palette: []byte{0xC0}, Pixels: []uint8{0}},
		{Bits: 2, Width: 1, Height: 1, CanvasWidth: 1, CanvasHeight: 1, Palette: []byte{1, 2, 3, 4, 5}, Pixels: []uint8{0}},
		{Bits: 4, Width: 2, Height: 1, CanvasWidth: 1, CanvasHeight: 1, Palette: []byte{0xC0}, Pixels: []uint8{0, 0}},
		{Bits: 4, Width: 1, Height: 1, CanvasWidth: 1, CanvasHeight: 1, Palette: []byte{0xC0}, Pixels: nil},
	}
	for index, image := range cases {
		if _, err := Encode(image); err == nil {
			t.Fatalf("case %d was accepted", index)
		}
	}
}

func TestDecodeRejectsCorruption(t *testing.T) {
	image := &Image{Bits: 4, Shape: ShapeRect, Width: 20, Height: 20, CanvasWidth: 20, CanvasHeight: 20, Palette: []byte{0xC0, 0xFF},
		Pixels: make([]uint8, 400)}
	stream, _ := Encode(image)
	for _, broken := range [][]byte{stream[:len(stream)-1], append(append([]byte(nil), stream...), 0), stream[:10]} {
		if _, _, err := Decode(broken); err == nil {
			t.Fatal("a broken stream was accepted")
		}
	}
	reference := append(append([]byte(nil), stream[:HeaderSize+2]...), 0xC1, 0x09, 0x00)
	if _, _, err := Decode(reference); err == nil {
		t.Fatal("a reference before the start was accepted")
	}
}

func FuzzDecode(f *testing.F) {
	image := &Image{Bits: 4, Shape: ShapeRound, Width: 30, Height: 20, CanvasWidth: 40, CanvasHeight: 40, Palette: []byte{0xC0, 0xFF, 0xF0},
		Pixels: make([]uint8, 600)}
	stream, _ := Encode(image)
	f.Add(stream)
	f.Add([]byte("TB\x01"))
	f.Fuzz(func(t *testing.T, data []byte) {
		header, packed, err := Decode(data)
		if err == nil && len(packed) != Size(header.Bits, header.Shape, header.Width, header.Height, header.CanvasWidth, header.CanvasHeight) {
			t.Fatal("decoded size differs from the declared geometry")
		}
	})
}
