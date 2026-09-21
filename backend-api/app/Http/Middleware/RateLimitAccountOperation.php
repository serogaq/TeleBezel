<?php

declare(strict_types=1);

namespace App\Http\Middleware;

use App\Cache\RateLimitCacheKeys;
use App\Exceptions\ApiException;
use App\Http\RequestContext;
use App\Support\Values;
use Closure;
use Illuminate\Http\Request;
use Illuminate\Support\Facades\RateLimiter;
use LogicException;
use Symfony\Component\HttpFoundation\Response;

final class RateLimitAccountOperation
{
    /** @param Closure(Request): Response $next */
    public function handle(Request $request, Closure $next, string $budget): Response
    {
        if ($budget === 'auth-check' && in_array($request->input('action'), ['submit_phone_number', 'submit_email_address', 'start_qr', 'resend_code'], true)) {
            $budget = 'auth-start';
        }
        [$maximum, $seconds] = match ($budget) {
            'create' => [5, 3600], 'auth-start' => [3, 600], 'auth-check', 'lifecycle' => [10, 600],
            default => throw new LogicException('Unknown rate-limit budget.'),
        };
        $subject = $budget === 'create' ? RequestContext::principal($request)->id : Values::string($request->route('uuid'));
        $key = RateLimitCacheKeys::accountOperation($budget, $subject);
        if (RateLimiter::increment($key, $seconds) > $maximum) {
            RateLimiter::decrement($key);
            throw new ApiException('rate_limit.exceeded', 429, RateLimiter::availableIn($key));
        }

        return $next($request);
    }
}
