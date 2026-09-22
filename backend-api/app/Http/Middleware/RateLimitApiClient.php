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

final class RateLimitApiClient
{
    /** @param Closure(Request): Response $next */
    public function handle(Request $request, Closure $next): Response
    {
        $key = RateLimitCacheKeys::publicApi(RequestContext::principal($request)->id);
        if (RateLimiter::tooManyAttempts($key, 60)) {
            throw new ApiException('rate_limit.exceeded', 429, RateLimiter::availableIn($key));
        }
        RateLimiter::hit($key, 60);

        return $next($request);
    }
}
