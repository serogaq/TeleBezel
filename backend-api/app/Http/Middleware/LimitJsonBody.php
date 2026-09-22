<?php

declare(strict_types=1);

namespace App\Http\Middleware;

use App\Exceptions\ApiException;
use Closure;
use Illuminate\Http\Request;
use Symfony\Component\HttpFoundation\Response;

final class LimitJsonBody
{
    /** @param Closure(Request): Response $next */
    public function handle(Request $request, Closure $next): Response
    {
        if ($request->is('v1/*') && strlen($request->getContent()) > 16 * 1024) {
            throw new ApiException('request.body_too_large', 413);
        }

        return $next($request);
    }
}
