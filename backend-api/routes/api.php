<?php

use App\Http\Controllers\DeviceController;
use App\Http\Controllers\HealthController;
use App\Http\Controllers\ProxyProfileController;
use App\Http\Controllers\QuickReplyController;
use App\Http\Controllers\SessionController;
use App\Http\Controllers\SettingsController;
use App\Http\Controllers\StatusController;
use App\Http\Controllers\TelegramAccountController;
use App\Http\Controllers\TelegramAuthorizationController;
use App\Http\Controllers\TelegramReadController;
use App\Http\Controllers\TelegramSendController;
use Illuminate\Support\Facades\Route;

Route::get('/healthz', [HealthController::class, 'live']);
Route::get('/readyz', [HealthController::class, 'ready']);
Route::middleware(['token', 'token-rate-limit'])->prefix('/v1')->whereUuid('id')->group(function (): void {
    Route::get('/status', StatusController::class);
    Route::post('/session/activity', [SessionController::class, 'activity']);
    Route::post('/session/logout', [SessionController::class, 'logout']);
    Route::middleware('permission:device.preferences')->group(function (): void {
        Route::get('/device/preferences', [DeviceController::class, 'show']);
        Route::put('/device/preferences', [DeviceController::class, 'update']);
    });
    Route::get('/quick-replies', [QuickReplyController::class, 'index'])->middleware('permission:quick_replies.read');
    Route::middleware(['permission:quick_replies.manage', 'throttle:settings-mutations'])->group(function (): void {
        Route::post('/quick-replies', [QuickReplyController::class, 'store']);
        Route::put('/quick-replies/reorder', [QuickReplyController::class, 'reorder']);
        Route::put('/quick-replies/{id}', [QuickReplyController::class, 'update']);
        Route::delete('/quick-replies/{id}', [QuickReplyController::class, 'destroy']);
    });
    Route::middleware(['permission:settings.manage', 'throttle:settings-mutations'])->group(function (): void {
        Route::post('/session/recovery-code', [SessionController::class, 'rotateRecoveryCode']);
        Route::get('/settings', [SettingsController::class, 'show']);
        Route::put('/settings', [SettingsController::class, 'update']);
        Route::get('/proxies', [ProxyProfileController::class, 'index']);
        Route::post('/proxies', [ProxyProfileController::class, 'store']);
        Route::post('/proxies/ping', [ProxyProfileController::class, 'pingAll']);
        Route::post('/proxies/direct', [ProxyProfileController::class, 'direct']);
        Route::put('/proxies/settings', [ProxyProfileController::class, 'configure']);
        Route::post('/proxies/{id}/ping', [ProxyProfileController::class, 'ping']);
        Route::post('/proxies/{id}/activate', [ProxyProfileController::class, 'activate']);
        Route::delete('/proxies/{id}', [ProxyProfileController::class, 'destroy']);
        Route::get('/devices', [SettingsController::class, 'devices']);
        Route::post('/devices', [SettingsController::class, 'storeDevice']);
        Route::delete('/devices/{id}', [SettingsController::class, 'revokeDevice']);
    });
    Route::prefix('/telegram/accounts')->whereUuid('uuid')->group(function (): void {
        Route::middleware('permission:accounts.read')->group(function (): void {
            Route::get('/', [TelegramAccountController::class, 'index']);
            Route::get('/{uuid}', [TelegramAccountController::class, 'show']);
        });
        Route::middleware('permission:messages.read')->group(function (): void {
            Route::get('/{uuid}/chats', [TelegramReadController::class, 'chats']);
            Route::get('/{uuid}/chats/{chatId}', [TelegramReadController::class, 'chat']);
            Route::get('/{uuid}/chats/{chatId}/messages', [TelegramReadController::class, 'messages']);
            Route::get('/{uuid}/chats/{chatId}/messages/{messageId}', [TelegramReadController::class, 'message']);
            Route::get('/{uuid}/chats/{chatId}/messages/{messageId}/preview/{previewId}', [TelegramReadController::class, 'preview']);
            Route::get('/{uuid}/updates', [TelegramReadController::class, 'updates']);
            Route::put('/{uuid}/chats/{chatId}/interests/{viewId}', [TelegramReadController::class, 'putInterest']);
            Route::delete('/{uuid}/chats/{chatId}/interests/{viewId}', [TelegramReadController::class, 'deleteInterest']);
        });
        Route::middleware('permission:messages.send')->group(function (): void {
            Route::post('/{uuid}/chats/{chatId}/messages', [TelegramSendController::class, 'send'])->middleware('account-rate-limit:send');
            Route::get('/{uuid}/sends/{operationId}', [TelegramSendController::class, 'status']);
        });
        Route::middleware('permission:accounts.manage')->group(function (): void {
            Route::post('/', [TelegramAccountController::class, 'store'])->middleware('account-rate-limit:create');
            Route::get('/{uuid}/authorization', [TelegramAuthorizationController::class, 'show']);
            Route::post('/{uuid}/authorization/actions', [TelegramAuthorizationController::class, 'action'])->middleware('account-rate-limit:auth-check');
            Route::post('/{uuid}/logout', [TelegramAccountController::class, 'logout'])->middleware('account-rate-limit:lifecycle');
            Route::get('/{uuid}/proxy', [TelegramAccountController::class, 'proxy']);
            Route::put('/{uuid}/proxy', [TelegramAccountController::class, 'updateProxy'])->middleware('account-rate-limit:lifecycle');
            Route::delete('/{uuid}', [TelegramAccountController::class, 'destroy'])->middleware('account-rate-limit:lifecycle');
        });
    });
});
