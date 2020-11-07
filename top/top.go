// 'pgcenter top' - top-like stats viewer.

package top

import (
	"context"
	"github.com/jroimartin/gocui"
	"github.com/lesovsky/pgcenter/internal/postgres"
	"github.com/lesovsky/pgcenter/internal/stat"
)

// app defines stuff required for application.
type app struct {
	postgresProps stat.PostgresProperties
	config        *config
	ui            *gocui.Gui
	db            *postgres.DB
	stats         *stat.Stat // TODO: в конечном счете от этой структуры следует избавиться т.к. стата берется из спец. стат горутины (см. collectStat)
	doExit        chan int
	doUpdate      chan int
}

// RunMain is the main entry point for 'pgcenter top' command
func RunMain(dbConfig *postgres.Config) error {
	config := newConfig()

	// Connect to Postgres.
	db, err := postgres.Connect(dbConfig)
	if err != nil {
		return err
	}
	defer db.Close()

	app := &app{
		config: config,
		db:     db,
		stats:  &stat.Stat{},
	}

	// Setup context - which kind of stats should be displayed
	err = app.Setup()
	if err != nil {
		return err
	}

	// Run application workers and UI.
	return mainLoop(context.TODO(), app)
}

// Initial setup of the context. Set defaults and override settings which depends on Postgres version, recovery status, etc.
func (app *app) Setup() error {
	// Aux stats is not displayed by default
	app.config.aux = auxNone

	// Read details about Postgres
	props, err := stat.ReadPostgresProperties(app.db)
	if err != nil {
		return err
	}

	app.postgresProps = props

	// Adjust queries depending on Postgres version
	app.config.views.Configure(app.postgresProps.VersionNum, app.postgresProps.GucTrackCommitTimestamp)
	app.config.sharedOptions.Adjust(app.postgresProps, "top")

	app.doExit = make(chan int)
	app.doUpdate = make(chan int)

	return nil
}
