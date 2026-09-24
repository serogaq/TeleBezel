<?php

declare(strict_types=1);

namespace App\Http\Resources;

use App\Data\MessageSendData;

final class SendResource extends ApiResource
{
    public function __construct(MessageSendData $send)
    {
        parent::__construct([
            'operation' => $send->toArray(),
        ]);
    }
}
