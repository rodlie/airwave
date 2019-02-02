#
# Regular cron jobs for the airwave package
#
0 4	* * *	root	[ -x /usr/bin/airwave_maintenance ] && /usr/bin/airwave_maintenance
