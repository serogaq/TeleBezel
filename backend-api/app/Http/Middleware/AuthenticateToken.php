<?php

declare(strict_types=1);

namespace App\Http\Middleware;

use App\Services\AuthenticationService;
use Closure;
use Illuminate\Http\Request;
use Symfony\Component\HttpFoundation\Response;

final readonly class AuthenticateToken
{
    public function __construct(private AuthenticationService $auth) {}

    /** @param Closure(Request): Response $next */
    public function handle(Request $request, Closure $next): Response
    {
        $cookie = $request->cookie(AuthenticationService::COOKIE);
        if ($request->headers->has('Authorization') || ! is_string($cookie)) {
            $principal = $this->auth->token($request->bearerToken());
        } else {
            $csrf = $request->header(AuthenticationService::CSRF_HEADER);
            $principal = $this->auth->cookie($cookie, is_string($csrf) ? $csrf : null, $request->isMethodSafe());
        }
        $request->attributes->set('principal', $principal);
        $request->attributes->set('principal_type', $principal->type);
        $request->attributes->set('principal_id', $principal->id);
        $request->attributes->set('instance_id', $principal->instanceId);

        return $next($request);
    }
}
