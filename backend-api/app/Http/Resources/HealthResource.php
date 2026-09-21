<?php

declare(strict_types=1);

namespace App\Http\Resources;

use Illuminate\Http\JsonResponse;

final class HealthResource
{
    public static function live(): JsonResponse
    {
        return response()->json([
            'status' => 'ok',
            'service' => 'backend-api',
            'version' => config()->string('telebezel.version'),
        ]);
    }

    public static function readiness(bool $ready): JsonResponse
    {
        return response()->json([
            'status' => $ready ? 'ready' : 'not_ready',
        ], $ready ? 200 : 503, [
            'Cache-Control' => 'no-store',
        ]);
    }
}
