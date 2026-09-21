<?php

namespace App\Http\Middleware;

use App\Models\ApiClient;
use App\Models\Device;
use Closure;
use Illuminate\Database\QueryException;
use Illuminate\Http\JsonResponse;
use Illuminate\Http\Request;
use Symfony\Component\HttpFoundation\Response;

final class AuthenticateApiClient
{
    public function handle(Request $request, Closure $next): Response
    {
        $token = $request->bearerToken();
        if (! is_string($token) || preg_match('/^tb_[A-Za-z0-9_-]{43}$/', $token) !== 1) {
            return $this->error($request, 401, 'auth.unauthorized');
        }

        try {
            $hash = hash('sha256', $token);
            $client = ApiClient::query()->where('token_hash', $hash)->whereNull('revoked_at')->first();
            $device = $client === null ? Device::query()->where('token_hash', $hash)->whereNull('revoked_at')->first() : null;
        } catch (QueryException) {
            return $this->error($request, 503, 'service.database_unavailable');
        }

        if ($client === null && $device === null) {
            return $this->error($request, 401, 'auth.unauthorized');
        }

        if ($client !== null) {
            $request->attributes->set('api_client_id', $client->getKey());
            $request->attributes->set('principal_type', 'api_client');
            $request->attributes->set('principal_id', $client->getKey());
        } else {
            $device->forceFill(['last_seen_at' => now()])->save();
            $request->attributes->set('device_id', $device->getKey());
            $request->attributes->set('instance_id', $device->instance_id);
            $request->attributes->set('principal_type', 'device');
            $request->attributes->set('principal_id', $device->getKey());
        }

        return $next($request);
    }

    private function error(Request $request, int $status, string $code): JsonResponse
    {
        return response()->json([
            'error' => ['code' => $code],
            'request_id' => $request->attributes->get('request_id'),
        ], $status, ['Cache-Control' => 'no-store']);
    }
}
