<?php

namespace App\Http\Controllers;

use App\Models\ProxyProfile;
use App\Services\ProxyProfileService;
use Illuminate\Http\JsonResponse;
use Illuminate\Http\Request;
use Illuminate\Validation\Rule;
use Symfony\Component\HttpFoundation\Response;

final class ProxyProfileController extends Controller
{
    public function index(Request $request, ProxyProfileService $profiles): JsonResponse
    {
        return response()->json(['data' => $profiles->all((string) $request->attributes->get('instance_id'))]);
    }

    public function store(Request $request, ProxyProfileService $profiles): JsonResponse
    {
        $data = $request->validate(['label' => ['required', 'string', 'max:100'],
            'mode' => ['required', Rule::in(['socks5', 'http', 'mtproto'])], 'host' => ['required', 'string', 'max:255'],
            'port' => ['required', 'integer', 'between:1,65535'], 'http_only' => ['sometimes', 'boolean'],
            'username' => ['nullable', 'string', 'max:255', 'prohibited_if:mode,mtproto'],
            'password' => ['nullable', 'string', 'max:255', 'prohibited_if:mode,mtproto'],
            'secret' => ['nullable', 'string', 'max:512', 'required_if:mode,mtproto', 'prohibited_unless:mode,mtproto']]);

        return response()->json(['data' => $profiles->create((string) $request->attributes->get('instance_id'),
            $data, (string) $request->attributes->get('request_id'))], 201);
    }

    public function ping(string $id, Request $request, ProxyProfileService $profiles): JsonResponse
    {
        $profile = $this->profile($id, $request);

        return response()->json(['data' => $profiles->ping($profile, (string) $request->attributes->get('request_id'))]);
    }

    public function pingAll(Request $request, ProxyProfileService $profiles): JsonResponse
    {
        return response()->json(['data' => $profiles->pingAll((string) $request->attributes->get('instance_id'),
            (string) $request->attributes->get('request_id'))]);
    }

    public function activate(string $id, Request $request, ProxyProfileService $profiles): JsonResponse
    {
        $profiles->activate((string) $request->attributes->get('instance_id'), $this->profile($id, $request),
            (string) $request->attributes->get('request_id'));

        return response()->json(['data' => $profiles->all((string) $request->attributes->get('instance_id'))]);
    }

    public function direct(Request $request, ProxyProfileService $profiles): JsonResponse
    {
        $profiles->activate((string) $request->attributes->get('instance_id'), null,
            (string) $request->attributes->get('request_id'));

        return response()->json(['data' => $profiles->all((string) $request->attributes->get('instance_id'))]);
    }

    public function configure(Request $request, ProxyProfileService $profiles): JsonResponse
    {
        $data = $request->validate(['failure_action' => ['required', Rule::in(['direct', 'next'])],
            'connect_timeout_seconds' => ['required', 'integer', 'between:3,300']]);
        $profiles->configure((string) $request->attributes->get('instance_id'), $data);

        return response()->json(['data' => $data]);
    }

    public function destroy(string $id, Request $request, ProxyProfileService $profiles): Response
    {
        $profiles->delete($this->profile($id, $request));

        return response()->noContent();
    }

    private function profile(string $id, Request $request): ProxyProfile
    {
        return ProxyProfile::query()->whereKey($id)->where('instance_id', $request->attributes->get('instance_id'))->firstOrFail();
    }
}
