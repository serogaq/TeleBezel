# TeleBezel backend-api

Laravel 13/Octane public API. PostgreSQL stores only application-owned state;
the service reaches TDLib exclusively through the private authenticated HTTP
contract. Run migrations explicitly and never from the container entrypoint.
