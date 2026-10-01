<?php

declare(strict_types=1);

namespace App\Contracts;

use App\Data\MediaSpec;

interface MediaRenderer
{
    /** Turns a source JPEG into a TBI1 image for the watch described by the spec. */
    public function render(string $source, MediaSpec $spec, string $requestId): string;
}
