<?php

declare(strict_types=1);

namespace App\Http\Controllers;

use App\Http\RequestContext;
use App\Http\Requests\CreateDeviceRequest;
use App\Http\Requests\UpdateSettingsRequest;
use App\Http\Resources\ActionResource;
use App\Http\Resources\DeviceResource;
use App\Http\Resources\SettingsResource;
use App\Services\AuthenticationService;
use App\Services\DeviceService;
use App\Services\SettingsService;
use Illuminate\Http\JsonResponse;
use Illuminate\Http\Request;
use Illuminate\View\View;

final class SettingsController extends Controller
{
    public function page(Request $request, AuthenticationService $auth): View
    {
        $token = $request->cookie(AuthenticationService::COOKIE);

        return view('settings', [
            'apiCsrf' => is_string($token) && AuthenticationService::wellFormed($token) ? $auth->csrf($token) : '',
        ]);
    }

    public function show(Request $request, SettingsService $settings): JsonResponse
    {
        return (new SettingsResource($settings->show(RequestContext::instanceId($request))))->respond(status: 200);
    }

    public function update(UpdateSettingsRequest $request, SettingsService $settings): JsonResponse
    {
        return (new SettingsResource($settings->update(RequestContext::instanceId($request), $request->inputData(), RequestContext::requestId($request))))->respond(status: 200);
    }

    public function devices(Request $request, DeviceService $devices): JsonResponse
    {
        return DeviceResource::listing($devices->all(RequestContext::instanceId($request)))->respond(status: 200);
    }

    public function storeDevice(CreateDeviceRequest $request, DeviceService $devices): JsonResponse
    {
        return (new DeviceResource($devices->create(RequestContext::instanceId($request), $request->inputData()->string('name'))))->respond(status: 201);
    }

    public function revokeDevice(string $id, Request $request, DeviceService $devices): JsonResponse
    {
        $devices->revoke(RequestContext::instanceId($request), $id, RequestContext::requestId($request));

        return (new ActionResource('revoked'))->respond();
    }
}
