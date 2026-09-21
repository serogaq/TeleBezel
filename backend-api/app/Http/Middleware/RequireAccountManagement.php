<?php

declare(strict_types=1);

namespace App\Http\Middleware;

use App\Exceptions\ApiException;
use App\Http\RequestContext;
use Closure;
use Illuminate\Http\Request;
use Symfony\Component\HttpFoundation\Response;

final class RequireAccountManagement
{
    /** @param Closure(Request): Response $next */
    public function handle(Request $request, Closure $next): Response
    {
        if (! in_array(RequestContext::principal($request)->type, ['owner', 'maintenance'], true)) {
            throw new ApiException('auth.insufficient_scope', 403);
        }

        return $next($request);
    }
}
