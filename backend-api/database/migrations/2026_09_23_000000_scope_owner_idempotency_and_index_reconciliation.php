<?php

use Illuminate\Database\Migrations\Migration;
use Illuminate\Database\Schema\Blueprint;
use Illuminate\Support\Facades\Schema;

return new class extends Migration
{
    public function up(): void
    {
        Schema::create('instance_account_idempotency_keys', function (Blueprint $table): void {
            $table->uuid('instance_id');
            $table->char('key_hash', 64);
            $table->char('request_hash', 64);
            $table->uuid('telegram_account_id');
            $table->timestampsTz();
            $table->primary(['instance_id', 'key_hash']);
            $table->index('created_at');
            $table->foreign('instance_id')->references('id')->on('instances')->cascadeOnDelete();
            $table->foreign('telegram_account_id')->references('id')->on('telegram_accounts')->restrictOnDelete();
        });
        Schema::table('telegram_accounts', function (Blueprint $table): void {
            $table->index(['lifecycle', 'id']);
        });
        Schema::table('account_idempotency_keys', function (Blueprint $table): void {
            $table->index('created_at');
        });
        Schema::table('owner_account_idempotency_keys', function (Blueprint $table): void {
            $table->index('created_at');
        });
        Schema::table('owner_sessions', function (Blueprint $table): void {
            $table->index('expires_at');
        });
        Schema::table('bootstrap_codes', function (Blueprint $table): void {
            $table->index('expires_at');
        });
    }

    public function down(): void
    {
        Schema::table('bootstrap_codes', function (Blueprint $table): void {
            $table->dropIndex(['expires_at']);
        });
        Schema::table('owner_sessions', function (Blueprint $table): void {
            $table->dropIndex(['expires_at']);
        });
        Schema::table('owner_account_idempotency_keys', function (Blueprint $table): void {
            $table->dropIndex(['created_at']);
        });
        Schema::table('account_idempotency_keys', function (Blueprint $table): void {
            $table->dropIndex(['created_at']);
        });
        Schema::table('telegram_accounts', function (Blueprint $table): void {
            $table->dropIndex(['lifecycle', 'id']);
        });
        Schema::dropIfExists('instance_account_idempotency_keys');
    }
};
