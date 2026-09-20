<?php

use App\Http\Controllers\HealthController;
use App\Http\Controllers\StatusController;
use App\Http\Controllers\TelegramAccountController;
use App\Http\Controllers\TelegramAuthorizationController;
use Illuminate\Support\Facades\Route;

Route::get('/healthz', [HealthController::class, 'live']);
Route::get('/readyz', [HealthController::class, 'ready']);
Route::middleware(['api-client', 'api-client-rate-limit'])->get('/v1/status', StatusController::class);
Route::middleware(['api-client', 'api-client-rate-limit'])->prefix('/v1/telegram/accounts')->group(function (): void {
    Route::get('/', [TelegramAccountController::class, 'index']);
    Route::post('/', [TelegramAccountController::class, 'store'])->middleware('account-rate-limit:create');
    Route::get('/{uuid}', [TelegramAccountController::class, 'show']);
    Route::get('/{uuid}/authorization', [TelegramAuthorizationController::class, 'show']);
    Route::post('/{uuid}/authorization/actions', [TelegramAuthorizationController::class, 'action'])->middleware('account-rate-limit:auth-check');
    Route::post('/{uuid}/logout', [TelegramAccountController::class, 'logout'])->middleware('account-rate-limit:lifecycle');
    Route::get('/{uuid}/proxy', [TelegramAccountController::class, 'proxy']);
    Route::put('/{uuid}/proxy', [TelegramAccountController::class, 'updateProxy'])->middleware('account-rate-limit:lifecycle');
    Route::delete('/{uuid}', [TelegramAccountController::class, 'destroy'])->middleware('account-rate-limit:lifecycle');
});
