<?php

return [
    'version' => trim((string) file_get_contents(base_path('VERSION'))),
    'tdlib' => [
        'base_url' => env('TDLIB_BASE_URL', 'http://backend-tdlib:8081'),
        'token' => env('TDLIB_INTERNAL_TOKEN'),
        'token_file' => env('TDLIB_INTERNAL_TOKEN_FILE'),
    ],
];
