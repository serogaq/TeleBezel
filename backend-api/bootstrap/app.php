<?php

use App\Http\Middleware\AuthenticateApiClient;
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
        $middleware->alias([
            'api-client' => AuthenticateApiClient::class,
            'api-client-rate-limit' => RateLimitApiClient::class,
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
    })->create();
