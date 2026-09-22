<?php

use App\Http\Controllers\OwnerSessionController;
use App\Http\Controllers\ProxyProfileController;
use App\Http\Controllers\SettingsController;
use App\Http\Controllers\TelegramAccountController;
use App\Http\Controllers\TelegramAuthorizationController;
use Illuminate\Support\Facades\Route;

Route::get('/settings', [SettingsController::class, 'page']);
Route::prefix('/v1/owner')->group(function (): void {
    Route::post('/bootstrap', [OwnerSessionController::class, 'bootstrap'])->middleware('throttle:bootstrap');
    Route::post('/login', [OwnerSessionController::class, 'login'])->middleware('throttle:login');
    Route::post('/recover', [OwnerSessionController::class, 'recover'])->middleware('throttle:recovery');
    Route::middleware(['owner', 'throttle:owner-mutations'])->whereUuid('id')->group(function (): void {
        Route::post('/activity', [OwnerSessionController::class, 'activity']);
        Route::post('/logout', [OwnerSessionController::class, 'logout']);
        Route::post('/recovery-code', [OwnerSessionController::class, 'rotateRecoveryCode']);
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
        Route::get('/quick-replies', [SettingsController::class, 'quickReplies']);
        Route::post('/quick-replies', [SettingsController::class, 'storeQuickReply']);
        Route::put('/quick-replies/reorder', [SettingsController::class, 'reorderQuickReplies']);
        Route::put('/quick-replies/{id}', [SettingsController::class, 'updateQuickReply']);
        Route::delete('/quick-replies/{id}', [SettingsController::class, 'destroyQuickReply']);
        Route::prefix('/telegram/accounts')->whereUuid('uuid')->group(function (): void {
            Route::get('/', [TelegramAccountController::class, 'index']);
            Route::post('/', [TelegramAccountController::class, 'store']);
            Route::get('/{uuid}', [TelegramAccountController::class, 'show']);
            Route::get('/{uuid}/authorization', [TelegramAuthorizationController::class, 'show']);
            Route::post('/{uuid}/authorization/actions', [TelegramAuthorizationController::class, 'action']);
            Route::post('/{uuid}/logout', [TelegramAccountController::class, 'logout']);
            Route::get('/{uuid}/proxy', [TelegramAccountController::class, 'proxy']);
            Route::put('/{uuid}/proxy', [TelegramAccountController::class, 'updateProxy']);
            Route::delete('/{uuid}', [TelegramAccountController::class, 'destroy']);
        });
    });
});
