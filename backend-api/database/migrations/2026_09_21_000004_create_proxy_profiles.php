<?php

use Illuminate\Database\Migrations\Migration;
use Illuminate\Database\Schema\Blueprint;
use Illuminate\Support\Facades\Schema;

return new class extends Migration
{
    public function up(): void
    {
        Schema::create('proxy_profiles', function (Blueprint $table): void {
            $table->uuid('id')->primary();
            $table->uuid('instance_id');
            $table->string('label', 100);
            $table->string('mode', 16);
            $table->string('host', 255);
            $table->unsignedSmallInteger('port');
            $table->boolean('http_only')->default(false);
            $table->string('username', 255)->nullable();
            $table->text('credentials')->nullable();
            $table->unsignedSmallInteger('position');
            $table->boolean('last_ping_ok')->nullable();
            $table->unsignedInteger('last_ping_ms')->nullable();
            $table->string('last_ping_error', 64)->nullable();
            $table->timestampTz('last_ping_at')->nullable();
            $table->timestampsTz();
            $table->foreign('instance_id')->references('id')->on('instances')->cascadeOnDelete();
            $table->unique(['instance_id', 'position']);
        });
        Schema::table('instances', function (Blueprint $table): void {
            $table->foreign('active_proxy_profile_id')->references('id')->on('proxy_profiles')->nullOnDelete();
        });

    }

    public function down(): void
    {
        Schema::table('instances', function (Blueprint $table): void {
            $table->dropForeign(['active_proxy_profile_id']);
        });
        Schema::dropIfExists('proxy_profiles');
    }
};
