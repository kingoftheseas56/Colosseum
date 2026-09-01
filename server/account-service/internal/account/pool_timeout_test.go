package account

import (
	"context"
	"errors"
	"fmt"
	"net"
	"testing"
	"time"

	"github.com/jackc/pgx/v5/pgxpool"
)

func TestAcquireDatabaseConnectionTimesOutBeforeTransaction(t *testing.T) {
	fixture := newServiceFixture(t)
	fixture.service.databaseAcquireTimeout = 10 * time.Millisecond

	maxConnections := fixture.pool.Config().MaxConns
	held := make([]*pgxpool.Conn, 0, maxConnections)
	for int32(len(held)) < maxConnections {
		conn, err := fixture.pool.Acquire(context.Background())
		if err != nil {
			t.Fatalf("Acquire(%d) error = %v", len(held), err)
		}
		held = append(held, conn)
	}
	defer func() {
		for _, conn := range held {
			conn.Release()
		}
	}()

	started := time.Now()
	conn, err := fixture.service.acquireDatabaseConnection(context.Background())
	if conn != nil {
		conn.Release()
		t.Fatal("acquireDatabaseConnection() returned a connection while pool was exhausted")
	}
	if !errors.Is(err, ErrDatabaseBusy) {
		t.Fatalf("acquireDatabaseConnection() error = %v, want ErrDatabaseBusy", err)
	}
	if elapsed := time.Since(started); elapsed > time.Second {
		t.Fatalf("acquireDatabaseConnection() took %v, want bounded timeout", elapsed)
	}
}

func TestAcquireDatabaseConnectionReturnsTypedBusyOnDeadline(t *testing.T) {
	listener, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatalf("net.Listen() error = %v", err)
	}
	accepted := make(chan net.Conn, 1)
	stop := make(chan struct{})
	go func() {
		conn, acceptErr := listener.Accept()
		if acceptErr != nil {
			return
		}
		select {
		case accepted <- conn:
		case <-stop:
			_ = conn.Close()
		}
	}()
	defer func() {
		close(stop)
		_ = listener.Close()
		select {
		case conn := <-accepted:
			_ = conn.Close()
		default:
		}
	}()

	cfg, err := pgxpool.ParseConfig(fmt.Sprintf(
		"postgres://user:password@%s/db?sslmode=disable",
		listener.Addr().String()))
	if err != nil {
		t.Fatalf("ParseConfig() error = %v", err)
	}
	cfg.MaxConns = 1
	cfg.MinConns = 0
	cfg.ConnConfig.ConnectTimeout = time.Minute
	pool, err := pgxpool.NewWithConfig(context.Background(), cfg)
	if err != nil {
		t.Fatalf("NewWithConfig() error = %v", err)
	}
	defer pool.Close()

	service := &Service{
		pool:                   pool,
		databaseAcquireTimeout: 20 * time.Millisecond,
	}
	started := time.Now()
	conn, err := service.acquireDatabaseConnection(context.Background())
	if conn != nil {
		conn.Release()
		t.Fatal("acquireDatabaseConnection() returned a connection from a stalled server")
	}
	var busy *DatabaseBusyError
	if !errors.As(err, &busy) {
		t.Fatalf("acquireDatabaseConnection() error = %v, want DatabaseBusyError", err)
	}
	if !errors.Is(err, ErrDatabaseBusy) {
		t.Fatalf("acquireDatabaseConnection() error = %v, want ErrDatabaseBusy", err)
	}
	if elapsed := time.Since(started); elapsed > time.Second {
		t.Fatalf("acquireDatabaseConnection() took %v, want bounded timeout", elapsed)
	}
}
