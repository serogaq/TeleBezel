<?php

declare(strict_types=1);

namespace Tests\Integration;

use App\Support\Values;
use FilesystemIterator;
use RecursiveDirectoryIterator;
use RecursiveIteratorIterator;
use RuntimeException;
use SplFileInfo;

/** Owns a real C++ HTTP server; only its Telegram transport is fake. */
final class TdlibProcess
{
    /** @var resource */
    private $process;

    /** @var array<int, resource> */
    private array $pipes;

    public string $url;

    private string $directory;

    private bool $stopped = false;

    public function __construct()
    {
        $this->directory = sys_get_temp_dir().'/telebezel-functional-'.bin2hex(random_bytes(12));
        mkdir($this->directory, 0700);
        $this->launch();
    }

    public function restart(): void
    {
        $this->halt();
        $this->stopped = false;
        $this->launch();
    }

    private function launch(): void
    {
        $binary = getenv('TDLIB_FIXTURE_BIN');
        if ($binary === false || ! is_executable($binary)) {
            throw new RuntimeException('Run make functional-test to build and run the TDLib integration fixture.');
        }
        $pipes = [];
        $process = proc_open([$binary, '0', $this->directory], [
            0 => ['pipe', 'r'],
            1 => ['pipe', 'w'],
            2 => ['file', $this->directory.'/server.log', 'a'],
        ], $pipes);
        if (! is_resource($process)) {
            throw new RuntimeException('Cannot launch fixture');
        }
        $this->process = $process;
        $this->pipes = $pipes;
        stream_set_timeout($this->pipes[1], 10);
        $line = fgets($this->pipes[1]);
        $ready = $line === false ? null : json_decode($line, true);
        if (! is_array($ready) || ! isset($ready['port']) || ! is_int($ready['port']) || $ready['port'] < 1 || $ready['port'] > 65535) {
            $logs = $this->logs();
            $this->stop();
            throw new RuntimeException('Fixture failed to bind HTTP: '.$logs);
        }
        $this->url = 'http://127.0.0.1:'.$ready['port'];

    }

    /** @param array<string, mixed> $command
     * @return array<string, mixed> */
    public function control(array $command): array
    {
        fwrite($this->pipes[0], json_encode($command, JSON_THROW_ON_ERROR)."\n");
        $line = fgets($this->pipes[1]);
        if ($line === false) {
            throw new RuntimeException('Fixture control timed out: '.$this->logs());
        }
        $result = json_decode($line, true, flags: JSON_THROW_ON_ERROR);
        if (! is_array($result) || isset($result['error'])) {
            throw new RuntimeException('Fixture rejected command: '.$line);
        }

        return Values::object($result);
    }

    public function logs(): string
    {
        $content = file_get_contents($this->directory.'/server.log');

        return $content === false ? '' : $content;
    }

    public function stop(): void
    {
        if ($this->stopped) {
            return;
        }
        $this->halt();
        $files = new RecursiveIteratorIterator(new RecursiveDirectoryIterator($this->directory, FilesystemIterator::SKIP_DOTS), RecursiveIteratorIterator::CHILD_FIRST);
        foreach ($files as $file) {
            if (! $file instanceof SplFileInfo) {
                throw new RuntimeException('Unexpected fixture file');
            }
            $file->isDir() ? rmdir($file->getPathname()) : unlink($file->getPathname());
        }
        rmdir($this->directory);
    }

    private function halt(): void
    {
        $this->stopped = true;
        foreach ($this->pipes as $pipe) {
            if (is_resource($pipe)) {
                fclose($pipe);
            }
        }
        for ($attempt = 0; $attempt < 100; $attempt++) {
            if (! proc_get_status($this->process)['running']) {
                break;
            }
            usleep(10000);
        }
        if (proc_get_status($this->process)['running']) {
            proc_terminate($this->process);
        }
        proc_close($this->process);
    }
}
