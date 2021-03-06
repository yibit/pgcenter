package top

import (
	"fmt"
	"github.com/lesovsky/pgcenter/internal/postgres"
	"github.com/stretchr/testify/assert"
	"sync"
	"testing"
	"time"
)

func Test_killSingle(t *testing.T) {
	victim, err := postgres.NewTestConnect()
	assert.NoError(t, err)
	defer victim.Close()

	var pid string
	err = victim.QueryRow("select pg_backend_pid()::text").Scan(&pid)
	assert.NoError(t, err)

	testcases := []struct {
		pid  string
		mode string
		want string
	}{
		{pid: pid, mode: "cancel", want: "Signals: done"},
		{pid: pid, mode: "terminate", want: "Signals: done"},
		{pid: pid, mode: "terminate", want: "Signals: done"}, // attempt to terminate the previously terminated pid should not fail
		{pid: "invalid", mode: "terminate", want: `Signals: do nothing, strconv.Atoi: parsing "invalid": invalid syntax`},
		{pid: pid, mode: "invalid", want: "Signals: do nothing, unknown mode"},
	}

	db, err := postgres.NewTestConnect()
	assert.NoError(t, err)

	for _, tc := range testcases {
		assert.Equal(t, tc.want, killSingle(db, tc.mode, tc.pid))
	}

	db.Close()
	assert.Equal(t, "Signals: do nothing, conn closed", killSingle(db, "cancel", pid))
}

func Test_killGroup(t *testing.T) {
	testcases := []struct {
		mode string
		mask int
		want string
	}{
		{mode: "cancel", mask: groupIdle, want: "Signals: cancelled"},
		{mode: "cancel", mask: groupActive, want: "Signals: cancelled"},
		{mode: "cancel", mask: groupIdleXact, want: "Signals: cancelled"},
		{mode: "terminate", mask: groupIdle, want: "Signals: terminated"},
		{mode: "terminate", mask: groupActive, want: "Signals: terminated"},
		{mode: "terminate", mask: groupIdleXact, want: "Signals: terminated"},
	}

	db, err := postgres.NewTestConnect()
	assert.NoError(t, err)
	defer db.Close()

	app := &app{
		config: newConfig(),
		db:     db,
	}

	// set default values
	app.config.view = app.config.views["activity"]
	app.config.queryOptions.QueryAgeThresh = "00:00:00"

	var wg sync.WaitGroup

	for i, tc := range testcases {
		t.Run(fmt.Sprintln(i), func(t *testing.T) {

			app.config.procMask |= tc.mask // assign mask

			ch := make(chan struct{})

			wg.Add(1)
			go func() {
				victim, err := postgres.NewTestConnect()
				assert.NoError(t, err)

				switch app.config.procMask {
				case groupIdle:
					_, _ = victim.Exec("select 1")
				case groupActive:
					_, _ = victim.Exec("select pg_sleep(10)")
				case groupIdleXact:
					_, _ = victim.Exec("begin")
					time.Sleep(2 * time.Second)
				case groupWaiting, groupOthers:
					// don't know how to emulate
				}

				<-ch
				if tc.mode == "cancel" {
					victim.Close()
				}
				close(ch)
				wg.Done()
			}()

			time.Sleep(1 * time.Second) // make sure victim connection is established and started
			assert.Contains(t, killGroup(app, tc.mode), tc.want)
			ch <- struct{}{}
			app.config.procMask = 0 // reset mask
		})
		wg.Wait()
	}

	// run test with invalid input
	t.Run("invalid input", func(t *testing.T) {
		app.config.view = app.config.views["tables"]
		msg := killGroup(app, "cancel")
		assert.Equal(t, "Signals: sending signals allowed in pg_stat_activity only", msg)

		app.config.view = app.config.views["activity"]
		app.config.procMask = 0
		msg = killGroup(app, "cancel")
		assert.Equal(t, "Signals: do nothing, process mask is empty", msg)

		app.config.procMask = groupIdle
		msg = killGroup(app, "invalid")
		assert.Equal(t, "Signals: do nothing, unknown mode", msg)
	})
}

func Test_setProcMask(t *testing.T) {
	testcases := []struct {
		answer string
		want   int
	}{
		{answer: "", want: 0},
		{answer: "i", want: groupIdle},
		{answer: "ix", want: groupIdle + groupIdleXact},
		{answer: "aw", want: groupWaiting + groupActive},
		{answer: "iax", want: groupIdle + groupIdleXact + groupActive},
		{answer: "aox", want: groupOthers + groupActive + groupIdleXact},
		{answer: "wixa", want: groupIdle + groupIdleXact + groupActive + groupWaiting},
		{answer: "woix", want: groupIdleXact + groupOthers + groupWaiting + groupIdle},
		{answer: "iowax", want: groupIdle + groupIdleXact + groupActive + groupWaiting + groupOthers},
	}

	config := newConfig()

	for _, tc := range testcases {
		got := setProcMask(tc.answer, config)
		assert.Equal(t, printMaskString(config.procMask), got)
		assert.Equal(t, tc.want, config.procMask)
	}
}

func Test_showProcMask(t *testing.T) {
	testcases := []int{
		0,
		groupIdle,
		func() int { var m int; m |= groupIdle; m |= groupIdleXact; return m }(),
		func() int { var m int; m |= groupIdle; m |= groupIdleXact; m |= groupActive; return m }(),
		func() int {
			var m int
			m |= groupIdle
			m |= groupIdleXact
			m |= groupActive
			m |= groupWaiting
			return m
		}(),
		func() int {
			var m int
			m |= groupIdle
			m |= groupIdleXact
			m |= groupActive
			m |= groupWaiting
			m |= groupOthers
			return m
		}(),
	}

	cfg := newConfig()
	for _, tc := range testcases {
		cfg.procMask = tc
		fn := showProcMask(cfg)
		assert.NoError(t, fn(nil, nil))
	}
}

func Test_printMaskString(t *testing.T) {
	testcases := map[int]string{
		0:  "Mask: empty ",
		2:  "Mask: idle ",
		6:  "Mask: idle idle_xact ",
		7:  "Mask: idle idle_xact active ",
		15: "Mask: idle idle_xact active waiting ",
		31: "Mask: idle idle_xact active waiting others ",
	}
	for k, v := range testcases {
		assert.Equal(t, v, printMaskString(k))
	}
}
