<?php

namespace App\Http\Controllers;

use App\Http\RequestContext;
use App\Http\Requests\BootstrapOwnerRequest;
use App\Http\Requests\LoginOwnerRequest;
use App\Http\Requests\RecoverOwnerRequest;
use App\Http\Resources\OwnerSessionResource;
use App\Services\AccessTokenService;
use App\Services\AuthenticationService;
use App\Services\OwnerAccessService;
use Illuminate\Http\JsonResponse;
use Illuminate\Http\Request;
use Symfony\Component\HttpFoundation\Cookie;

final class SessionController extends Controller
{
    public function bootstrap(BootstrapOwnerRequest $request, OwnerAccessService $access, AuthenticationService $auth): JsonResponse
    {
        $data = $request->inputData();
        [$instance, $token, $recovery] = $access->bootstrap($data->string('bootstrap_code'), $data->string('password'));

        return $this->authenticated($auth, [
            'instance_id' => $instance,
            'recovery_code' => $recovery,
        ], $token, 201);
    }

    public function login(LoginOwnerRequest $request, OwnerAccessService $access, AuthenticationService $auth): JsonResponse
    {
        $data = $request->inputData();
        [$instance, $token] = $access->login($data->string('password'));

        return $this->authenticated($auth, [
            'instance_id' => $instance,
        ], $token);
    }

    public function recover(RecoverOwnerRequest $request, OwnerAccessService $access, AuthenticationService $auth): JsonResponse
    {
        $data = $request->inputData();
        [$token, $recovery] = $access->recover($data->string('recovery_code'), $data->string('password'));

        return $this->authenticated($auth, [
            'recovery_code' => $recovery,
        ], $token);
    }

    public function rotateRecoveryCode(Request $request, OwnerAccessService $access): JsonResponse
    {
        $recovery = $access->rotateRecoveryCode(RequestContext::instanceId($request));

        return (new OwnerSessionResource([
            'recovery_code' => $recovery,
        ]))->respond(status: 201);
    }

    public function activity(Request $request, OwnerAccessService $access): JsonResponse
    {
        $access->activity(RequestContext::principal($request)->id);

        return (new OwnerSessionResource([
            'active' => true,
        ]))->respond();
    }

    public function logout(Request $request, OwnerAccessService $access): JsonResponse
    {
        $access->logout(RequestContext::principal($request)->id, RequestContext::requestId($request));

        return (new OwnerSessionResource([
            'authenticated' => false,
        ]))->respond()->withoutCookie(AuthenticationService::COOKIE);
    }

    /** @param array<string, mixed> $data */
    private function authenticated(AuthenticationService $auth, array $data, string $token, int $status = 200): JsonResponse
    {
        $cookie = Cookie::create(AuthenticationService::COOKIE, $token, now()->addHours(AccessTokenService::WEB_LIFETIME_HOURS), '/', null, true, true, false, Cookie::SAMESITE_STRICT);

        return (new OwnerSessionResource([
            ...$data,
            'csrf_token' => $auth->csrf($token),
        ]))->respond(status: $status)->withCookie($cookie);
    }
}
