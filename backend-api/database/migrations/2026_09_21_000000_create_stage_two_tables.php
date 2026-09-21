<?php

use Illuminate\Database\Migrations\Migration;
use Illuminate\Database\Schema\Blueprint;
use Illuminate\Support\Facades\Schema;

return new class extends Migration
{
    public function up(): void
    {
        Schema::create('instances', function (Blueprint $table): void {
            $table->uuid('id')->primary();
            $table->string('owner_password_hash')->nullable();
            $table->string('recovery_code_hash')->nullable();
            $table->text('app_key_check')->nullable();
            $table->unsignedBigInteger('telegram_api_id')->nullable();
            $table->text('telegram_api_hash')->nullable();
            $table->unsignedBigInteger('configuration_revision')->default(1);
            $table->uuid('active_proxy_profile_id')->nullable();
            $table->string('proxy_failure_action', 16)->default('next');
            $table->unsignedSmallInteger('proxy_connect_timeout_seconds')->default(10);
            $table->timestampTz('proxy_activated_at')->nullable();
            $table->timestampTz('proxy_failure_started_at')->nullable();
            $table->timestampsTz();
        });

        Schema::create('bootstrap_codes', function (Blueprint $table): void {
            $table->uuid('id')->primary();
            $table->char('code_hash', 64)->unique();
            $table->timestampTz('expires_at');
            $table->timestampTz('consumed_at')->nullable();
            $table->timestampsTz();
        });

        Schema::create('owner_sessions', function (Blueprint $table): void {
            $table->uuid('id')->primary();
            $table->uuid('instance_id');
            $table->char('token_hash', 64)->unique();
            $table->timestampTz('authenticated_at');
            $table->timestampTz('last_interactive_at');
            $table->timestampTz('expires_at');
            $table->timestampTz('revoked_at')->nullable();
            $table->timestampsTz();
            $table->foreign('instance_id')->references('id')->on('instances')->cascadeOnDelete();
        });

        Schema::create('devices', function (Blueprint $table): void {
            $table->uuid('id')->primary();
            $table->uuid('instance_id');
            $table->string('name', 100);
            $table->string('token_prefix', 12);
            $table->char('token_hash', 64)->unique();
            $table->string('locale', 8)->default('auto');
            $table->uuid('default_account_id')->nullable();
            $table->string('chat_list', 16)->default('main');
            $table->timestampTz('last_seen_at')->nullable();
            $table->timestampTz('revoked_at')->nullable();
            $table->timestampsTz();
            $table->foreign('instance_id')->references('id')->on('instances')->cascadeOnDelete();
            $table->foreign('default_account_id')->references('id')->on('telegram_accounts')->nullOnDelete();
        });

        Schema::create('quick_replies', function (Blueprint $table): void {
            $table->uuid('id')->primary();
            $table->uuid('instance_id');
            $table->string('text', 512);
            $table->unsignedSmallInteger('position');
            $table->timestampsTz();
            $table->unique(['instance_id', 'position']);
            $table->foreign('instance_id')->references('id')->on('instances')->cascadeOnDelete();
        });

        Schema::create('scheduler_statuses', function (Blueprint $table): void {
            $table->string('name')->primary();
            $table->timestampTz('last_tick_at')->nullable();
            $table->timestampTz('run_started_at')->nullable();
            $table->timestampTz('run_finished_at')->nullable();
            $table->string('last_result', 32)->nullable();
            $table->unsignedInteger('duration_ms')->nullable();
            $table->unsignedInteger('succeeded')->default(0);
            $table->unsignedInteger('failed')->default(0);
            $table->unsignedInteger('deferred')->default(0);
            $table->timestampsTz();
        });

    }

    public function down(): void
    {
        Schema::dropIfExists('scheduler_statuses');
        Schema::dropIfExists('quick_replies');
        Schema::dropIfExists('devices');
        Schema::dropIfExists('owner_sessions');
        Schema::dropIfExists('bootstrap_codes');
        Schema::dropIfExists('instances');
    }
};
