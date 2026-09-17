<?php

namespace App\Http\Middleware;

use Closure;
use Illuminate\Database\QueryException;
use Illuminate\Http\Request;
use Illuminate\Support\Facades\RateLimiter;
use Symfony\Component\HttpFoundation\Response;

final class RateLimitApiClient
{
    public function handle(Request $request, Closure $next): Response
    {
        $key = 'public-api:'.$request->attributes->get('api_client_id');
        try {
            if (RateLimiter::tooManyAttempts($key, 60)) {
                return response()->json([
                    'error' => ['code' => 'rate_limit.exceeded'],
                    'request_id' => $request->attributes->get('request_id'),
                ], 429, [
                    'Cache-Control' => 'no-store',
                    'Retry-After' => (string) RateLimiter::availableIn($key),
                ]);
            }
            RateLimiter::hit($key, 60);
        } catch (QueryException) {
            return response()->json([
                'error' => ['code' => 'service.database_unavailable'],
                'request_id' => $request->attributes->get('request_id'),
            ], 503, ['Cache-Control' => 'no-store']);
        }

        return $next($request);
    }
}
