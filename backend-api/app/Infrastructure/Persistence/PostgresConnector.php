<?php

declare(strict_types=1);

namespace App\Infrastructure\Persistence;

use Illuminate\Database\Connectors\PostgresConnector as BaseConnector;
use InvalidArgumentException;
use PDO;

final class PostgresConnector extends BaseConnector
{
    /** @param array<array-key, mixed> $config */
    public function connect(array $config): PDO
    {
        $connection = parent::connect($config);
        foreach (['statement_timeout', 'lock_timeout'] as $setting) {
            if (! array_key_exists($setting, $config) || $config[$setting] === null || $config[$setting] === '') {
                continue;
            }
            $milliseconds = filter_var($config[$setting], FILTER_VALIDATE_INT);
            if ($milliseconds === false || $milliseconds < 0) {
                throw new InvalidArgumentException("Invalid PostgreSQL {$setting}.");
            }
            $connection->exec("set {$setting} = {$milliseconds}");
        }

        return $connection;
    }
}
