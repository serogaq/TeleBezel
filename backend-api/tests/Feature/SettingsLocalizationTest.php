<?php

use App\Services\LocalizationService;

test('the settings page follows the browser language and the saved choice', function (): void {
    $this->get('/settings')->assertOk()->assertSee('<html lang="en">', false)->assertSee('Owner access');
    $this->withHeader('Accept-Language', 'ru-RU,ru;q=0.9,en;q=0.5')->get('/settings')->assertOk()
        ->assertSee('<html lang="ru">', false)->assertSee('Доступ владельца')->assertDontSee('Owner access');
    $this->withHeader('Accept-Language', 'ru-RU')->withUnencryptedCookie(LocalizationService::COOKIE, 'en')->get('/settings')
        ->assertSee('Owner access');
    $this->flushHeaders()->withUnencryptedCookie(LocalizationService::COOKIE, 'de')->get('/settings')->assertSee('<html lang="en">', false);
});

test('the page hands the script exactly the strings of its language', function (): void {
    $page = $this->withHeader('Accept-Language', 'ru')->get('/settings')->getContent();
    expect(preg_match('#<script id="strings" type="application/json">(.*?)</script>#s', (string) $page, $matches))->toBe(1);
    $strings = json_decode(html_entity_decode($matches[1]), true);
    expect($strings['sign_in'])->toBe('Вход')->and(array_keys($strings))->toBe(array_keys(app(LocalizationService::class)->strings('en')));
});
