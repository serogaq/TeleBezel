<?php

declare(strict_types=1);

namespace App\Infrastructure\Media;

use App\Contracts\MediaRenderer;
use App\Data\MediaSpec;
use App\Exceptions\ApiException;
use Illuminate\Http\Client\ConnectionException;
use Illuminate\Support\Facades\Http;

final class HttpMediaRenderer implements MediaRenderer
{
    public function render(string $source, MediaSpec $spec, string $requestId): string
    {
        $token = config('telebezel.media.token');
        if (! is_string($token) || $token === '') {
            throw new ApiException('service.media_unavailable', 503);
        }
        try {
            $response = Http::baseUrl(config()->string('telebezel.media.base_url'))->withToken($token)->withHeaders([
                'X-Request-ID' => $requestId,
            ])->connectTimeout(1)->timeout(5)->withBody($source, 'image/jpeg')->post('/internal/v1/render?'.http_build_query($spec->query()));
        } catch (ConnectionException) {
            throw new ApiException('service.media_unavailable', 503);
        }
        if ($response->successful()) {
            return $response->body();
        }
        $error = $response->json('error.code');
        if ($response->status() === 422 && in_array($error, ['media.unsupported', 'media.too_large'], true)) {
            throw new ApiException('media.unsupported', 422);
        }
        if ($response->status() === 503 && in_array($error, ['service.busy', 'media.deadline'], true)) {
            throw new ApiException('media.busy', 503, 1);
        }
        throw new ApiException('service.media_unavailable', 503);
    }
}
