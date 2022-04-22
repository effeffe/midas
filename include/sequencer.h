// Definition of sequencer structure in ODB

#define SEQ_NEST_LEVEL_LOOP 10
#define SEQ_NEST_LEVEL_IF   10
#define SEQ_NEST_LEVEL_SUB  4

typedef struct {
   BOOL  new_file;
   char  path[256];
   char  filename[256];
   char  error[256];
   int   error_line;
   int   serror_line;
   char  message[256];
   BOOL  message_wait;
   BOOL  running;
   BOOL  finished;
   BOOL  paused;
   BOOL  debug;
   int   current_line_number;
   int   scurrent_line_number;
   BOOL  stop_after_run;
   BOOL  transition_request;
   int   loop_start_line[SEQ_NEST_LEVEL_LOOP];
   int   sloop_start_line[SEQ_NEST_LEVEL_LOOP];
   int   loop_end_line[SEQ_NEST_LEVEL_LOOP];
   int   sloop_end_line[SEQ_NEST_LEVEL_LOOP];
   int   loop_counter[SEQ_NEST_LEVEL_LOOP];
   int   loop_n[SEQ_NEST_LEVEL_LOOP];
   char  subdir[256];
   int   subdir_end_line;
   int   subdir_not_notify;
   int   if_index;
   int   if_line[SEQ_NEST_LEVEL_IF];
   int   if_else_line[SEQ_NEST_LEVEL_IF];
   int   if_endif_line[SEQ_NEST_LEVEL_IF];
   int   stack_index;
   int   subroutine_end_line[SEQ_NEST_LEVEL_SUB];
   int   subroutine_return_line[SEQ_NEST_LEVEL_SUB];
   int   subroutine_call_line[SEQ_NEST_LEVEL_SUB];
   int   ssubroutine_call_line[SEQ_NEST_LEVEL_SUB];
   char  subroutine_param[SEQ_NEST_LEVEL_SUB][256];
   float wait_value;
   float wait_limit;
   DWORD start_time;
   char  wait_type[32];
   char  wait_odb[256];
   char  last_msg[10];
} SEQUENCER;

#define SEQUENCER_STR(_name) const char *_name[] = {\
"[.]",\
"New File = BOOL : n",\
"Path = STRING : [256] ",\
"Filename = STRING : [256] ",\
"Error = STRING : [256] ",\
"Error line = INT : 0",\
"SError line = INT : 0",\
"Message = STRING : [256] ",\
"Message Wait = BOOL : n",\
"Running = BOOL : n",\
"Finished = BOOL : y",\
"Paused = BOOL : n",\
"Debug = BOOL : n",\
"Current line number = INT : 0",\
"SCurrent line number = INT : 0",\
"Stop after run = BOOL : n",\
"Transition request = BOOL : n",\
"Loop start line = INT[10] :",\
"[0] 0",\
"[1] 0",\
"[2] 0",\
"[3] 0",\
"[4] 0",\
"[5] 0",\
"[6] 0",\
"[7] 0",\
"[8] 0",\
"[9] 0",\
"SLoop start line = INT[10] :",\
"[0] 0",\
"[1] 0",\
"[2] 0",\
"[3] 0",\
"[4] 0",\
"[5] 0",\
"[6] 0",\
"[7] 0",\
"[8] 0",\
"[9] 0",\
"Loop end line = INT[10] :",\
"[0] 0",\
"[1] 0",\
"[2] 0",\
"[3] 0",\
"[4] 0",\
"[5] 0",\
"[6] 0",\
"[7] 0",\
"[8] 0",\
"[9] 0",\
"SLoop end line = INT[10] :",\
"[0] 0",\
"[1] 0",\
"[2] 0",\
"[3] 0",\
"[4] 0",\
"[5] 0",\
"[6] 0",\
"[7] 0",\
"[8] 0",\
"[9] 0",\
"Loop counter = INT[10] :",\
"[0] 0",\
"[1] 0",\
"[2] 0",\
"[3] 0",\
"[4] 0",\
"[5] 0",\
"[6] 0",\
"[7] 0",\
"[8] 0",\
"[9] 0",\
"Loop n = INT[10] :",\
"[0] 0",\
"[1] 0",\
"[2] 0",\
"[3] 0",\
"[4] 0",\
"[5] 0",\
"[6] 0",\
"[7] 0",\
"[8] 0",\
"[9] 0",\
"Subdir = STRING : [256] ",\
"Subdir end line = INT : 0",\
"Subdir not notify = INT : 0",\
"If index = INT : 0",\
"If line = INT[10] :",\
"[0] 0",\
"[1] 0",\
"[2] 0",\
"[3] 0",\
"[4] 0",\
"[5] 0",\
"[6] 0",\
"[7] 0",\
"[8] 0",\
"[9] 0",\
"If else line = INT[10] :",\
"[0] 0",\
"[1] 0",\
"[2] 0",\
"[3] 0",\
"[4] 0",\
"[5] 0",\
"[6] 0",\
"[7] 0",\
"[8] 0",\
"[9] 0",\
"If endif line = INT[10] :",\
"[0] 0",\
"[1] 0",\
"[2] 0",\
"[3] 0",\
"[4] 0",\
"[5] 0",\
"[6] 0",\
"[7] 0",\
"[8] 0",\
"[9] 0",\
"Stack index = INT : 0",\
"Subroutine end line = INT[4] :",\
"[0] 0",\
"[1] 0",\
"[2] 0",\
"[3] 0",\
"Subroutine return line = INT[4] :",\
"[0] 0",\
"[1] 0",\
"[2] 0",\
"[3] 0",\
"Subroutine call line = INT[4] :",\
"[0] 0",\
"[1] 0",\
"[2] 0",\
"[3] 0",\
"SSubroutine call line = INT[4] :",\
"[0] 0",\
"[1] 0",\
"[2] 0",\
"[3] 0",\
"Subroutine param = STRING[4] : ",\
"[256] ",\
"[256] ",\
"[256] ",\
"[256] ",\
"Wait value = FLOAT : 0",\
"Wait limit = FLOAT : 0",\
"Start time = DWORD : 0",\
"Wait type = STRING : [32] ",\
"Wait ODB = STRING : [256] ",\
"Last msg = STRING : [10] 00:00:00",\
"",\
NULL }
