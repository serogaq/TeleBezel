<?php

namespace App\Http\Resources;

use App\Data\AccountData;
use Illuminate\Http\JsonResponse;
use Illuminate\Http\Request;
use Illuminate\Http\Resources\Json\JsonResource;

final class TelegramAccountResource extends JsonResource
{
    public static $wrap = null;

    public function __construct(private readonly AccountData $account)
    {
        parent::__construct($account);
    }

    /** @return array<string, mixed> */
    public function toArray(Request $request): array
    {
        $account = $this->account;

        return [
            'id' => $account->id,
            'label' => $account->label,
            'lifecycle' => $account->lifecycle->value,
            'desired_revision' => $account->desired_revision,
            'applied_revision' => $account->applied_revision,
            'proxy' => $account->proxy_id === null ? null : [
                'id' => $account->proxy_id,
                'server' => $account->proxy_server,
                'port' => $account->proxy_port,
                'type' => $account->proxy_type,
            ],
            'runtime' => [
                'available' => $account->runtime_available,
                'authorization_state' => $account->authorization_state,
                'connection_state' => $account->connection_state,
            ],
            'telegram_identity' => $account->telegram_identity,
            'operation' => $account->operation_id === null ? null : [
                'id' => $account->operation_id,
            ],
            'last_error' => $account->last_error_code === null ? null : [
                'code' => $account->last_error_code,
            ],
            'created_at' => $account->created_at?->toISOString(),
            'updated_at' => $account->updated_at?->toISOString(),
        ];
    }

    public function respond(Request $request, string $requestId, int $status = 200): JsonResponse
    {
        return (new ApiResource($this->toArray($request)))->respond($requestId, $status);
    }
}
