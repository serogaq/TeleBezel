<?php

declare(strict_types=1);

namespace App\Http\Controllers;

use App\Http\RequestContext;
use App\Http\Requests\UpdateDeviceRequest;
use App\Http\Resources\DeviceResource;
use App\Services\DeviceService;
use Illuminate\Http\JsonResponse;
use Illuminate\Http\Request;

final class DeviceController extends Controller
{
    public function show(Request $request, DeviceService $devices): JsonResponse
    {
        return (new DeviceResource($devices->preferences(RequestContext::principal($request))))->respond();
    }

    public function update(UpdateDeviceRequest $request, DeviceService $devices): JsonResponse
    {
        return (new DeviceResource($devices->update(RequestContext::principal($request), $request->inputData())))->respond();
    }
}
