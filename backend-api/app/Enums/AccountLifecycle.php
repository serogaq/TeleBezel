<?php

namespace App\Enums;

enum AccountLifecycle: string
{
    case Provisioning = 'provisioning';
    case Active = 'active';
    case LogoutPending = 'logout_pending';
    case Removing = 'removing';
    case Removed = 'removed';
}
