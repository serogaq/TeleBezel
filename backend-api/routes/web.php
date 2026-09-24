<?php

use App\Http\Controllers\SessionController;
use App\Http\Controllers\SettingsController;
use Illuminate\Support\Facades\Route;

Route::get('/settings', [SettingsController::class, 'page']);
Route::prefix('/v1/session')->group(function (): void {
    Route::post('/bootstrap', [SessionController::class, 'bootstrap'])->middleware('throttle:bootstrap');
    Route::post('/login', [SessionController::class, 'login'])->middleware('throttle:login');
    Route::post('/recover', [SessionController::class, 'recover'])->middleware('throttle:recovery');
});
