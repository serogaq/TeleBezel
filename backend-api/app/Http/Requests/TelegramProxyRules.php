<?php

namespace App\Http\Requests;

use Illuminate\Validation\Rule;

final class TelegramProxyRules
{
    /** @return array<string, mixed> */
    public static function forPrefix(string $prefix = ''): array
    {
        $field = static fn (string $name): string => $prefix.$name;
        $mode = $field('mode');

        return [
            $mode => ['required', Rule::in(['inherit', 'direct', 'socks5', 'http', 'mtproto'])],
            $field('id') => ['required_unless:'.$mode.',inherit', 'prohibited_if:'.$mode.',inherit', 'uuid'],
            $field('host') => ['required_if:'.$mode.',socks5,http,mtproto', 'prohibited_if:'.$mode.',inherit,direct', 'string', 'max:255'],
            $field('port') => ['required_if:'.$mode.',socks5,http,mtproto', 'prohibited_if:'.$mode.',inherit,direct', 'integer', 'between:1,65535'],
            $field('http_only') => ['prohibited_unless:'.$mode.',http', 'boolean'],
            $field('username') => ['prohibited_if:'.$mode.',inherit,direct,mtproto', 'string', 'max:255'],
            $field('password') => ['prohibited_if:'.$mode.',inherit,direct,mtproto', 'string', 'max:255'],
            $field('secret') => ['required_if:'.$mode.',mtproto', 'prohibited_unless:'.$mode.',mtproto', 'string', 'max:512'],
        ];
    }
}
