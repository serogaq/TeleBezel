// Package tbi writes and reads TBI1, the paletted image format the watch
// decodes straight into its drawing buffer.
package tbi

import (
	"encoding/binary"
	"errors"
	"hash/crc32"
)

const (
	HeaderSize = 22
	Version    = 1
	ShapeRect  = 0
	ShapeRound = 1
	MaxColors  = 16
	MaxCanvas  = 1024
	MaxBytes   = 1 << 20
)

// Image is an unpacked indexed image placed centred on a canvas.
type Image struct {
	Bits         int
	Shape        int
	Width        int
	Height       int
	CanvasWidth  int
	CanvasHeight int
	Tag          uint32
	Palette      []byte
	Pixels       []uint8
}

// Row is the stored part of one image row: the first column relative to the
// image and the number of pixels kept.
type Row struct {
	X     int
	Count int
	Bytes int
}

func isqrt(value uint32) uint32 {
	var root uint32
	bit := uint32(1) << 30
	for bit > value {
		bit >>= 2
	}
	for bit != 0 {
		if value >= root+bit {
			value -= root + bit
			root = (root >> 1) + bit
		} else {
			root >>= 1
		}
		bit >>= 2
	}
	return root
}

// RowAt reproduces the watch's geometry exactly: a round canvas keeps only
// the pixels inside the circle, with one pixel of slack on each side.
func RowAt(bits, shape, width, height, canvasWidth, canvasHeight, y int) Row {
	left := (canvasWidth - width) / 2
	top := (canvasHeight - height) / 2
	from, to := left, left+width
	if shape == ShapeRound {
		diameter := canvasWidth
		dy := 2*(top+y) + 1 - canvasHeight
		from, to = 0, 0
		if dy > -diameter && dy < diameter {
			half := int(isqrt(uint32(diameter*diameter - dy*dy)))
			from = (diameter-half-1)/2 - 1
			to = (diameter+half-1)/2 + 2
		}
		if from < left {
			from = left
		}
		if to > left+width {
			to = left + width
		}
	}
	count := 0
	if to > from {
		count = to - from
	}
	return Row{X: from - left, Count: count, Bytes: (count*bits + 7) / 8}
}

// Size is the number of bytes the watch allocates for the image.
func Size(bits, shape, width, height, canvasWidth, canvasHeight int) int {
	total := 0
	for y := 0; y < height; y++ {
		total += RowAt(bits, shape, width, height, canvasWidth, canvasHeight, y).Bytes
	}
	return total
}

// Pack stores the visible pixels of each row, most significant bits first.
func Pack(image *Image) []byte {
	out := make([]byte, 0, Size(image.Bits, image.Shape, image.Width, image.Height, image.CanvasWidth, image.CanvasHeight))
	mask := uint8(1<<image.Bits - 1)
	for y := 0; y < image.Height; y++ {
		row := RowAt(image.Bits, image.Shape, image.Width, image.Height, image.CanvasWidth, image.CanvasHeight, y)
		packed := make([]byte, row.Bytes)
		for index := 0; index < row.Count; index++ {
			value := image.Pixels[y*image.Width+row.X+index] & mask
			bit := index * image.Bits
			packed[bit>>3] |= value << (8 - image.Bits - bit&7)
		}
		out = append(out, packed...)
	}
	return out
}

// Compress emits literal runs, byte repeats and back-references into the
// already decoded output; the watch decodes it without any extra buffer.
func Compress(data []byte) []byte {
	const window = 4096
	out := make([]byte, 0, len(data)/2)
	literal := make([]byte, 0, 128)
	flush := func() {
		for len(literal) > 0 {
			part := literal
			if len(part) > 128 {
				part = part[:128]
			}
			out = append(out, byte(len(part)-1))
			out = append(out, part...)
			literal = literal[len(part):]
		}
		literal = literal[:0]
	}
	heads := make(map[uint32]int)
	previous := make([]int, len(data))
	key := func(position int) uint32 {
		return uint32(data[position]) | uint32(data[position+1])<<8 | uint32(data[position+2])<<16
	}
	insert := func(position int) {
		if position+2 >= len(data) {
			return
		}
		k := key(position)
		if head, ok := heads[k]; ok {
			previous[position] = head
		} else {
			previous[position] = -1
		}
		heads[k] = position
	}
	position := 0
	for position < len(data) {
		run := 1
		for position+run < len(data) && data[position+run] == data[position] && run < 66 {
			run++
		}
		best, distance := 0, 0
		if position+2 < len(data) {
			if candidate, ok := heads[key(position)]; ok {
				for steps := 0; candidate >= 0 && position-candidate <= window && steps < 64; steps++ {
					length := 0
					for position+length < len(data) && length < 66 && data[candidate+length] == data[position+length] {
						length++
					}
					if length > best {
						best, distance = length, position-candidate
					}
					candidate = previous[candidate]
				}
			}
		}
		switch {
		case run >= 3 && run >= best:
			flush()
			out = append(out, byte(0x80|(run-3)), data[position])
			for step := 0; step < run; step++ {
				insert(position + step)
			}
			position += run
		case best >= 4:
			flush()
			out = append(out, byte(0xC0|(best-3)), byte(distance), byte(distance>>8))
			for step := 0; step < best; step++ {
				insert(position + step)
			}
			position += best
		default:
			literal = append(literal, data[position])
			insert(position)
			position++
		}
	}
	flush()
	return out
}

// Encode writes the header, the palette and the compressed rows.
func Encode(image *Image) ([]byte, error) {
	if image.Bits != 1 && image.Bits != 2 && image.Bits != 4 {
		return nil, errors.New("unsupported bit depth")
	}
	if len(image.Palette) == 0 || len(image.Palette) > 1<<image.Bits || len(image.Palette) > MaxColors {
		return nil, errors.New("invalid palette")
	}
	if image.Width <= 0 || image.Height <= 0 || image.Width > image.CanvasWidth || image.Height > image.CanvasHeight ||
		len(image.Pixels) != image.Width*image.Height {
		return nil, errors.New("invalid geometry")
	}
	packed := Pack(image)
	header := make([]byte, HeaderSize, HeaderSize+len(image.Palette))
	header[0], header[1], header[2] = 'T', 'B', Version
	header[3], header[4], header[5] = byte(image.Bits), byte(image.Shape), byte(len(image.Palette))
	binary.LittleEndian.PutUint16(header[6:], uint16(image.Width))
	binary.LittleEndian.PutUint16(header[8:], uint16(image.Height))
	binary.LittleEndian.PutUint16(header[10:], uint16(image.CanvasWidth))
	binary.LittleEndian.PutUint16(header[12:], uint16(image.CanvasHeight))
	binary.LittleEndian.PutUint32(header[14:], image.Tag)
	binary.LittleEndian.PutUint32(header[18:], crc32.ChecksumIEEE(packed))
	header = append(header, image.Palette...)
	return append(header, Compress(packed)...), nil
}

// Header is the parsed fixed part of a TBI1 stream.
type Header struct {
	Bits, Shape, Colors                      int
	Width, Height, CanvasWidth, CanvasHeight int
	Tag, CRC                                 uint32
	Palette                                  []byte
}

// Decode checks a stream the way the watch does and returns the packed rows.
func Decode(stream []byte) (Header, []byte, error) {
	var header Header
	if len(stream) < HeaderSize || stream[0] != 'T' || stream[1] != 'B' || stream[2] != Version {
		return header, nil, errors.New("bad header")
	}
	header.Bits, header.Shape, header.Colors = int(stream[3]), int(stream[4]), int(stream[5])
	header.Width = int(binary.LittleEndian.Uint16(stream[6:]))
	header.Height = int(binary.LittleEndian.Uint16(stream[8:]))
	header.CanvasWidth = int(binary.LittleEndian.Uint16(stream[10:]))
	header.CanvasHeight = int(binary.LittleEndian.Uint16(stream[12:]))
	header.Tag = binary.LittleEndian.Uint32(stream[14:])
	header.CRC = binary.LittleEndian.Uint32(stream[18:])
	if (header.Bits != 1 && header.Bits != 2 && header.Bits != 4) || header.Shape > ShapeRound || header.Colors < 1 ||
		header.Colors > 1<<header.Bits || header.Width == 0 || header.Height == 0 || header.Width > header.CanvasWidth ||
		header.Height > header.CanvasHeight || header.CanvasWidth > MaxCanvas || header.CanvasHeight > MaxCanvas ||
		len(stream) < HeaderSize+header.Colors {
		return header, nil, errors.New("bad header")
	}
	header.Palette = stream[HeaderSize : HeaderSize+header.Colors]
	size := Size(header.Bits, header.Shape, header.Width, header.Height, header.CanvasWidth, header.CanvasHeight)
	if size > MaxBytes {
		return header, nil, errors.New("bad header")
	}
	out := make([]byte, 0, size)
	body := stream[HeaderSize+header.Colors:]
	for position := 0; position < len(body); {
		op := body[position]
		position++
		switch {
		case op < 0x80:
			count := int(op) + 1
			if position+count > len(body) || len(out)+count > size {
				return header, nil, errors.New("bad literal")
			}
			out = append(out, body[position:position+count]...)
			position += count
		case op < 0xC0:
			count := int(op&0x3F) + 3
			if position >= len(body) || len(out)+count > size {
				return header, nil, errors.New("bad repeat")
			}
			for step := 0; step < count; step++ {
				out = append(out, body[position])
			}
			position++
		default:
			count := int(op&0x3F) + 3
			if position+2 > len(body) {
				return header, nil, errors.New("bad reference")
			}
			distance := int(body[position]) | int(body[position+1])<<8
			position += 2
			if distance == 0 || distance > len(out) || len(out)+count > size {
				return header, nil, errors.New("bad reference")
			}
			for step := 0; step < count; step++ {
				out = append(out, out[len(out)-distance])
			}
		}
	}
	if len(out) != size || crc32.ChecksumIEEE(out) != header.CRC {
		return header, nil, errors.New("bad data")
	}
	return header, out, nil
}
