<?php

use Illuminate\Database\Migrations\Migration;
use Illuminate\Database\Schema\Blueprint;
use Illuminate\Support\Facades\Schema;

return new class extends Migration
{
    public function up(): void
    {
        Schema::create('owner_account_idempotency_keys', function (Blueprint $table): void {
            $table->uuid('owner_session_id');
            $table->char('key_hash', 64);
            $table->char('request_hash', 64);
            $table->uuid('telegram_account_id');
            $table->timestampsTz();
            $table->primary(['owner_session_id', 'key_hash']);
            $table->foreign('owner_session_id')->references('id')->on('owner_sessions')->restrictOnDelete();
            $table->foreign('telegram_account_id')->references('id')->on('telegram_accounts')->restrictOnDelete();
        });
    }

    public function down(): void
    {
        Schema::dropIfExists('owner_account_idempotency_keys');
    }
};
