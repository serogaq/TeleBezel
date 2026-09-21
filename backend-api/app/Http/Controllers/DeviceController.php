<?php

namespace App\Http\Controllers;

use App\Exceptions\ApiException;
use App\Models\Device;
use Illuminate\Http\JsonResponse;
use Illuminate\Http\Request;

final class DeviceController extends Controller
{
    public function show(Request $request): JsonResponse
    {
        $device = $this->device($request);

        return response()->json(['data' => $device->only(['id', 'name', 'locale', 'default_account_id', 'chat_list'])], 200, ['Cache-Control' => 'no-store']);
    }

    public function update(Request $request): JsonResponse
    {
        $device = $this->device($request);
        $data = $request->validate(['name' => ['sometimes', 'string', 'max:100'], 'locale' => ['sometimes', 'in:auto,ru,en'],
            'default_account_id' => ['sometimes', 'nullable', 'uuid', 'exists:telegram_accounts,id'], 'chat_list' => ['sometimes', 'in:main,archive']]);
        $device->fill($data)->save();

        return $this->show($request);
    }

    private function device(Request $request): Device
    {
        $this->deviceOnly($request);

        return Device::query()->findOrFail($request->attributes->get('device_id'));
    }

    private function deviceOnly(Request $request): void
    {
        if ($request->attributes->get('principal_type') !== 'device') {
            throw new ApiException('auth.insufficient_scope', 403);
        }
    }
}
