<?php

use App\Exceptions\ApiException;
use App\Http\Middleware\AuthenticateApiClient;
use App\Http\Middleware\AuthenticateOwner;
use App\Http\Middleware\LimitJsonBody;
use App\Http\Middleware\RateLimitAccountOperation;
use App\Http\Middleware\RateLimitApiClient;
use App\Http\Middleware\RequestId;
use App\Http\Middleware\SecurityHeaders;
use Illuminate\Database\QueryException;
use Illuminate\Foundation\Application;
use Illuminate\Foundation\Configuration\Exceptions;
use Illuminate\Foundation\Configuration\Middleware;
use Illuminate\Http\Request;

return Application::configure(basePath: dirname(__DIR__))
    ->withRouting(
        api: __DIR__.'/../routes/api.php',
        web: __DIR__.'/../routes/web.php',
        apiPrefix: '',
        commands: __DIR__.'/../routes/console.php',
    )
    ->withMiddleware(function (Middleware $middleware): void {
        $middleware->trustProxies(at: array_values(array_filter(array_map('trim', explode(',', (string) env('TRUSTED_PROXIES', '127.0.0.1'))))),
            headers: Request::HEADER_X_FORWARDED_FOR | Request::HEADER_X_FORWARDED_HOST | Request::HEADER_X_FORWARDED_PORT | Request::HEADER_X_FORWARDED_PROTO);
        $middleware->append(RequestId::class);
        $middleware->append(SecurityHeaders::class);
        $middleware->append(LimitJsonBody::class);
        $middleware->alias([
            'api-client' => AuthenticateApiClient::class,
            'owner' => AuthenticateOwner::class,
            'api-client-rate-limit' => RateLimitApiClient::class,
            'account-rate-limit' => RateLimitAccountOperation::class,
        ]);
    })
    ->withExceptions(function (Exceptions $exceptions): void {
        $exceptions->shouldRenderJsonWhen(
            fn (Request $request) => $request->is('v1/*') || $request->expectsJson(),
        );
        $exceptions->render(function (QueryException $exception, Request $request) {
            if (! $request->is('v1/*')) {
                return null;
            }

            $requestId = $request->attributes->get('request_id');

            return response()->json([
                'error' => ['code' => 'service.database_unavailable'],
                'request_id' => $requestId,
            ], 503, ['Cache-Control' => 'no-store', 'X-Request-ID' => (string) $requestId]);
        });
        $exceptions->render(function (ApiException $exception, Request $request) {
            $headers = ['Cache-Control' => 'no-store', 'X-Request-ID' => (string) $request->attributes->get('request_id')];
            if ($exception->retryAfter !== null) {
                $headers['Retry-After'] = (string) $exception->retryAfter;
            }

            return response()->json(['error' => ['code' => $exception->errorCode], 'request_id' => $request->attributes->get('request_id')], $exception->status, $headers);
        });
    })->create();
