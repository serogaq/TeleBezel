<?php

declare(strict_types=1);

namespace App\Http\Middleware;

use App\Services\AuthenticationService;
use Closure;
use Illuminate\Http\Request;
use Symfony\Component\HttpFoundation\Response;

final readonly class AuthenticateOwner
{
    public function __construct(private AuthenticationService $auth) {}

    /** @param Closure(Request): Response $next */
    public function handle(Request $request, Closure $next): Response
    {
        $auth = $this->auth;
        $principal = $auth->owner(is_string($request->cookie('telebezel_owner')) ? $request->cookie('telebezel_owner') : null);
        $request->attributes->set('principal', $principal);
        $request->attributes->set('principal_type', $principal->type);
        $request->attributes->set('principal_id', $principal->id);
        $request->attributes->set('api_client_id', $principal->id);
        $request->attributes->set('instance_id', $principal->instanceId);
        if ($principal->type === 'device') {
            $request->attributes->set('device_id', $principal->id);
        }

        return $next($request);
    }
}
