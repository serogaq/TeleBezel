<?php

namespace App\Http\Middleware;

use Closure;
use Illuminate\Database\QueryException;
use Illuminate\Http\Request;
use Illuminate\Support\Facades\RateLimiter;
use Symfony\Component\HttpFoundation\Response;

final class RateLimitAccountOperation
{
    public function handle(Request $request, Closure $next, string $budget): Response
    {
        if ($budget === 'auth-check' && in_array($request->input('action'), ['submit_phone_number', 'submit_email_address', 'start_qr', 'resend_code'], true)) {
            $budget = 'auth-start';
        }
        $limits = [
            'create' => [5, 3600, (string) $request->attributes->get('api_client_id')],
            'auth-start' => [3, 600, (string) $request->route('uuid')],
            'auth-check' => [10, 600, (string) $request->route('uuid')],
            'lifecycle' => [10, 600, (string) $request->route('uuid')],
        ];
        [$maxAttempts, $decay, $subject] = $limits[$budget] ?? throw new \LogicException('Unknown rate-limit budget.');
        $key = "account-operation:{$budget}:{$subject}";
        try {
            $attempts = RateLimiter::increment($key, $decay);
            if ($attempts > $maxAttempts) {
                RateLimiter::decrement($key);

                return response()->json(['error' => ['code' => 'rate_limit.exceeded'], 'request_id' => $request->attributes->get('request_id')], 429, ['Cache-Control' => 'no-store', 'Retry-After' => (string) RateLimiter::availableIn($key)]);
            }
        } catch (QueryException) {
            return response()->json(['error' => ['code' => 'service.database_unavailable'], 'request_id' => $request->attributes->get('request_id')], 503, ['Cache-Control' => 'no-store']);
        }

        return $next($request);
    }
}
