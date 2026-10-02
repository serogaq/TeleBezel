<?php

declare(strict_types=1);

namespace App\Data;

// What the watch can hold: its screen, whether the screen is round, how many
// bytes it can spare for the image and which bit depths it can draw.
final readonly class MediaSpec
{
    /** @param list<string> $formats */
    public function __construct(
        public int $width,
        public int $height,
        public string $shape,
        public int $budget,
        public array $formats,
    ) {}

    public static function fromInput(Input $input): self
    {
        $formats = array_values(array_unique(explode(',', $input->string('formats'))));
        usort($formats, fn (string $left, string $right): int => strcmp($right, $left));

        return new self($input->integer('width'), $input->integer('height'), $input->string('shape'), intdiv($input->integer('budget'), 1024) * 1024, $formats);
    }

    public function canonical(): string
    {
        return "{$this->width}x{$this->height}:{$this->shape}:{$this->budget}:".implode(',', $this->formats);
    }

    /** @return array<string, int|string> */
    public function query(): array
    {
        return [
            'width' => $this->width,
            'height' => $this->height,
            'shape' => $this->shape,
            'budget' => $this->budget,
            'formats' => implode(',', $this->formats),
        ];
    }

    public function allows(int $bits): bool
    {
        return in_array('p'.$bits, $this->formats, true);
    }
}
