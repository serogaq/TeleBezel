<?php

declare(strict_types=1);

namespace App\Http\Resources;

use Illuminate\Http\JsonResponse;
use Illuminate\Http\Request;
use Illuminate\Http\Resources\Json\JsonResource;

/** Explicitly projected service data; never an Eloquent model or raw upstream response. */
class ApiResource extends JsonResource
{
    /** @param array<array-key, mixed> $data */
    public function __construct(private readonly array $data)
    {
        parent::__construct($data);
    }

    /** @return array<array-key, mixed> */
    public function toArray(Request $request): array
    {
        return $this->data;
    }

    public function respond(?string $requestId = null, int $status = 200): JsonResponse
    {
        $body = [
            'data' => $this->data,
        ];
        if ($requestId !== null) {
            $body['request_id'] = $requestId;
        }

        return response()->json($body, $status, [
            'Cache-Control' => 'no-store',
        ]);
    }
}
