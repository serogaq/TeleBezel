<?php

use Illuminate\Database\Migrations\Migration;
use Illuminate\Database\Schema\Blueprint;
use Illuminate\Support\Facades\DB;
use Illuminate\Support\Facades\Schema;

return new class extends Migration
{
    public function up(): void
    {
        Schema::create('message_sends', function (Blueprint $table): void {
            $table->uuid('id')->primary();
            $table->uuid('instance_id');
            $table->uuid('token_id');
            $table->uuid('account_id');
            $table->uuid('storage_generation');
            $table->unsignedBigInteger('authorization_generation');
            $table->bigInteger('chat_id');
            $table->bigInteger('reply_to_message_id')->nullable();
            $table->char('key_hash', 64);
            $table->char('request_hmac', 64);
            $table->string('state', 16);
            $table->bigInteger('message_id')->nullable();
            $table->string('error_code', 64)->nullable();
            $table->unsignedInteger('retry_after')->nullable();
            $table->boolean('retryable')->default(false);
            $table->boolean('reply_dropped')->default(false);
            $table->timestampsTz();
            $table->unique(['token_id', 'account_id', 'key_hash']);
            $table->index(['token_id', 'account_id', 'updated_at']);
            $table->index('created_at');
            $table->foreign('instance_id')->references('id')->on('instances')->cascadeOnDelete();
            $table->foreign('token_id')->references('id')->on('access_tokens')->cascadeOnDelete();
            $table->foreign('account_id')->references('id')->on('telegram_accounts')->cascadeOnDelete();
        });
        DB::statement("ALTER TABLE message_sends ADD CONSTRAINT message_sends_state_check CHECK (state IN ('dispatching','pending','sent','failed','unknown'))");
        DB::statement("ALTER TABLE message_sends ADD CONSTRAINT message_sends_sent_check CHECK (state != 'sent' OR message_id IS NOT NULL)");
    }

    public function down(): void
    {
        Schema::dropIfExists('message_sends');
    }
};
