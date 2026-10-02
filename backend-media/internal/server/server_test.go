package server

import (
	"bytes"
	"context"
	"io"
	"log/slog"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
	"time"

	"github.com/serogaq/TeleBezel/backend-media/internal/render"
)

const token = "0123456789abcdef0123456789abcdef"
const query = "/internal/v1/render?width=200&height=228&shape=rect&budget=28672&formats=p4,p2"

func service() *Server {
	return New(token, 1, time.Second, slog.New(slog.NewTextHandler(io.Discard, nil)))
}

func call(t *testing.T, handler http.Handler, method, path, bearer string, body []byte) *httptest.ResponseRecorder {
	t.Helper()
	request := httptest.NewRequest(method, path, bytes.NewReader(body))
	if bearer != "" {
		request.Header.Set("Authorization", "Bearer "+bearer)
	}
	recorder := httptest.NewRecorder()
	handler.ServeHTTP(recorder, request)
	return recorder
}

func TestRequiresTheInternalToken(t *testing.T) {
	handler := service().Handler()
	for _, bearer := range []string{"", "wrong", token + "x"} {
		if response := call(t, handler, "POST", query, bearer, []byte("x")); response.Code != http.StatusUnauthorized {
			t.Fatalf("bearer %q got %d", bearer, response.Code)
		}
	}
	if response := call(t, handler, "GET", "/healthz", "", nil); response.Code != http.StatusOK {
		t.Fatalf("health got %d", response.Code)
	}
}

func TestRejectsInvalidRequests(t *testing.T) {
	handler := service().Handler()
	for _, path := range []string{
		"/internal/v1/render?width=999&height=228&shape=rect&budget=28672&formats=p4",
		"/internal/v1/render?width=200&height=228&shape=rect&budget=28672&formats=p4&path=/etc/passwd",
		"/internal/v1/render?width=200&height=228&shape=rect&budget=28672",
		"/internal/v1/render?url=http://example.com",
	} {
		if response := call(t, handler, "POST", path, token, []byte("x")); response.Code != http.StatusUnprocessableEntity {
			t.Fatalf("%s got %d", path, response.Code)
		}
	}
	if response := call(t, handler, "GET", query, token, nil); response.Code != http.StatusNotFound && response.Code != http.StatusMethodNotAllowed {
		t.Fatalf("GET render got %d", response.Code)
	}
	large := call(t, handler, "POST", query, token, make([]byte, render.MaxInputBytes+1))
	if large.Code != http.StatusRequestEntityTooLarge {
		t.Fatalf("an oversized body got %d", large.Code)
	}
	garbage := call(t, handler, "POST", query, token, []byte("not an image"))
	if garbage.Code != http.StatusUnprocessableEntity || !strings.Contains(garbage.Body.String(), "media.unsupported") {
		t.Fatalf("garbage got %d %s", garbage.Code, garbage.Body.String())
	}
	if strings.Contains(garbage.Body.String(), "not an image") {
		t.Fatal("the response echoes the input")
	}
}

func TestBusyWhenAllSlotsAreTaken(t *testing.T) {
	server := service()
	release := make(chan struct{})
	started := make(chan struct{})
	server.render = func(ctx context.Context, input []byte, spec render.Spec) (render.Result, error) {
		close(started)
		<-release
		return render.Result{Data: []byte("TB")}, nil
	}
	handler := server.Handler()
	done := make(chan int)
	go func() { done <- call(t, handler, "POST", query, token, []byte("x")).Code }()
	<-started
	busy := call(t, handler, "POST", query, token, []byte("x"))
	if busy.Code != http.StatusServiceUnavailable || busy.Header().Get("Retry-After") != "1" {
		t.Fatalf("a second render got %d", busy.Code)
	}
	close(release)
	if code := <-done; code != http.StatusOK {
		t.Fatalf("the first render got %d", code)
	}
}

func TestDeadlineIsReportedAsRetryable(t *testing.T) {
	server := service()
	server.render = func(ctx context.Context, input []byte, spec render.Spec) (render.Result, error) {
		<-ctx.Done()
		return render.Result{}, ctx.Err()
	}
	server.deadline = 10 * time.Millisecond
	response := call(t, server.Handler(), "POST", query, token, []byte("x"))
	if response.Code != http.StatusServiceUnavailable || !strings.Contains(response.Body.String(), "media.deadline") {
		t.Fatalf("a slow render got %d %s", response.Code, response.Body.String())
	}
}
