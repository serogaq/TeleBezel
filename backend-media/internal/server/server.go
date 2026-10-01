// Package server exposes the render pipeline on the private network. It has
// no volumes, no Telegram access and only its own bearer token.
package server

import (
	"context"
	"crypto/subtle"
	"encoding/json"
	"errors"
	"io"
	"log/slog"
	"net/http"
	"strconv"
	"time"

	"github.com/serogaq/TeleBezel/backend-media/internal/render"
)

// Server renders one image per request with bounded concurrency.
type Server struct {
	token    []byte
	slots    chan struct{}
	deadline time.Duration
	log      *slog.Logger
	render   func(context.Context, []byte, render.Spec) (render.Result, error)
}

// New builds the handler set.
func New(token string, concurrency int, deadline time.Duration, logger *slog.Logger) *Server {
	return &Server{token: []byte(token), slots: make(chan struct{}, concurrency), deadline: deadline, log: logger, render: render.Render}
}

// Handler routes the two endpoints the service offers.
func (s *Server) Handler() http.Handler {
	mux := http.NewServeMux()
	mux.HandleFunc("GET /healthz", func(w http.ResponseWriter, _ *http.Request) {
		writeJSON(w, http.StatusOK, map[string]string{"status": "ok"})
	})
	mux.HandleFunc("POST /internal/v1/render", s.renderHandler)
	mux.HandleFunc("/", func(w http.ResponseWriter, _ *http.Request) { failure(w, http.StatusNotFound, "http.not_found") })
	return mux
}

func writeJSON(w http.ResponseWriter, status int, body any) {
	w.Header().Set("Content-Type", "application/json")
	w.Header().Set("Cache-Control", "no-store")
	w.WriteHeader(status)
	_ = json.NewEncoder(w).Encode(body)
}

func failure(w http.ResponseWriter, status int, code string) {
	writeJSON(w, status, map[string]any{"error": map[string]string{"code": code}})
}

func (s *Server) authorized(r *http.Request) bool {
	const prefix = "Bearer "
	header := r.Header.Get("Authorization")
	if len(s.token) == 0 || len(header) <= len(prefix) || header[:len(prefix)] != prefix {
		return false
	}
	return subtle.ConstantTimeCompare([]byte(header[len(prefix):]), s.token) == 1
}

func integer(r *http.Request, name string) (int, bool) {
	value, err := strconv.Atoi(r.URL.Query().Get(name))
	return value, err == nil
}

func (s *Server) renderHandler(w http.ResponseWriter, r *http.Request) {
	started := time.Now()
	status := http.StatusOK
	outcome := "ok"
	size := 0
	defer func() {
		s.log.Info("render", "status", status, "outcome", outcome, "bytes_out", size, "duration_ms", time.Since(started).Milliseconds())
	}()
	if !s.authorized(r) {
		status, outcome = http.StatusUnauthorized, "auth.unauthorized"
		failure(w, status, outcome)
		return
	}
	width, okWidth := integer(r, "width")
	height, okHeight := integer(r, "height")
	budget, okBudget := integer(r, "budget")
	spec, err := render.ParseSpec(width, height, r.URL.Query().Get("shape"), r.URL.Query().Get("formats"), budget)
	if !okWidth || !okHeight || !okBudget || err != nil || len(r.URL.Query()) != 5 {
		status, outcome = http.StatusUnprocessableEntity, "request.invalid"
		failure(w, status, outcome)
		return
	}
	select {
	case s.slots <- struct{}{}:
		defer func() { <-s.slots }()
	default:
		status, outcome = http.StatusServiceUnavailable, "service.busy"
		w.Header().Set("Retry-After", "1")
		failure(w, status, outcome)
		return
	}
	body, err := io.ReadAll(http.MaxBytesReader(w, r.Body, render.MaxInputBytes))
	if err != nil {
		status, outcome = http.StatusRequestEntityTooLarge, "request.body_too_large"
		failure(w, status, outcome)
		return
	}
	ctx, cancel := context.WithTimeout(r.Context(), s.deadline)
	defer cancel()
	result, err := s.render(ctx, body, spec)
	switch {
	case err == nil:
	case errors.Is(err, context.DeadlineExceeded) || errors.Is(err, context.Canceled):
		status, outcome = http.StatusServiceUnavailable, "media.deadline"
		w.Header().Set("Retry-After", "1")
		failure(w, status, outcome)
		return
	case errors.Is(err, render.ErrTooLarge):
		status, outcome = http.StatusUnprocessableEntity, "media.too_large"
		failure(w, status, outcome)
		return
	default:
		status, outcome = http.StatusUnprocessableEntity, "media.unsupported"
		failure(w, status, outcome)
		return
	}
	size = len(result.Data)
	w.Header().Set("Content-Type", "application/octet-stream")
	w.Header().Set("Cache-Control", "no-store")
	w.Header().Set("X-Media-Class", result.Class)
	w.Header().Set("X-Media-Pipeline", render.PipelineVersion)
	w.Header().Set("Content-Length", strconv.Itoa(size))
	_, _ = w.Write(result.Data)
}
