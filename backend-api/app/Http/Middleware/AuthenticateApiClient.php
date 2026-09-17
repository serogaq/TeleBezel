<?php

namespace App\Http\Middleware;

use App\Models\ApiClient;
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
        } catch (QueryException) {
            return $this->error($request, 503, 'service.database_unavailable');
        }

        if ($client === null || ! hash_equals($client->token_hash, $hash)) {
            return $this->error($request, 401, 'auth.unauthorized');
        }

        $request->attributes->set('api_client_id', $client->getKey());

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
