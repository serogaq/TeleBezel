<?php

namespace App\Http\Middleware;

use Closure;
use Illuminate\Http\Request;
use Symfony\Component\HttpFoundation\Response;

final class LimitJsonBody
{
    public function handle(Request $request, Closure $next): Response
    {
        if ($request->is('v1/*') && strlen($request->getContent()) > 16 * 1024) {
            return response()->json([
                'error' => ['code' => 'request.body_too_large'],
                'request_id' => $request->attributes->get('request_id'),
            ], 413, ['Cache-Control' => 'no-store']);
        }

        return $next($request);
    }
}
