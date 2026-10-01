<?php

declare(strict_types=1);

namespace App\Infrastructure\Media;

use App\Contracts\RenditionStore;
use FilesystemIterator;

// Renditions live on the container's own tmpfs: bounded by size, evicted by
// last use, gone on restart. They never hold anything the account could not
// read again from Telegram.
final class FilesystemRenditionStore implements RenditionStore
{
    public function get(string $accountId, string $key): ?string
    {
        $path = $this->path($accountId, $key);
        if ($path === null || ! is_file($path)) {
            return null;
        }
        $content = @file_get_contents($path);
        if ($content === false) {
            return null;
        }
        @touch($path);

        return $content;
    }

    public function put(string $accountId, string $key, string $rendition): void
    {
        $path = $this->path($accountId, $key);
        if ($path === null) {
            return;
        }
        $directory = dirname($path);
        if (! is_dir($directory) && ! @mkdir($directory, 0700, true) && ! is_dir($directory)) {
            return;
        }
        $temporary = $path.'.'.bin2hex(random_bytes(4));
        if (@file_put_contents($temporary, $rendition, LOCK_EX) === false || ! @rename($temporary, $path)) {
            @unlink($temporary);

            return;
        }
        $this->trim();
    }

    public function forgetAccount(string $accountId): void
    {
        $directory = $this->root().'/'.$accountId;
        if (preg_match('/\A[0-9a-f-]{36}\z/', $accountId) !== 1 || ! is_dir($directory)) {
            return;
        }
        foreach (new FilesystemIterator($directory) as $file) {
            if ($file instanceof \SplFileInfo && $file->isFile()) {
                @unlink($file->getPathname());
            }
        }
        @rmdir($directory);
    }

    private function root(): string
    {
        return rtrim(config()->string('telebezel.media.cache_directory'), '/');
    }

    private function path(string $accountId, string $key): ?string
    {
        if (preg_match('/\A[0-9a-f-]{36}\z/', $accountId) !== 1 || preg_match('/\A[0-9a-f]{64}\z/', $key) !== 1) {
            return null;
        }

        return $this->root().'/'.$accountId.'/'.$key.'.tbi';
    }

    private function trim(): void
    {
        $limit = config()->integer('telebezel.media.cache_bytes');
        $files = [];
        $total = 0;
        $found = glob($this->root().'/*/*.tbi');
        foreach ($found === false ? [] : $found as $file) {
            $size = @filesize($file);
            $used = @filemtime($file);
            if ($size === false || $used === false) {
                continue;
            }
            $files[] = [$used, $size, $file];
            $total += $size;
        }
        if ($total <= $limit) {
            return;
        }
        usort($files, fn (array $left, array $right): int => $left[0] <=> $right[0]);
        foreach ($files as [, $size, $file]) {
            if ($total <= $limit) {
                break;
            }
            if (@unlink($file)) {
                $total -= $size;
            }
        }
    }
}
