<?php

declare(strict_types=1);

namespace App\Support;

use App\Data\MediaSpec;

// Reads the header of a TBI1 image and checks it against the watch it was
// made for, before the image is cached or sent anywhere.
final class Tbi
{
    public const int HEADER = 22;

    public const int MAX_STREAM = 65536;

    /** @return array{width: int, height: int, bits: int, shape: string, tag: int, crc: int, size: int}|null */
    public static function inspect(string $stream, MediaSpec $spec): ?array
    {
        if (strlen($stream) < self::HEADER + 1 || strlen($stream) > self::MAX_STREAM || ! str_starts_with($stream, "TB\x01")) {
            return null;
        }
        $header = unpack('Cbits/Cshape/Ccolors/vwidth/vheight/vcanvasWidth/vcanvasHeight/Vtag/Vcrc', $stream, 3);
        if (! is_array($header)) {
            return null;
        }
        $value = static fn (string $key): int => is_int($header[$key] ?? null) ? $header[$key] : -1;
        $bits = $value('bits');
        $shape = $value('shape') === 1 ? 'round' : ($value('shape') === 0 ? 'rect' : '');
        $colors = $value('colors');
        $width = $value('width');
        $height = $value('height');
        if (! in_array($bits, [1, 2, 4], true) || ! $spec->allows($bits) || $shape !== $spec->shape || $colors < 1 || $colors > 1 << $bits
            || $width < 1 || $height < 1 || $width > $spec->width || $height > $spec->height || $value('canvasWidth') !== $spec->width
            || $value('canvasHeight') !== $spec->height || strlen($stream) < self::HEADER + $colors) {
            return null;
        }
        $size = self::size($bits, $shape === 'round', $width, $height, $spec->width, $spec->height);
        if ($size < 1 || $size > $spec->budget) {
            return null;
        }

        return [
            'width' => $width,
            'height' => $height,
            'bits' => $bits,
            'shape' => $shape,
            'tag' => $value('tag'),
            'crc' => $value('crc'),
            'size' => $size,
        ];
    }

    // The same row spans the watch computes: a round canvas keeps only what
    // lies inside the circle.
    public static function size(int $bits, bool $round, int $width, int $height, int $canvasWidth, int $canvasHeight): int
    {
        $left = intdiv($canvasWidth - $width, 2);
        $top = intdiv($canvasHeight - $height, 2);
        $total = 0;
        for ($y = 0; $y < $height; $y++) {
            $from = $left;
            $to = $left + $width;
            if ($round) {
                $diameter = $canvasWidth;
                $dy = 2 * ($top + $y) + 1 - $canvasHeight;
                $from = 0;
                $to = 0;
                if ($dy > -$diameter && $dy < $diameter) {
                    $half = (int) floor(sqrt($diameter * $diameter - $dy * $dy));
                    while (($half + 1) * ($half + 1) <= $diameter * $diameter - $dy * $dy) {
                        $half++;
                    }
                    while ($half * $half > $diameter * $diameter - $dy * $dy) {
                        $half--;
                    }
                    $from = intdiv($diameter - $half - 1, 2) - 1;
                    $to = intdiv($diameter + $half - 1, 2) + 2;
                }
                $from = max($from, $left);
                $to = min($to, $left + $width);
            }
            $total += intdiv(max(0, $to - $from) * $bits + 7, 8);
        }

        return $total;
    }
}
