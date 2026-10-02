// Command telebezel-media serves the watch image pipeline to backend-api.
package main

import (
	"context"
	"errors"
	"log/slog"
	"net/http"
	"os"
	"os/signal"
	"strconv"
	"strings"
	"syscall"
	"time"

	"github.com/serogaq/TeleBezel/backend-media/internal/server"
)

func environment(name, fallback string) string {
	if value := os.Getenv(name); value != "" {
		return value
	}
	return fallback
}

func token() (string, error) {
	if file := os.Getenv("MEDIA_INTERNAL_TOKEN_FILE"); file != "" {
		value, err := os.ReadFile(file)
		if err != nil {
			return "", errors.New("the token file cannot be read")
		}
		return strings.TrimSpace(string(value)), nil
	}
	return strings.TrimSpace(os.Getenv("MEDIA_INTERNAL_TOKEN")), nil
}

func health(address string) int {
	client := http.Client{Timeout: 2 * time.Second}
	response, err := client.Get("http://" + address + "/healthz")
	if err != nil {
		return 1
	}
	defer response.Body.Close()
	if response.StatusCode != http.StatusOK {
		return 1
	}
	return 0
}

func main() {
	logger := slog.New(slog.NewJSONHandler(os.Stdout, nil))
	address := environment("MEDIA_LISTEN_ADDRESS", "0.0.0.0:8082")
	if len(os.Args) == 2 && os.Args[1] == "--healthcheck" {
		os.Exit(health(strings.Replace(address, "0.0.0.0", "127.0.0.1", 1)))
	}
	secret, err := token()
	if err != nil || len(secret) < 32 {
		logger.Error("startup", "error", "an internal token of at least 32 characters is required")
		os.Exit(2)
	}
	concurrency, err := strconv.Atoi(environment("MEDIA_CONCURRENCY", "2"))
	if err != nil || concurrency < 1 || concurrency > 16 {
		logger.Error("startup", "error", "MEDIA_CONCURRENCY must be between 1 and 16")
		os.Exit(2)
	}
	service := server.New(secret, concurrency, 3*time.Second, logger)
	httpServer := &http.Server{
		Addr:              address,
		Handler:           service.Handler(),
		ReadHeaderTimeout: 5 * time.Second,
		ReadTimeout:       10 * time.Second,
		WriteTimeout:      10 * time.Second,
		IdleTimeout:       30 * time.Second,
		MaxHeaderBytes:    16 << 10,
	}
	stop, cancel := signal.NotifyContext(context.Background(), syscall.SIGINT, syscall.SIGTERM)
	defer cancel()
	go func() {
		<-stop.Done()
		shutdown, done := context.WithTimeout(context.Background(), 5*time.Second)
		defer done()
		_ = httpServer.Shutdown(shutdown)
	}()
	logger.Info("startup", "address", address, "concurrency", concurrency)
	if err := httpServer.ListenAndServe(); err != nil && !errors.Is(err, http.ErrServerClosed) {
		logger.Error("serve", "error", err.Error())
		os.Exit(1)
	}
}
