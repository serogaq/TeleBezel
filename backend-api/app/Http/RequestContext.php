<?php

declare(strict_types=1);

namespace App\Http;

use App\Data\PrincipalContext;
use App\Exceptions\ApiException;
use App\Support\Values;
use Illuminate\Http\Request;

final class RequestContext
{
    public static function principal(Request $request): PrincipalContext
    {
        $principal = $request->attributes->get('principal');
        if (! $principal instanceof PrincipalContext) {
            throw new ApiException('auth.unauthorized', 401);
        }

        return $principal;
    }

    public static function instanceId(Request $request): string
    {
        return self::principal($request)->instanceId ?? throw new ApiException('auth.insufficient_scope', 403);
    }

    public static function requestId(Request $request): string
    {
        return Values::string($request->attributes->get('request_id'));
    }
}
