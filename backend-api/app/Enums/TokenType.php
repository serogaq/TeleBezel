<?php

declare(strict_types=1);

namespace App\Enums;

enum TokenType: string
{
    case Device = 'device';
    case Maintenance = 'maintenance';

    /** @return list<Permission> */
    public function permissions(): array
    {
        return match ($this) {
            self::Device => [Permission::AccountsRead, Permission::MessagesRead, Permission::MessagesSend, Permission::QuickRepliesRead, Permission::DevicePreferences],
            self::Maintenance => [Permission::AccountsRead, Permission::AccountsManage, Permission::SettingsManage, Permission::QuickRepliesManage, Permission::QuickRepliesRead],
        };
    }

    /** @return list<string> */
    public function permissionNames(): array
    {
        return array_map(fn (Permission $permission): string => $permission->value, $this->permissions());
    }

    public function allows(string $permission): bool
    {
        return in_array($permission, $this->permissionNames(), true);
    }
}
