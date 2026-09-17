<?php

namespace App\Http\Middleware;

use Closure;
use Illuminate\Http\Request;
use Illuminate\Support\Facades\Log;
use Illuminate\Support\Str;
use Symfony\Component\HttpFoundation\Response;

final class RequestId
{
    public function handle(Request $request, Closure $next): Response
    {
        $requestId = (string) Str::uuid();
        $request->attributes->set('request_id', $requestId);
        $response = $next($request);
        $response->headers->set('X-Request-ID', $requestId);
        Log::info('http_request', [
            'request_id' => $requestId,
            'method' => $request->method(),
            'route' => $request->route()?->uri() ?? 'unmatched',
            'status' => $response->getStatusCode(),
        ]);

        return $response;
    }
}
