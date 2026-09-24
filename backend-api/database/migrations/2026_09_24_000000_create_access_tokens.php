<?php

use Illuminate\Database\Migrations\Migration;
use Illuminate\Database\Schema\Blueprint;
use Illuminate\Support\Facades\DB;
use Illuminate\Support\Facades\Schema;

return new class extends Migration
{
    public function up(): void
    {
        Schema::create('access_tokens', function (Blueprint $table): void {
            $table->uuid('id')->primary();
            $table->uuid('instance_id');
            $table->string('name', 100);
            $table->string('type', 16);
            $table->string('token_prefix', 12);
            $table->char('token_hash', 64)->unique();
            $table->jsonb('claims');
            $table->timestampTz('expires_at')->nullable();
            $table->unsignedInteger('idle_timeout_seconds')->nullable();
            $table->timestampTz('last_active_at')->nullable();
            $table->timestampTz('last_used_at')->nullable();
            $table->timestampTz('revoked_at')->nullable();
            $table->timestampsTz();
            $table->index(['instance_id', 'type', 'created_at']);
            $table->index('expires_at');
            $table->foreign('instance_id')->references('id')->on('instances')->cascadeOnDelete();
        });
        DB::statement("ALTER TABLE access_tokens ADD CONSTRAINT access_tokens_type_check CHECK (type IN ('device','maintenance'))");
        DB::statement("ALTER TABLE access_tokens ADD CONSTRAINT access_tokens_claims_check CHECK (jsonb_typeof(claims->'permissions') = 'array' AND jsonb_exists(claims, 'accounts'))");
        DB::statement('ALTER TABLE access_tokens ADD CONSTRAINT access_tokens_idle_check CHECK (idle_timeout_seconds IS NULL OR idle_timeout_seconds > 0)');

        Schema::create('device_profiles', function (Blueprint $table): void {
            $table->uuid('token_id')->primary();
            $table->string('locale', 8)->default('auto');
            $table->uuid('default_account_id')->nullable();
            $table->string('chat_list', 16)->default('main');
            $table->timestampsTz();
            $table->foreign('token_id')->references('id')->on('access_tokens')->cascadeOnDelete();
            $table->foreign('default_account_id')->references('id')->on('telegram_accounts')->nullOnDelete();
        });

        Schema::create('account_idempotency_keys', function (Blueprint $table): void {
            $table->uuid('token_id');
            $table->char('key_hash', 64);
            $table->char('request_hash', 64);
            $table->uuid('telegram_account_id');
            $table->timestampsTz();
            $table->primary(['token_id', 'key_hash']);
            $table->index('created_at');
            $table->foreign('token_id')->references('id')->on('access_tokens')->cascadeOnDelete();
            $table->foreign('telegram_account_id')->references('id')->on('telegram_accounts')->restrictOnDelete();
        });
    }

    public function down(): void
    {
        Schema::dropIfExists('account_idempotency_keys');
        Schema::dropIfExists('device_profiles');
        Schema::dropIfExists('access_tokens');
    }
};
