<?php

use App\Exceptions\ApiException;
use App\Exceptions\ExceptionEnvelope;
use App\Http\Middleware\AuthenticateApiClient;
use App\Http\Middleware\AuthenticateOwner;
use App\Http\Middleware\LimitJsonBody;
use App\Http\Middleware\RateLimitAccountOperation;
use App\Http\Middleware\RateLimitApiClient;
use App\Http\Middleware\RequestId;
use App\Http\Middleware\RequireAccountManagement;
use App\Http\Middleware\SecurityHeaders;
use App\Http\Resources\ErrorResource;
use Illuminate\Contracts\Encryption\DecryptException;
use Illuminate\Database\QueryException;
use Illuminate\Foundation\Application;
use Illuminate\Foundation\Configuration\Exceptions;
use Illuminate\Foundation\Configuration\Middleware;
use Illuminate\Http\Request;
use Symfony\Component\HttpKernel\Exception\MethodNotAllowedHttpException;
use Symfony\Component\HttpKernel\Exception\NotFoundHttpException;

return Application::configure(basePath: dirname(__DIR__))
    ->withRouting(
        api: __DIR__.'/../routes/api.php',
        web: __DIR__.'/../routes/web.php',
        apiPrefix: '',
        commands: __DIR__.'/../routes/console.php',
    )
    ->withMiddleware(function (Middleware $middleware): void {
        $middleware->trustProxies(headers: Request::HEADER_X_FORWARDED_FOR | Request::HEADER_X_FORWARDED_HOST | Request::HEADER_X_FORWARDED_PORT | Request::HEADER_X_FORWARDED_PROTO);
        $middleware->append(RequestId::class);
        $middleware->append(SecurityHeaders::class);
        $middleware->append(LimitJsonBody::class);
        $middleware->alias([
            'api-client' => AuthenticateApiClient::class,
            'owner' => AuthenticateOwner::class,
            'api-client-rate-limit' => RateLimitApiClient::class,
            'account-rate-limit' => RateLimitAccountOperation::class,
            'account-management' => RequireAccountManagement::class,
        ]);
    })
    ->withExceptions(function (Exceptions $exceptions): void {
        $exceptions->shouldRenderJsonWhen(
            fn (Request $request) => $request->is('v1/*') || $request->expectsJson(),
        );
        $exceptions->render(function (QueryException $exception, Request $request) {
            return ExceptionEnvelope::applies($request) ? ErrorResource::respond(ExceptionEnvelope::query($exception), $request) : null;
        });
        $exceptions->render(function (NotFoundHttpException $exception, Request $request) {
            return ExceptionEnvelope::applies($request) ? ErrorResource::respond(ExceptionEnvelope::notFound($exception, $request), $request) : null;
        });
        $exceptions->render(function (MethodNotAllowedHttpException $exception, Request $request) {
            return ExceptionEnvelope::applies($request) ? ErrorResource::respond(ExceptionEnvelope::methodNotAllowed($exception), $request) : null;
        });
        $exceptions->render(function (DecryptException $exception, Request $request) {
            return ExceptionEnvelope::applies($request) ? ErrorResource::respond(ExceptionEnvelope::decrypt($exception, $request), $request) : null;
        });
        $exceptions->render(function (ApiException $exception, Request $request) {
            return ErrorResource::respond($exception, $request);
        });
    })->create();
