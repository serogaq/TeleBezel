<?php

declare(strict_types=1);

namespace App\Http\Resources;

final class ActionResource extends ApiResource
{
    public function __construct(string $action, bool $completed = true)
    {
        if (! in_array($action, ['revoked', 'deleted'], true)) {
            throw new \LogicException('Unknown action result');
        }
        parent::__construct([
            $action => $completed,
        ]);
    }
}
