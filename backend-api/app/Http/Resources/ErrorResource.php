<?php

declare(strict_types=1);

namespace App\Http\Resources;

use App\Exceptions\ApiException;
use App\Http\RequestContext;
use Illuminate\Http\JsonResponse;
use Illuminate\Http\Request;

final class ErrorResource
{
    public static function respond(ApiException $exception, Request $request): JsonResponse
    {
        $requestId = RequestContext::requestId($request);
        $headers = [
            'Cache-Control' => 'no-store',
            'X-Request-ID' => $requestId,
        ];
        if ($exception->retryAfter !== null) {
            $headers['Retry-After'] = (string) $exception->retryAfter;
        }

        return response()->json([
            'error' => [
                'code' => $exception->errorCode,
            ],
            'request_id' => $requestId,
        ], $exception->status, $headers);
    }
}
