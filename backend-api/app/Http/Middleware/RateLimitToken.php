<?php

declare(strict_types=1);

namespace App\Http\Middleware;

use App\Cache\RateLimitCacheKeys;
use App\Exceptions\ApiException;
use App\Http\RequestContext;
use Closure;
use Illuminate\Http\Request;
use Illuminate\Support\Facades\RateLimiter;
use Symfony\Component\HttpFoundation\Response;

final class RateLimitToken
{
    /** @param Closure(Request): Response $next */
    public function handle(Request $request, Closure $next): Response
    {
        $principal = RequestContext::principal($request);
        $maximum = $principal->type === 'device' ? 60 : 240;
        $key = RateLimitCacheKeys::publicApi($principal->id);
        if (RateLimiter::tooManyAttempts($key, $maximum)) {
            throw new ApiException('rate_limit.exceeded', 429, RateLimiter::availableIn($key));
        }
        RateLimiter::hit($key, 60);

        return $next($request);
    }
}
