<?php

return [
    'version' => trim((string) file_get_contents(base_path('VERSION'))),
    'tdlib' => [
        'base_url' => env('TDLIB_BASE_URL', 'http://backend-tdlib:8081'),
        'token' => env('TDLIB_INTERNAL_TOKEN'),
        'token_file' => env('TDLIB_INTERNAL_TOKEN_FILE'),
    ],
    'media' => [
        'base_url' => env('MEDIA_BASE_URL', 'http://backend-media:8082'),
        'token' => env('MEDIA_INTERNAL_TOKEN'),
        'cache_directory' => env('MEDIA_CACHE_DIRECTORY', storage_path('framework/cache/media')),
        'cache_bytes' => (int) env('MEDIA_CACHE_BYTES', 58720256),
    ],
];
