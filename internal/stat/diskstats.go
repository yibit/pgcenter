package stat

import (
	"bufio"
	"fmt"
	"github.com/lesovsky/pgcenter/internal/postgres"
	"os"
	"regexp"
	"strings"
)

// Diskstat describes IO statistics for single device based on /proc/diskstats.
// See details https://www.kernel.org/doc/Documentation/ABI/testing/procfs-diskstats
type Diskstat struct {
	/* basic */
	Major, Minor int     // 1 - major number; 2 - minor number
	Device       string  // 3 - device name
	Rcompleted   float64 // 4 - reads completed successfully
	Rmerged      float64 // 5 - reads merged
	Rsectors     float64 // 6 - sectors read
	Rspent       float64 // 7 - time spent reading (ms)
	Wcompleted   float64 // 8 - writes completed
	Wmerged      float64 // 9 - writes merged
	Wsectors     float64 // 10 - sectors written
	Wspent       float64 // 11 - time spent writing (ms)
	Ioinprogress float64 // 12 - I/Os currently in progress
	Tspent       float64 // 13 - time spent doing I/Os (ms)
	Tweighted    float64 // 14 - weighted time spent doing I/Os (ms)
	Dcompleted   float64 // 15 - discards completed successfully
	Dmerged      float64 //	16 - discards merged
	Dsectors     float64 //	17 - sectors discarded
	Dspent       float64 //	18 - time spent discarding
	Fcompleted   float64 // 19 - flush requests completed successfully
	Fspent       float64 // 20 - time spent flushing
	/* advanced */
	Uptime    float64 // system uptime, used for interval calculation
	Completed float64 // reads and writes completed
	Rawait    float64 // average time (in milliseconds) for read requests issued to the device to be served. This includes the time spent by the requests in queue and the time spent servicing them.
	Wawait    float64 // average time (in milliseconds) for write requests issued to the device to be served. This includes the time spent by the requests in queue and the time spent servicing them.
	Await     float64 // average time (in milliseconds) for I/O requests issued to the device to be served. This includes the time spent by the requests in queue and the time spent servicing them.
	Arqsz     float64 // average size (in sectors) of the requests that were issued to the device.
	Util      float64 // percentage of elapsed time during which I/O requests were issued to the device (bandwidth utilization for the device). Device saturation occurs when this value is close to 100% for devices serving requests serially.
	// But for devices serving requests in parallel, such as RAID arrays and modern SSDs, this number does not reflect their performance limits.
}

// Diskstats is the container for all stats related to all block devices
type Diskstats []Diskstat

const (
	// ProcDiskstats provides IO statistics of block devices. For more details refer to Linux kernel's Documentation/iostats.txt.
	ProcDiskstats = "/proc/diskstats"
	// pgProcDiskstatsQuery is the SQL for retrieving IO stats from Postgres instance
	pgProcDiskstatsQuery = "SELECT * FROM pgcenter.sys_proc_diskstats ORDER BY (maj,min)"
)

func readDiskstats(db *postgres.DB, schemaExists bool) (Diskstats, error) {
	if db.Local {
		return readDiskstatsLocal("/proc/diskstats")
	} else if schemaExists {
		return readDiskstatsRemote(db)
	}

	return Diskstats{}, nil
}

func readDiskstatsLocal(statfile string) (Diskstats, error) {
	var stat Diskstats
	f, err := os.Open(statfile)
	if err != nil {
		return stat, err
	}

	uptime, err := uptime()
	if err != nil {
		return nil, err
	}

	scanner := bufio.NewScanner(f)

	for scanner.Scan() {
		line := scanner.Text()
		values := strings.Fields(line)

		// Linux kernel <= 4.18 have 14 columns, 4.18+ have 18, 5.5+ have 20 columns
		// for details see https://www.kernel.org/doc/Documentation/ABI/testing/procfs-diskstats)
		if len(values) != 14 && len(values) != 18 && len(values) != 20 {
			return nil, fmt.Errorf("%s bad content: unknown file format, wrong number of columns in line: %s", statfile, line)
		}

		var d = Diskstat{}

		switch len(values) {
		case 14:
			_, err = fmt.Sscan(line,
				&d.Major, &d.Minor, &d.Device,
				&d.Rcompleted, &d.Rmerged, &d.Rsectors, &d.Rspent, &d.Wcompleted, &d.Wmerged, &d.Wsectors, &d.Wspent,
				&d.Ioinprogress, &d.Tspent, &d.Tweighted,
			)
		case 18:
			_, err = fmt.Sscan(line,
				&d.Major, &d.Minor, &d.Device,
				&d.Rcompleted, &d.Rmerged, &d.Rsectors, &d.Rspent, &d.Wcompleted, &d.Wmerged, &d.Wsectors, &d.Wspent,
				&d.Ioinprogress, &d.Tspent, &d.Tweighted, &d.Dcompleted, &d.Dmerged, &d.Dsectors, &d.Dspent,
			)
		case 20:
			_, err = fmt.Sscan(line,
				&d.Major, &d.Minor, &d.Device,
				&d.Rcompleted, &d.Rmerged, &d.Rsectors, &d.Rspent, &d.Wcompleted, &d.Wmerged, &d.Wsectors, &d.Wspent,
				&d.Ioinprogress, &d.Tspent, &d.Tweighted, &d.Dcompleted, &d.Dmerged, &d.Dsectors, &d.Dspent,
				&d.Fcompleted, &d.Fspent,
			)
		default:
			// should not be here, but anyway check for that
			err = fmt.Errorf("unknown file format, wrong number of columns in line: %s", line)
		}
		if err != nil {
			return nil, fmt.Errorf("%s bad content: %w", statfile, err)
		}

		// skip pseudo block devices.
		re := regexp.MustCompile(`^(ram|loop|fd)`)
		if re.MatchString(d.Device) {
			continue
		}

		d.Uptime = uptime
		stat = append(stat, d)
	}

	return stat, nil
}

func readDiskstatsRemote(db *postgres.DB) (Diskstats, error) {
	var uptime float64
	err := db.QueryRow(pgProcUptimeQuery).Scan(&uptime)
	if err != nil {
		return nil, err
	}

	rows, err := db.Query(pgProcDiskstatsQuery)
	if err != nil {
		return nil, err
	}
	defer rows.Close()

	var stat Diskstats
	for rows.Next() {
		var d = Diskstat{}

		err := rows.Scan(&d.Major, &d.Minor, &d.Device,
			&d.Rcompleted, &d.Rmerged, &d.Rsectors, &d.Rspent,
			&d.Wcompleted, &d.Wmerged, &d.Wsectors, &d.Wspent,
			&d.Ioinprogress, &d.Tspent, &d.Tweighted,
			&d.Dcompleted, &d.Dmerged, &d.Dsectors, &d.Dspent,
			&d.Fcompleted, &d.Fspent)
		if err != nil {
			return nil, err
		}

		// skip pseudo block devices.
		re := regexp.MustCompile(`^(ram|loop|fd)`)
		if re.MatchString(d.Device) {
			continue
		}

		d.Uptime = uptime
		stat = append(stat, d)
	}

	return stat, nil
}

func countDiskstatsUsage(prev Diskstats, curr Diskstats, ticks float64) Diskstats {
	if len(curr) != len(prev) {
		// TODO: make possible to diff snapshots with different number of devices.
		return nil
	}

	stat := make([]Diskstat, len(curr))

	for i := 0; i < len(curr); i++ {
		// Skip inactive devices.
		if curr[i].Rcompleted+curr[i].Wcompleted == 0 {
			continue
		}

		stat[i].Major = curr[i].Major
		stat[i].Minor = curr[i].Minor
		stat[i].Device = curr[i].Device
		itv := curr[i].Uptime - prev[i].Uptime

		stat[i].Completed = curr[i].Rcompleted + curr[i].Wcompleted

		stat[i].Util = sValue(prev[i].Tspent, curr[i].Tspent, itv, ticks) / 10

		if ((curr[i].Rcompleted + curr[i].Wcompleted) - (prev[i].Rcompleted + prev[i].Wcompleted)) > 0 {
			stat[i].Await = ((curr[i].Rspent - prev[i].Rspent) + (curr[i].Wspent - prev[i].Wspent)) /
				((curr[i].Rcompleted + curr[i].Wcompleted) - (prev[i].Rcompleted + prev[i].Wcompleted))
		} else {
			stat[i].Await = 0
		}

		if ((curr[i].Rcompleted + curr[i].Wcompleted) - (prev[i].Rcompleted + prev[i].Wcompleted)) > 0 {
			stat[i].Arqsz = ((curr[i].Rsectors - prev[i].Rsectors) + (curr[i].Wsectors - prev[i].Wsectors)) /
				((curr[i].Rcompleted + curr[i].Wcompleted) - (prev[i].Rcompleted + prev[i].Wcompleted))
		} else {
			stat[i].Arqsz = 0
		}

		if (curr[i].Rcompleted - prev[i].Rcompleted) > 0 {
			stat[i].Rawait = (curr[i].Rspent - prev[i].Rspent) / (curr[i].Rcompleted - prev[i].Rcompleted)
		} else {
			stat[i].Rawait = 0
		}

		if (curr[i].Wcompleted - prev[i].Wcompleted) > 0 {
			stat[i].Wawait = (curr[i].Wspent - prev[i].Wspent) / (curr[i].Wcompleted - prev[i].Wcompleted)
		} else {
			stat[i].Wawait = 0
		}

		stat[i].Rmerged = sValue(prev[i].Rmerged, curr[i].Rmerged, itv, ticks)
		stat[i].Wmerged = sValue(prev[i].Wmerged, curr[i].Wmerged, itv, ticks)
		stat[i].Rcompleted = sValue(prev[i].Rcompleted, curr[i].Rcompleted, itv, ticks)
		stat[i].Wcompleted = sValue(prev[i].Wcompleted, curr[i].Wcompleted, itv, ticks)
		stat[i].Rsectors = sValue(prev[i].Rsectors, curr[i].Rsectors, itv, ticks) / 2048
		stat[i].Wsectors = sValue(prev[i].Wsectors, curr[i].Wsectors, itv, ticks) / 2048
		stat[i].Tweighted = sValue(prev[i].Tweighted, curr[i].Tweighted, itv, ticks) / 1000
	}

	return stat
}
