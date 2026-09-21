<?php

namespace App\Http\Controllers;

use App\Models\OwnerSession;
use App\Services\OwnerAccessService;
use Illuminate\Http\JsonResponse;
use Illuminate\Http\Request;
use Symfony\Component\HttpFoundation\Cookie;

final class OwnerSessionController extends Controller
{
    public function bootstrap(Request $request, OwnerAccessService $access): JsonResponse
    {
        $data = $request->validate(['bootstrap_code' => ['required', 'string', 'max:200'], 'password' => ['required', 'string', 'min:12', 'max:200']]);
        [$instance, $token, $recovery] = $access->bootstrap($data['bootstrap_code'], $data['password']);

        return $this->authenticated(['instance_id' => $instance->id, 'recovery_code' => $recovery], $token, 201);
    }

    public function login(Request $request, OwnerAccessService $access): JsonResponse
    {
        $data = $request->validate(['password' => ['required', 'string', 'max:200']]);
        [$instance, $token] = $access->login($data['password']);

        return $this->authenticated(['instance_id' => $instance->id], $token);
    }

    public function recover(Request $request, OwnerAccessService $access): JsonResponse
    {
        $data = $request->validate(['recovery_code' => ['required', 'string', 'max:200'], 'password' => ['required', 'string', 'min:12', 'max:200']]);
        [$token, $recovery] = $access->recover($data['recovery_code'], $data['password']);

        return $this->authenticated(['recovery_code' => $recovery], $token);
    }

    public function rotateRecoveryCode(Request $request, OwnerAccessService $access): JsonResponse
    {
        $recovery = $access->rotateRecoveryCode((string) $request->attributes->get('instance_id'));

        return response()->json(['data' => ['recovery_code' => $recovery]], 201, ['Cache-Control' => 'no-store']);
    }

    public function activity(Request $request): JsonResponse
    {
        /** @var OwnerSession $session */
        $session = $request->attributes->get('owner_session');
        $session->forceFill(['last_interactive_at' => now()])->save();

        return response()->json(['data' => ['active' => true]], 200, ['Cache-Control' => 'no-store']);
    }

    public function logout(Request $request): JsonResponse
    {
        /** @var OwnerSession $session */
        $session = $request->attributes->get('owner_session');
        $session->forceFill(['revoked_at' => now()])->save();

        return response()->json(['data' => ['authenticated' => false]], 200, ['Cache-Control' => 'no-store'])
            ->withoutCookie('telebezel_owner');
    }

    /** @param array<string, mixed> $data */
    private function authenticated(array $data, string $token, int $status = 200): JsonResponse
    {
        $cookie = Cookie::create('telebezel_owner', $token, now()->addHours(12), '/', null, true, true, false, Cookie::SAMESITE_STRICT);

        return response()->json(['data' => $data], $status, ['Cache-Control' => 'no-store'])->withCookie($cookie);
    }
}
