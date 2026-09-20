<?php

use Illuminate\Database\Migrations\Migration;
use Illuminate\Database\Schema\Blueprint;
use Illuminate\Support\Facades\DB;
use Illuminate\Support\Facades\Schema;

return new class extends Migration
{
    public function up(): void
    {
        Schema::create('telegram_accounts', function (Blueprint $table): void {
            $table->uuid('id')->primary();
            $table->string('label', 100)->nullable();
            $table->uuid('storage_generation');
            $table->string('lifecycle', 32);
            $table->unsignedBigInteger('desired_revision')->default(1);
            $table->unsignedBigInteger('applied_revision')->nullable();
            $table->uuid('proxy_id')->nullable();
            $table->string('proxy_server', 255)->nullable();
            $table->unsignedSmallInteger('proxy_port')->nullable();
            $table->string('proxy_type', 16)->nullable();
            $table->boolean('runtime_available')->default(false);
            $table->string('authorization_state', 64)->nullable();
            $table->string('connection_state', 64)->nullable();
            $table->string('last_error_code', 100)->nullable();
            $table->uuid('operation_id')->nullable();
            $table->uuid('logout_operation_id')->nullable();
            $table->timestampTz('logout_completed_at')->nullable();
            $table->timestampsTz();
            $table->softDeletesTz();
            $table->index(['lifecycle', 'updated_at']);
        });
        DB::statement("ALTER TABLE telegram_accounts ADD CONSTRAINT telegram_accounts_lifecycle_check CHECK (lifecycle IN ('provisioning','active','logout_pending','removing','removed'))");
        DB::statement('ALTER TABLE telegram_accounts ADD CONSTRAINT telegram_accounts_revision_check CHECK (desired_revision > 0 AND (applied_revision IS NULL OR applied_revision <= desired_revision))');
        DB::statement("ALTER TABLE telegram_accounts ADD CONSTRAINT telegram_accounts_proxy_label_check CHECK ((proxy_id IS NULL AND proxy_server IS NULL AND proxy_port IS NULL AND proxy_type IS NULL) OR (proxy_id IS NOT NULL AND proxy_type IN ('direct','socks5','http','mtproto') AND ((proxy_type = 'direct' AND proxy_server IS NULL AND proxy_port IS NULL) OR (proxy_type != 'direct' AND proxy_server IS NOT NULL AND proxy_port BETWEEN 1 AND 65535))))");
        DB::statement("ALTER TABLE telegram_accounts ADD CONSTRAINT telegram_accounts_removed_scrubbed_check CHECK (lifecycle != 'removed' OR (label IS NULL AND proxy_id IS NULL AND proxy_server IS NULL AND proxy_port IS NULL AND proxy_type IS NULL))");

        Schema::create('account_idempotency_keys', function (Blueprint $table): void {
            $table->uuid('api_client_id');
            $table->char('key_hash', 64);
            $table->char('request_hash', 64);
            $table->uuid('telegram_account_id');
            $table->timestampsTz();
            $table->primary(['api_client_id', 'key_hash']);
            $table->foreign('api_client_id')->references('id')->on('api_clients')->restrictOnDelete();
            $table->foreign('telegram_account_id')->references('id')->on('telegram_accounts')->restrictOnDelete();
        });
    }

    public function down(): void
    {
        Schema::dropIfExists('account_idempotency_keys');
        Schema::dropIfExists('telegram_accounts');
    }
};
