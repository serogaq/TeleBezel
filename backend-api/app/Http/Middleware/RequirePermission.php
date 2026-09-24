<?php

declare(strict_types=1);

namespace App\Http\Middleware;

use App\Exceptions\ApiException;
use App\Http\RequestContext;
use Closure;
use Illuminate\Http\Request;
use Symfony\Component\HttpFoundation\Response;

final class RequirePermission
{
    /** @param Closure(Request): Response $next */
    public function handle(Request $request, Closure $next, string $permission): Response
    {
        $principal = RequestContext::principal($request);
        if (! $principal->can($permission)) {
            throw new ApiException('auth.insufficient_scope', 403);
        }
        $account = $request->route('uuid');
        if (is_string($account) && ! $principal->canAccessAccount($account)) {
            throw new ApiException('account.not_found', 404);
        }

        return $next($request);
    }
}
