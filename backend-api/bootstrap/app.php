<?php

use App\Exceptions\ApiException;
use App\Http\Middleware\AuthenticateApiClient;
use App\Http\Middleware\LimitJsonBody;
use App\Http\Middleware\RateLimitAccountOperation;
use App\Http\Middleware\RateLimitApiClient;
use App\Http\Middleware\RequestId;
use Illuminate\Database\QueryException;
use Illuminate\Foundation\Application;
use Illuminate\Foundation\Configuration\Exceptions;
use Illuminate\Foundation\Configuration\Middleware;
use Illuminate\Http\Request;

return Application::configure(basePath: dirname(__DIR__))
    ->withRouting(
        api: __DIR__.'/../routes/api.php',
        apiPrefix: '',
        commands: __DIR__.'/../routes/console.php',
    )
    ->withMiddleware(function (Middleware $middleware): void {
        $middleware->append(RequestId::class);
        $middleware->append(LimitJsonBody::class);
        $middleware->alias([
            'api-client' => AuthenticateApiClient::class,
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
