#include <xpc/xpc.h>
#include <mach/task.h>
#include <objc/objc.h>

#ifdef X86_64
#define _64	1
#endif

#ifdef ARM64
#define _64	1
#endif

#define JDEBUG	1

//
// jlaunchctl: launchctl(1) clone to interface with launchd (2.0.0 +)
//
// Author: J Levin, NewOSXBook.com.
//
// Original author: An unsung hero @AAPL. But hey, this is almost the same as his code :)
// 
// Why: because launchctl source has been closed as of 10.10/8.0, and it's unavailable for iOS -
//      where it might actually be very very useful (*nudge*, *nudge*, *wink*, *wink*)
//
//      This also demonstrates XPC serialization nicely (something I go into more detail in MOXiI 2)
//
// To build:
//
// 	cc  -Wall jlctl.c -o jlctl
//  			or
// 	gcc-iphone -Wall jlctl.c -o jlctl
//
// Note: Apple removed more headers from the iOS SDK (e.g. <xpc/xpc.h> and <launch.h>). 
// You'll need to copy them over from the Mac OS X SDK if you want the iOS version to compile..
//
// Disclaimer: I didn't implement all the launchctl commands - only the most useful ones
//
// Promise: Stay tuned for launchd(8) - to be open-sourced once again, for MOXiI 2)
//
// License: Free. Use it, by all means. And if you do find it useful, let me know.
//          Likewise if you want a feature/command 
//
// ChangeLog: Fix for 32-bit (That's for you, rdacted) 
//
//            10/09/2017 - Updated for Darwin 17 (some routines renumbered!)
//                         hostinfo now works better than original (AAPL, please take my code)
//			   full set of launchctl routines
// 
//

__attribute((used)) const char *ver[]  = {
	"@(#) PROGRAM:jlaunchctl  PROJECT:libjpc-1205.1.10",

#ifdef X86_64
	"@(#) VERSION:Darwin Bootstrapper Control Interface Version 5.0.0: Mon Jul 31 17:29:58 PDT 2017; root:libjpc_executables-1205.1.10~25/jlaunchctl/RELEASE_X86_64" 
#endif
#ifdef ARM
	"@(#) VERSION:Darwin Bootstrapper Control Interface Version 5.0.0: Mon Jul 31 17:29:58 PDT 2017; root:libjpc_executables-1205.1.10~25/jlaunchctl/RELEASE_ARM" 
#endif

#ifdef ARM64
	"@(#) VERSION:Darwin Bootstrapper Control Interface Version 5.0.0: Mon Jul 31 17:29:58 PDT 2017; root:libjpc_executables-1205.1.10~25/jlaunchctl/RELEASE_ARM64" 
#endif



};


extern void *objc_retain (void *);
extern int xpc_pipe_routine (xpc_object_t *xpc_pipe, xpc_object_t *inDict, xpc_object_t **out);
extern char *xpc_strerror (int);
extern int csr_check (int what);

// imported from jUtils - this will compile cleanly without it.
void dumpDict (const char *DictName,  xpc_object_t    *dict);

// This is undocumented, but sooooo useful :)
extern mach_port_t xpc_dictionary_copy_mach_send(xpc_object_t, char *key);

#ifdef JDEBUG
int jdebug = 1;
#else
int jdebug = 0;
#endif
void usage (char *Command);

int __attribute__ ((section("__TEXT, __launchctl"))) k;

// Some of the routine #s launchd recognizes. There are quite a few subsystems
// (stay tuned for MOXiI 2 - list is too long for now)

#define ROUTINE_DEBUG		0x2c1	// 705
#define ROUTINE_SUBMIT		100
#define ROUTINE_BLAME		0x2c3 	// 707
#define ROUTINE_DUMP_PROCESS	0x2c4	// 708
#define ROUTINE_RUNSTATS	0x2c5	// 709
#define ROUTINE_LOAD		0x320	// 800
#define ROUTINE_UNLOAD		0x321	// 801
#define ROUTINE_LOOKUP		0x324
#define ROUTINE_ENABLE		0x328	// 808
#define ROUTINE_DISABLE		0x329   // 809
#define ROUTINE_STATUS		0x32b   // 811

#define ROUTINE_KILL		0x32c
#define ROUTINE_VERSION		0x33c
#define ROUTINE_PRINT_CACHE	0x33c
#define ROUTINE_PRINT		0x33c	// also VERSION.., cache..
#define ROUTINE_REBOOT_USERSPACE	803 // 10.11/9.0 only
#define ROUTINE_START		0x32d	// 813
#define ROUTINE_STOP		0x32e	// 814
#define ROUTINE_LIST		0x32f	// 815
#define ROUTINE_SETENV		0x333	// 819
#define ROUTINE_GETENV		0x334  // 820
#define ROUTINE_RESOLVE_PORT		0x336
#define ROUTINE_EXAMINE		0x33a
#define ROUTINE_LIMIT		0x339	// 825
#define ROUTINE_DUMP_DOMAIN	0x33c	// 828
#define ROUTINE_ASUSER	0x344	// ...
#define ROUTINE_DUMP_STATE	0x342	// 034
#define ROUTINE_DUMPJPCATEGORY		0x345	// was 346 in iOS 9


// XPC sets up global variables using os_alloc_once. By reverse engineering
// you can determine the values. The only one we actually need is the fourth
// one, which is used as an argument to xpc_pipe_routine

struct xpc_global_data {
	uint64_t	a;
	uint64_t	xpc_flags;
	mach_port_t	task_bootstrap_port;  /* 0x10 */
#ifndef _64
	uint32_t	padding;
#endif
	xpc_object_t	xpc_bootstrap_pipe;   /* 0x18 */
	// and there's more, but you'll have to wait for MOXiI 2 for those...
	// ...
};

// os_alloc_once_table:
//
// Ripped this from XNU's libsystem
#define OS_ALLOC_ONCE_KEY_MAX	100

struct _os_alloc_once_s {
	long once;
	void *ptr;
};
extern struct _os_alloc_once_s _os_alloc_once_table[];


typedef 	int (*cmdFunc) (int , int , int, char **);

struct command {
	char *command;
	char *shortHelp;
	char *longHelp;
	cmdFunc	funcPtr;
};

struct command	command_table[];

int do_status (char *ServiceName)
{
#if 0

	"subsystem" => <uint64: 0x10031b500>: 3
	"handle" => <uint64: 0x10031b430>: 0
	"routine" => <uint64: 0x10031b560>: 811
	"name" => <string: 0x10031b4a0> { length = 29, contents = "com.apple.MobileFileIntegrity" }
	"type" => <uint64: 0x10031b900>: 1

#endif

   xpc_object_t dict = xpc_dictionary_create(NULL, NULL,0);
   xpc_dictionary_set_uint64 (dict, "subsystem", 3);               // subsystem (3)
   xpc_dictionary_set_uint64(dict, "type",1);                      // set to 1
   xpc_dictionary_set_uint64(dict, "handle",0); 		   // can be 0 - for system
   xpc_dictionary_set_uint64(dict, "routine", ROUTINE_STATUS) ; // 811
   xpc_dictionary_set_string (dict,"name", ServiceName);

   xpc_object_t	*outDict = NULL;
   struct xpc_global_data  *xpc_gd  = (struct xpc_global_data *)  _os_alloc_once_table[1].ptr;

   int rc = xpc_pipe_routine (xpc_gd->xpc_bootstrap_pipe, dict, &outDict);
   if (rc == 0) {
       int err = xpc_dictionary_get_int64 (outDict, "error");
       if (err) printf("Error:  %d - %s\n", err, xpc_strerror(err));
	else
	{
		// We actually got a reply!
		void *loadedVal = xpc_dictionary_get_value(outDict,"loaded");
		if (!loadedVal) {
			// If NULL, this doesn't exist
			fprintf(stderr,"%s: No such service\n", ServiceName);
			return 0;
		}

	
		
		// Still here? That means we need to show loaded, enabled
		int loaded = xpc_dictionary_get_bool(outDict,"loaded");
		int enabled = xpc_dictionary_get_bool(outDict,"enabled");

		printf("%s: Service %sloaded, %senabled\n",
			ServiceName,
			(loaded) ?"": "not ",
			(enabled)?"": "not ");

	
		return 0;
	}
       return (err);
   } // outDict


return (0);

}


int do_attach (char *ServiceName)
{

#if 0

(lldb) mem read $rdi
0x100200118: 13 15 13 00 a0 00 00 00 07 07 00 00 07 06 00 00  ....?...........
0x100200128: 07 0a 00 00 00 00 00 10 21 43 50 58 05 00 00 00  ........!CPX....
0x100200138: 00 f0 00 00 78 00 00 00 05 00 00 00 73 75 62 73  .?..x.......subs
0x100200148: 79 73 74 65 6d 00 00 00 00 40 00 00 02 00 00 00  ystem....@......
0x100200158: 00 00 00 00 68 61 6e 64 6c 65 00 00 00 40 00 00  ....handle...@..
0x100200168: 00 00 00 00 00 00 00 00 72 6f 75 74 69 6e 65 00  ........routine.
0x100200178: 00 40 00 00 bf 02 00 00 00 00 00 00 6e 61 6d 65  .@..?.......name
0x100200188: 00 00 00 00 00 90 00 00 10 00 00 00 63 6f 6d 2e  ............com.
0x100200198: 61 70 70 6c 65 2e 6b 65 78 74 64 00 74 79 70 65  apple.kextd.type
0x1002001a8: 00 00 00 00 00 40 00 00 01 00 00 00 00 00 00 00  .....@..........
0x1002001b8: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
0x1002001c8: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................

#endif 
 printf("@TODO - tell J he forgot this, or complete it yourself :-)\n");
 return (0);
}
int do_dumpjpcategory (void)
{

#if 0
(lldb) mem read $x0
0x15ce00568: 13 15 13 80 98 00 00 00 07 07 00 00 07 05 00 00  ................
0x15ce00578: 07 0a 00 00 00 00 00 10 01 00 00 00 03 0e 00 00  ................
0x15ce00588: 00 00 00 00 00 00 11 00 21 43 50 58 05 00 00 00  ........!CPX....
0x15ce00598: 00 f0 00 00 60 00 00 00 05 00 00 00 73 75 62 73  ....`.......subs
0x15ce005a8: 79 73 74 65 6d 00 00 00 00 40 00 00 03 00 00 00  ystem....@......
0x15ce005b8: 00 00 00 00 66 64 00 00 00 b0 00 00 68 61 6e 64  ....fd......hand
0x15ce005c8: 6c 65 00 00 00 40 00 00 00 00 00 00 00 00 00 00  le...@..........
0x15ce005d8: 72 6f 75 74 69 6e 65 00 00 40 00 00 46 03 00 00  routine..@..F...
0x15ce005e8: 00 00 00 00 74 79 70 65 00 00 00 00 00 40 00 00  ....type.....@..
0x15ce005f8: 01 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
(lldb) 
#endif 

   xpc_object_t dict = xpc_dictionary_create(NULL,  // const char * const *keys,
                                             NULL, // const xpc_object_t *values,
	 				     0);   // size_t count);

   // And here are the human readable arguments:
   // Args: 
   xpc_dictionary_set_uint64 (dict, "subsystem", 3);               // subsystem (3)
   xpc_dictionary_set_uint64(dict, "type",1);                      // set to 1
   xpc_dictionary_set_uint64(dict, "handle",0); 		   // can be 0 - for system
   xpc_dictionary_set_fd(dict, "fd",1);                             // out fd
   xpc_dictionary_set_uint64(dict, "routine", ROUTINE_DUMPJPCATEGORY) ; // 0x345

   xpc_object_t	*outDict = NULL;
   struct xpc_global_data  *xpc_gd  = (struct xpc_global_data *)  _os_alloc_once_table[1].ptr;

   int rc = xpc_pipe_routine (xpc_gd->xpc_bootstrap_pipe, dict, &outDict);
   if (rc == 0) {
       int err = xpc_dictionary_get_int64 (outDict, "error");
       if (err) printf("Error:  %d - %s\n", err, xpc_strerror(err));
	else
	{
		// We actually got a reply!
		//dumpDict("..", outDict);
	}
       return (err);
   } // outDict


return (0);

}

int do_dumpstate (void)
{
#if 0
(lldb) mem read $rdi
0x100200118: 13 15 13 80 98 00 00 00 07 07 00 00 07 06 00 00  ................
0x100200128: 07 0b 00 00 00 00 00 10 01 00 00 00 03 0a 00 00  ................
0x100200138: 00 00 00 00 00 00 11 00 21 43 50 58 05 00 00 00  ........!CPX....
0x100200148: 00 f0 00 00 60 00 00 00 05 00 00 00 73 75 62 73  .?..`.......subs
0x100200158: 79 73 74 65 6d 00 00 00 00 40 00 00 03 00 00 00  ystem....@......
0x100200168: 00 00 00 00 66 64 00 00 00 b0 00 00 68 61 6e 64  ....fd...?..hand
0x100200178: 6c 65 00 00 00 40 00 00 00 00 00 00 00 00 00 00  le...@..........
0x100200188: 72 6f 75 74 69 6e 65 00 00 40 00 00 43 03 00 00  routine..@..C...
0x100200198: 00 00 00 00 74 79 70 65 00 00 00 00 00 40 00 00  ....type.....@..
0x1002001a8: 01 00 00 00 00 00 00 00 03 0a 00 00 00 00 00 00  ................
#endif
    // Funny. This was iOS 9 only and on dev builds, as 0x343. 
    // In 1205 I have this working as 0x842, on release. *shrug*

   xpc_object_t dict = xpc_dictionary_create(NULL,  // const char * const *keys,

                                             NULL, // const xpc_object_t *values,
	 				     0);   // size_t count);
   // And here are the human readable arguments:
   // Args: 
   xpc_dictionary_set_uint64 (dict, "subsystem", 3);               // subsystem (3)
   xpc_dictionary_set_uint64(dict, "type",1);                      // set to 1
   xpc_dictionary_set_uint64(dict, "handle",0); 		   // can be 0 - for system
   xpc_dictionary_set_fd(dict, "fd", 1);                         // out fd
   xpc_dictionary_set_uint64(dict, "routine", ROUTINE_DUMP_STATE);

   xpc_object_t	*outDict = NULL;
   struct xpc_global_data  *xpc_gd  = (struct xpc_global_data *)  _os_alloc_once_table[1].ptr;

   int rc = xpc_pipe_routine (xpc_gd->xpc_bootstrap_pipe, dict, &outDict);
   if (rc == 0) {
       int err = xpc_dictionary_get_int64 (outDict, "error");
       if (err) printf("Error:  %d - %s\n", err, xpc_strerror(err));
	else
	{
		// We actually got a reply!
		//dumpDict("..", outDict);
	}
       return (err);
   } // outDict

   return 0;
} // do dumpState

int do_wholaunched(char *Pid)
{

	// Does the same as xpc_copy_bootstrap, but not for self - for anyone

   xpc_object_t dict = xpc_dictionary_create(NULL,  // const char * const *keys,
                                             NULL, // const xpc_object_t *values,
	 				     0);   // size_t count);
	
   // This would be handled by xpc_service_routine, the way I do it on OpenXPC
   // but it's unexported, so I recreate this
	
    // <dictionary: 0x100102b20> { count = 5, transaction: 0, voucher = 0x0, contents =\n\t"subsystem" => <uint64: 0x100102af0>: 2\n\t"handle" => <uint64: 0x100102080>: 15583\n\t"routine" => <uint64: 0x100102c20>: 711\n\t"self" => <bool: 0x7fff9e9dbb28>: true\n\t"type" => <uint64: 0x100102030>: 5\n}"

   xpc_dictionary_set_uint64 (dict, "subsystem", 2);               // subsystem (3)
   xpc_dictionary_set_bool(dict, "self",0);                      // not for ourselves
   xpc_dictionary_set_uint64(dict, "type",5); 		   
   xpc_dictionary_set_uint64(dict, "handle",atoi(Pid));   // pid
   xpc_dictionary_set_uint64(dict, "routine", 711);

   xpc_object_t	*outDict = NULL;
   struct xpc_global_data  *xpc_gd  = (struct xpc_global_data *)  _os_alloc_once_table[1].ptr;

 int rc = xpc_pipe_routine (xpc_gd->xpc_bootstrap_pipe, dict, &outDict);
   if (rc == 0) {
       int err = xpc_dictionary_get_int64 (outDict, "error");
       if (err) printf("Error:  %d - %s\n", err, xpc_strerror(err));
        else
        {
                // We actually got a reply!
                dumpDict("..", err);
        }
       return (err);
   } // outDict


 return 0;
}


int do_getenv (char *EnvVar)
{

#if 0

subsystem: 3
  handle: 0
  routine: 820
  type: 1
  envvar: >foo<
  legacy: true

#endif

   xpc_object_t dict = xpc_dictionary_create(NULL,  // const char * const *keys,
                                             NULL, // const xpc_object_t *values,
	 				     0);   // size_t count);


   xpc_dictionary_set_uint64 (dict, "subsystem", 3);               // subsystem (3)
   xpc_dictionary_set_uint64(dict, "handle",0); 		   // can be 0 - for system
   xpc_dictionary_set_string(dict, "envvar", EnvVar);
   xpc_dictionary_set_uint64(dict, "routine",ROUTINE_GETENV);
   xpc_dictionary_set_uint64(dict, "type",1);
   xpc_dictionary_set_bool(dict, "legacy", 1);

   xpc_object_t	*outDict = NULL;
   struct xpc_global_data  *xpc_gd  = (struct xpc_global_data *)  _os_alloc_once_table[1].ptr;

   int rc = xpc_pipe_routine (xpc_gd->xpc_bootstrap_pipe, dict, &outDict);
   if (rc == 0) {
       int err = xpc_dictionary_get_int64 (outDict, "error");
       if (err) printf("Error:  %d - %s\n", err, xpc_strerror(err));
	else
	{
		// We actually got a reply!
		printf("%s\n", xpc_dictionary_get_string(outDict,"value"));
	}
       return (err);

	};
	return 0;
	
} // do_getenv

int do_setenv	(char *EnvVar,char *Val)
{
#if 0
  subsystem: 3
  handle: 0
  routine: 819
  envvars: (dictionary)
    FOO: BAR
  type: 1
  legacy: true

#endif
   xpc_object_t dict = xpc_dictionary_create(NULL,  // const char * const *keys,
                                             NULL, // const xpc_object_t *values,
	 				     0);   // size_t count);

   xpc_object_t envvars = xpc_dictionary_create (NULL, NULL,0);
   if (Val){
   xpc_dictionary_set_string(envvars, EnvVar, Val);
	}
   else {
   xpc_dictionary_set_string(envvars, EnvVar, "");

	}

   xpc_dictionary_set_uint64 (dict, "subsystem", 3);               // subsystem (3)
   xpc_dictionary_set_uint64(dict, "handle",0); 		   // can be 0 - for system
   xpc_dictionary_set_value(dict, "envvars", envvars);
   xpc_dictionary_set_uint64(dict, "routine",ROUTINE_SETENV);
   xpc_dictionary_set_uint64(dict, "type",1);
   xpc_dictionary_set_bool(dict, "legacy", 1);

   xpc_object_t	*outDict = NULL;
   struct xpc_global_data  *xpc_gd  = (struct xpc_global_data *)  _os_alloc_once_table[1].ptr;

   int rc = xpc_pipe_routine (xpc_gd->xpc_bootstrap_pipe, dict, &outDict);
   if (rc == 0) {
       int err = xpc_dictionary_get_int64 (outDict, "error");
       if (err) printf("Error:  %d - %s\n", err, xpc_strerror(err));
	else
	{
		// We actually got a reply!
		//dumpDict("..", outDict);
	}
       return (err);
   } // outDict

  return 0;
} // SETENV


int do_start	(char *ServiceName,int StartStop)
{
   xpc_object_t dict = xpc_dictionary_create(NULL,  // const char * const *keys,
                                             NULL, // const xpc_object_t *values,
	 				     0);   // size_t count);

#if 0
  // For your debugging pleasure: the dump of the start/stop message,
  // obtained by breaking on xpc_pipe_routine, then setting an additional 
  // breakpoint on mach_msg, and reading the third mach_msg()'s first argument
  // (In other words, the message sent by the first actual message down the
  // XPC pipe)

0x100200118: 13 15 13 00 b4 00 00 00 1b 03 00 00 13 04 00 00  ....?...........
0x100200128: 07 0a 00 00 00 00 00 10 21 43 50 58 05 00 00 00  ........!CPX....
0x100200138: 00 f0 00 00 8c 00 00 00 06 00 00 00 73 75 62 73  .?..........subs
0x100200148: 79 73 74 65 6d 00 00 00 00 40 00 00 03 00 00 00  ystem....@......
0x100200158: 00 00 00 00 68 61 6e 64 6c 65 00 00 00 40 00 00  ....handle...@..
0x100200168: 00 00 00 00 00 00 00 00 72 6f 75 74 69 6e 65 00  ........routine.
0x100200178: 00 40 00 00 2d 03 00 00 00 00 00 00 6e 61 6d 65  .@..-.......name
0x100200188: 00 00 00 00 00 90 00 00 12 00 00 00 63 6f 6d 2e  ............com.
0x100200198: 61 70 70 6c 65 2e 73 61 6e 64 62 6f 78 00 00 00  apple.sandbox...
0x1002001a8: 74 79 70 65 00 00 00 00 00 40 00 00 01 00 00 00  type.....@......
0x1002001b8: 00 00 00 00 6c 65 67 61 63 79 00 00 00 20 00 00  ....legacy... ..
#endif
   // And here are the human readable arguments:
   // Args: 
   xpc_dictionary_set_uint64 (dict, "subsystem", 3);               // subsystem (3)
   xpc_dictionary_set_uint64(dict, "type",1);                      // set to 1
   xpc_dictionary_set_uint64(dict, "handle",0); 		   // can be 0 - for system
   xpc_dictionary_set_string(dict, "name", ServiceName);                       // mandatory
   xpc_dictionary_set_bool(dict, "legacy", 1);                       // mandatory
   xpc_dictionary_set_uint64(dict, "routine", StartStop ? ROUTINE_START : ROUTINE_STOP);      // routine (0x32d)

   xpc_object_t	*outDict = NULL;
   struct xpc_global_data  *xpc_gd  = (struct xpc_global_data *)  _os_alloc_once_table[1].ptr;

   int rc = xpc_pipe_routine (xpc_gd->xpc_bootstrap_pipe, dict, &outDict);
   if (rc == 0) {
       int err = xpc_dictionary_get_int64 (outDict, "error");
       if (err) printf("Error:  %d - %s\n", err, xpc_strerror(err));
	else
	{
		// We actually got a reply!
		//dumpDict("..", outDict);
	}
       return (err);
   } // outDict

   return (0);

} //do_start 

int do_examine (void)
{
  // only on dev builds - returns 142 :(
   xpc_object_t dict = xpc_dictionary_create(NULL,  // const char * const *keys,
                                             NULL, // const xpc_object_t *values,
	 				     0);   // size_t count);

#if 0
0x100300118: 13 15 13 00 80 00 00 00 1b 03 00 00 13 04 00 00  ................
0x100300128: 07 0a 00 00 00 00 00 10 21 43 50 58 05 00 00 00  ........!CPX....
0x100300138: 00 f0 00 00 58 00 00 00 04 00 00 00 73 75 62 73  .?..X.......subs
0x100300148: 79 73 74 65 6d 00 00 00 00 40 00 00 03 00 00 00  ystem....@......
0x100300158: 00 00 00 00 68 61 6e 64 6c 65 00 00 00 40 00 00  ....handle...@..
0x100300168: 00 00 00 00 00 00 00 00 72 6f 75 74 69 6e 65 00  ........routine.
0x100300178: 00 40 00 00 3a 03 00 00 00 00 00 00 74 79 70 65  .@..:.......type
0x100300188: 00 00 00 00 00 40 00 00 01 00 00 00 00 00 00 00  .....@..........
#endif

   xpc_dictionary_set_uint64 (dict, "subsystem", 3);               // subsystem (3)
   xpc_dictionary_set_uint64(dict, "handle",0); 		   // system (uid 0)
   xpc_dictionary_set_uint64(dict, "type", 1);                         // 
   xpc_dictionary_set_uint64(dict, "routine", ROUTINE_EXAMINE);      // routine (0x33a

   xpc_object_t	*outDict = NULL;


   struct xpc_global_data  *xpc_gd  = (struct xpc_global_data *)  _os_alloc_once_table[1].ptr;

   int rc = xpc_pipe_routine (xpc_gd->xpc_bootstrap_pipe, dict, &outDict);
   if (rc == 0) {
       int err = xpc_dictionary_get_int64 (outDict, "error");
       if (err) printf("Error:  %d - %s\n", err, xpc_strerror(err));
	// 142 - Examination is only available on the DEVELOPMENT variant.
	return (err);
		}

	return (0);
} // do_examine

int do_list	(char *ServiceName)
{
   xpc_object_t dict = xpc_dictionary_create(NULL,  // const char * const *keys,
                                             NULL, // const xpc_object_t *values,
	 				     0);   // size_t count);

#if 0
0x100200118: 13 15 13 00 90 00 00 00 1b 03 00 00 13 04 00 00  ................
0x100200128: 07 0a 00 00 00 00 00 10 21 43 50 58 05 00 00 00  ........!CPX....
0x100200138: 00 f0 00 00 68 00 00 00 05 00 00 00 73 75 62 73  .?..h.......subs
0x100200148: 79 73 74 65 6d 00 00 00 00 40 00 00 03 00 00 00  ystem....@......
0x100200158: 00 00 00 00 68 61 6e 64 6c 65 00 00 00 40 00 00  ....handle...@..
0x100200168: 00 00 00 00 00 00 00 00 72 6f 75 74 69 6e 65 00  ........routine.
0x100200178: 00 40 00 00 2f 03 00 00 00 00 00 00 74 79 70 65  .@../.......type
0x100200188: 00 00 00 00 00 40 00 00 01 00 00 00 00 00 00 00  .....@..........
0x100200198: 6c 65 67 61 63 79 00 00 00 20 00 00 01 00 00 00  legacy... ......
0x1002001a8: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................

// With an argument:

(lldb) mem read $x0
0x155d02cc8: 13 15 13 00 a0 00 00 00 07 07 00 00 07 05 00 00  ................
0x155d02cd8: 07 0a 00 00 00 00 00 10 21 43 50 58 05 00 00 00  ........!CPX....
0x155d02ce8: 00 f0 00 00 78 00 00 00 05 00 00 00 73 75 62 73  ....x.......subs
0x155d02cf8: 79 73 74 65 6d 00 00 00 00 40 00 00 03 00 00 00  ystem....@......
0x155d02d08: 00 00 00 00 68 61 6e 64 6c 65 00 00 00 40 00 00  ....handle...@..
0x155d02d18: 00 00 00 00 00 00 00 00 72 6f 75 74 69 6e 65 00  ........routine.
0x155d02d28: 00 40 00 00 2f 03 00 00 00 00 00 00 6e 61 6d 65  .@../.......name
0x155d02d38: 00 00 00 00 00 90 00 00 0f 00 00 00 63 6f 6d 2e  ............com.
0x155d02d48: 61 70 70 6c 65 2e 67 65 6f 64 00 00 74 79 70 65  apple.geod..type
0x155d02d58: 00 00 00 00 00 40 00 00 01 00 00 00 00 00 00 00  .....@..........


#endif
   // Args: 
   xpc_dictionary_set_uint64 (dict, "subsystem", 3);               // subsystem (3)
   xpc_dictionary_set_uint64(dict, "handle",0); 		   // can be 0 - for system
   xpc_dictionary_set_uint64(dict, "routine", ROUTINE_LIST);      // routine (0x32f)
   xpc_dictionary_set_uint64(dict, "type",1);                      // set to 1
    if (ServiceName)
	{
	printf("NAME: %s\n", ServiceName);
	xpc_dictionary_set_string(dict, "name", ServiceName);
	}
else
   xpc_dictionary_set_bool(dict, "legacy", 1);                       // mandatory


   xpc_object_t	*outDict = NULL;


   struct xpc_global_data  *xpc_gd  = (struct xpc_global_data *)  _os_alloc_once_table[1].ptr;

   int rc = xpc_pipe_routine (xpc_gd->xpc_bootstrap_pipe, dict, &outDict);
   if (rc == 0) {
       int err = xpc_dictionary_get_int64 (outDict, "error");
       if (err) printf("Error:  %d - %s\n", err, xpc_strerror(err));
	else
	{

		// We actually got a reply!
		xpc_object_t	svcs = xpc_dictionary_get_value(outDict, //xpc_object_t dictionary, 
					       ServiceName ? "service" : "services"); //replyKey); //const char *key);

		if (!svcs)
		{
			fprintf(stderr,"Error: no services returned for list\n"); return 1;
		}
		xpc_type_t	svcsType = xpc_get_type(svcs);
		if (svcsType != XPC_TYPE_DICTIONARY)
		{
			fprintf(stderr,"Error: services returned for list aren't a dictionary!\n"); return 2;
		}

		if (!ServiceName) {
			printf("PID\tStatus\tLabel\n");

		xpc_dictionary_apply(svcs, ^ bool (const char *key, xpc_object_t value) 
		{
			
			// value is a nested dictionary. @TODO: check that. Bleh
			xpc_dictionary_apply(value, ^bool (const char *key, xpc_object_t value1)
			{
				int64_t	val = xpc_int64_get_value(value1);
				if (val) printf("%lld\t", val); else printf("-\t");
				return 1;
			});
			printf ("%s\n", key);

			return 1;
		});
		} // !ServiceName
		else { // for a service, it's a bit different
			dumpDict ("svcs", svcs);
			if (jdebug) { fprintf(stderr, "%s", xpc_copy_description(svcs));}


		}
		
		

	}
       return (err);
   } // outDict

   return (0);

} // do_list

int do_resolve	(pid_t Pid, int Name)
{

#if 0
--- Dictionary 0x7fe7e04025b0, 6 values:
  subsystem: 3
  handle: 0
  routine: 822
  process: |1
  name: 2051
  type: 1
--- End Dictionary 0x7fe7e04025b0
#endif

   xpc_object_t dict = xpc_dictionary_create(NULL,  // const char * const *keys,
                                             NULL, // const xpc_object_t *values,
	 				     0);   // size_t count);

   // Args: 

   xpc_dictionary_set_uint64 (dict, "subsystem", 3);               // subsystem (3)
   xpc_dictionary_set_uint64(dict, "type",1);                      // set to 1
   xpc_dictionary_set_uint64(dict, "handle",0); 		   // can be 0
   xpc_dictionary_set_uint64(dict, "name", Name);                         // out fd
   xpc_dictionary_set_int64(dict, "process", Pid);                         // out fd
   xpc_dictionary_set_uint64(dict, "routine", ROUTINE_RESOLVE_PORT);       // routine (0x339)


   xpc_object_t	*outDict = NULL;
   struct xpc_global_data  *xpc_gd  = (struct xpc_global_data *)  _os_alloc_once_table[1].ptr;

   int rc = xpc_pipe_routine (xpc_gd->xpc_bootstrap_pipe, dict, &outDict);
   if (rc == 0) {
       int err = xpc_dictionary_get_int64 (outDict, "error");
       if (err)  { printf("Error:  %d - %s\n", err, xpc_strerror(err)); return (err); }
	if (outDict) dumpDict("reply", outDict);
	if (jdebug && outDict) { fprintf(stderr, "%s", xpc_copy_description(outDict));}

   } // outDict

   return (0);
} ; // do_resolve_port

int do_arbitrary	(int Routine)
{
   xpc_object_t dict = xpc_dictionary_create(NULL,  // const char * const *keys,
                                             NULL, // const xpc_object_t *values,
	 				     0);   // size_t count);

   // Args: 

   int subsys = Routine / 0x100; // integer division fine
   xpc_dictionary_set_uint64 (dict, "subsystem", subsys );               // subsystem (3)
   xpc_dictionary_set_bool(dict, "print",1);                       // true, naturally
   xpc_dictionary_set_uint64(dict, "type",1);                      // set to 1
   xpc_dictionary_set_uint64(dict, "handle",0); 		   // can be 0
   xpc_dictionary_set_fd(dict, "file", 1);                         // out fd
   xpc_dictionary_set_uint64(dict, "routine", Routine);       // routine (0x339)

	printf("Subsystem %d, Routine %d (0x%x)\n", subsys, Routine, Routine);

   xpc_object_t	*outDict = NULL;
   struct xpc_global_data  *xpc_gd  = (struct xpc_global_data *)  _os_alloc_once_table[1].ptr;

   int rc = xpc_pipe_routine (xpc_gd->xpc_bootstrap_pipe, dict, &outDict);
   if (rc == 0) {
       int err = xpc_dictionary_get_int64 (outDict, "error");
       if (err)  { printf("Error:  %d - %s\n", err, xpc_strerror(err)); return (err); }
	if (outDict) dumpDict("reply", outDict);
	if (jdebug && outDict) { fprintf(stderr, "%s", xpc_copy_description(outDict));}

   } // outDict

   return (0);
} ; // do_arbitrary

int do_limit	(void)
{
   xpc_object_t dict = xpc_dictionary_create(NULL,  // const char * const *keys,
                                             NULL, // const xpc_object_t *values,
	 				     0);   // size_t count);

   // Args: 
   xpc_dictionary_set_uint64 (dict, "subsystem", 3);               // subsystem (3)
   xpc_dictionary_set_bool(dict, "print",1);                       // true, naturally
   xpc_dictionary_set_uint64(dict, "type",1);                      // set to 1
   xpc_dictionary_set_uint64(dict, "handle",0); 		   // can be 0
   xpc_dictionary_set_fd(dict, "file", 1);                         // out fd
   xpc_dictionary_set_uint64(dict, "routine", ROUTINE_LIMIT);      // routine (0x339)

   xpc_object_t	*outDict = NULL;
   struct xpc_global_data  *xpc_gd  = (struct xpc_global_data *)  _os_alloc_once_table[1].ptr;

   int rc = xpc_pipe_routine (xpc_gd->xpc_bootstrap_pipe, dict, &outDict);
   if (rc == 0) {
       int err = xpc_dictionary_get_int64 (outDict, "error");
       if (err) printf("Error:  %d - %s\n", err, xpc_strerror(err));
       return (err);
   } // outDict

   return (0);

} // do_limit

int do_asuser (int Uid, char *Cmd)
{
   // OS X only: this return 45 (unsupported) on iOS so I've compiled
   //            it out of the available commands #if ARM

#if 0
0x100200590: 13 15 13 00 60 00 00 00 07 07 00 00 07 06 00 00  ....`...........
0x1002005a0: 07 0a 00 00 00 00 00 10 21 43 50 58 05 00 00 00  ........!CPX....
0x1002005b0: 00 f0 00 00 68 00 00 00 05 00 00 00 73 75 62 73  .?..h.......subs
0x1002005c0: 79 73 74 65 6d 00 00 00 00 40 00 00 03 00 00 00  ystem....@......
0x1002005d0: 00 00 00 00 68 61 6e 64 6c 65 00 00 00 40 00 00  ....handle...@..
0x1002005e0: 00 00 00 00 00 00 00 00 72 6f 75 74 69 6e 65 00  ........routine.
0x1002005f0: 00 40 00 00 44 03 00 00 00 00 00 00 75 69 64 00  .@..D.......uid.
0x100200600: 00 40 00 00 00 00 00 00 00 00 00 00 74 79 70 65  .@..........type
0x100200610: 00 00 00 00 00 40 00 00 01 00 00 00 00 00 00 00  .....@..........
0x100200620: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
#endif
  // This one is a bit different: it first uses a message to get the 
  // bootstrap and exception ports for the UID:

   xpc_object_t dict = xpc_dictionary_create(NULL,  // const char * const *keys,
                                             NULL, // const xpc_object_t *values,
	 				     0);   // size_t count);
   xpc_dictionary_set_uint64(dict, "routine",ROUTINE_ASUSER); // routine   (0x344)
   xpc_dictionary_set_uint64 (dict, "type", 1);                
   xpc_dictionary_set_uint64 (dict, "handle", 0);                // 0 = system
   xpc_dictionary_set_uint64 (dict, "uid", 0);                // 0 = system
   xpc_dictionary_set_uint64 (dict, "subsystem", 3);                // subsystem (2)

   xpc_object_t	*outDict = NULL;

   struct xpc_global_data  *xpc_gd  = (struct xpc_global_data *)  _os_alloc_once_table[1].ptr;

   int rc = xpc_pipe_routine (xpc_gd->xpc_bootstrap_pipe, dict, &outDict);

   if (rc == 0) {

       int err = xpc_dictionary_get_int64 (outDict, "error");
  
       if (err) {printf("Error:  %d - %s\n", err, xpc_strerror(err)); return (err);}
	if (jdebug && outDict) { fprintf(stderr, "%s", xpc_copy_description(outDict));}
		mach_port_t excp = xpc_dictionary_copy_mach_send (outDict, "exception");
		mach_port_t bsp = xpc_dictionary_copy_mach_send (outDict, "bootstrap");
		printf("BSP: %x\n", bsp);
		printf("EXC: %x\n", excp);
	}
   return (0);
}

int do_kill (char *Signal, char *ServiceName)
{

   xpc_object_t dict = xpc_dictionary_create(NULL,  // const char * const *keys,
                                             NULL, // const xpc_object_t *values,
	 				     0);   // size_t count);

#if 0
(lldb) mem read $x0
0x137600728: 13 15 13 00 b4 00 00 00 07 07 00 00 07 05 00 00  ................
0x137600738: 07 0a 00 00 00 00 00 10 21 43 50 58 05 00 00 00  ........!CPX....
0x137600748: 00 f0 00 00 8c 00 00 00 06 00 00 00 73 75 62 73  ............subs
0x137600758: 79 73 74 65 6d 00 00 00 00 40 00 00 03 00 00 00  ystem....@......
0x137600768: 00 00 00 00 68 61 6e 64 6c 65 00 00 00 40 00 00  ....handle...@..
0x137600778: 00 00 00 00 00 00 00 00 72 6f 75 74 69 6e 65 00  ........routine.
0x137600788: 00 40 00 00 2c 03 00 00 00 00 00 00 6e 61 6d 65  .@..,.......name
0x137600798: 00 00 00 00 00 90 00 00 0f 00 00 00 63 6f 6d 2e  ............com.
0x1376007a8: 61 70 70 6c 65 2e 70 74 70 64 00 00 74 79 70 65  apple.ptpd..type
0x1376007b8: 00 00 00 00 00 40 00 00 01 00 00 00 00 00 00 00  .....@..........
0x1376007c8: 73 69 67 6e 61 6c 00 00 00 30 00 00 01 00 00 00  signal...0......
0x1376007d8: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
#endif

   xpc_dictionary_set_uint64(dict, "routine",ROUTINE_KILL); // routine   (0x32c)
   xpc_dictionary_set_uint64 (dict, "type", 1);                
   xpc_dictionary_set_uint64 (dict, "handle", 0);                // 0 = system
   xpc_dictionary_set_uint64 (dict, "subsystem", 3);                // subsystem (3)
   xpc_dictionary_set_string (dict, "name", ServiceName);	 
   xpc_dictionary_set_int64 (dict, "signal", atoi(Signal));	 

   xpc_object_t	*outDict = NULL;

   struct xpc_global_data  *xpc_gd  = (struct xpc_global_data *)  _os_alloc_once_table[1].ptr;

   int rc = xpc_pipe_routine (xpc_gd->xpc_bootstrap_pipe, dict, &outDict);

  
   if (rc == 0) {
       rc = xpc_dictionary_get_int64 (outDict, "error");
       if (rc) {printf("Error:  %d - %s\n", rc, xpc_strerror(rc)); return (rc);}

	}

  return (rc);
} // do_kill

int do_debug (char *ServiceName, int Handle)
{

#if 0
Mon Oct  9 15:45:31 2017 ==pr==> <pipe: 0x7ff1c44020c0> { name =  }
--- Dictionary 0x7ff1c44025c0, 5 values:
  subsystem: 2
  handle: 0
  routine: 705
  name: >com.apple.kextd<
  type: 1
--- End Dictionary 0x7ff1c44025c0
#endif

   xpc_object_t dict = xpc_dictionary_create(NULL,  // const char * const *keys,
                                             NULL, // const xpc_object_t *values,
	 				     0);   // size_t count);


   xpc_dictionary_set_uint64(dict, "routine",ROUTINE_DEBUG);
   xpc_dictionary_set_uint64 (dict, "type", 1);                
   xpc_dictionary_set_uint64 (dict, "handle", Handle);                // 0 = system
   xpc_dictionary_set_uint64 (dict, "subsystem", 2);                // subsystem (2)
   xpc_dictionary_set_string (dict, "name", ServiceName);	 


   xpc_object_t	*outDict = NULL;

   struct xpc_global_data  *xpc_gd  = (struct xpc_global_data *)  _os_alloc_once_table[1].ptr;

   int rc = xpc_pipe_routine (xpc_gd->xpc_bootstrap_pipe, dict, &outDict);

   if (rc == 0) {
       int err = xpc_dictionary_get_int64 (outDict, "error");
       if (err) {printf("Error:  %d - %s\n", err, xpc_strerror(err)); return (err);}
	
   printf("Service configured for next launch.\n");

       return (err);
	}

   return (0);
} // do_debug

int do_blame (char *ServiceName, int Uid)
{
   xpc_object_t dict = xpc_dictionary_create(NULL,  // const char * const *keys,
                                             NULL, // const xpc_object_t *values,
	 				     0);   // size_t count);

#if 0
0x100200118: 13 15 13 00 a0 00 00 00 1b 03 00 00 13 04 00 00  ....?...........
0x100200128: 07 0a 00 00 00 00 00 10 21 43 50 58 05 00 00 00  ........!CPX....
0x100200138: 00 f0 00 00 78 00 00 00 05 00 00 00 73 75 62 73  .?..x.......subs
0x100200148: 79 73 74 65 6d 00 00 00 00 40 00 00 02 00 00 00  ystem....@......
0x100200158: 00 00 00 00 68 61 6e 64 6c 65 00 00 00 40 00 00  ....handle...@..
0x100200168: 00 00 00 00 00 00 00 00 72 6f 75 74 69 6e 65 00  ........routine.
0x100200178: 00 40 00 00 c3 02 00 00 00 00 00 00 6e 61 6d 65  .@..?.......name
0x100200188: 00 00 00 00 00 90 00 00 10 00 00 00 63 6f 6d 2e  ............com.
0x100200198: 61 70 70 6c 65 2e 6b 65 78 74 64 00 74 79 70 65  apple.kextd.type
0x1002001a8: 00 00 00 00 00 40 00 00 01 00 00 00 00 00 00 00  .....@..........
#endif
   // Args: 

   xpc_dictionary_set_uint64(dict, "routine",ROUTINE_BLAME); // routine   (0x2c3)
   xpc_dictionary_set_uint64 (dict, "type", 1);                
   xpc_dictionary_set_uint64 (dict, "handle", Uid);                // 0 = system
   xpc_dictionary_set_uint64 (dict, "subsystem", 2);                // subsystem (2)
   xpc_dictionary_set_string (dict, "name", ServiceName);	 


   xpc_object_t	*outDict = NULL;

   struct xpc_global_data  *xpc_gd  = (struct xpc_global_data *)  _os_alloc_once_table[1].ptr;

   int rc = xpc_pipe_routine (xpc_gd->xpc_bootstrap_pipe, dict, &outDict);

   if (rc == 0) {
       int err = xpc_dictionary_get_int64 (outDict, "error");
       if (err) {printf("Error:  %d - %s\n", err, xpc_strerror(err)); return (err);}
	
	xpc_object_t	reason = xpc_dictionary_get_value(outDict, //xpc_object_t dictionary, 
					       "reason"); //const char *key);
	 printf("%s\n",xpc_string_get_string_ptr(reason));


       return (err);
	
   } // outDict

   return (0);
} // do blame 


int do_procinfo (unsigned long pid)
{
   xpc_object_t dict = xpc_dictionary_create(NULL,  // const char * const *keys,
                                             NULL, // const xpc_object_t *values,
	 				     0);   // size_t count);

   // Args: 


   xpc_dictionary_set_uint64(dict, "routine",ROUTINE_DUMP_PROCESS); // routine   (0x2c4)
   xpc_dictionary_set_uint64 (dict, "subsystem", 2);                // subsystem (2)

   // Super cool feature: we can pass an fd over XPC.
   // In fact in many methods launchd expects an fd, which it will take over
   // and write to (actual transfer of fd is done via file ports)
   xpc_dictionary_set_fd(dict, "fd",1);                             // out fd

   xpc_dictionary_set_int64(dict, "pid",pid);                       // PID to get info of

   xpc_object_t	*outDict = NULL;


   struct xpc_global_data  *xpc_gd  = (struct xpc_global_data *)  _os_alloc_once_table[1].ptr;

   int rc = xpc_pipe_routine (xpc_gd->xpc_bootstrap_pipe, dict, &outDict);
   if (rc == 0) {
       int err = xpc_dictionary_get_int64 (outDict, "error");
       if (err) printf("Error:  %d - %s\n", err, xpc_strerror(err));
       return (err);
   } // outDict

   return (0);
} // do procinfo

int do_print_disabled (char *Nevermind, int Uid)
{
   xpc_object_t dict = xpc_dictionary_create(NULL,  // const char * const *keys,
                                             NULL, // const xpc_object_t *values,
	 				     0);   // size_t count);

#if 0
0x100200118: 13 15 13 80 ac 00 00 00 1b 03 00 00 13 04 00 00  ....?...........
0x100200128: 07 0b 00 00 00 00 00 10 01 00 00 00 03 0a 00 00  ................
0x100200138: 00 00 00 00 00 00 11 00 21 43 50 58 05 00 00 00  ........!CPX....
0x100200148: 00 f0 00 00 74 00 00 00 06 00 00 00 73 75 62 73  .?..t.......subs
0x100200158: 79 73 74 65 6d 00 00 00 00 40 00 00 03 00 00 00  ystem....@......
0x100200168: 00 00 00 00 66 64 00 00 00 b0 00 00 68 61 6e 64  ....fd...?..hand
0x100200178: 6c 65 00 00 00 40 00 00 00 00 00 00 00 00 00 00  le...@..........
0x100200188: 72 6f 75 74 69 6e 65 00 00 40 00 00 3c 03 00 00  routine..@..<...
0x100200198: 00 00 00 00 64 69 73 61 62 6c 65 64 00 00 00 00  ....disabled....
0x1002001a8: 00 20 00 00 01 00 00 00 74 79 70 65 00 00 00 00  . ......type....
0x1002001b8: 00 40 00 00 01 00 00 00 00 00 00 00 00 00 00 00  .@..............
0x1002001c8: 00 00 00 00 00 00 00 00 03 0a 00 00 00 00 00 00  ................
#endif


   xpc_dictionary_set_uint64 (dict, "subsystem", 3);
   xpc_dictionary_set_uint64(dict, "handle",Uid);
   xpc_dictionary_set_uint64(dict, "type",(Uid ? 2: 1));
   xpc_dictionary_set_uint64(dict, "routine", ROUTINE_PRINT);
   xpc_dictionary_set_bool(dict,"disabled",1);
   xpc_dictionary_set_fd(dict, "fd",1);                             // out fd
   xpc_object_t	*outDict = NULL;

   struct xpc_global_data  *xpc_gd  = (struct xpc_global_data *)  _os_alloc_once_table[1].ptr;

   int rc = xpc_pipe_routine (xpc_gd->xpc_bootstrap_pipe, dict, &outDict);
   if (rc == 0) {
       int err = xpc_dictionary_get_int64 (outDict, "error");
       if (err) printf("Error:  %d - %s\n", err, xpc_strerror(err));
       return (err);
   } // outDict

   return (0);

} // print_disabled

int do_print_cache (void)
{
   xpc_object_t dict = xpc_dictionary_create(NULL,  // const char * const *keys,
                                             NULL, // const xpc_object_t *values,
	 				     0);   // size_t count);

#if 0
0x100200118: 13 15 13 80 a8 00 00 00 1b 03 00 00 13 04 00 00  ....?...........
0x100200128: 07 0b 00 00 00 00 00 10 01 00 00 00 03 0a 00 00  ................
0x100200138: 00 00 00 00 00 00 11 00 21 43 50 58 05 00 00 00  ........!CPX....
0x100200148: 00 f0 00 00 70 00 00 00 06 00 00 00 73 75 62 73  .?..p.......subs
0x100200158: 79 73 74 65 6d 00 00 00 00 40 00 00 03 00 00 00  ystem....@......
0x100200168: 00 00 00 00 66 64 00 00 00 b0 00 00 68 61 6e 64  ....fd...?..hand
0x100200178: 6c 65 00 00 00 40 00 00 00 00 00 00 00 00 00 00  le...@..........
0x100200188: 72 6f 75 74 69 6e 65 00 00 40 00 00 3c 03 00 00  routine..@..<...
0x100200198: 00 00 00 00 74 79 70 65 00 00 00 00 00 40 00 00  ....type.....@..
0x1002001a8: 01 00 00 00 00 00 00 00 63 61 63 68 65 00 00 00  ........cache...

#endif


   xpc_dictionary_set_uint64 (dict, "subsystem", 3);
   xpc_dictionary_set_uint64(dict, "handle",0);
   xpc_dictionary_set_uint64(dict, "type",1);
   xpc_dictionary_set_uint64(dict, "routine", ROUTINE_PRINT);
   xpc_dictionary_set_bool(dict,"cache",1);
   xpc_dictionary_set_fd(dict, "fd",1);                             // out fd
   xpc_object_t	*outDict = NULL;

   struct xpc_global_data  *xpc_gd  = (struct xpc_global_data *)  _os_alloc_once_table[1].ptr;

   int rc = xpc_pipe_routine (xpc_gd->xpc_bootstrap_pipe, dict, &outDict);
   if (rc == 0) {
       int err = xpc_dictionary_get_int64 (outDict, "error");
       if (err) printf("Error:  %d - %s\n", err, xpc_strerror(err));
       return (err);
   } // outDict

   return (0);

} // do_print_cache

int do_print (char *ServiceName, int Uid)
{
   xpc_object_t dict = xpc_dictionary_create(NULL,  // const char * const *keys,
                                             NULL, // const xpc_object_t *values,
	 				     0);   // size_t count);

#if 0  // print system/com.apple.kextd
(lldb) mem read $rdi
0x100200118: 13 15 13 80 b8 00 00 00 1b 03 00 00 13 04 00 00  ....?...........
0x100200128: 07 0b 00 00 00 00 00 10 01 00 00 00 03 0a 00 00  ................
0x100200138: 00 00 00 00 00 00 11 00 21 43 50 58 05 00 00 00  ........!CPX....
0x100200148: 00 f0 00 00 80 00 00 00 06 00 00 00 73 75 62 73  .?..........subs
0x100200158: 79 73 74 65 6d 00 00 00 00 40 00 00 02 00 00 00  ystem....@......
0x100200168: 00 00 00 00 66 64 00 00 00 b0 00 00 68 61 6e 64  ....fd...?..hand
0x100200178: 6c 65 00 00 00 40 00 00 00 00 00 00 00 00 00 00  le...@..........
0x100200188: 72 6f 75 74 69 6e 65 00 00 40 00 00 c4 02 00 00  routine..@..?...
0x100200198: 00 00 00 00 6e 61 6d 65 00 00 00 00 00 90 00 00  ....name........
0x1002001a8: 10 00 00 00 63 6f 6d 2e 61 70 70 6c 65 2e 6b 65  ....com.apple.ke
0x1002001b8: 78 74 64 00 74 79 70 65 00 00 00 00 00 40 00 00  xtd.type.....@..
0x1002001c8: 01 00 00 00 00 00 00 00 03 0a 00 00 00 00 00 00  ................

// and for User/501/com.apple.accounts
0x100104ec8: 13 15 13 80 c4 00 00 00 1b 03 00 00 13 04 00 00  ....?...........
0x100104ed8: 07 0b 00 00 00 00 00 10 01 00 00 00 03 0a 00 00  ................
0x100104ee8: 00 00 00 00 00 00 11 00 21 43 50 58 05 00 00 00  ........!CPX....
0x100104ef8: 00 f0 00 00 8c 00 00 00 06 00 00 00 73 75 62 73  .?..........subs
0x100104f08: 79 73 74 65 6d 00 00 00 00 40 00 00 02 00 00 00  ystem....@......
0x100104f18: 00 00 00 00 66 64 00 00 00 b0 00 00 68 61 6e 64  ....fd...?..hand
0x100104f28: 6c 65 00 00 00 40 00 00 f5 01 00 00 00 00 00 00  le...@..?.......
0x100104f38: 72 6f 75 74 69 6e 65 00 00 40 00 00 c4 02 00 00  routine..@..?...
0x100104f48: 00 00 00 00 6e 61 6d 65 00 00 00 00 00 90 00 00  ....name........
0x100104f58: 1b 00 00 00 63 6f 6d 2e 61 70 70 6c 65 2e 69 6e  ....com.apple.in
0x100104f68: 74 65 72 6e 65 74 61 63 63 6f 75 6e 74 73 00 00  ternetaccounts..
0x100104f78: 74 79 70 65 00 00 00 00 00 40 00 00 02 00 00 00  type.....@......
0x100104f88: 00 00 00 00 00 00 00 00 11 00 00 00 00 00 00 00  ................
0x100104f98: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
0x100104fa8: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
0x100104fb8: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................

// Printing a domain is actually 0x33c:
0x100200118: 13 15 13 80 98 00 00 00 1b 03 00 00 13 04 00 00  ................
0x100200128: 07 0b 00 00 00 00 00 10 01 00 00 00 03 0a 00 00  ................
0x100200138: 00 00 00 00 00 00 11 00 21 43 50 58 05 00 00 00  ........!CPX....
0x100200148: 00 f0 00 00 60 00 00 00 05 00 00 00 73 75 62 73  .?..`.......subs
0x100200158: 79 73 74 65 6d 00 00 00 00 40 00 00 03 00 00 00  ystem....@......
0x100200168: 00 00 00 00 66 64 00 00 00 b0 00 00 68 61 6e 64  ....fd...?..hand
0x100200178: 6c 65 00 00 00 40 00 00 00 00 00 00 00 00 00 00  le...@..........
0x100200188: 72 6f 75 74 69 6e 65 00 00 40 00 00 3c 03 00 00  routine..@..<...
0x100200198: 00 00 00 00 74 79 70 65 00 00 00 00 00 40 00 00  ....type.....@..
0x1002001a8: 01 00 00 00 00 00 00 00 03 0a 00 00 00 00 00 00  ................


#endif

   xpc_dictionary_set_uint64(dict, "handle",Uid);
   if (ServiceName)
	{  xpc_dictionary_set_string(dict, "name", ServiceName);
   	   xpc_dictionary_set_uint64(dict, "routine", ROUTINE_DUMP_PROCESS);
           xpc_dictionary_set_uint64 (dict, "subsystem", 2);
	}
   else
	{
	   xpc_dictionary_set_uint64(dict, "routine", ROUTINE_DUMP_DOMAIN);
	   xpc_dictionary_set_uint64 (dict, "subsystem", 3);
	}

   xpc_dictionary_set_uint64(dict, "type",(Uid ? 2: 1));

   xpc_dictionary_set_fd(dict, "fd",1);                             // out fd
   xpc_object_t	*outDict = NULL;

   struct xpc_global_data  *xpc_gd  = (struct xpc_global_data *)  _os_alloc_once_table[1].ptr;

   int rc = xpc_pipe_routine (xpc_gd->xpc_bootstrap_pipe, dict, &outDict);
   if (rc == 0) {
       int err = xpc_dictionary_get_int64 (outDict, "error");
       if (err) printf("Error:  %d - %s\n", err, xpc_strerror(err));
       return (err);
   } // outDict

   return (0);


} // do_print



int do_version(void)
{
   xpc_object_t dict = xpc_dictionary_create(NULL,  // const char * const *keys,
                                             NULL, // const xpc_object_t *values,
	 				     0);   // size_t count);

#if 0
0x100200118: 13 15 13 80 a8 00 00 00 1b 03 00 00 13 04 00 00  ....?...........
0x100200128: 07 0b 00 00 00 00 00 10 01 00 00 00 03 0a 00 00  ................
0x100200138: 00 00 00 00 00 00 11 00 21 43 50 58 05 00 00 00  ........!CPX....
0x100200148: 00 f0 00 00 70 00 00 00 06 00 00 00 73 75 62 73  .?..p.......subs
0x100200158: 79 73 74 65 6d 00 00 00 00 40 00 00 03 00 00 00  ystem....@......
0x100200168: 00 00 00 00 66 64 00 00 00 b0 00 00 68 61 6e 64  ....fd...?..hand
0x100200178: 6c 65 00 00 00 40 00 00 00 00 00 00 00 00 00 00  le...@..........
0x100200188: 72 6f 75 74 69 6e 65 00 00 40 00 00 3c 03 00 00  routine..@..<...
0x100200198: 00 00 00 00 74 79 70 65 00 00 00 00 00 40 00 00  ....type.....@..
0x1002001a8: 01 00 00 00 00 00 00 00 76 65 72 73 69 6f 6e 00  ........version.
0x1002001b8: 00 20 00 00 01 00 00 00 03 0a 00 00 00 00 00 00  . ..............
0x1002001c8: 00 00 00 00 00 00 00 00 11 00 00 00 00 00 00 00  ................
#endif



   // Args: 
   xpc_dictionary_set_uint64(dict, "routine",ROUTINE_VERSION); // routine   (0x33c)
   xpc_dictionary_set_uint64 (dict, "handle", 0);               
   xpc_dictionary_set_bool (dict, "version", 1);  // without this launchd would print system              
   xpc_dictionary_set_uint64 (dict, "type", 1);               
   xpc_dictionary_set_uint64 (dict, "subsystem", 3);                // subsystem (3)
   xpc_dictionary_set_fd(dict, "fd",1);                             // out fd

   xpc_object_t	*outDict = NULL;


   struct xpc_global_data  *xpc_gd  = (struct xpc_global_data *)  _os_alloc_once_table[1].ptr;

   int rc = xpc_pipe_routine (xpc_gd->xpc_bootstrap_pipe, dict, &outDict);
   if (rc == 0) {
       int err = xpc_dictionary_get_int64 (outDict, "error");
       if (err) printf("Error:  %d - %s\n", err, xpc_strerror(err));
       return (err);
   } // outDict

   return (0);

} // do version

int do_variant(void)
{
   xpc_object_t dict = xpc_dictionary_create(NULL,  // const char * const *keys,
                                             NULL, // const xpc_object_t *values,
	 				     0);   // size_t count);

   // Args: 
   xpc_dictionary_set_uint64(dict, "routine",ROUTINE_VERSION); // routine   (0x33c)
   xpc_dictionary_set_uint64 (dict, "handle", 0);               
   xpc_dictionary_set_bool (dict, "variant", 1);  // without this launchd would print system              
   xpc_dictionary_set_uint64 (dict, "type", 1);               
   xpc_dictionary_set_uint64 (dict, "subsystem", 3);                // subsystem (3)
   xpc_dictionary_set_fd(dict, "fd",1);                             // out fd

   xpc_object_t	*outDict = NULL;


   struct xpc_global_data  *xpc_gd  = (struct xpc_global_data *)  _os_alloc_once_table[1].ptr;

   int rc = xpc_pipe_routine (xpc_gd->xpc_bootstrap_pipe, dict, &outDict);
   if (rc == 0) {
       int err = xpc_dictionary_get_int64 (outDict, "error");
       if (err) printf("Error:  %d - %s\n", err, xpc_strerror(err));
       return (err);
   } // outDict

   return (0);

} // do_variant


int do_runstats (char *ServiceName)
{
   xpc_object_t dict = xpc_dictionary_create(NULL,  // const char * const *keys,
                                             NULL, // const xpc_object_t *values,
	 				     0);   // size_t count);

#if 0  // runstats system/com.apple.kextd
0x100200118: 13 15 13 00 a0 00 00 00 1b 03 00 00 13 04 00 00  ....?...........
0x100200128: 07 0a 00 00 00 00 00 10 21 43 50 58 05 00 00 00  ........!CPX....
0x100200138: 00 f0 00 00 78 00 00 00 05 00 00 00 73 75 62 73  .?..x.......subs
0x100200148: 79 73 74 65 6d 00 00 00 00 40 00 00 02 00 00 00  ystem....@......
0x100200158: 00 00 00 00 68 61 6e 64 6c 65 00 00 00 40 00 00  ....handle...@..
0x100200168: 00 00 00 00 00 00 00 00 72 6f 75 74 69 6e 65 00  ........routine.
0x100200178: 00 40 00 00 c5 02 00 00 00 00 00 00 6e 61 6d 65  .@..?.......name
0x100200188: 00 00 00 00 00 90 00 00 10 00 00 00 63 6f 6d 2e  ............com.
0x100200198: 61 70 70 6c 65 2e 6b 65 78 74 64 00 74 79 70 65  apple.kextd.type
0x1002001a8: 00 00 00 00 00 40 00 00 01 00 00 00 00 00 00 00  .....@..........
0x1002001b8: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
0x1002001c8: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
#endif

   xpc_dictionary_set_uint64 (dict, "subsystem", 2);
   xpc_dictionary_set_uint64(dict, "handle",0);
   xpc_dictionary_set_string(dict, "name", ServiceName);
   xpc_dictionary_set_uint64(dict, "type",1);
   xpc_dictionary_set_uint64(dict, "routine", ROUTINE_RUNSTATS);
   xpc_object_t	*outDict = NULL;

   struct xpc_global_data  *xpc_gd  = (struct xpc_global_data *)  _os_alloc_once_table[1].ptr;

   int rc = xpc_pipe_routine (xpc_gd->xpc_bootstrap_pipe, dict, &outDict);
   if (rc == 0) {
       int err = xpc_dictionary_get_int64 (outDict, "error");
       if (err) printf("Error:  %d - %s\n", err, xpc_strerror(err));
       return (err);
   } // outDict

   return (0);


} // do_runstats
int do_lookup (char *EndpointName, int Uid)
{

#if 0
0x100111e98: 13 15 13 00 e8 00 00 00 1b 03 00 00 03 0e 00 00  ....?...........
0x100111ea8: 07 0f 00 00 00 00 00 10 21 43 50 58 05 00 00 00  ........!CPX....
0x100111eb8: 00 f0 00 00 c0 00 00 00 07 00 00 00 73 75 62 73  .?..?.......subs
0x100111ec8: 79 73 74 65 6d 00 00 00 00 40 00 00 03 00 00 00  ystem....@......
0x100111ed8: 00 00 00 00 68 61 6e 64 6c 65 00 00 00 40 00 00  ....handle...@..
0x100111ee8: 00 00 00 00 00 00 00 00 72 6f 75 74 69 6e 65 00  ........routine.
0x100111ef8: 00 40 00 00 24 03 00 00 00 00 00 00 66 6c 61 67  .@..$.......flag
0x100111f08: 73 00 00 00 00 40 00 00 08 00 00 00 00 00 00 00  s....@..........
0x100111f18: 6e 61 6d 65 00 00 00 00 00 90 00 00 27 00 00 00  name........'...
0x100111f28: 63 6f 6d 2e 61 70 70 6c 65 2e 63 6f 72 65 73 65  com.apple.corese
0x100111f38: 72 76 69 63 65 73 2e 6c 61 75 6e 63 68 73 65 72  rvices.launchser
0x100111f48: 76 69 63 65 73 64 00 00 74 79 70 65 00 00 00 00  vicesd..type....
0x100111f58: 00 40 00 00 01 00 00 00 00 00 00 00 6c 6f 6f 6b  .@..........look
0x100111f68: 75 70 2d 68 61 6e 64 6c 65 00 00 00 00 40 00 00  up-handle....@..
0x100111f78: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
0x100111f88: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................

#endif

	uint32_t flags = 0x8;
	printf("UID: %d.. Flags: %d.. Name: %s\n", Uid, flags, EndpointName);
	
   xpc_object_t dict = xpc_dictionary_create(NULL,  // const char * const *keys,
                                             NULL, // const xpc_object_t *values,
	 				     0);   // size_t count);

   // And here are the human readable arguments:
   // Args: 
   xpc_dictionary_set_uint64(dict, "subsystem", 3);               // subsystem (3)
   xpc_dictionary_set_uint64(dict, "flags",flags);                      // set to 8
   xpc_dictionary_set_uint64(dict, "type",1);                      // set to 1
   xpc_dictionary_set_uint64(dict, "handle",Uid); 		   // can be 0 - for system
   xpc_dictionary_set_string(dict, "name", EndpointName);            // mandatory
   xpc_dictionary_set_uint64(dict, "lookup-handle", 0);                       // mandatory
   xpc_dictionary_set_uint64(dict, "routine", ROUTINE_LOOKUP);

   xpc_object_t	*outDict = NULL;


   struct xpc_global_data  *xpc_gd  = (struct xpc_global_data *)  _os_alloc_once_table[1].ptr;

   int rc = xpc_pipe_routine (xpc_gd->xpc_bootstrap_pipe, dict, &outDict);
   if (rc == 0) {
       int err = xpc_dictionary_get_int64 (outDict, "error");
       if (err) printf("Error:  %d - %s\n", err, xpc_strerror(err));
	else
	{
		// We actually got a reply! The port we need is in "port"..
		
       		xpc_object_t portAsXPCObject = xpc_dictionary_get_value (outDict, "port");
		if (portAsXPCObject) printf ("Got port 0x%x\n",  * ((uint32_t *)portAsXPCObject));
		
		
		

	}
       return (err);
   } // outDict

  return 0;
} // do_lookup

int do_submit	(char *Program, int KeepAlive)
{
#if 0

0x1001051f8: 13 15 13 00 38 01 00 00 1b 03 00 00 13 04 00 00  ....8...........
0x100105208: 07 0a 00 00 00 00 00 10 21 43 50 58 05 00 00 00  ........!CPX....
0x100105218: 00 f0 00 00 10 01 00 00 05 00 00 00 73 75 62 73  .?..........subs
0x100105228: 79 73 74 65 6d 00 00 00 00 40 00 00 07 00 00 00  ystem....@......
0x100105238: 00 00 00 00 68 61 6e 64 6c 65 00 00 00 40 00 00  ....handle...@..
0x100105248: 00 00 00 00 00 00 00 00 72 65 71 75 65 73 74 00  ........request.
0x100105258: 00 f0 00 00 a8 00 00 00 01 00 00 00 53 75 62 6d  .?..?.......Subm
0x100105268: 69 74 4a 6f 62 00 00 00 00 f0 00 00 90 00 00 00  itJob....?......
0x100105278: 04 00 00 00 4c 61 62 65 6c 00 00 00 00 90 00 00  ....Label.......
0x100105288: 05 00 00 00 74 65 73 74 00 00 00 00 4b 65 65 70  ....test....Keep
0x100105298: 41 6c 69 76 65 00 00 00 00 20 00 00 01 00 00 00  Alive.... ......
0x1001052a8: 50 72 6f 67 72 61 6d 00 00 90 00 00 2d 00 00 00  Program.....-...
0x1001052b8: 2f 41 70 70 6c 69 63 61 74 69 6f 6e 73 2f 43 68  /Applications/Ch
0x1001052c8: 65 73 73 2e 61 70 70 2f 43 6f 6e 74 65 6e 74 73  ess.app/Contents
0x1001052d8: 2f 4d 61 63 4f 53 2f 43 68 65 73 73 00 00 00 00  /MacOS/Chess....
0x1001052e8: 50 72 6f 67 72 61 6d 41 72 67 75 6d 65 6e 74 73  ProgramArguments
0x1001052f8: 00 00 00 00 00 e0 00 00 04 00 00 00 00 00 00 00  .....?..........
0x100105308: 72 6f 75 74 69 6e 65 00 00 40 00 00 64 00 00 00  routine..@..d...
0x100105318: 00 00 00 00 74 79 70 65 00 00 00 00 00 40 00 00  ....type.....@..
#endif

   xpc_object_t dict = xpc_dictionary_create(NULL,  // const char * const *keys,
                                             NULL, // const xpc_object_t *values,
	 				     0);   // size_t count);


   xpc_object_t	request = xpc_dictionary_create(NULL,  // const char * const *keys,
                                             NULL, // const xpc_object_t *values,
                                             0);   // size_t count);

   xpc_object_t submitJob = xpc_dictionary_create(NULL,  // const char * const *keys,
                                             NULL, // const xpc_object_t *values,
                                             0);   // size_t count);

   
   xpc_dictionary_set_bool   (submitJob, "KeepAlive", KeepAlive);
   xpc_dictionary_set_string (submitJob, "Program", Program);
   xpc_dictionary_set_string (submitJob, "Label", "xxx");

 
   xpc_object_t programArguments = xpc_array_create(NULL, 0);
   xpc_dictionary_set_value  (submitJob, "ProgramArguments", programArguments);

   xpc_dictionary_set_value (request, "SubmitJob", submitJob);
   xpc_dictionary_set_value (dict, "request", request);
  
   // Args: 
   xpc_dictionary_set_uint64 (dict, "subsystem", 7);               // subsystem (7)
   xpc_dictionary_set_uint64(dict, "type",7);                      // set to 7
   xpc_dictionary_set_uint64(dict, "handle",0); 		   // can be 0
   xpc_dictionary_set_uint64(dict, "routine", ROUTINE_SUBMIT);      // routine (100)


   xpc_object_t	*outDict = NULL;

   struct xpc_global_data  *xpc_gd  = (struct xpc_global_data *)  _os_alloc_once_table[1].ptr;

   int rc = xpc_pipe_routine (xpc_gd->xpc_bootstrap_pipe, dict, &outDict);
   if (rc == 0) {
       int err = xpc_dictionary_get_int64 (outDict, "error");
       if (err) printf("Error:  %d - %s\n", err, xpc_strerror(err));
       return (err);
   } // outDict

   return (0);

} // do_submit


int do_enable(char *Service, int Enable)
{

	if (!Service) return -1;
	
#if 0
Mon Oct  9 16:25:32 2017 ==pr==> <pipe: 0x7fbbf94020c0> { name =  }
--- Dictionary 0x7fbbf94025c0, 6 values:
  subsystem: 3
  handle: 0
  routine: 809
  name: >fooooo<
  type: 1
  names: Array (1 values)
  names (0): >fooooo<

#endif

   xpc_object_t dict = xpc_dictionary_create(NULL,  // const char * const *keys,
                                             NULL, // const xpc_object_t *values,
	 				     0);   // size_t count);


   xpc_object_t s = xpc_string_create(Service);
   xpc_object_t names = xpc_array_create(&s, 1);

   xpc_dictionary_set_value (dict, "names", names);

  
   // Args: 
   xpc_dictionary_set_uint64 (dict, "subsystem", 3);               // subsystem (3)
	// xpc_dictionary_set_bool(dict, "legacy", true);
   xpc_dictionary_set_uint64(dict, "type",1);                      // set to 1
   xpc_dictionary_set_uint64(dict, "handle",0); 		   // can be 0
   xpc_dictionary_set_uint64(dict, "routine",Enable? ROUTINE_ENABLE : ROUTINE_DISABLE);
   xpc_dictionary_set_string(dict,"name", Service);


   xpc_object_t	*outDict = NULL;
   struct xpc_global_data  *xpc_gd  = (struct xpc_global_data *)  _os_alloc_once_table[1].ptr;


   int rc = xpc_pipe_routine (xpc_gd->xpc_bootstrap_pipe, dict, &outDict);
   if (rc == 0) {
       xpc_object_t errs = xpc_dictionary_get_value (outDict, "errors");
       int c = xpc_dictionary_get_count(errs);
       if (c) dumpDict ("test", outDict);
   } // outDict

   return (rc);



} //  do_enable


int do_load(char *Plist, int Legacy)
{

	if (!Plist) return -1;
	
#if 0


subsystem: 3
  handle: 0
  enable: false
  routine: 800
  paths: Array (1 values)
DESC: <string: 0x7fd9d3402850> { length = 12, contents = "/tmp/c.plist" }
  paths (0): >/tmp/c.plist<

  type: 1
  legacy: true
--- End Dictionary 0

#endif
   xpc_object_t dict = xpc_dictionary_create(NULL,  // const char * const *keys,
                                             NULL, // const xpc_object_t *values,
	 				     0);   // size_t count);


   xpc_object_t s = xpc_string_create(Plist);
   xpc_object_t paths = xpc_array_create(&s, 1);

   xpc_dictionary_set_value (dict, "paths", paths);

  
   // Args: 
   xpc_dictionary_set_uint64 (dict, "subsystem", 3);               // subsystem (3)
   xpc_dictionary_set_bool(dict, "enable", false);
	if (Legacy) xpc_dictionary_set_bool(dict, "legacy", true);
   xpc_dictionary_set_uint64(dict, "type",1);                      // set to 1
   xpc_dictionary_set_uint64(dict, "handle",0); 		   // can be 0
   xpc_dictionary_set_uint64(dict, "routine", ROUTINE_LOAD);


   xpc_object_t	*outDict = NULL;
   struct xpc_global_data  *xpc_gd  = (struct xpc_global_data *)  _os_alloc_once_table[1].ptr;


   int rc = xpc_pipe_routine (xpc_gd->xpc_bootstrap_pipe, dict, &outDict);
   if (rc == 0) {
       xpc_object_t errs = xpc_dictionary_get_value (outDict, "errors");
	int c = xpc_dictionary_get_count(errs);
       if (c) dumpDict ("test", outDict);
   } // outDict

   return (0);



} //  do_load

int do_unload(char *Plist, int Legacy)
{

	
#if 0
subsystem: 3
  handle: 0
  routine: 801
  paths: Array (1 values)
  paths (0): >/tmp/c.plist<
  disable: false
  type: 1
  legacy: true
--- End Dictionary 0x7fa7bb4025c0
#endif
   xpc_object_t dict = xpc_dictionary_create(NULL,  // const char * const *keys,
                                             NULL, // const xpc_object_t *values,
	 				     0);   // size_t count);

	

if (Plist) {
   xpc_object_t s = xpc_string_create(Plist);
   xpc_object_t paths = xpc_array_create(&s, 1);
   xpc_dictionary_set_value (dict, "paths", paths);
}


  
   // Args: 
   xpc_dictionary_set_uint64 (dict, "subsystem", 3);               // subsystem (3)
   xpc_dictionary_set_bool(dict, "disable", false);
	if (Legacy) xpc_dictionary_set_bool(dict, "legacy", true);
   xpc_dictionary_set_uint64(dict, "type",1);                      // set to 1
   xpc_dictionary_set_uint64(dict, "handle",0); 		   // can be 0
   xpc_dictionary_set_uint64(dict, "routine", ROUTINE_UNLOAD);


   xpc_object_t	*outDict = NULL;
   struct xpc_global_data  *xpc_gd  = (struct xpc_global_data *)  _os_alloc_once_table[1].ptr;


   int rc = xpc_pipe_routine (xpc_gd->xpc_bootstrap_pipe, dict, &outDict);
   if (rc == 0) {
       xpc_object_t errs = xpc_dictionary_get_value (outDict, "errors");
	int c = xpc_dictionary_get_count(errs);
       if (c) dumpDict ("test", outDict);

   } // outDict

   return (0);



} //  do_unload


int command_bootstrap(int a, int b, int argc, char **argv)
{
	if (argc <2) { usage("bootstrap"); return 64;}
	// @TODO : stat
	return (do_load(argv[2],0));
}
int command_bootout(int a, int b, int argc, char **argv)
{
	if (argc <2) { usage("bootstrap"); return 64;}
	// @TODO : stat
	return (do_unload(NULL, 0)); //argv[2],0));
}


int command_load(int a, int b, int argc, char **argv)
{
	if (argc <2) { usage("load"); return 64;}
	// @TODO : stat
	return (do_load(argv[2],1));
}
int command_unload(int a, int b, int argc, char **argv)
{
	if (argc <2) { usage("unload"); return 64;}
	// @TODO : stat
	return (do_unload(argv[2],1));
}


int command_debug(int a, int b, int argc, char **argv)
{
	if (argc <2) { usage("debug"); return 64;}
	return (do_debug(argv[2],1));
} // debug

int command_enable(int a, int b, int argc, char **argv)
{
	if (argc <2) { usage("enable"); return 64;}
	return (do_enable(argv[2],1));
} // enable

int command_disable(int a, int b, int argc, char **argv)
{
	if (argc <2) { usage("disable"); return 64;}
	// @TODO : stat
	return (do_enable(argv[2],0));
}


int command_wholaunched (int a, int b, int argc, char **argv){

	return (do_wholaunched(argv[2]));
	

}

int command_start (int a, int b, int argc , char **argv)
{

	if (argc <2) {usage("start"); return 64;}

	return (do_start(argv[2],1));


}; // command_start

int command_status (int a, int b, int argc , char **argv)
{

	if (argc <2) {usage("status"); return 64;}

	return (do_status(argv[2]));


}; // command_start


int command_stop (int a, int b, int argc , char **argv)
{

	if (argc <2) {usage("start"); return 64;}

	return (do_start(argv[2],0));

}; // command_stop


int command_getenv (int a, int b, int argc , char **argv)
{

	if (argc <2) {usage("getenv"); return 64;}

	return (do_getenv(argv[2]));

} // command_getenv

int command_setenv (int a, int b, int argc , char **argv)
{

	if (argc <3) {usage("setenv"); return 64;}

	return (do_setenv(argv[2],argv[3]));

}; // command_setenv

int command_unsetenv (int a, int b, int argc , char **argv)
{

	if (argc <2) {usage("unsetenv"); return 64;}

	return (do_setenv(argv[2],NULL));

}; // command_setenv

int command_examine (int a, int b, int argc , char **argv)
{

	return (do_examine());

} // examine 
int command_dumpstate (int a, int b, int argc , char **argv)
{

	return (do_dumpstate());

} 

int command_list (int a, int b, int argc , char **argv)
{

	return (do_list((argc > 1 ? argv[2] : NULL)));

}; // command_limit

int command_lookup (int a, int b, int argc , char **argv)
{

	if (argc <2) {usage("lookup"); return 64;}
	int Uid = 0 ;
	char *u = getenv ("uid");
	if (u) sscanf (u, "%d", &Uid);
	return (do_lookup (argv[2],Uid));

}; // command_limit


int command_limit (int a, int b, int argc , char **argv)
{

	return (do_limit());

}; // command_limit

int command_asuser (int a, int b, int argc , char **argv)
{

	if (argc != 1) return (64); // or something like that
	return (do_asuser(0, "XX"));

}; // command_limit


// _copy_attrs is private , in jpc

#if 0
int command_attrs (int a, int b, int argc, char **argv)
{
	printf ("HERE : %s\n", argv[2]);
	xpc_object_t attrs;
	int rc = _copy_attrs (argv[2], &attrs);
	printf("NOW HERE\n");
	if (rc == 0) {
		dumpDict ("Attributes:" ,attrs);

	}
	else { printf("RC: %d\n", rc);}
	printf("DONE\n");

}
#endif

int command_dumpjpcategory (int a , int b, int argc, char **argv)
{
	
	if (argc != 1) return (64); // or something like that
	return (do_dumpjpcategory());
}
int command_submit (int a , int b, int argc, char **argv)
{
	
	if (argc < 2) return (64); // or something like that
	return (do_submit(argv[2], 0));
}

int command_runstats (int a, int b, int argc , char **argv)
{
	if (argc < 2) return (0x75); // or something like that
	return (do_runstats (argv[2]));


}; // command_runstats


int command_print_cache (int a, int b, int argc , char **argv)
{
	
	return (do_print_cache());


}; // command_print_cache

int command_print_disabled (int a, int b, int argc , char **argv)
{
	
	int Uid = 0; // or 501, or what not..
	char *u = getenv ("uid");
	if (u) sscanf (u, "%d", &Uid);

	return (do_print_disabled (0,Uid));


}; // command_print_disabled

int command_print (int a, int b, int argc , char **argv)
{
	char *service = NULL;

	if (argc >= 2) { service = argv[2]; }

	int Uid = 0;
	char *u = getenv ("uid");
	if (u) sscanf (u, "%d", &Uid);
	return (do_print (service,Uid));


}; // command_print

int command_blame (int a, int b, int argc , char **argv)
{
	if (argc < 2) { usage("print"); return (64);}

	int Uid = 0;
	return (do_blame (argv[2],Uid));


}; // command_blame

int command_kill (int a, int b, int argc , char **argv)
{
	// Kind of redundant when you need uid 0 to kill others
	// But this might hint of a future entitlement for killing..
	if (argc != 3) { usage("kill"); return (64);}

	return (do_kill (argv[2], argv[3]));


}; // command_print


int command_procinfo (int a, int b, int argc , char **argv)
{
	if (argc < 2) {usage ("procinfo"); return (64);}
	return (do_procinfo (strtol (argv[2],0 ,0)));


}; // command_procinfo
int command_hostinfo (int a, int b, int argc , char **argv)
{
	// Just do everything here:


struct commpage {

// Uncovered by disassembling launchctl in the 10.11 version. Look for "comm page"
// and you'll see the address is loaded into %r13, with clear offsets 
// The printf format specifiers yield the exact field sizes (as do the diffs in offsets)

#pragma pack(1)
	unsigned char 	signature[16];
	uint64_t	cpu_capabilities_64;  /* 0x10 */
	uint32_t	unused; 	      /* 0x18 */
	uint16_t	unusedToo;	      /* 0x1c */
	unsigned short	version;              /* 0x1e */
	uint32_t	cpu_capabilities_32; /* 0x20 */
	uint16_t	locks_related;        /* 0x24 */
	uint16_t	cache_line_size;     /* 0x26 */
	uint32_t	scheduler_generation;/* 0x28 */
	uint32_t	memory_pressure;        /* 0x2c */
	uint32_t	mutex_max_spin_count;/* 0x30 */

	uint8_t		active_cpu_count;	/* 0x34*/
	uint8_t		physical_cpu_count;
	uint8_t		logical_cpu_count; 
	uint8_t		unusedAsWell;	     /* 0x37*/
	uint64_t	page_memory_size;    /* 0x38 */
#ifdef ARM
	uint64_t	timeofday_timebase;
	uint32_t	timeofday_timestamp_sec;
	uint32_t	timeofday_timestamp_usec;
	uint32_t	timeofday_ticks_per_sec;
	uint32_t	timeofday_ticks_per_usec;  /* 0x54 */
	uint32_t	timeofday_magic;           /* 0x58 */
	uint32_t	whateverThree;             /* 0x5c */
	uint32_t	timebase_add;              /* 0x60 */
	uint32_t	timebase_shift;            /* 0x64 */
	uint64_t	whateverSomeMore[3];       /* 0x68-0x80 */
#endif
	uint32_t	cpu_family;		   /* 0x80 or 0x40 */
#ifdef ARM
	uint32_t	firmware_debug_flags;	   /* 0x84 */
	uint64_t	timebase_offset;	   /* 0x88 */
	uint64_t	user_timebase;		   /* 0x90 */
#else
	uint32_t	kdebug_enable;                      /* 0x44 */
	uint32_t	atm_diagnostic_config;     /* 0x48 */
	uint32_t	unusedAgain;			/* 0x4c */
	uint64_t	nanotime_base;		   /* 50 */
	uint32_t	nanotime_scale;		   /* 58. */
	uint32_t	nanotime_shift;		   /* ... */

#endif
	// etc.. etc
#pragma pack()

};

#ifdef ARM
// for ARM64

//#define COMMPAGE_ADDRESS	0xffffff80001fc000

#define COMMPAGE_ADDRESS	0xfffffff0001fc000ULL

#else
#define COMMPAGE_ADDRESS	0x7FFFFFE00000ULL
#endif



#define CSR_ALLOW_UNTRUSTED_KEXTS               (1 << 0)
#define CSR_ALLOW_UNRESTRICTED_FS               (1 << 1)
#define CSR_ALLOW_TASK_FOR_PID                  (1 << 2)
#define CSR_ALLOW_KERNEL_DEBUGGER               (1 << 3)
#define CSR_ALLOW_APPLE_INTERNAL                (1 << 4)
#define CSR_ALLOW_DESTRUCTIVE_DTRACE    (1 << 5) /* name deprecated */
#define CSR_ALLOW_UNRESTRICTED_DTRACE   (1 << 5)
#define CSR_ALLOW_UNRESTRICTED_NVRAM    (1 << 6)
#define CSR_ALLOW_DEVICE_CONFIGURATION  (1 << 7)
#define CSR_ALLOW_ANY_RECOVERY_OS       (1 << 8)
#define CSR_ALLOW_UNAPPROVED_KEXTS      (1 << 9)


	printf("allows untrusted kernel extensions = %d\n", (csr_check(CSR_ALLOW_UNTRUSTED_KEXTS) == 0 ? 1 :0));
	printf("allows unrestricted filesystem access = %d\n", (csr_check(CSR_ALLOW_UNRESTRICTED_FS ) == 0 ? 1 : 0));
	printf("allows task_for_pid = %d\n", (csr_check(CSR_ALLOW_TASK_FOR_PID) == 0 ? 1 : 0));
	printf("allows kernel debugging = %d\n", (csr_check(CSR_ALLOW_KERNEL_DEBUGGER) == 0 ? 1 : 0));
	printf("allows apple-internal = %d\n", (csr_check(CSR_ALLOW_APPLE_INTERNAL  ) == 0 ? 1 : 0));
	printf("allows unrestricted dtrace = %d\n", (csr_check(CSR_ALLOW_UNRESTRICTED_DTRACE) == 0 ? 1 : 0));
	printf("allows device configuration = %d\n", (csr_check(CSR_ALLOW_DEVICE_CONFIGURATION ) == 0 ? 1 : 0));
	printf("allows any recovery os = %d\n", (csr_check(CSR_ALLOW_ANY_RECOVERY_OS) == 0 ? 1 : 0));
	printf("allows unapproved kexts = %d\n", (csr_check(CSR_ALLOW_UNAPPROVED_KEXTS) == 0 ? 1 : 0));

	// Do 10.11 and iOS 9 comm page 
	printf("comm page = { (Address: 0x%llx)\n", COMMPAGE_ADDRESS);

	struct commpage *cp = (struct commpage *) COMMPAGE_ADDRESS;
	int sig = 0;

	// First print Cupertino style
	printf("\tsignature = 0x");

	for (sig = 0; sig < 16; sig++)
	{
		printf("%2.2x", cp->signature[sig]);
	}


	// AAPL: Wouldn't it make more sense to print the commpage the same as it is in XNU?
  	//       Your launchctl in 10.11 prints it as a huge hex blob. Sucks.

	printf(" (%15s)\n\tsignature len = 16\n",cp->signature);

	printf("\tcpu capabilities64 = %llx\n", cp->cpu_capabilities_64);
	printf("\tversion = %hu\n", (cp->version));
	printf("\tbuilt version = %d\n", 0xd);
	printf("\tcpu capabilities = %x\n", cp->cpu_capabilities_32);
	// ncpu we obtain from capabilities
	printf("\tncpu = %x\n", (cp->cpu_capabilities_32 & 0x00ff0000) >> 16);
	printf("\tcache line size = %hu\n", cp->cache_line_size);
	printf("\tscheduler generation = %u\n", cp->scheduler_generation);
	printf("\tmutex max spin count = %u\n", cp->mutex_max_spin_count);
	printf("\tactive cpu count = %hhu\n", cp->active_cpu_count);
	printf("\tphysical cpu count = %hhu\n", cp->physical_cpu_count);
	printf("\tlogical cpu count = %hhu\n", cp->logical_cpu_count);
	printf("\tpage memory size = %lld\n", cp->page_memory_size);
#ifdef ARM
	printf ("\ttimeofday = {\t\ttimebase = %lld\n", cp->timeofday_timebase);
	printf("\t\ttimestap (sec) = %d\n", cp->timeofday_timestamp_sec);
	printf("\t\ttimestap (usec) = %d\n", cp->timeofday_timestamp_usec);
	printf("\t\tticks per sec = %d\n", cp->timeofday_ticks_per_sec);
	printf("\t\tticks per usec = %d\n", cp->timeofday_ticks_per_usec);
	printf("\t\tmagic = 0x%x\n", cp->timeofday_magic);
	printf("\t\ttimebase_add = %d\n", cp->timebase_add);
	printf("\t\ttimebase_shift = %d\n", cp->timebase_shift);


#if 0
	// These are apparently just empty, so leave out
	printf("\t\tWhatever: 0x%llx 0x%llx 0x%llx\n",
		cp->whateverSomeMore[0],
		cp->whateverSomeMore[1],
		cp->whateverSomeMore[2]);
#endif 
	printf ("\t}");
		
	printf("\tcpu family = 0x%x\n", cp->cpu_family);
	printf("\tfirmware debug flags = 0x%x (0x%llx)\n", cp->firmware_debug_flags, &(cp->firmware_debug_flags));
	printf("\ttimebase offset = 0x%llx\n", cp->timebase_offset);
	printf("\tuser timebase = 0x%llx\n", cp->user_timebase);
	
#else
	printf("\tcpu family = 0x%x\n", cp->cpu_family);
	printf("\tnanotime base = 0x%llx\n", cp->nanotime_base);
	printf("\tnanotime scale = 0x%x\n", cp->nanotime_scale);
	printf("\tnanotime shift = 0x%x\n", cp->nanotime_shift);

#endif
#ifndef ARM
	printf("\tkdebug ennable: 0x%x\n", cp->kdebug_enable);
	printf("\tatm diagnostic config: 0x%x\n", cp->atm_diagnostic_config);
#endif

		//offsetof(struct commpage, is_development_build));

	printf("\n}\n");
	
	return (0);


}; // command_hostinfo


int command_version (int a, int b, int argc , char **argv)
{
	return (do_version ());


}; // command_version

int command_variant (int a, int b, int argc , char **argv)
{
	return (do_variant ());


}; // command_variant

int vproc_swap_integer(mach_port_t vp, int /* vproc_gsk_t */ key, int64_t *inval, int64_t *outval);;
int  vproc_swap_string(mach_port_t vp,  int /* vproc_gsk_t */ key, const char *instr, char **outstr);
int vproc_swap_complex(mach_port_t  vp, int /*vproc_gsk_t */ key, launch_data_t inval, launch_data_t *outval);

int command_vproc_test (int a, int b, int argc , char **argv)
{

	uint64_t pid = 0;
	int i = 0; 
	char *val = NULL;
	for ( i = 0 ; i < 27; i++) {
		pid = 0;
		int rc = vproc_swap_integer (NULL, i, NULL, &pid);
		printf("%d: %d (RC: %d %s)\n", i, pid, rc, strerror(rc));

		rc = vproc_swap_complex (NULL, i, NULL, &val);
		printf("HERE RC: %d \n", rc);
		val = NULL;

		
	}
	return 0;

}

int command_managername (int a, int b, int argc, char **argv)
{
	char *name = NULL;

	int rc = vproc_swap_string (NULL, 6, NULL, &name);
	if (rc)
	{
		fprintf(stderr, "Could not get manager name.\n");	
		exit (rc);
	}
	printf ("%s\n", name);
	

	return 0;

} // managername
int command_managerpid (int a, int b, int argc, char **argv)
{
	uint64_t pid = 0;
	int rc = vproc_swap_integer (NULL, 4, NULL, &pid);
	if (rc)
	{
		fprintf(stderr, "Could not get manager PID.\n");	
		exit (rc);
	}
	printf ("%d\n", pid);
	

	return 0;

} // managerpid

int command_manageruid (int a, int b, int argc, char **argv)
{
	uint64_t pid = 0;
	int rc = vproc_swap_integer (NULL, 3, NULL, &pid);
	if (rc)
	{
		fprintf(stderr, "Could not get manager PID.\n");	
		exit (rc);
	}
	printf ("%d\n", pid);
	

	return 0;

} // manageruid

int command_error (int a, int b, int argc , char **argv)
{
   
   if (argc < 2) {usage ("error"); return (64);}
 
   if (strcmp(argv[2],"posix") == 0) {

 	}
   if (strcmp(argv[2],"mach") == 0) {

 	}
   if (strcmp(argv[2],"bootstrap") == 0) {

 	}
   
   int err = strtol (argv[2], 0,0);

   printf ("%d: %s\n",err,  xpc_strerror (err));
   return (0);

}; // command_error

int command_help (int a, int b, int argc, char **argv)
{
	int cmd = 0;
	if (argc < 2)
	{
	   printf("Usage: %s <subcommand> ... | help [subcommand]\n", getprogname());

	   for (cmd = 0;
		command_table[cmd].command;
		cmd++)
		{
		
		   printf("\t%-16s%s\n",
			   command_table[cmd].command, 
			        command_table[cmd].shortHelp);

		}
	int bitness = 32;
#ifdef _64
	bitness = 64;
#endif

	   printf("\nThis is the %d-bit version of %s\n", bitness, getprogname());

	}

	else
	{
	  usage (argv[2]);

	}

	  return (0);
}


struct command command_table[] =
{
	
	{ "bootstrap", "Bootstraps a domain or a service into a domain.", "<a.plist>", command_bootstrap },
	{ "bootout", "Tears down a domain or removes a service from a domain.", "<a.plist>", command_bootout },
	{ "enable", "Enables an existing service.", "<service-target>", command_enable},
	{ "disable", "Disables an existing service.", "<service-target>", command_disable},
	 { "debug", "Configures the next invocation of a service for debugging.", "<service-target> (no arguments, yet)", command_debug },
	{ "kill", "Sends a signal to the service instance.", "<signal-number> <service_target>" , command_kill },
	{ "blame", "Prints the reason a service is running." ,"<service-target> (no need for system/)", command_blame },
	{ "print", "Prints a description of a domain or service.", "<target>", command_print },
	{ "print-cache", "Prints information about the service cache.", "", command_print_cache },
	{ "print-disabled", "Prints which services are disabled.", "<target>\n", command_print_disabled },
	{ "procinfo", "Prints port information about a process.", "<pid>" , command_procinfo } ,
	{ "hostinfo", "Prints port information about the host.", "", command_hostinfo },
	{ "limit", "Reads or modifies launchd's resource limits.", "[<limit-name> [<both-limits> | <soft-limit> <hard-limit>]", command_limit},
	{ "runstats", "Prints performance statistics for a service.", "<service-target>", command_runstats },
	{ "examine", "Runs the specified analysis tool against launchd in a non-reentrant manner.", "[<tool> [arg0, arg1, ... , @PID, ...]]\nWith no arguments, causes launchd to fork(2) itself for examination by\nsubsequent analysis tools and prints the PID of this instance to stdout. You\nare responsible for killing this instance.\nAlternatively, the arguments to this subcommand consist of an invocation of a\ntool with which to examine launchd, with the argument for the PID or process\nname replaced with the \"@PID\" argument. So to examine launchd for leaks, the\ninvocation would be:\n$ launchctl examine leaks @PID\n\n\nNote this won't work on RELEASE launchd. If someone has a DEVELOPMENT build, J is extremely interested - let him know :-)" , command_examine }, 
	{ "dumpstate", "Dumps launchd state to stdout.",  "", command_dumpstate },

//#ifdef ARM // no longer iOS only as of MacOS 12 or 13?
	{ "dumpjpcategory", "Dumps the jetsam properties category for all services.", "", command_dumpjpcategory },

//#endif

  	{ "load", "Bootstraps a service or directory of services.", "<a.plist>" , command_load } ,
  	{ "unload", "Unloads a service or directory of services." , "<a.plist>", command_unload} ,
  	{ "list", "Lists information about services.", "[service-name]", command_list} ,
	{ "start", "Starts the specified service.","<service-name>", command_start} ,
	{ "stop", "Stops the specified service if it is running.","<service-name>", command_stop} ,
	{ "setenv" , "Sets the specified environment variables for all services within the domain.", "<key> <value>", command_setenv },
	{ "unsetenv" , "Unsets the specified environment variables for all services within the domain.", "<key>", command_unsetenv },
	{ "getenv", "Gets the value of an environment variable from within launchd.", "<key>", command_getenv },

#ifndef ARM
	{ "asuser", "Execute a program in the bootstrap context of a given user.", "<uid> <program> [...]", command_asuser },
#endif

	{ "submit", "Submit a basic job from the command line.", ".....", command_submit },
	{ "managerpid", "Prints the PID of the launchd controlling the session.",  "", command_managerpid },
	{ "manageruid", "Prints the UID of the current launchd session.",  "", command_manageruid },
	{ "managername", "Prints the name of the current launchd session.",  "", command_managername },

	{ "error", "Prints a description of an error.", "[posix|mach|bootstrap] <code>" , command_error },
  	{ "variant", "Prints the launchd variant.", "" ,command_variant},
  	{ "version", "Prints the launchd version.", "" ,command_version},
	{ "help", "Prints the usage for a given subcommand.", "<subcommand>", command_help },

	//{ "wholaunched", "Prints the bootstrap information for a launched service", "<pid>", command_wholaunched },
	
	// { "attrs",  "Get attrs for a service", "<label>", command_attrs },

	

#ifdef ARM 
  	{ "lookup", "Lookup Mach/XPC endpoint by name (new command, for iOS).", "[service-name]", command_lookup } ,
#endif
	{ "status", "New: checks service status", "[service-name]", command_status },
	{ NULL, NULL, NULL},


} ;

void usage (char *Command)
{
	int cmd = 0;
	while (command_table[cmd].command)
	{
		if (strcmp(command_table[cmd].command, Command) == 0)
			{
				fprintf(stderr,"Usage: jlctl %s %s\n",
				command_table[cmd].command,command_table[cmd].longHelp);
				return;
			}
		cmd++;
	}

	// If we can't find the command..
	exit(64);
}




int main (int argc , char **argv)
{

   if (argc < 2) { (void) command_help (0, 0, argc, argv); exit(1);}

   // @TODO: atoi so that JDEBUG=0 actually disables debug in debug build..

   if (getenv("JDEBUG")) jdebug = 1;

   int cmd = 0;

   for (cmd = 0;
	command_table[cmd].command;
	cmd++)
	{
		if (strcmp (command_table[cmd].command, argv[1]) == 0)
			return (command_table[cmd].funcPtr (0 , 0, argc -1, argv++));

	}


   // If we're here, do what launchctl does - dump help
   (void) command_help (0, 0, 0, argv); 
	exit(1);
#ifdef JDEBUG
     

	extern xpc_object_t xpc_coalition_copy_info(int cid);

	xpc_object_t out = xpc_coalition_copy_info(atoi(argv[1]));

	if (jdebug && out) { fprintf(stderr, "%s", xpc_copy_description(out));}
	exit(1);
#endif


   exit(1);

} // end main


#if 0


 // Use xpc_strerror() to get the error descriptions, such as :
 // 22 - invalid arg
 //  33 - Numerical argument out of domain (e.g. routine 1)
 //  37 - Operation already in progress
 //  45 - Operation not supported
 //  144 - Requestor lacks required entitlement
 // 142 - Operation only supported on development builds


	// Bonus - spawn_via_launchd 
	char *session = strdup ("[0x0-0x43043].com.apple.Preview");
	char **args = (char**) calloc(sizeof(char *),3);
	args[0] = strdup (argv[1]);
	args[1] = strdup(argv[2]);

  printf ("SPAWN via launchd:\n")
extern int _spawn_via_launchd (char *Label, char **args, void *whatever, int vers);
	int r = _spawn_via_launchd(session, args, NULL, 3);
	printf("R: %d\n",r );


#endif

