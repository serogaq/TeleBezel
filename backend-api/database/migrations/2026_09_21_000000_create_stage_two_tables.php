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
            $table->json('global_proxy')->nullable();
            $table->unsignedBigInteger('configuration_revision')->default(1);
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

        Schema::create('pairing_requests', function (Blueprint $table): void {
            $table->uuid('id')->primary();
            $table->uuid('instance_id');
            $table->char('exchange_secret_hash', 64);
            $table->char('page_ticket_hash', 64)->unique();
            $table->string('callback_state', 128);
            $table->string('origin', 2048);
            $table->string('device_name', 100);
            $table->timestampTz('expires_at');
            $table->timestampTz('ticket_consumed_at')->nullable();
            $table->timestampTz('approved_at')->nullable();
            $table->uuid('device_id')->nullable();
            $table->text('exchange_result')->nullable();
            $table->timestampTz('exchange_expires_at')->nullable();
            $table->timestampTz('confirmed_at')->nullable();
            $table->timestampsTz();
            $table->foreign('instance_id')->references('id')->on('instances')->cascadeOnDelete();
            $table->foreign('device_id')->references('id')->on('devices')->nullOnDelete();
        });

        Schema::create('configuration_launches', function (Blueprint $table): void {
            $table->uuid('id')->primary();
            $table->uuid('device_id');
            $table->char('ticket_hash', 64)->unique();
            $table->string('callback_state', 128);
            $table->timestampTz('expires_at');
            $table->timestampTz('consumed_at')->nullable();
            $table->timestampsTz();
            $table->foreign('device_id')->references('id')->on('devices')->cascadeOnDelete();
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

        Schema::table('telegram_accounts', function (Blueprint $table): void {
            $table->unsignedBigInteger('authorization_generation')->default(1);
            $table->uuid('effective_config_id')->nullable();
            $table->timestampTz('last_reconcile_attempt_at')->nullable();
            $table->timestampTz('last_reconcile_success_at')->nullable();
            $table->timestampTz('next_reconcile_at')->nullable();
            $table->unsignedSmallInteger('reconcile_failures')->default(0);
            $table->unsignedBigInteger('reconcile_blocked_revision')->nullable();
        });
    }

    public function down(): void
    {
        Schema::table('telegram_accounts', function (Blueprint $table): void {
            $table->dropColumn(['authorization_generation', 'effective_config_id', 'last_reconcile_attempt_at',
                'last_reconcile_success_at', 'next_reconcile_at', 'reconcile_failures', 'reconcile_blocked_revision']);
        });
        Schema::dropIfExists('scheduler_statuses');
        Schema::dropIfExists('quick_replies');
        Schema::dropIfExists('configuration_launches');
        Schema::dropIfExists('pairing_requests');
        Schema::dropIfExists('devices');
        Schema::dropIfExists('owner_sessions');
        Schema::dropIfExists('bootstrap_codes');
        Schema::dropIfExists('instances');
    }
};
