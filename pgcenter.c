/*
 * pgcenter: adminitrative console for PostgreSQL.
 * (C) 2015 by Alexey V. Lesovsky (lesovsky <at> gmail.com)
 */

#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <ncurses.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include "libpq-fe.h"
#include "pgcenter.h"

/*
 ******************************************************** startup function **
 * Print usage.
 ****************************************************************************
 */
void print_usage(void)
{
    printf("%s is the adminitrative console for PostgreSQL.\n\n", PROGRAM_NAME);
    printf("Usage:\n \
  %s [OPTION]... [DBNAME [USERNAME]]\n\n", PROGRAM_NAME);
    printf("General options:\n \
  -?, --help                show this help, then exit.\n \
  -V, --version             print version, then exit.\n\n");
    printf("Options:\n \
  -h, --host=HOSTNAME       database server host or socket directory (default: \"/tmp\")\n \
  -p, --port=PORT           database server port (default: \"5432\")\n \
  -U, --username=USERNAME   database user name (default: \"current user\")\n \
  -d, --dbname=DBNAME       database name (default: \"current user\")\n \
  -w, --no-password         never prompt for password\n \
  -W, --password            force password prompt (should happen automatically)\n\n");
    printf("Report bugs to %s.\n", PROGRAM_AUTHORS_CONTACTS);
}

/*
 ******************************************************** routine function **
 * Trap keys in program main mode.
 *
 * RETURNS:
 * 1 if key is pressed or 0 if not.
 ****************************************************************************
 */
int key_is_pressed(void)
{
    int ch = getch();
    if (ch != ERR) {
        ungetch(ch);
        return 1;
    } else
        return 0;
}

/*
 *********************************************************** init function **
 * Allocate memory for screens options struct array.
 *
 * OUT:
 * @screen_s   Initialized array of screens options.
 ****************************************************************************
 */
void init_screens(struct screen_s *screens[])
{
    int i;
    for (i = 0; i < MAX_CONSOLE; i++) {
        if ((screens[i] = (struct screen_s *) malloc(SCREEN_SIZE)) == NULL) {
                perror("malloc");
                exit(EXIT_FAILURE);
        }
        memset(screens[i], 0, SCREEN_SIZE);
        screens[i]->screen = i;
        screens[i]->conn_used = false;
        strcpy(screens[i]->host, "");
        strcpy(screens[i]->port, "");
        strcpy(screens[i]->user, "");
        strcpy(screens[i]->dbname, "");
        strcpy(screens[i]->password, "");
        strcpy(screens[i]->conninfo, "");
        screens[i]->log_opened = false;
        screens[i]->current_context = DEFAULT_QUERY_CONTEXT;
        strcpy(screens[i]->pg_stat_activity_min_age, PG_STAT_ACTIVITY_MIN_AGE_DEFAULT);

        screens[i]->context_list[PG_STAT_DATABASE_NUM].context = pg_stat_database;
        screens[i]->context_list[PG_STAT_DATABASE_NUM].order_key = PG_STAT_DATABASE_ORDER_MIN;
        screens[i]->context_list[PG_STAT_DATABASE_NUM].order_desc = true;
        screens[i]->context_list[PG_STAT_REPLICATION_NUM].context = pg_stat_replication;
        screens[i]->context_list[PG_STAT_REPLICATION_NUM].order_key = PG_STAT_REPLICATION_ORDER_MIN;
        screens[i]->context_list[PG_STAT_REPLICATION_NUM].order_desc = true;
        screens[i]->context_list[PG_STAT_USER_TABLES_NUM].context = pg_stat_user_tables;
        screens[i]->context_list[PG_STAT_USER_TABLES_NUM].order_key = PG_STAT_USER_TABLES_ORDER_MIN;
        screens[i]->context_list[PG_STAT_USER_TABLES_NUM].order_desc = true;
        screens[i]->context_list[PG_STAT_USER_INDEXES_NUM].context = pg_stat_user_indexes;
        screens[i]->context_list[PG_STAT_USER_INDEXES_NUM].order_key = PG_STAT_USER_INDEXES_ORDER_MIN;
        screens[i]->context_list[PG_STAT_USER_INDEXES_NUM].order_desc = true;
        screens[i]->context_list[PG_STATIO_USER_TABLES_NUM].context = pg_statio_user_tables;
        screens[i]->context_list[PG_STATIO_USER_TABLES_NUM].order_key = PG_STATIO_USER_TABLES_ORDER_MIN;
        screens[i]->context_list[PG_STATIO_USER_TABLES_NUM].order_desc = true;
        screens[i]->context_list[PG_TABLES_SIZE_NUM].context = pg_tables_size;
        screens[i]->context_list[PG_TABLES_SIZE_NUM].order_key = PG_TABLES_SIZE_ORDER_MIN;
        screens[i]->context_list[PG_TABLES_SIZE_NUM].order_desc = true;
        screens[i]->context_list[PG_STAT_ACTIVITY_LONG_NUM].context = pg_stat_activity_long;
        screens[i]->context_list[PG_STAT_ACTIVITY_LONG_NUM].order_key = PG_STAT_ACTIVITY_LONG_ORDER_MIN;
        screens[i]->context_list[PG_STAT_ACTIVITY_LONG_NUM].order_desc = true;
        screens[i]->context_list[PG_STAT_USER_FUNCTIONS_NUM].context = pg_stat_user_functions;
        screens[i]->context_list[PG_STAT_USER_FUNCTIONS_NUM].order_key = PG_STAT_USER_FUNCTIONS_ORDER_MIN;
        screens[i]->context_list[PG_STAT_USER_FUNCTIONS_NUM].order_desc = true;
    }
}

/*
 ******************************************************** startup function **
 * Password prompt.
 *
 * IN:
 * @prompt          Text of prompt.
 * @maxlen          Length of input string.
 * @echo            Echo input string.
 *
 * RETURNS:
 * @password        Password.
 ****************************************************************************
 */
char * password_prompt(const char *prompt, int maxlen, bool echo)
{
    struct termios t_orig, t;
    char *password;
    password = (char *) malloc(maxlen + 1);

    if (!echo) {
        tcgetattr(fileno(stdin), &t);
        t_orig = t;
        t.c_lflag &= ~ECHO;
        tcsetattr(fileno(stdin), TCSAFLUSH, &t);
    }

    fputs(prompt, stdout);
    if (fgets(password, maxlen + 1, stdin) == NULL)
        password[0] = '\0';

    if (!echo) {
        tcsetattr(fileno(stdin), TCSAFLUSH, &t_orig);
        fputs("\n", stdout);
        fflush(stdout);
    }

    return password;
}

/*
 ******************************************************** startup function **
 * Take input parameters and add them into connections options.
 *
 * IN:
 * @argc            Input arguments count.
 * @argv[]          Input arguments array.
 *
 * OUT:
 * @screens[]     Array where connections options will be saved.
 ****************************************************************************
 */
void create_initial_conn(int argc, char *argv[], struct screen_s * screens[])
{
    struct passwd *pw = getpwuid(getuid());

    /* short options */
    const char * short_options = "h:p:U:d:wW?";

    /* long options */
    const struct option long_options[] = {
        {"help", no_argument, NULL, '?'},
        {"host", required_argument, NULL, 'h'},
        {"port", required_argument, NULL, 'p'},
        {"dbname", required_argument, NULL, 'd'},
        {"no-password", no_argument, NULL, 'w'},
        {"password", no_argument, NULL, 'W'},
        {"user", required_argument, NULL, 'U'},
        {NULL, 0, NULL, 0}
    };

    int param, option_index;
    enum trivalue prompt_password = TRI_DEFAULT;

    if (argc > 1) {
        if ((strcmp(argv[1], "-?") == 0)
                || (argc == 2 && (strcmp(argv[1], "--help") == 0)))
        {
            print_usage();
            exit(EXIT_SUCCESS);
        }
        if (strcmp(argv[1], "--version") == 0
                || strcmp(argv[1], "-V") == 0)
        {
            printf("%s %.1f.%d\n", PROGRAM_NAME, PROGRAM_VERSION, PROGRAM_RELEASE);
            exit(EXIT_SUCCESS);
        }
    }

    while ( (param = getopt_long(argc, argv,
                short_options, long_options, &option_index)) != -1 ) {
        switch (param) {
            case 'h':
                strcpy(screens[0]->host, optarg);
                break;
            case 'p':
                strcpy(screens[0]->port, optarg);
                break;
            case 'U':
                strcpy(screens[0]->user, optarg);
                break;
            case 'd':
                strcpy(screens[0]->dbname, optarg);
                break;
            case 'w':
                prompt_password = TRI_NO;
                break;
            case 'W':
                prompt_password = TRI_YES;
                break;
            case '?': default:
                fprintf(stderr,"Try \"%s --help\" for more information.\n", argv[0]);
                exit(EXIT_SUCCESS);
                break;
        }
    }
    while (argc - optind >= 1) {
        if ( (argc - optind > 1)
                && strlen(screens[0]->user) == 0
                && strlen(screens[0]->dbname) == 0 )
            strcpy(screens[0]->user, argv[optind]);
        else if ( (argc - optind >= 1) && strlen(screens[0]->dbname) == 0 )
            strcpy(screens[0]->dbname, argv[optind]);
        else
            fprintf(stderr,
                    "%s: warning: extra command-line argument \"%s\" ignored\n",
                    argv[0], argv[optind]);
        optind++;
    }
    if ( strlen(screens[0]->host) == 0 )
        strcpy(screens[0]->host, DEFAULT_HOST);

    if ( strlen(screens[0]->port) == 0 )
        strcpy(screens[0]->port, DEFAULT_PORT);

    if ( strlen(screens[0]->user) == 0 )
        strcpy(screens[0]->user, pw->pw_name);

    if ( prompt_password == TRI_YES )
        strcpy(screens[0]->password, password_prompt("Password: ", 100, false));

    if ( strlen(screens[0]->user) != 0 && strlen(screens[0]->dbname) == 0 )
        strcpy(screens[0]->dbname, screens[0]->user);

    screens[0]->conn_used = true;
}

/*
 ******************************************************** startup function **
 * Read ~/.pgcenterrc cfile and fill up conrections options array.
 *
 * IN:
 * @argc            Input arguments count.
 * @argv[]          Input arguments array.
 * @pos             Start position inside array.
 *
 * OUT:
 * @screens       Connections options array.
 *
 * RETURNS:
 * Success or failure.
 ****************************************************************************
 */
int create_pgcenterrc_conn(int argc, char *argv[],
                struct screen_s * screens[], const int pos)
{
    FILE *fp;
    static char pgcenterrc_path[PATH_MAX];
    struct stat statbuf;
    char strbuf[BUFFERSIZE];
    int i = pos;
    struct passwd *pw = getpwuid(getuid());

    strcpy(pgcenterrc_path, pw->pw_dir);
    strcat(pgcenterrc_path, "/");
    strcat(pgcenterrc_path, PGCENTERRC_FILE);

    if (access(pgcenterrc_path, F_OK) == -1)
        return PGCENTERRC_READ_ERR;

    stat(pgcenterrc_path, &statbuf);
    if ( statbuf.st_mode & (S_IRWXG | S_IRWXO) ) {
        fprintf(stderr,
                    "WARNING: %s has wrong permissions.\n", pgcenterrc_path);
        return PGCENTERRC_READ_ERR;
    }

    /* read connections settings from .pgcenterrc */
    if ((fp = fopen(pgcenterrc_path, "r")) != NULL) {
        while (fgets(strbuf, 4096, fp) != 0) {
            sscanf(strbuf, "%[^:]:%[^:]:%[^:]:%[^:]:%[^:\n]",
                        screens[i]->host, screens[i]->port,
                        screens[i]->dbname,   screens[i]->user,
                        screens[i]->password);
                        screens[i]->screen = i;
                        screens[i]->conn_used = true;
                        i++;
        }
        fclose(fp);
        return PGCENTERRC_READ_OK;
    } else {
        fprintf(stdout,
                    "WARNING: failed to open %s. Try use defaults.\n", pgcenterrc_path);
        return PGCENTERRC_READ_ERR;
    }
}

/*
 ******************************************************** startup function **
 * Prepare conninfo string for PQconnectdb.
 *
 * IN:
 * @screens       Screens options array without filled conninfo.
 *
 * OUT:
 * @screens       Screens options array with conninfo.
 ****************************************************************************
 */
void prepare_conninfo(struct screen_s * screens[])
{
    int i;
    for ( i = 0; i < MAX_CONSOLE; i++ )
        if (screens[i]->conn_used) {
            strcat(screens[i]->conninfo, "host=");
            strcat(screens[i]->conninfo, screens[i]->host);
            strcat(screens[i]->conninfo, " port=");
            strcat(screens[i]->conninfo, screens[i]->port);
            strcat(screens[i]->conninfo, " user=");
            strcat(screens[i]->conninfo, screens[i]->user);
            strcat(screens[i]->conninfo, " dbname=");
            strcat(screens[i]->conninfo, screens[i]->dbname);
            if ((strlen(screens[i]->password)) != 0) {
                strcat(screens[i]->conninfo, " password=");
                strcat(screens[i]->conninfo, screens[i]->password);
            }
        }
}

/*
 ******************************************************** startup function **
 * Open connections to PostgreSQL using conninfo string from screen struct.
 *
 * IN:
 * @screens         Screens options array.
 *
 * OUT:
 * @conns           Array of connections.
 ****************************************************************************
 */
void open_connections(struct screen_s * screens[], PGconn * conns[])
{
    int i;
    for ( i = 0; i < MAX_CONSOLE; i++ ) {
        if (screens[i]->conn_used) {
            conns[i] = PQconnectdb(screens[i]->conninfo);
            if ( PQstatus(conns[i]) == CONNECTION_BAD && PQconnectionNeedsPassword(conns[i]) == 1) {
                printf("%s:%s %s@%s require ", 
                                screens[i]->host, screens[i]->port,
                                screens[i]->user, screens[i]->dbname);
                strcpy(screens[i]->password, password_prompt("password: ", 100, false));
                strcat(screens[i]->conninfo, " password=");
                strcat(screens[i]->conninfo, screens[i]->password);
                conns[i] = PQconnectdb(screens[i]->conninfo);
            } else if ( PQstatus(conns[i]) == CONNECTION_BAD ) {
                printf("Unable to connect to %s:%s %s@%s",
                screens[i]->host, screens[i]->port,
                screens[i]->user, screens[i]->dbname);
            }
        }
    }
}

/*
 * prepare query
 *
 */
void prepare_query(struct screen_s * screen, char * query)
{
    char tmp[2];
    switch (screen->current_context) {
        case pg_stat_database: default:
            strcpy(query, PG_STAT_DATABASE_QUERY);
            break;
        case pg_stat_replication:
            strcpy(query, PG_STAT_REPLICATION_QUERY);
            break;
        case pg_stat_user_tables:
            strcpy(query, PG_STAT_USER_TABLES_QUERY);
            break;
        case pg_stat_user_indexes:
            strcpy(query, PG_STAT_USER_INDEXES_QUERY);
            break;
        case pg_statio_user_tables:
            strcpy(query, PG_STATIO_USER_TABLES_QUERY);
            break;
        case pg_tables_size:
            strcpy(query, PG_TABLES_SIZE_QUERY);
            break;
        case pg_stat_activity_long:
            /* 
             * здесь мы собираем запрос из нескольких частей, т.о. даем пользователю
             * менять значения min_age которое используется в условии запроса 
             */
            strcpy(query, PG_STAT_ACTIVITY_LONG_QUERY_P1);
            strcat(query, screen->pg_stat_activity_min_age);
            strcat(query, PG_STAT_ACTIVITY_LONG_QUERY_P2);
            strcat(query, screen->pg_stat_activity_min_age);
            strcat(query, PG_STAT_ACTIVITY_LONG_QUERY_P3);
            break;
        case pg_stat_user_functions:
            /* here we use query ORDER BY, thus we should incrementig order key */
            sprintf(tmp, "%d", screen->context_list[PG_STAT_USER_FUNCTIONS_NUM].order_key + 1);
            strcpy(query, PG_STAT_USER_FUNCTIONS_QUERY_P1);
            strcat(query, tmp);             /* insert number of field into ORDER BY .. */
            strcat(query, PG_STAT_USER_FUNCTIONS_QUERY_P2);
            break;
    }
}

/*
 ********************************************************* routine function **
 * Send query to PostgreSQL.
 *
 * IN:
 * @conn            PostgreSQL connection.
 * @query_context   Type of query.
 *
 * RETURNS:
 * Answer from PostgreSQL.
 ****************************************************************************
 */
PGresult * do_query(PGconn * conn, char * query)
{
    PGresult    *res;
    res = PQexec(conn, query);
    if ( PQresultStatus(res) != PG_TUP_OK ) {
        puts("We didn't get any data.");
        PQclear(res);
        return NULL;
    } else
        return res;
}

/*
 ************************************************* summary window function **
 * Print current time.
 *
 * OUT:
 * @strtime         Current time.
 ****************************************************************************
 */
void get_time(char * strtime)
{
    time_t rawtime;
    struct tm *timeinfo;

    time(&rawtime);
    timeinfo = localtime(&rawtime);
    strftime(strtime, 20, "%Y-%m-%d %H:%M:%S", timeinfo);
}

/*
 ************************************************* summary window function **
 * Print title into summary window: program name and current time.
 *
 * IN:
 * @window          Window where title will be printed.
 ****************************************************************************
 */
void print_title(WINDOW * window, char * progname)
{
    char *strtime = (char *) malloc(sizeof(char) * 20);
    get_time(strtime);
    wprintw(window, "%s: %s, ", progname, strtime);
    free(strtime);
}

/*
 ************************************************* summary window function **
 * Read /proc/loadavg and return load average value.
 *
 * IN:
 * @m       Minute value.
 *
 * RETURNS:
 * Load average for 1, 5 or 15 minutes.
 ****************************************************************************
 */
float get_loadavg(int m)
{
    if ( m != 1 && m != 5 && m != 15 )
        m = 1;

    float avg = 0, avg1, avg5, avg15;
    FILE *loadavg_fd;

    if ((loadavg_fd = fopen(LOADAVG_FILE, "r")) == NULL) {
        fprintf(stderr, "can't open %s\n", LOADAVG_FILE);
        exit(EXIT_FAILURE);
    } else {
        fscanf(loadavg_fd, "%f %f %f", &avg1, &avg5, &avg15);
        fclose(loadavg_fd);
    }

    switch (m) {
        case 1: avg = avg1; break;
        case 5: avg = avg5; break;
        case 15: avg = avg15; break;
    }
    return avg;
}

/*
 ************************************************* summary window function **
 * Print load average into summary window.
 *
 * IN:
 * @window      Window where load average will be printed.
 ****************************************************************************
 */
void print_loadavg(WINDOW * window)
{
    wprintw(window, "load average: %.2f, %.2f, %.2f\n",
                    get_loadavg(1), get_loadavg(5), get_loadavg(15));
}

/*
 ************************************************* summary window function **
 * Print current connection info.
 *
 * IN:
 * @window          Window where info will be printed.
 * @conn_opts       Struct with connections options.
 * @conn            Current connection.
 * @console_no      Current console number.
 ****************************************************************************
 */
void print_conninfo(WINDOW * window, struct screen_s * screen, PGconn *conn, int console_no)
{
    static char state[8];
    switch (PQstatus(conn)) {
        case CONNECTION_OK:
            strcpy(state, "ok");
            break;
        case CONNECTION_BAD:
            strcpy(state, "failed");
            break;
        default:
            strcpy(state, "unknown");
            break;
    }
    wprintw(window, "  conn %i: %s:%s %s@%s\t conn state: %s\n",
                console_no,
                screen->host, screen->port,
                screen->user, screen->dbname,
                state);
}

/*
 ************************************************* sumamry window function **
 * Print current pgbouncer summary info: total clients, servers, etc.
 *
 * IN:
 * @window          Window where info will be printed.
 * @conn            Current pgbouncer connection.
 ****************************************************************************
 */
void print_postgres_activity(WINDOW * window, PGconn * conn)
{
    int t_count, i_count, it_count, a_count, w_count, o_count;
    PGresult *res;

    res = do_query(conn, PG_STAT_ACTIVITY_COUNT_TOTAL_QUERY);
    t_count = atoi(PQgetvalue(res, 0, 0));
    PQclear(res);

    res = do_query(conn, PG_STAT_ACTIVITY_COUNT_IDLE_QUERY);
    i_count = atoi(PQgetvalue(res, 0, 0));
    PQclear(res);

    res = do_query(conn, PG_STAT_ACTIVITY_COUNT_IDLE_IN_T_QUERY);
    it_count = atoi(PQgetvalue(res, 0, 0));
    PQclear(res);

    res = do_query(conn, PG_STAT_ACTIVITY_COUNT_ACTIVE_QUERY);
    a_count = atoi(PQgetvalue(res, 0, 0));
    PQclear(res);

    res = do_query(conn, PG_STAT_ACTIVITY_COUNT_WAITING_QUERY);
    w_count = atoi(PQgetvalue(res, 0, 0));
    PQclear(res);

    res = do_query(conn, PG_STAT_ACTIVITY_COUNT_OTHERS_QUERY);
    o_count = atoi(PQgetvalue(res, 0, 0));
    PQclear(res);

    wprintw(window,
            "activity:%3i total,%3i idle,%3i idle_in_tnx,%3i active,%3i waiting,%3i others",
            t_count, i_count, it_count, a_count, w_count, o_count);
}

/*
 **************************************************** get cpu stat function **
 * Allocate memory for cpu statistics struct.
 *
 * OUT:
 * @st_cpu      Struct for cpu statistics.
 ****************************************************************************
 */
void init_stats(struct stats_cpu_struct *st_cpu[])
{
    int i;
    /* Allocate structures for CPUs "all" and 0 */
    for (i = 0; i < 2; i++) {
        if ((st_cpu[i] = (struct stats_cpu_struct *) 
            malloc(STATS_CPU_SIZE * 2)) == NULL) {
            perror("malloc");
            exit(EXIT_FAILURE);
        }
        memset(st_cpu[i], 0, STATS_CPU_SIZE * 2);
    }
}

/*
 *************************************************** get cpu stat function **
 * Get system clock resolution.
 *
 * OUT:
 * @hz      Number of intervals per second.
 ****************************************************************************
 */
void get_HZ(void)
{
    long ticks;
    if ((ticks = sysconf(_SC_CLK_TCK)) == -1)
        perror("sysconf");
    
    hz = (unsigned int) ticks;
}

/*
 *************************************************** get cpu stat function **
 * Read machine uptime independently of the number of processors.
 *
 * OUT:
 * @uptime          Uptime value in jiffies.
 ****************************************************************************
 */
void read_uptime(unsigned long long *uptime)
{
    FILE *fp;
    char line[128];
    unsigned long up_sec, up_cent;

    if ((fp = fopen(UPTIME_FILE, "r")) == NULL)
        return;

    if (fgets(line, sizeof(line), fp) == NULL) {
        fclose(fp);
        return;
    }

    sscanf(line, "%lu.%lu", &up_sec, &up_cent);
    *uptime = (unsigned long long) up_sec * HZ +
    (unsigned long long) up_cent * HZ / 100;
    fclose(fp);
}

/*
 **************************************************** get cpu stat function **
 * Read cpu statistics from /proc/stat. Also calculate uptime if 
 * read_uptime() function return NULL.
 *
 * IN:
 * @st_cpu          Struct where stat will be saved.
 * @nbr             Total number of CPU (including cpu "all").
 *
 * OUT:
 * @st_cpu          Struct with statistics.
 * @uptime          Machine uptime multiplied by the number of processors.
 * @uptime0         Machine uptime. Filled only if previously set to zero.
 ****************************************************************************
 */
void read_cpu_stat(struct stats_cpu_struct *st_cpu, int nbr,
                            unsigned long long *uptime, unsigned long long *uptime0)
{
    FILE *stat_fp;
    struct stats_cpu_struct *st_cpu_i;
    struct stats_cpu_struct sc;
    char line[8192];
    int proc_nb;

    if ((stat_fp = fopen(STAT_FILE, "r")) == NULL) {
        fprintf(stderr, "Cannot open %s: %s\n", STAT_FILE, strerror(errno));
        exit(EXIT_FAILURE);
    }

    while ( (fgets(line, sizeof(line), stat_fp)) != NULL ) {
        if (!strncmp(line, "cpu ", 4)) {
            memset(st_cpu, 0, STATS_CPU_SIZE);
            sscanf(line + 5, "%llu %llu %llu %llu %llu %llu %llu %llu %llu %llu",
                            &st_cpu->cpu_user,      &st_cpu->cpu_nice,
                            &st_cpu->cpu_sys,       &st_cpu->cpu_idle,
                            &st_cpu->cpu_iowait,    &st_cpu->cpu_steal,
                            &st_cpu->cpu_hardirq,   &st_cpu->cpu_softirq,
                            &st_cpu->cpu_guest,     &st_cpu->cpu_guest_nice);
                            *uptime = st_cpu->cpu_user + st_cpu->cpu_nice +
                                st_cpu->cpu_sys + st_cpu->cpu_idle +
                                st_cpu->cpu_iowait + st_cpu->cpu_steal +
                                st_cpu->cpu_hardirq + st_cpu->cpu_softirq +
                                st_cpu->cpu_guest + st_cpu->cpu_guest_nice;
        } else if (!strncmp(line, "cpu", 3)) {
            if (nbr > 1) {
                memset(&sc, 0, STATS_CPU_SIZE);
                sscanf(line + 3, "%d %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu",
                                &proc_nb,           &sc.cpu_user,
                                &sc.cpu_nice,       &sc.cpu_sys,
                                &sc.cpu_idle,       &sc.cpu_iowait,
                                &sc.cpu_steal,      &sc.cpu_hardirq,
                                &sc.cpu_softirq,    &sc.cpu_guest,
                                &sc.cpu_guest_nice);

                                if (proc_nb < (nbr - 1)) {
                                    st_cpu_i = st_cpu + proc_nb + 1;
                                    *st_cpu_i = sc;
                                }

                                if (!proc_nb && !*uptime0) {
                                    *uptime0 = sc.cpu_user + sc.cpu_nice   +
                                    sc.cpu_sys     + sc.cpu_idle   +
                                    sc.cpu_iowait  + sc.cpu_steal  +
                                    sc.cpu_hardirq + sc.cpu_softirq;
                                    printf("read_cpu_stat: uptime0 = %llu\n", *uptime0);
                                }
            }
        }
    }
    fclose(stat_fp);
}

/*
 *************************************************** get cpu stat function **
 * Compute time interval.
 *
 * IN:
 * @prev_uptime     Previous uptime value in jiffies.
 * @curr_interval   Current uptime value in jiffies.
 *
 * RETURNS:
 * Interval of time in jiffies.
 ****************************************************************************
 */
unsigned long long get_interval(unsigned long long prev_uptime,
                                        unsigned long long curr_uptime)
{
    unsigned long long itv;
    
    /* first run prev_uptime=0 so displaying stats since system startup */
    itv = curr_uptime - prev_uptime;

    if (!itv) {     /* Paranoia checking */
        itv = 1;
    }

    return itv;
}

/*
 *************************************************** get cpu stat function **
 * Workaround for CPU counters read from /proc/stat: Dyn-tick kernels
 * have a race issue that can make those counters go backward.
 ****************************************************************************
 */
double ll_sp_value(unsigned long long value1, unsigned long long value2,
                unsigned long long itv)
{
    if (value2 < value1)
        return (double) 0;
    else
        return SP_VALUE(value1, value2, itv);
}

/*
 *************************************************** get cpu stat function **
 * Display cpu statistics in specified window.
 *
 * IN:
 * @window      Window in which spu statistics will be printed.
 * @st_cpu      Struct with cpu statistics.
 * @curr        Index in array for current sample statistics.
 * @itv         Interval of time.
 ****************************************************************************
 */
void write_cpu_stat_raw(WINDOW * window, struct stats_cpu_struct *st_cpu[],
                int curr, unsigned long long itv)
{
    wprintw(window, 
            "    %%cpu: %4.1f us, %4.1f sy, %4.1f ni, %4.1f id, %4.1f wa, %4.1f hi, %4.1f si, %4.1f st\n",
            ll_sp_value(st_cpu[!curr]->cpu_user, st_cpu[curr]->cpu_user, itv),
            ll_sp_value(st_cpu[!curr]->cpu_sys + st_cpu[!curr]->cpu_softirq + st_cpu[!curr]->cpu_hardirq,
            st_cpu[curr]->cpu_sys + st_cpu[curr]->cpu_softirq + st_cpu[curr]->cpu_hardirq, itv),
            ll_sp_value(st_cpu[!curr]->cpu_nice, st_cpu[curr]->cpu_nice, itv),
            (st_cpu[curr]->cpu_idle < st_cpu[!curr]->cpu_idle) ?
            0.0 :
            ll_sp_value(st_cpu[!curr]->cpu_idle, st_cpu[curr]->cpu_idle, itv),
            ll_sp_value(st_cpu[!curr]->cpu_iowait, st_cpu[curr]->cpu_iowait, itv),
            ll_sp_value(st_cpu[!curr]->cpu_hardirq, st_cpu[curr]->cpu_hardirq, itv),
            ll_sp_value(st_cpu[!curr]->cpu_softirq, st_cpu[curr]->cpu_softirq, itv),
            ll_sp_value(st_cpu[!curr]->cpu_steal, st_cpu[curr]->cpu_steal, itv));
    wrefresh(window);
}

/*
 ************************************************* summary window function **
 * Composite function which read cpu stats and uptime then print out stats 
 * to specified window.
 *
 * IN:
 * @window      Window where spu statistics will be printed.
 * @st_cpu      Struct with cpu statistics.
 ****************************************************************************
 */
void print_cpu_usage(WINDOW * window, struct stats_cpu_struct *st_cpu[])
{
    static unsigned long long uptime[2]  = {0, 0};
    static unsigned long long uptime0[2] = {0, 0};
    static int curr = 1;
    static unsigned long long itv;

    uptime0[curr] = 0;
    read_uptime(&(uptime0[curr]));
    read_cpu_stat(st_cpu[curr], 2, &(uptime[curr]), &(uptime0[curr]));
    itv = get_interval(uptime[!curr], uptime[curr]);
    write_cpu_stat_raw(window, st_cpu, curr, itv);
    itv = get_interval(uptime0[!curr], uptime0[curr]);
    curr ^= 1;
}

/*
 ******************************************************** routine function **
 * Calculate column width for output data.
 *
 * IN:
 * @n_rows          Number of rows in query result.
 * @n_cols          Number of columns in query result.
 * @res             Query result.
 * @arr             Array with sorted result.
 *
 * OUT:
 * @columns         Struct with column names and their max width.
 ****************************************************************************
 */
void calculate_width(struct colAttrs *columns, PGresult *res, char ***arr, int n_rows, int n_cols)
{
    int i, col, row;

    for (col = 0, i = 0; col < n_cols; col++, i++) {
        /* determine length of column names */
        strcpy(columns[i].name, PQfname(res, col));
        int colname_len = strlen(PQfname(res, col));
        int width = colname_len;
        /* determine length of values from result array */
        for (row = 0; row < n_rows; row++ ) {
            int val_len = strlen(arr[row][col]);
            if ( val_len >= width )
                width = val_len;
        }
        columns[i].width = width + 2;
    }
}

/*
 ****************************************************** key press function **
 * Switch console using specified number.
 *
 * IN:
 * @window          Window where cmd status will be written.
 * @screens[]     Struct with connections options.
 * @ch              Intercepted key (number from 1 to 8).
 * @console_no      Active console number.
 * @console_index   Index of active console.
 *
 * RETURNS:
 * Index console on which performed switching.
 ****************************************************************************
 */
int switch_conn(WINDOW * window, struct screen_s * screens[],
                int ch, int console_index, int console_no)
{
    if ( screens[ch - '0' - 1]->conn_used ) {
        console_no = ch - '0', console_index = console_no - 1;
        wprintw(window, "Switch to another pgbouncer connection (console %i)",
                console_no);
    } else
        wprintw(window, "Do not switch because no connection associated (stay on console %i)",
                console_no);
    return console_index;
}

/*
 ******************************************************** routine function **
 * Allocate memory for 3D pointer array.
 *
 * IN:
 * @arr             3D pointer array.
 * @n_rows          Number of rows of current query result.
 * @n_cols          Number of columns of current query result.
 *
 * RETURNS:
 * Returns allocated space based on rows and column numbers.
 ****************************************************************************
 */
char *** init_array(char ***arr, int n_rows, int n_cols)
{
    int i, j;

    arr = malloc(sizeof(char **) * n_rows);
    for (i = 0; i < n_rows; i++) {
        arr[i] = malloc(sizeof(char *) * n_cols);
            for (j = 0; j < n_cols; j++)
                arr[i][j] = malloc(sizeof(char) * 255);
    }
    return arr;
}

/*
 ******************************************************** routine function **
 * Free space occupied by 3D pointer arrays
 *
 * IN:      
 * @arr             3D pointer array.
 * @n_rows          Number of rows of current query result.
 * @n_cols          Number of columns of current query result.
 *
 * RETURNS:
 * Returns pointer to empty 3D pointer array.
 ****************************************************************************
 */
char *** free_array(char ***arr, int n_rows, int n_cols)
{
    int i, j;

    for (i = 0; i < n_rows; i++) {
        for (j = 0; j < n_cols; j++)
            free(arr[i][j]);
        free(arr[i]);
    }
    free(arr);
    return arr;
}

/*
 ******************************************************** routine function **
 * Copy database query results into array.
 *
 * IN:
 * @arr             3D pointer array where query results will be stored.
 * @res             Database query result.
 * @n_rows          Number of rows in query result.
 * @n_cols          Number of cols in query result.
 ****************************************************************************
 */
void pgrescpy(char ***arr, PGresult *res, int n_rows, int n_cols)
{
    int i, j;

    for (i = 0; i < n_rows; i++)
        for (j = 0; j < n_cols; j++)
            strcpy(arr[i][j], PQgetvalue(res, i, j));
}

/*
 ******************************************************** routime function **
 * Diff arrays and build array with deltas.
 *
 * IN:
 * @p_arr           Array with results of previous query.
 * @c_arr           Array with results of current query.
 * @context         Current used query.
 * @n_rows          Total number of rows from query result.
 * @n_cols          Total number of columns from query result.
 *
 * OUT:
 * @res_arr         Array where difference result will be stored.
 ****************************************************************************
 */
void diff_arrays(char ***p_arr, char ***c_arr, char ***res_arr, enum context context, int n_rows, int n_cols)
{
    int i, j, min, max;
 
    switch (context) {
        case pg_stat_database:
            min = PG_STAT_DATABASE_ORDER_MIN;
            max = PG_STAT_DATABASE_ORDER_MAX;
            break;
        case pg_stat_replication:
            min = PG_STAT_REPLICATION_ORDER_MIN;
            max = PG_STAT_REPLICATION_ORDER_MAX;
            break;
        case pg_stat_user_tables:
            min = PG_STAT_USER_TABLES_ORDER_MIN;
            max = PG_STAT_USER_TABLES_ORDER_MAX;
            break;
        case pg_stat_user_indexes:
            min = PG_STAT_USER_INDEXES_ORDER_MIN;
            max = PG_STAT_USER_INDEXES_ORDER_MAX;
            break;
        case pg_statio_user_tables:
            min = PG_STATIO_USER_TABLES_ORDER_MIN;
            max = PG_STATIO_USER_TABLES_ORDER_MAX;
            break;
        case pg_tables_size:
            min = PG_TABLES_SIZE_ORDER_MIN;
            max = PG_TABLES_SIZE_ORDER_MAX;
            break;
        case pg_stat_activity_long:
            /* 
             * здесь выставлен INVALID_ORDER_KEY на уровне pgcenter.h т.к. 
             * нам ненужен ни diff массивов (показываем всегда последние значения),
             * ни сортировка массива в дальнешем и мы просто копируем в res_arr 
             * все содержимое текущего c_arr.
             */
            min = PG_STAT_ACTIVITY_LONG_ORDER_MIN;
            max = PG_STAT_ACTIVITY_LONG_ORDER_MAX;
            break;
        case pg_stat_user_functions:
            /* здесь мы делаем diff только по одному полю calls/s */
            min = max = PG_STAT_USER_FUNCTIONS_DIFF_COL;
//            max = PG_STAT_USER_FUNCTIONS_DIFF_COL;
            break;
        default:
            break;
    }

    for (i = 0; i < n_rows; i++) {
        for (j = 0; j < n_cols; j++)
            if (j < min || j > max)
                strcpy(res_arr[i][j], c_arr[i][j]);     // copy unsortable values as is
            else {
                int n = snprintf(NULL, 0, "%lli", atoll(c_arr[i][j]) - atoll(p_arr[i][j]));
                char buf[n+1];
                snprintf(buf, n+1, "%lli", atoll(c_arr[i][j]) - atoll(p_arr[i][j]));
                strcpy(res_arr[i][j], buf);
            }
    }
}

/*
 ******************************************************** routine function **
 * Sort array using specified order key (column number).
 *
 * IN:
 * @res_arr         Array which content will be sorted.
 * @n_rows          Number of rows in query result.
 * @n_cols          Number of columns in query result.
 * @screen          Current screen.
 *
 * OUT:
 * @res_arr         Sorted array.
 ****************************************************************************
 */
void sort_array(char ***res_arr, int n_rows, int n_cols, struct screen_s * screen)
{
    int i, j, x, order_key;
    bool desc;
    char *temp = malloc(sizeof(char) * 255);

    for (i = 0; i < TOTAL_CONTEXTS; i++)
        if (screen->current_context == screen->context_list[i].context) {
            order_key = screen->context_list[i].order_key;
            desc = screen->context_list[i].order_desc;
        }

    if (screen->current_context == pg_stat_user_functions)
        return;

    if (order_key == INVALID_ORDER_KEY)
        return;

    for (i = 0; i < n_rows; i++) {
        for (j = i + 1; j < n_rows; j++) {
            if (desc)
                if (atoll(res_arr[j][order_key]) > atoll(res_arr[i][order_key])) {        // desc: j > i
                    for (x = 0; x < n_cols; x++) {
                        strcpy(temp, res_arr[i][x]);
                        strcpy(res_arr[i][x], res_arr[j][x]);
                        strcpy(res_arr[j][x], temp);
                    }
                }
            if (!desc)
                if (atoll(res_arr[i][order_key]) > atoll(res_arr[j][order_key])) {        // asc: i > j
                    for (x = 0; x < n_cols; x++) {
                        strcpy(temp, res_arr[i][x]);
                        strcpy(res_arr[i][x], res_arr[j][x]);
                        strcpy(res_arr[j][x], temp);
                    }
                }
        }
    }
}

/*
 ******************************************************** routine function **
 * Print array content into ncurses screen.
 *
 * IN:
 * @window          Ncurses window where result will be printed.
 * @res             Query result, used for column width calculation.
 * @arr             Array which content will be printed.
 * @n_rows          Number of rows in query result.
 * @n_cols          Number of columns in query result.
 * @screen          Current screen, used for getting order key and highlight 
 *                  appropriate column.
 ****************************************************************************
 */
void print_data(WINDOW *window, PGresult *res, char ***arr, int n_rows, int n_cols, struct screen_s * screen)
{
    int i, j, x, order_key;
    struct colAttrs *columns = (struct colAttrs *) malloc(sizeof(struct colAttrs) * n_cols);

    calculate_width(columns, res, arr, n_rows, n_cols);
    wclear(window);

    for (i = 0; i < TOTAL_CONTEXTS; i++)
        if (screen->current_context == screen->context_list[i].context)
            order_key = screen->context_list[i].order_key;

    /* print header */
    wattron(window, A_BOLD);
    for (j = 0, x = 0; j < n_cols; j++, x++)
        if (j == order_key) {
            wattron(window, A_REVERSE);
            wprintw(window, "%-*s", columns[x].width, PQfname(res, j));
            wattroff(window, A_REVERSE);
        } else
            wprintw(window, "%-*s", columns[x].width, PQfname(res, j));
    wprintw(window, "\n");
    wattroff(window, A_BOLD);

    /* print data from array */
    for (i = 0; i < n_rows; i++) {
        for (j = 0, x = 0; j < n_cols; j++, x++)
            wprintw(window, "%-*s", columns[x].width, arr[i][j]);
        wprintw(window, "\n");
    }
    wrefresh(window);
    free(columns);
}

/*
 ****************************************************** key-press function **
 * Change column-based sort
 *
 * IN:
 * @screen              Current screen.
 * @increment           Direction (left or right column).
 ****************************************************************************
 */
void change_sort_order(struct screen_s * screens, bool increment, bool * first_iter)
{
    int min, max, i;
    switch (screens->current_context) {
        case pg_stat_database:
            min = PG_STAT_DATABASE_ORDER_MIN;
            max = PG_STAT_DATABASE_ORDER_MAX;
            break;
        case pg_stat_replication:
            min = PG_STAT_REPLICATION_ORDER_MIN;
            max = PG_STAT_REPLICATION_ORDER_MAX;
            break;
        case pg_stat_user_tables:
            min = PG_STAT_USER_TABLES_ORDER_MIN;
            max = PG_STAT_USER_TABLES_ORDER_MAX;
            break;
        case pg_stat_user_indexes:
            min = PG_STAT_USER_INDEXES_ORDER_MIN;
            max = PG_STAT_USER_INDEXES_ORDER_MAX;
            break;
        case pg_statio_user_tables:
            min = PG_STATIO_USER_TABLES_ORDER_MIN;
            max = PG_STATIO_USER_TABLES_ORDER_MAX;
            break;
        case pg_tables_size:
            min = PG_TABLES_SIZE_ORDER_MIN - 3;
            max = PG_TABLES_SIZE_ORDER_MAX;
            break;
        case pg_stat_activity_long:
            min = PG_STAT_ACTIVITY_LONG_ORDER_MIN;
            max = PG_STAT_ACTIVITY_LONG_ORDER_MIN;
            break;
        case pg_stat_user_functions:
            min = PG_STAT_USER_FUNCTIONS_ORDER_MIN;
            max = PG_STAT_USER_FUNCTIONS_ORDER_MAX;
            *first_iter = true;
            break;
        default:
            break;
    }
    if (increment) {
        for (i = 0; i < TOTAL_CONTEXTS; i++) {
            if (screens->current_context == screens->context_list[i].context) {
                if (screens->context_list[i].order_key + 1 > max)
                    screens->context_list[i].order_key = min;
                else 
                    screens->context_list[i].order_key++;
            }
        }
    }

    if (!increment)
        for (i = 0; i < TOTAL_CONTEXTS; i++) {
            if (screens->current_context == screens->context_list[i].context) {
                if (screens->context_list[i].order_key - 1 < min)
                    screens->context_list[i].order_key = max;
                else
                    screens->context_list[i].order_key--;
            }
        }
}

/*
 ***************************************************** cmd window function **
 * Read input from cmd window.
 *
 * IN:
 * @window          Window where pause status will be printed.
 * @pos             When you delete wrong input, cursor do not moving beyond.
 *
 * OUT:
 * @with_esc        Flag which determines when function finish with ESC.
 *
 * RETURNS:
 * Pointer to the input string.
 ****************************************************************************
 */
void cmd_readline(WINDOW *window, int pos, bool * with_esc, char * str)
{
    int ch;
    int i = 0;

    while ((ch = wgetch(window)) != ERR ) {
        if (ch == 27) {
            wclear(window);
            wprintw(window, "Do nothing. Operation canceled. ");
            nodelay(window, TRUE);
            *with_esc = true;              // Finish with ESC.
            strcpy(str, "");
            return;
        } else if (ch == 10) {
            str[i] = '\0';
            nodelay(window, TRUE);
            *with_esc = false;              // Normal finish with Newline.
            return;
        } else if (ch == KEY_BACKSPACE || ch == KEY_DC || ch == 127) {
            if (i > 0) {
                i--;
                wdelch(window);
            } else {
                wmove(window, 0, pos);
                continue;
            }
        } else {
            str[i] = ch;
            i++;
        }
    }
}

/*
 ****************************************************** key-press function **
 * Change pg_stat_activity long queries min age
 *
 * IN:
 * @window              Ncurses window where msg will be printed.
 * @screen              Current screen.
 ****************************************************************************
 */
void change_min_age(WINDOW * window, struct screen_s * screen)
{
    unsigned int hour, min, sec;
    bool * with_esc = (bool *) malloc(sizeof(bool));
    char min_age[BUFFERSIZE_S];

    echo();
    cbreak();
    nodelay(window, FALSE);
    keypad(window, TRUE);

    wprintw(window, "Enter new min age, format: HH:MM:SS[.NN]: ");
    wrefresh(window);

    cmd_readline(window, 42, with_esc, min_age);
    if (strlen(min_age) != 0 && *with_esc == false) {
        if ((sscanf(min_age, "%u:%u:%u", &hour, &min, &sec)) == 0 || (hour > 23 || min > 59 || sec > 59)) {
            wprintw(window, "Nothing to do. Failed read or invalid value.");
        } else {
            strncpy(screen->pg_stat_activity_min_age, min_age, sizeof(screen->pg_stat_activity_min_age) - 1);
            screen->pg_stat_activity_min_age[sizeof(screen->pg_stat_activity_min_age) - 1] = '\0';
        }
    } else if (strlen(min_age) == 0 && *with_esc == false ) {
        wprintw(window, "Nothing to do. Leave min age %s", screen->pg_stat_activity_min_age);
    }
    
    free(with_esc);
    noecho();
    cbreak();
    nodelay(window, TRUE);
    keypad(window, FALSE);
}
/*
 ****************************************************************************
 * Main program
 ****************************************************************************
 */
int main(int argc, char *argv[])
{
    struct screen_s *screens[MAX_CONSOLE];
    struct stats_cpu_struct *st_cpu[2];
    WINDOW *w_sys, *w_cmd, *w_dba;
    int ch;                             /* key press */
    bool *first_iter = (bool *) malloc(sizeof(bool));
    *first_iter = true;
    static int console_no = 1;
    static int console_index = 0;

    PGconn      *conns[8];
    PGresult    *p_res, *c_res;
    char query[1024];
    int n_rows, n_cols, n_prev_rows;

    /* arrays for PGresults */
    char ***p_arr, ***c_arr, ***r_arr;

    /* process cmd args */
    init_screens(screens);
    if ( argc > 1 ) {
        create_initial_conn(argc, argv, screens);
        create_pgcenterrc_conn(argc, argv, screens, 1);
    } else
        if (create_pgcenterrc_conn(argc, argv, screens, 0) == PGCENTERRC_READ_ERR)
            create_initial_conn(argc, argv, screens);

    /* CPU stats related actions */
    init_stats(st_cpu);
    get_HZ();

    /* open connections to postgres */
    prepare_conninfo(screens);
    open_connections(screens, conns);

    /* init screens */
    initscr();
    cbreak();
    noecho();
    nodelay(stdscr, TRUE);

    w_sys = newwin(5, 0, 0, 0);
    w_cmd = newwin(1, 0, 4, 0);
    w_dba = newwin(0, 0, 5, 0);

    curs_set(0);

    /* main loop */
    while (1) {
        if (key_is_pressed()) {
            curs_set(1);
            ch = getch();
            switch (ch) {
                case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8':
                    console_index = switch_conn(w_cmd, screens, ch, console_index, console_no);
                    console_no = console_index + 1;
                    break;
                case '\033':            // catching arrows: if the first value is esc
                    getch();            // skip the [
                    switch (getch()) {
                        case 'A':       // arrow up
                            /* reserved */
                            break;
                        case 'B':       // arrow down
                            /* reserved */
                            break;
                        case 'C':       // arrow right
                            change_sort_order(screens[console_index], true, first_iter);
                            break;
                        case 'D':       // arrow left
                            change_sort_order(screens[console_index], false, first_iter);
                            break;
                    }
                    break;
                case 'd':
                    wprintw(w_cmd, "Show pg_stat_database");
                    screens[console_index]->current_context = pg_stat_database;
                    *first_iter = true;
                    break;
                case 'r':
                    wprintw(w_cmd, "Show pg_stat_replication");
                    screens[console_index]->current_context = pg_stat_replication;
                    *first_iter = true;
                    break;
                case 't':
                    wprintw(w_cmd, "Show pg_stat_user_tables");
                    screens[console_index]->current_context = pg_stat_user_tables;
                    *first_iter = true;
                    break;
                case 'i':
                    wprintw(w_cmd, "Show pg_stat_user_indexes");
                    screens[console_index]->current_context = pg_stat_user_indexes;
                    *first_iter = true;
                    break;
                case 'y':
                    wprintw(w_cmd, "Show pg_statio_user_tables");
                    screens[console_index]->current_context = pg_statio_user_tables;
                    *first_iter = true;
                    break;
                case 's':
                    wprintw(w_cmd, "Show relations sizes");
                    screens[console_index]->current_context = pg_tables_size;
                    *first_iter = true;
                    break;
                case 'l':
                    wprintw(w_cmd, "Show long transactions (transactions and queries threshold: %s)",
                                    screens[console_index]->pg_stat_activity_min_age);
                    screens[console_index]->current_context = pg_stat_activity_long;
                    *first_iter = true;
                    break;
                case 'm':
                    if (screens[console_index]->current_context == pg_stat_activity_long) {
                        change_min_age(w_cmd, screens[console_index]);
                        *first_iter = true;
                    } else
                        wprintw(w_cmd, "Not allowed here.");                // temporary
                    break;
                case 'f':
                    wprintw(w_cmd, "Show pg_stat_user_functions");
                    screens[console_index]->current_context = pg_stat_user_functions;
                    *first_iter = true;
                    break;
                default:
                    wprintw(w_cmd, "Unknown command - try 'h' for help.");                                                                                     
                    break;
            }
            curs_set(0);
        } else {
            wclear(w_sys);

            /* 
             * Sysstat screen 
             */
            print_title(w_sys, argv[0]);
            print_loadavg(w_sys);
            print_cpu_usage(w_sys, st_cpu);
            print_conninfo(w_sys, screens[console_index], conns[console_index], console_no);
            print_postgres_activity(w_sys, conns[console_index]);
            wrefresh(w_sys);

            /* 
             * Database screen. 
             */
            prepare_query(screens[console_index], query);
            c_res = do_query(conns[console_index], query);
            n_rows = PQntuples(c_res);
            n_cols = PQnfields(c_res);

            /* 
             * on startup or when context switched current data snapshot copied 
             * to previous data snapshot and restart cycle
             */
            if (*first_iter) {
                p_res = c_res;
                usleep(10000);
                *first_iter = false;
                continue;
            }

            /* 
             * when number of rows changed (when db/table/index created or 
             * droped), update previous snapshot to current state and start new 
             * iteration. 
             */
            if (n_prev_rows < n_rows) {
                p_res = c_res;
                n_prev_rows = n_rows;
                usleep(10000);
                continue;
            }

            /* create storages for values from PQgetvalue */
            p_arr = init_array(p_arr, n_rows, n_cols);
            c_arr = init_array(c_arr, n_rows, n_cols);
            r_arr = init_array(r_arr, n_rows, n_cols);

            /* copy whole query results (current, previous) into arrays */
            pgrescpy(p_arr, p_res, n_rows, n_cols);
            pgrescpy(c_arr, c_res, n_rows, n_cols);

            /* diff current and previous arrays and build result array */
            diff_arrays(p_arr, c_arr, r_arr, screens[console_index]->current_context, n_rows, n_cols);

            /* sort result array using order key */
            sort_array(r_arr, n_rows, n_cols, screens[console_index]);

            /* print sorted result array */
            print_data(w_dba, c_res, r_arr, n_rows, n_cols, screens[console_index]);

            /* replace previous database query result with current result */
            p_res = c_res;
            n_prev_rows = n_rows;

            /* free memory allocated for arrays */
            free_array(p_arr, n_rows, n_cols);
            free_array(c_arr, n_rows, n_cols);
            free_array(r_arr, n_rows, n_cols);

            wrefresh(w_cmd);
            wclear(w_cmd);

            /* refresh interval */
            sleep(1);
        }
    }
}