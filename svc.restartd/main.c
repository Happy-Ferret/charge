/* 
 * Charge System Management Framework
 * Sccsid @(#)main.c	1.4 (Charge) 29/06/14
 */
 
static const char sccsid[] ="@(#)main.c	1.4 (Charge) 29/06/14";

#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/param.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <syslog.h>
#include <string.h>
#include <err.h>
#include <unistd.h>
#include "pidlist.h"
#include "ini.h"
#include "util.h"
#include "svc.h"

int selfpipe[2]; /* when written to, this makes kevent return immediately 
 * useful for moving from S_EXITED onwards */
 
#define WSELFPIPE write(selfpipe[1], " ", 1);
#define ENTERSTATE(s) dbg("entering state " #s "\n"); svc->State=(s); svc->MainPIDExited =0;
#define ENTERAUXSTATE(s) dbg("entering AUX state " #s "\n"); svc->AuxState=(s);

int
parseconfig(void* user, const char* section, const char* name,
            const char* value)
{
	Service* svc = (Service*)user;

#define MATCH(s, n) strcmp(section, s) == 0 && strcmp(name, n) == 0
	if (MATCH("Service", "Type"))
	{
		if (!stricmp("Simple", value))
			svc->Type =T_SIMPLE;
		else if (!stricmp("Forking", value))
			svc->Type =T_FORKING;
		else if (!stricmp("Oneshot", value))
			svc->Type =T_ONESHOT;
		dbg("Service Type: %s\n", value);
	}
	else if (MATCH("Service", "PIDFile"))
	{
		svc->PIDFile =strdup(value);
		dbg("Using PIDFile for svc.MainPID, PIDFile: %s\n", svc->PIDFile);
	}
	else if (MATCH("Service", "ExecStartPre"))
		svc->ExecStartPre =strdup(value);
	else if (MATCH("Service", "ExecStart"))
		svc->ExecStart =strdup(value);
	else if (MATCH("Service", "ExecStartPost"))
		svc->ExecStartPost =strdup(value);
	else if (MATCH("Service", "ExecStopPost"))
		svc->ExecStopPost =strdup(value);
	if (MATCH("Service", "Restart"))
	{
		if (!stricmp("No", value))
			svc->Restart =R_NO;
		else if (!stricmp("Always", value))
			svc->Restart =R_ALWAYS;
		else if (!stricmp("On-success", value))
			svc->Restart =R_ON_SUCCESS;
		else if (!stricmp("On-failure", value))
			svc->Restart =R_ON_FAILURE;
		dbg("Service Type: %s\n", value);
	}
	else
	{
		return 0;  /* unknown section/name, error */
	}
	return 1;
}

void
process_proc_kevents(int *kq, struct kevent *ke, Service *svc)
{
	if (ke->fflags & NOTE_FORK)
		dbg("pid %lu called fork()\n", ke->ident);

	if (ke->fflags & NOTE_CHILD)
	{
		dbg("we have a child: %lu\n", ke->ident);
		attach_pid_to_kqueue(kq, ke, ke->ident);
		PIDList_addpid(&svc->PL, ke->ident);
	}

	if (ke->fflags & NOTE_EXIT)
	{
		dbg("pid %lu exited with code %d\n", ke->ident, WEXITSTATUS(ke->data));
		svc->MainPIDExitWstat =ke->data;
		detach_pid_from_kqueue(kq, ke, ke->ident);
		PIDList_delpid(&svc->PL, ke->ident);
		PIDList_delpid(&svc->AuxPL, ke->ident); /* one of these contains it */
		reap();

		if (ke->ident == svc->MainPID)
		{
			dbg("Main PID has exited.\n");
			svc->MainPIDExited =1;
			WSELFPIPE
		}
		else if (ke->ident == svc->AuxMainPID)
		{
			dbg("Main AUX PID has exited.\n");
			svc->AuxMainPIDExited =1;
			WSELFPIPE
		}
	}
	if (ke->fflags & NOTE_TRACKERR)
		printf("couldnt attach to child of %lu\n", ke->ident);
}

int
svc_start_pre(int *kq, struct kevent *ke, Service *svc)
{
	if (svc->ExecStartPre)
	{
		int pid=forkexecve(svc->ExecStartPre, kq, ke, svc, 0);
		if(! pid)
			return 1; /* fail */
		else
		{
			ENTERSTATE(S_START_PRE)
			svc->MainPIDExited =0;
			svc->MainPID =pid;
			set_kqueue_timer(kq, ke, svc->StartTimeout, TIMER_STATELIMIT);
			svc->StateTimerOn =1;
		}
	}
	else
	{
		dbg("skipping state S_START_PRE\n");
	}
	return 0;
}
int
svc_start(int *kq, struct kevent *ke, Service *svc)
{
	int pid=forkexecve(svc->ExecStart, kq, ke, svc, 0);
	if(! pid)
		return 1; /* fail */
	else
	{
		ENTERSTATE(S_START)
		svc->MainPIDExited =0;
		if(svc->Type != T_FORKING)
			svc->MainPID =pid;
		else 
		{
			svc->MainPID =pid;
		}
		if(svc->Type == T_SIMPLE)
		{
			ENTERSTATE(S_ONLINE)
			svc->AuxWant =S_START_POST;
			
		}
	}
	return 0;
}
int
svc_start_post(int *kq, struct kevent *ke, Service *svc)
{
	if (svc->ExecStartPost)
	{
		int pid=forkexecve(svc->ExecStartPost, kq, ke, svc, 1);
		if(! pid)
			return 1; /* fail */
		else
		{
			ENTERAUXSTATE(S_START_POST)
			svc->AuxMainPIDExited =0;
			svc->AuxMainPID =pid;
			svc->AuxWant =S_NONE;
			set_kqueue_timer(kq, ke, svc->StopTimeout, TIMER_AUX);
			svc->AuxStateTimerOn =1;
		}
	}
	else
	{
		dbg("skipping through AUX state S_START_POST\n");
		svc->AuxState =S_START_POST;
		svc->AuxMainPIDExited =2; /* when this is seen, just skip errorcheck */
		svc->AuxWant =S_NONE;
	}
	return 0;
}
int
svc_start_post_oneshot(int *kq, struct kevent *ke, Service *svc)
{
	if (svc->ExecStartPost)
	{
		int pid=forkexecve(svc->ExecStartPost, kq, ke, svc, 0);
		if(! pid)
			return 1; /* fail */
		else
		{
			dbg("entering state S_START_POST\n");
			svc->State =S_START_POST;
			svc->MainPIDExited =0;
			svc->MainPID =pid;
		}
	}
	else
	{
		dbg("skipping threough state S_START_POST\n");
		svc->State =S_START_POST;
		svc->MainPIDExited =2; /* when this is seen, just skip errorcheck */
	}
	return 0;
}
int
svc_stop_post(int *kq, struct kevent *ke, Service *svc)
{
	svc->PIDFileRead =0;
	if (svc->ExecStopPost)
	{
		int pid=forkexecve(svc->ExecStopPost, kq, ke, svc, 0);
		if(! pid)
			return 1; /* fail */
		else
		{
			ENTERSTATE(S_STOP_POST)
			svc->MainPIDExited =0;
			svc->MainPID =pid;
			svc->TimedOut =0;
			set_kqueue_timer(kq, ke, svc->StopTimeout, TIMER_STATELIMIT);
		
		}
	}
	else
		ENTERSTATE(S_STOP_POST)
	return 0;
}
int
svc_kill_stage_1(int *kq, struct kevent *ke, Service *svc, int Type)
{	
	if (Type == MAIN)
	{
		ENTERSTATE(S_STOP_SIGTERM)
		svc->TimedOut =0;
		svc->KillTimedOut =0;
		kill(svc->MainPID, SIGTERM);
		set_kqueue_timer(kq, ke, svc->StopTimeout, TIMER_KILL);
		svc->KillTimerOn =1;
	}
	else
	{
		ENTERAUXSTATE(S_STOP_SIGTERM)
		svc->AuxTimedOut =0;
		svc->AuxKillTimedOut =0;
		kill(svc->AuxMainPID, SIGTERM);
		set_kqueue_timer(kq, ke, svc->StopTimeout, TIMER_AUXKILL);
		svc->AuxKillTimerOn =1;
	}
	WSELFPIPE
	return 0;
}
int
svc_kill_stage_2(int *kq, struct kevent *ke, Service *svc, int Type)
{
	if (Type == MAIN)
	{
		ENTERSTATE(S_STOP_SIGKILL)
		svc->KillTimedOut =0;
		purgepids(svc->PL, SIGKILL);
	}
	else
	{
		ENTERAUXSTATE(S_STOP_SIGKILL)
		svc->AuxKillTimedOut =0;
		purgepids(svc->AuxPL, SIGKILL);
	}
	return 0;
}
int
should_restart(Service *svc) /* 1 for yes, 0 for no */
{
	if (svc->Restart == R_ALWAYS)
		return 1;
	else if (svc->Restart == R_NO)
		return 0;
	else if (svc->Restart == R_ON_SUCCESS)
	{
		if((WEXITSTATUS(svc->MainPIDExitWstat) != 0) || svc->TimedOut)
			return 0;
		else
			return 1;
	}
	else if (svc->Restart == R_ON_FAILURE)
	{
		if((WEXITSTATUS(svc->MainPIDExitWstat) != 0) || svc->TimedOut)
			return 1;
		else
			return 0;
	}
	return 0;
}
int
svc_transition_if_necessary(int *kq, struct kevent *ke, Service *svc)
{
	switch(svc->State)
	{
	case S_INACTIVE:
		svc_start_pre (kq, ke, svc);
		break;
	case S_START_PRE:
	{
		int proceed =1;
		if (svc->TimedOut == 1)
		{
			svc->Want =S_FAILED;
			svc_kill_stage_1(kq, ke, svc, MAIN);
			return 0;
		}
		if (svc->MainPIDExited)
		{
			if (!(should_restart(svc)))
				proceed =0;
		}
		else
			return 0;
		if(svc->StateTimerOn)
		{
			unset_kqueue_timer(kq, ke, svc->StartTimeout, TIMER_STATELIMIT);
			svc->StateTimerOn =0;
		}
		if(!svc->PIDsPurged)
		{
			proceed =0;
			purgepids(svc->PL, SIGTERM);
			usleep(200);
			if (svc->PL != NULL)
			{
				sleep (5);
				purgepids(svc->PL, SIGKILL);
			}
		}
		if (proceed)
		{
			svc_start(kq, ke, svc);
			set_kqueue_timer(kq, ke, svc->StartTimeout, TIMER_STATELIMIT);
			svc->StateTimerOn =1;
		}
		else
		{
			svc->Want =S_FAILED;
		}	
		break;
	}
	case S_START:
		if ((! svc->PIDFileRead) && svc->PIDFile && svc->Type == T_FORKING)
		{
			int res =check_pidfile(svc);
			if(svc->MainPID && !res)
			{
				ENTERSTATE(S_ONLINE)
				unset_kqueue_timer(kq, ke, svc->StartTimeout, TIMER_STATELIMIT);
				svc->StateTimerOn =0;
				svc->AuxWant =S_START_POST;
				return 0;
			}
			if (svc->TimedOut)
			{
				dbg("service didn't fork in time\n");
				svc->Want =S_FAILED;
				svc_kill_stage_1(kq, ke, svc, MAIN);
				return 0;
			}
		}
		break;
	
	case S_ONLINE:
		if(svc->StateTimerOn)
			unset_kqueue_timer(kq, ke, svc->StartTimeout, TIMER_STATELIMIT);
		svc->StateTimerOn =0;
		if(svc->MainPIDExited && svc->AuxState == S_NONE)
		{
			//if we want to restart, set svc->want = s_offline?
			if(should_restart(svc))
				svc->Want =S_STOP_POST;
			else
				svc->Want =S_FAILED;
			/*if(svc->PL)
				svc_kill_stage_2(kq, ke, svc, MAIN);
			else */ /* let S_STOP_POST handle this. */
			//ENTERSTATE(S_STOP_POST)
			svc_kill_stage_1(kq, ke, svc, MAIN);
		}
		else if (!svc->MainPIDExited) /* S_START_PRE is probably running */
		{
			//WSELFPIPE /* keep returning from kevent() until aux is clear */
		}
		break;
	
	case S_STOP_SIGTERM:
		if (svc->PIDsPurged)
		{
			if(svc->KillTimerOn == 1)
				unset_kqueue_timer(kq, ke, svc->StopTimeout, TIMER_KILL);
			svc->KillTimerOn =0;
			ENTERSTATE(S_STOP_SIGKILL)
			WSELFPIPE
		}
		else if(svc->KillTimedOut == 1)
		{
			svc_kill_stage_2(kq, ke, svc, MAIN);
		}
		break;
	case S_STOP_SIGKILL:
		if(svc->AuxState != S_NONE)
			return 0;
		if(should_restart(svc))
		{
			svc->Want =S_INACTIVE;
		}
		else
			svc->Want =S_FAILED;
			
		svc_stop_post(kq, ke, svc);
		break;
	case S_STOP_POST:
		if (svc->TimedOut)
		{
			dbg("S_STOP_POST expired\n");
			if(should_restart(svc))
				svc->Want =S_INACTIVE;
			else
				svc->Want =S_FAILED;
			svc_kill_stage_1(kq, ke, svc, MAIN);
			return 0;
		}
		if (svc->MainPIDExited)
		{
			ENTERSTATE(svc->Want)
		}
		
	}
	return 0;
}
int
svc_aux_transition_if_necessary(int *kq, struct kevent *ke, Service *svc)
{
	switch(svc->AuxWant)
	{
	case S_START_POST:
		dbg("StartPost\n");
		svc_start_post(kq, ke, svc);
		break;
	}
	if(svc->AuxTimedOut)
	{
		svc_kill_stage_1(kq, ke, svc, AUX);
	}
	if (svc->AuxMainPIDExited)
	{
		if (svc->AuxStateTimerOn)
		{
			unset_kqueue_timer(kq, ke, svc->StartTimeout, TIMER_AUX);
			svc->AuxStateTimerOn =0;
		}
		/* purge logic here */
		svc->AuxMainPIDExited =0;
		ENTERAUXSTATE(S_NONE)
	}
	if (svc->AuxPIDsPurged)
	{
		if(svc->AuxKillTimerOn == 1)
			unset_kqueue_timer(kq, ke, svc->StopTimeout, TIMER_AUXKILL);
		svc->AuxKillTimerOn =0;
	}
	else if(svc->AuxKillTimedOut == 1)
	{
		svc_kill_stage_2(kq, ke, svc, AUX);
		ENTERAUXSTATE(S_FAILED)
	}
	return 0;
}
int
main(int argc, char **argv)
{
	int kq, i;
	char ch; /* dispose of selfpipe reads here */
	struct kevent ke;
	struct timespec tmout = { 3,     /* block for 5 seconds at most */
		       0
	};   /* nanoseconds */
	Service svc;
	char* svcname="test.service";
	char nametmp[MAXPATHLEN];
	
	dbg("svc.restartd(8) %s\n", sccsid);
	clearsvc(&svc);
	
	if (pipe(selfpipe) == -1) { dbg("selfpipe failed\n"); return 1; }
	fcntl(selfpipe[0], F_SETFD, FD_CLOEXEC);
	fcntl(selfpipe[1], F_SETFD, FD_CLOEXEC);
	fcntl(selfpipe[0],F_SETFL,fcntl(selfpipe[0],F_GETFL,0) | O_NONBLOCK);
	fcntl(selfpipe[1],F_SETFL,fcntl(selfpipe[1],F_GETFL,0) | O_NONBLOCK);
	
	sprintf(nametmp, "/run/svc/%s", svcname);

	if(mkdir(nametmp, 0755) == -1)
		dbg("mkdir %s failed\n", nametmp);

	if (ini_parse("test.service", parseconfig, &svc) < 0)
	{
		printf("Can't load 'test.ini'\n");
		return 1;
	}

	kq = kqueue();
	if (kq == -1)
		err(1, "kq!");
		
	EV_SET(&ke, selfpipe[0], EVFILT_READ, EV_ADD, 0, 0, 0);
	if (kevent(kq, &ke, 1, 0, 0, 0) == -1) { dbg("kevent failed\n"); return 1; }

	if (svc.PIDFile)
		remove(svc.PIDFile);

	svc_transition_if_necessary(&kq, &ke, &svc);
	while (1)
	{
		memset(&ke, 0x00, sizeof(struct kevent));
		sleep(1);

		/* kevent shall block for 3 seconds - or until something happens */
		i = kevent(kq, NULL, 0, &ke, 1, &tmout);
		if (i == -1)
			err(1, "kevent!");
		process_proc_kevents(&kq, &ke, &svc);
		
		if(ke.ident == selfpipe[0])
		{
			dbg("Selfpipe written to\n");
			read(selfpipe[0], &ch, 1); /* dispose of selfpipe data */
		}
		if (ke.filter == EVFILT_TIMER)
		{
			if (ke.ident == TIMER_STATELIMIT && svc.StateTimerOn)
			{
				dbg("timeout\n");
				svc.TimedOut =1;
			}
			else if (ke.ident == TIMER_AUX && svc.AuxStateTimerOn)
			{
				dbg("AUX timeout\n");
				svc.AuxTimedOut =1;
			}
			else if (ke.ident == TIMER_KILL && svc.KillTimerOn)
			{
				dbg("KILL timeout\n");
				svc.KillTimedOut =1;
			}
			else if (ke.ident == TIMER_AUXKILL && svc.AuxKillTimerOn)
			{
				dbg("AUXKILL timeout\n");
				svc.AuxKillTimedOut =1;
			}
		}
		if (svc.PL == NULL)
			svc.PIDsPurged =1;
		else
			svc.PIDsPurged =0;

		if (svc.AuxPL == NULL)
			svc.AuxPIDsPurged =1;
		else
			svc.AuxPIDsPurged =0;

		svc_transition_if_necessary(&kq, &ke, &svc);
		svc_aux_transition_if_necessary(&kq, &ke, &svc);
		// Maybe writing a byte to a self-pipe is better.

		//if (!svc.MainPIDExited) PIDList_print(&svc.PL);
	}

	return(0);
}
