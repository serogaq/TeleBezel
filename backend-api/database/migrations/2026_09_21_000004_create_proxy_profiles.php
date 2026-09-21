<?php

use Illuminate\Database\Migrations\Migration;
use Illuminate\Database\Schema\Blueprint;
use Illuminate\Support\Facades\Crypt;
use Illuminate\Support\Facades\DB;
use Illuminate\Support\Facades\Schema;
use Illuminate\Support\Str;

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
            $table->uuid('active_proxy_profile_id')->nullable();
            $table->string('proxy_failure_action', 16)->default('direct');
            $table->unsignedSmallInteger('proxy_connect_timeout_seconds')->default(10);
            $table->timestampTz('proxy_activated_at')->nullable();
            $table->timestampTz('proxy_failure_started_at')->nullable();
            $table->foreign('active_proxy_profile_id')->references('id')->on('proxy_profiles')->nullOnDelete();
        });

        foreach (DB::table('instances')->whereNotNull('global_proxy')->get(['id', 'global_proxy']) as $instance) {
            try {
                $proxy = json_decode(Crypt::decryptString($instance->global_proxy), true, 16, JSON_THROW_ON_ERROR);
            } catch (Throwable) {
                continue;
            }
            if (! is_array($proxy) || ! in_array($proxy['mode'] ?? null, ['socks5', 'http', 'mtproto'], true)) {
                continue;
            }
            $id = (string) Str::uuid();
            $credentials = array_filter(['password' => $proxy['password'] ?? null, 'secret' => $proxy['secret'] ?? null], fn ($value) => $value !== null);
            DB::table('proxy_profiles')->insert(['id' => $id, 'instance_id' => $instance->id, 'label' => 'Imported proxy',
                'mode' => $proxy['mode'], 'host' => $proxy['host'], 'port' => $proxy['port'],
                'http_only' => $proxy['http_only'] ?? false, 'username' => $proxy['username'] ?? null,
                'credentials' => $credentials === [] ? null : Crypt::encryptString(json_encode($credentials, JSON_THROW_ON_ERROR)),
                'position' => 0, 'created_at' => now(), 'updated_at' => now()]);
            DB::table('instances')->where('id', $instance->id)->update(['active_proxy_profile_id' => $id,
                'proxy_activated_at' => now()]);
        }
    }

    public function down(): void
    {
        Schema::table('instances', function (Blueprint $table): void {
            $table->dropForeign(['active_proxy_profile_id']);
            $table->dropColumn(['active_proxy_profile_id', 'proxy_failure_action', 'proxy_connect_timeout_seconds',
                'proxy_activated_at', 'proxy_failure_started_at']);
        });
        Schema::dropIfExists('proxy_profiles');
    }
};
