<?php

namespace App\Http\Middleware;

use App\Models\OwnerSession;
use Closure;
use Illuminate\Http\Request;
use Symfony\Component\HttpFoundation\Response;

final class AuthenticateOwner
{
    public function handle(Request $request, Closure $next): Response
    {
        $token = $request->cookie('telebezel_owner');
        $session = is_string($token) ? OwnerSession::query()
            ->where('token_hash', hash('sha256', $token))->whereNull('revoked_at')->first() : null;
        $expired = $session === null || $session->expires_at->isPast()
            || $session->authenticated_at->addHours(12)->isPast()
            || $session->last_interactive_at->addMinutes(30)->isPast();

        if ($expired) {
            if ($session !== null) {
                $session->forceFill(['revoked_at' => now()])->save();
            }

            return response()->json(['error' => ['code' => 'owner.authentication_required'],
                'request_id' => $request->attributes->get('request_id')], 401, ['Cache-Control' => 'no-store']);
        }

        $request->attributes->set('owner_session', $session);
        $request->attributes->set('instance_id', $session->instance_id);
        $request->attributes->set('api_client_id', $session->id);
        $request->attributes->set('principal_type', 'owner');
        $request->attributes->set('principal_id', $session->id);

        return $next($request);
    }
}
