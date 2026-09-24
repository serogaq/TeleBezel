<?php

declare(strict_types=1);

namespace App\Enums;

enum Permission: string
{
    case AccountsRead = 'accounts.read';
    case AccountsManage = 'accounts.manage';
    case MessagesRead = 'messages.read';
    case MessagesSend = 'messages.send';
    case QuickRepliesRead = 'quick_replies.read';
    case QuickRepliesManage = 'quick_replies.manage';
    case SettingsManage = 'settings.manage';
    case DevicePreferences = 'device.preferences';
}
