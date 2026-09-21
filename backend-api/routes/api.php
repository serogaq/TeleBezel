<?php

use App\Http\Controllers\DeviceController;
use App\Http\Controllers\HealthController;
use App\Http\Controllers\StatusController;
use App\Http\Controllers\TelegramAccountController;
use App\Http\Controllers\TelegramAuthorizationController;
use App\Http\Controllers\TelegramReadController;
use Illuminate\Support\Facades\Route;

Route::get('/healthz', [HealthController::class, 'live']);
Route::get('/readyz', [HealthController::class, 'ready']);
Route::middleware(['api-client', 'api-client-rate-limit'])->get('/v1/status', StatusController::class);
Route::middleware(['api-client', 'api-client-rate-limit'])->group(function (): void {
    Route::get('/v1/device/preferences', [DeviceController::class, 'show']);
    Route::put('/v1/device/preferences', [DeviceController::class, 'update']);
});
Route::middleware(['api-client', 'api-client-rate-limit'])->prefix('/v1/telegram/accounts')->group(function (): void {
    Route::get('/', [TelegramAccountController::class, 'index']);
    Route::post('/', [TelegramAccountController::class, 'store'])->middleware(['account-management', 'account-rate-limit:create']);
    Route::get('/{uuid}', [TelegramAccountController::class, 'show']);
    Route::get('/{uuid}/authorization', [TelegramAuthorizationController::class, 'show']);
    Route::get('/{uuid}/chats', [TelegramReadController::class, 'chats']);
    Route::get('/{uuid}/chats/{chatId}', [TelegramReadController::class, 'chat']);
    Route::get('/{uuid}/chats/{chatId}/messages', [TelegramReadController::class, 'messages']);
    Route::get('/{uuid}/chats/{chatId}/messages/{messageId}', [TelegramReadController::class, 'message']);
    Route::get('/{uuid}/chats/{chatId}/messages/{messageId}/preview/{previewId}', [TelegramReadController::class, 'preview']);
    Route::get('/{uuid}/updates', [TelegramReadController::class, 'updates']);
    Route::put('/{uuid}/chats/{chatId}/interests/{viewId}', [TelegramReadController::class, 'putInterest']);
    Route::delete('/{uuid}/chats/{chatId}/interests/{viewId}', [TelegramReadController::class, 'deleteInterest']);
    Route::post('/{uuid}/authorization/actions', [TelegramAuthorizationController::class, 'action'])->middleware(['account-management', 'account-rate-limit:auth-check']);
    Route::post('/{uuid}/logout', [TelegramAccountController::class, 'logout'])->middleware(['account-management', 'account-rate-limit:lifecycle']);
    Route::get('/{uuid}/proxy', [TelegramAccountController::class, 'proxy']);
    Route::put('/{uuid}/proxy', [TelegramAccountController::class, 'updateProxy'])->middleware(['account-management', 'account-rate-limit:lifecycle']);
    Route::delete('/{uuid}', [TelegramAccountController::class, 'destroy'])->middleware(['account-management', 'account-rate-limit:lifecycle']);
});
