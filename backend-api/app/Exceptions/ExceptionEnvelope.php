<?php

declare(strict_types=1);

namespace App\Exceptions;

use App\Models\AccessToken;
use App\Models\ProxyProfile;
use App\Models\TelegramAccount;
use Illuminate\Contracts\Encryption\DecryptException;
use Illuminate\Database\Eloquent\ModelNotFoundException;
use Illuminate\Database\QueryException;
use Illuminate\Http\Request;
use Illuminate\Support\Facades\Log;
use Symfony\Component\HttpKernel\Exception\MethodNotAllowedHttpException;
use Symfony\Component\HttpKernel\Exception\NotFoundHttpException;

final class ExceptionEnvelope
{
    private const MODEL_CODES = [
        ProxyProfile::class => 'proxy.not_found',
        AccessToken::class => 'device.not_found',
        TelegramAccount::class => 'account.not_found',
    ];

    private const PATH_CODES = [
        '#^v1/telegram/accounts/[^/]+/sends/#' => 'operation.not_found',
        '#^v1/telegram/accounts/#' => 'account.not_found',
        '#^v1/proxies/#' => 'proxy.not_found',
        '#^v1/devices/#' => 'device.not_found',
        '#^v1/quick-replies/#' => 'quick_replies.not_found',
    ];

    public static function applies(Request $request): bool
    {
        return $request->is('v1/*');
    }

    public static function notFound(NotFoundHttpException $exception, Request $request): ApiException
    {
        $previous = $exception->getPrevious();
        if ($previous instanceof ModelNotFoundException) {
            return new ApiException(self::MODEL_CODES[$previous->getModel()] ?? 'resource.not_found', 404);
        }
        foreach (self::PATH_CODES as $pattern => $code) {
            if (preg_match($pattern, $request->path()) === 1) {
                return new ApiException($code, 404);
            }
        }

        return new ApiException('http.not_found', 404);
    }

    public static function methodNotAllowed(MethodNotAllowedHttpException $exception): ApiException
    {
        return new ApiException('http.method_not_allowed', 405);
    }

    public static function query(QueryException $exception): ApiException
    {
        $state = (string) $exception->getCode();
        if ($state === '22P02' || $state === '22003' || $state === '22001') {
            return new ApiException('request.invalid', 422);
        }
        if (str_starts_with($state, '23')) {
            return new ApiException('operation.conflict', 409);
        }

        return new ApiException('service.database_unavailable', 503);
    }

    public static function decrypt(DecryptException $exception, Request $request): ApiException
    {
        Log::error('stored_secret_undecryptable', [
            'request_id' => $request->attributes->get('request_id'),
            'path' => $request->route()?->uri(),
        ]);

        return new ApiException('storage.invalid_key', 503);
    }
}
