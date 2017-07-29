/********************************************************************\

  Name:         sequencer.cxx
  Created by:   Stefan Ritt

  Contents:     MIDAS sequencer functions

  $Id: alarm.c 5212 2011-10-10 15:55:23Z ritt $

\********************************************************************/

#include "midas.h"
#include "msystem.h"
#include "strlcpy.h"
#include "mxml.h"
#include <assert.h>

#define XNAME_LENGTH 256

/**dox***************************************************************/
/** @file sequencer.cxx
The Midas Sequencer file
*/

/** @defgroup alfunctioncode Midas Sequencer Functions
 */

/**dox***************************************************************/
/** @addtogroup seqfunctioncode
 *
 *  @{  */

/*------------------------------------------------------------------*/

typedef struct {
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
   int   current_line_number;
   int   scurrent_line_number;
   BOOL  stop_after_run;
   BOOL  transition_request;
   int   loop_start_line[4];
   int   sloop_start_line[4];
   int   loop_end_line[4];
   int   sloop_end_line[4];
   int   loop_counter[4];
   int   loop_n[4];
   char  subdir[256];
   int   subdir_end_line;
   int   subdir_not_notify;
   int   if_index;
   int   if_line[4];
   int   if_else_line[4];
   int   if_endif_line[4];
   int   stack_index;
   int   subroutine_end_line[4];
   int   subroutine_return_line[4];
   int   subroutine_call_line[4];
   int   ssubroutine_call_line[4];
   char  subroutine_param[4][256];
   float wait_value;
   float wait_limit;
   DWORD start_time;
   char  wait_type[32];
   char  last_msg[10];
} SEQUENCER;

#define SEQUENCER_STR(_name) const char *_name[] = {\
"[.]",\
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
"Current line number = INT : 0",\
"SCurrent line number = INT : 0",\
"Stop after run = BOOL : n",\
"Transition request = BOOL : n",\
"Loop start line = INT[4] :",\
"[0] 0",\
"[1] 0",\
"[2] 0",\
"[3] 0",\
"SLoop start line = INT[4] :",\
"[0] 0",\
"[1] 0",\
"[2] 0",\
"[3] 0",\
"Loop end line = INT[4] :",\
"[0] 0",\
"[1] 0",\
"[2] 0",\
"[3] 0",\
"SLoop end line = INT[4] :",\
"[0] 0",\
"[1] 0",\
"[2] 0",\
"[3] 0",\
"Loop counter = INT[4] :",\
"[0] 0",\
"[1] 0",\
"[2] 0",\
"[3] 0",\
"Loop n = INT[4] :",\
"[0] 0",\
"[1] 0",\
"[2] 0",\
"[3] 0",\
"Subdir = STRING : [256] ",\
"Subdir end line = INT : 0",\
"Subdir not notify = INT : 0",\
"If index = INT : 0",\
"If line = INT[4] :",\
"[0] 0",\
"[1] 0",\
"[2] 0",\
"[3] 0",\
"If else line = INT[4] :",\
"[0] 0",\
"[1] 0",\
"[2] 0",\
"[3] 0",\
"If endif line = INT[4] :",\
"[0] 0",\
"[1] 0",\
"[2] 0",\
"[3] 0",\
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
"Last msg = STRING : [10] 00:00:00",\
"",\
NULL }

SEQUENCER_STR(sequencer_str);
SEQUENCER seq;
PMXML_NODE pnseq = NULL;


/*------------------------------------------------------------------*/

void seq_error(const char *str)
{
   HNDLE hDB, hKey;
   
   strlcpy(seq.error, str, sizeof(seq.error));
   seq.error_line = seq.current_line_number;
   seq.serror_line = seq.scurrent_line_number;
   seq.running = FALSE;
   seq.transition_request = FALSE;

   cm_get_experiment_database(&hDB, NULL);
   db_find_key(hDB, 0, "/Sequencer/State", &hKey);
   assert(hKey);
   db_set_record(hDB, hKey, &seq, sizeof(seq), 0);
   
   cm_msg(MTALK, "sequencer", "Sequencer has stopped with error.");
}

/*------------------------------------------------------------------*/

int strbreak(char *str, char list[][XNAME_LENGTH], int size, const char *brk, BOOL ignore_quotes)
/* break comma-separated list into char array, stripping leading
 and trailing blanks */
{
   int i, j;
   char *p;
   
   memset(list, 0, size * XNAME_LENGTH);
   p = str;
   if (!p || !*p)
      return 0;
   
   while (*p == ' ')
      p++;
   
   for (i = 0; *p && i < size; i++) {
      if (*p == '"' && !ignore_quotes) {
         p++;
         j = 0;
         memset(list[i], 0, XNAME_LENGTH);
         do {
            /* convert two '"' to one */
            if (*p == '"' && *(p + 1) == '"') {
               list[i][j++] = '"';
               p += 2;
            } else if (*p == '"') {
               break;
            } else
               list[i][j++] = *p++;
            
         } while (j < XNAME_LENGTH - 1);
         list[i][j] = 0;
         
         /* skip second '"' */
         p++;
         
         /* skip blanks and break character */
         while (*p == ' ')
            p++;
         if (*p && strchr(brk, *p))
            p++;
         while (*p == ' ')
            p++;
         
      } else {
         strlcpy(list[i], p, XNAME_LENGTH);
         
         for (j = 0; j < (int) strlen(list[i]); j++)
            if (strchr(brk, list[i][j])) {
               list[i][j] = 0;
               break;
            }
         
         p += strlen(list[i]);
         while (*p == ' ')
            p++;
         if (*p && strchr(brk, *p))
            p++;
         while (*p == ' ')
            p++;
         
         while (list[i][strlen(list[i]) - 1] == ' ')
            list[i][strlen(list[i]) - 1] = 0;
      }
      
      if (!*p)
         break;
   }
   
   if (i == size)
      return size;
   
   return i + 1;
}

/*------------------------------------------------------------------*/

extern char *stristr(const char *str, const char *pattern);

void strsubst(char *string, int size, const char *pattern, const char *subst)
/* subsitute "pattern" with "subst" in "string" */
{
   char *tail, *p;
   int s;
   
   p = string;
   for (p = stristr(p, pattern); p != NULL; p = stristr(p, pattern)) {
      
      if (strlen(pattern) == strlen(subst)) {
         memcpy(p, subst, strlen(subst));
      } else if (strlen(pattern) > strlen(subst)) {
         memcpy(p, subst, strlen(subst));
         memmove(p + strlen(subst), p + strlen(pattern), strlen(p + strlen(pattern)) + 1);
      } else {
         tail = (char *) malloc(strlen(p) - strlen(pattern) + 1);
         strcpy(tail, p + strlen(pattern));
         s = size - (p - string);
         strlcpy(p, subst, s);
         strlcat(p, tail, s);
         free(tail);
         tail = NULL;
      }
      
      p += strlen(subst);
   }
}

/*------------------------------------------------------------------*/

BOOL msl_parse(char *filename, char *error, int error_size, int *error_line)
{
   char str[256], *buf, *pl, *pe;
   char list[100][XNAME_LENGTH], list2[100][XNAME_LENGTH], **lines;
   int i, j, n, size, n_lines, endl, line, fhin, nest, incl, library;
   FILE *fout = NULL;
   
   fhin = open(filename, O_RDONLY | O_TEXT);
   if (strchr(filename, '.')) {
      strlcpy(str, filename, sizeof(str));
      *strchr(str, '.') = 0;
      strlcat(str, ".xml", sizeof(str));
      fout = fopen(str, "wt");
   }
   if (fhin > 0 && fout) {
      size = (int)lseek(fhin, 0, SEEK_END);
      lseek(fhin, 0, SEEK_SET);
      buf = (char *)malloc(size+1);
      size = (int)read(fhin, buf, size);
      buf[size] = 0;
      close(fhin);

      /* look for any includes */
      lines = (char **)malloc(sizeof(char *));
      incl = 0;
      pl = buf;
      library = FALSE;
      for (n_lines=0 ; *pl ; n_lines++) {
         lines = (char **)realloc(lines, sizeof(char *)*n_lines+1);
         lines[n_lines] = pl;
         if (strchr(pl, '\n')) {
            pe = strchr(pl, '\n');
            *pe = 0;
            if (*(pe-1) == '\r') {
               *(pe-1) = 0;
            }
            pe++;
         } else
            pe = pl+strlen(pl);
         strlcpy(str, pl, sizeof(str));
         pl = pe;
         strbreak(str, list, 100, ", ", FALSE);
         if (equal_ustring(list[0], "include")) {
            if (!incl) {
               fprintf(fout, "<?xml version=\"1.0\" encoding=\"ISO-8859-1\"?>\n");
               fprintf(fout, "<!DOCTYPE RunSequence [\n");
               incl = 1;
            }
            fprintf(fout, "  <!ENTITY %s SYSTEM \"%s.xml\">\n", list[1], list[1]); 
         }
         if (equal_ustring(list[0], "library")) {
            fprintf(fout, "<Library name=\"%s\">\n", list[1]);
            library = TRUE;
         }
      }
      if (incl)
         fprintf(fout, "]>\n");
      else if (!library)
         fprintf(fout, "<?xml version=\"1.0\" encoding=\"ISO-8859-1\"?>\n");
            
      /* parse rest of file */
      if (!library)
         fprintf(fout, "<RunSequence xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" xsi:noNamespaceSchemaLocation=\"\">\n");
      for (line=0 ; line<n_lines ; line++) {
         n = strbreak(lines[line], list, 100, ", ", FALSE);
         
         /* remove any comment */
         for (i=0 ; i<n ; i++) {
            if (list[i][0] == '#') {
               for (j=i ; j<n ; j++)
                  list[j][0] = 0;
               break;
            }
         }
         
         if (equal_ustring(list[0], "library")) {
                     
         } else if (equal_ustring(list[0], "include")) {
               fprintf(fout, "&%s;\n", list[1]); 
            
         } else if (equal_ustring(list[0], "call")) {
            fprintf(fout, "<Call l=\"%d\" name=\"%s\">", line+1, list[1]);
            for (i=2 ; i < 100 && list[i][0] ; i++) {
               if (i > 2)
                  fprintf(fout, ",");
               fprintf(fout, "%s", list[i]);
            }
            fprintf(fout, "</Call>\n");
            
         } else if (equal_ustring(list[0], "cat")) {
            fprintf(fout, "<Cat l=\"%d\" name=\"%s\">", line+1, list[1]);
            for (i=2 ; i < 100 && list[i][0] ; i++) {
               if (i > 2)
                  fprintf(fout, ",");
               fprintf(fout, "\"%s\"", list[i]);
            }
            fprintf(fout, "</Cat>\n");

         } else if (equal_ustring(list[0], "comment")) {
            fprintf(fout, "<Comment l=\"%d\">%s</Comment>\n", line+1, list[1]);
            
         } else if (equal_ustring(list[0], "goto")) {
            fprintf(fout, "<Goto l=\"%d\" sline=\"%s\" />\n", line+1, list[1]);
            
         } else if (equal_ustring(list[0], "if")) {
            fprintf(fout, "<If l=\"%d\" condition=\"", line+1);
            for (i=1 ; i<100 && list[i][0] && stricmp(list[i], "THEN") != 0 ; i++)
               fprintf(fout, "%s", list[i]);
            fprintf(fout, "\">\n");

         } else if (equal_ustring(list[0], "else")) {
            fprintf(fout, "<Else />\n");
            
         } else if (equal_ustring(list[0], "endif")) {
            fprintf(fout, "</If>\n");
            
         } else if (equal_ustring(list[0], "loop")) {
            /* find end of loop */
            for (i=line,nest=0 ; i<n_lines ; i++) {
               strbreak(lines[i], list2, 100, ", ", FALSE);
               if (equal_ustring(list2[0], "loop"))
                  nest++;
               if (equal_ustring(list2[0], "endloop")) {
                  nest--;
                  if (nest == 0)
                     break;
               }
            }
            if (i<n_lines)
               endl = i+1;
            else
               endl = line+1;
            if (list[2][0] == 0)
               fprintf(fout, "<Loop l=\"%d\" le=\"%d\" n=\"%s\">\n", line+1, endl, list[1]);
            else if (list[3][0] == 0){
               fprintf(fout, "<Loop l=\"%d\" le=\"%d\" var=\"%s\" n=\"%s\">\n", line+1, endl, list[1], list[2]);
            } else {
               fprintf(fout, "<Loop l=\"%d\" le=\"%d\" var=\"%s\" values=\"", line+1, endl, list[1]);
               for (i=2 ; i < 100 && list[i][0] ; i++) {
                  if (i > 2)
                     fprintf(fout, ",");
                  fprintf(fout, "%s", list[i]);
               }
               fprintf(fout, "\">\n");
            }
         } else if (equal_ustring(list[0], "endloop")) {
            fprintf(fout, "</Loop>\n");
            
         } else if (equal_ustring(list[0], "message")) {
            fprintf(fout, "<Message l=\"%d\"%s>%s</Message>\n", line+1, 
                    list[2][0] == '1'? " wait=\"1\"" : "", list[1]);

         } else if (equal_ustring(list[0], "odbinc")) {
            if (list[2][0] == 0)
               strlcpy(list[2], "1", 2);
            fprintf(fout, "<ODBInc l=\"%d\" path=\"%s\">%s</ODBInc>\n", line+1, list[1], list[2]);
            
         } else if (equal_ustring(list[0], "odbset")) {
            if (list[3][0])
               fprintf(fout, "<ODBSet l=\"%d\" notify=\"%s\" path=\"%s\">%s</ODBSet>\n", line+1, list[3], list[1], list[2]);
            else
               fprintf(fout, "<ODBSet l=\"%d\" path=\"%s\">%s</ODBSet>\n", line+1, list[1], list[2]);
            
         } else if (equal_ustring(list[0], "odbget")) {
               fprintf(fout, "<ODBGet l=\"%d\" path=\"%s\">%s</ODBGet>\n", line+1, list[1], list[2]);

         } else if (equal_ustring(list[0], "odbsubdir")) {
            if (list[2][0])
               fprintf(fout, "<ODBSubdir l=\"%d\" notify=\"%s\" path=\"%s\">\n", line+1, list[2], list[1]);
            else
               fprintf(fout, "<ODBSubdir l=\"%d\" path=\"%s\">\n", line+1, list[1]);
         } else if (equal_ustring(list[0], "endodbsubdir")) {
            fprintf(fout, "</ODBSubdir>\n");
            
         } else if (equal_ustring(list[0], "param")) {
            if (list[2][0] == 0)
               fprintf(fout, "<Param l=\"%d\" name=\"%s\" />\n", line+1, list[1]);
            else if (!list[3][0] && equal_ustring(list[2], "bool")) {
               fprintf(fout, "<Param l=\"%d\" name=\"%s\" type=\"bool\" />\n", line+1, list[1]);
            } else if (!list[3][0]) {
               fprintf(fout, "<Param l=\"%d\" name=\"%s\" comment=\"%s\" />\n", line+1, list[1], list[2]);
            } else {
               fprintf(fout, "<Param l=\"%d\" name=\"%s\" comment=\"%s\" options=\"", line+1, list[1], list[2]);
               for (i=3 ; i < 100 && list[i][0] ; i++) {
                  if (i > 3)
                     fprintf(fout, ",");
                  fprintf(fout, "%s", list[i]);
               }
               fprintf(fout, "\" />\n");
            }

         } else if (equal_ustring(list[0], "rundescription")) {
            fprintf(fout, "<RunDescription l=\"%d\">%s</RunDescription>\n", line+1, list[1]);
            
         } else if (equal_ustring(list[0], "script")) {
            if (list[2][0] == 0)
               fprintf(fout, "<Script l=\"%d\">%s</Script>\n", line+1, list[1]);
            else {
               fprintf(fout, "<Script l=\"%d\" params=\"", line+1);
               for (i=2 ; i < 100 && list[i][0] ; i++) {
                  if (i > 2)
                     fprintf(fout, ",");
                  fprintf(fout, "%s", list[i]);
               }
               fprintf(fout, "\">%s</Script>\n", list[1]);
            }
            
         } else if (equal_ustring(list[0], "set")) {
            fprintf(fout, "<Set l=\"%d\" name=\"%s\">%s</Set>\n", line+1, list[1], list[2]);

         } else if (equal_ustring(list[0], "subroutine")) {
            fprintf(fout, "\n<Subroutine l=\"%d\" name=\"%s\">\n", line+1, list[1]);
         } else if (equal_ustring(list[0], "endsubroutine")) {
            fprintf(fout, "</Subroutine>\n");

         } else if (equal_ustring(list[0], "transition")) {
            fprintf(fout, "<Transition l=\"%d\">%s</Transition>\n", line+1, list[1]);
            
         } else if (equal_ustring(list[0], "wait")) {
            if (!list[2][0])
               fprintf(fout, "<Wait l=\"%d\" for=\"seconds\">%s</Wait>\n", line+1, list[1]);
            else if (!list[3][0])
               fprintf(fout, "<Wait l=\"%d\" for=\"%s\">%s</Wait>\n", line+1, list[1], list[2]);
            else {
               fprintf(fout, "<Wait l=\"%d\" for=\"%s\" path=\"%s\" op=\"%s\">%s</Wait>\n", 
                       line+1, list[1], list[2], list[3], list[4]);
            }
         
         } else if (list[0][0] == 0 || list[0][0] == '#'){
            /* skip empty or outcommented lines */
         } else {
            sprintf(error, "Invalid command \"%s\"", list[0]);
            *error_line = line + 1;
            return FALSE;
         }
      }

      free(lines);
      free(buf);
      if (library)
         fprintf(fout, "\n</Library>\n");
      else
         fprintf(fout, "</RunSequence>\n");
      fclose(fout);
   } else {
      sprintf(error, "File error on \"%s\"", filename);
      return FALSE;
   }

   return TRUE;
}

/*------------------------------------------------------------------*/

BOOL eval_var(const char *value, char *result, int size)
{
   HNDLE hDB, hkey;
   KEY key;
   const char *p;
   char *s;
   char str[256], data[256];
   int index, i;
   
   cm_get_experiment_database(&hDB, NULL);
   p = value;
   
   while (*p == ' ')
      p++;
   
   if (*p == '$') {
      p++;
      strcpy(str, p);
      if (strchr(str, '('))
         *(strchr(str, '(')+1) = 0;
      if (atoi(p) > 0) {
         index = atoi(p);
         if (seq.stack_index > 0) {
            strlcpy(str, seq.subroutine_param[seq.stack_index-1], sizeof(str));
            s = strtok(str, ",");
            if (s == NULL) {
               if (index == 1) {
                  strlcpy(result, str, size);
                  if (result[0] == '$') {
                     strlcpy(str, result, sizeof(str));
                     return eval_var(str, result, size);
                  }                  
                  return TRUE;
               } else
                  goto error;
            }
            for (i=1; s != NULL && i<index; i++)
               s = strtok(NULL, ",");
            if (s != NULL) {
               strlcpy(result, s, size);
               if (result[0] == '$') {
                  strlcpy(str, result, sizeof(str));
                  return eval_var(str, result, size);
               }                  
               return TRUE;
            }
            else 
               goto error;
         } else
            goto error;
      } else if (equal_ustring(str, "ODB(")) {
         strlcpy(str, p+4, sizeof(str));
         if (strchr(str, ')') == NULL)
            return FALSE;
         *strchr(str, ')') = 0;
         if (str[0] == '"' && str[strlen(str)-1] == '"') {
            memmove(str, str+1, strlen(str));
            str[strlen(str)-1] = 0;
         }
         index = 0;
         if (strchr(str, '[')) {
            index = atoi(strchr(str, '[')+1);
            *strchr(str, '[') = 0;
         }
         if (!db_find_key(hDB, 0, str, &hkey))
            return FALSE;
         db_get_key(hDB, hkey, &key);
         i = sizeof(data);
         db_get_data(hDB, hkey, data, &i, key.type);
         db_sprintf(result, data, sizeof(data), index, key.type);
         return TRUE;
         
      } else {
         sprintf(str, "/Sequencer/Variables/%s", value+1);
         if (db_get_value(hDB, 0, str, result, &size, TID_STRING, FALSE) == DB_SUCCESS)
            return TRUE;
      }
   } else {
      strlcpy(result, value, size);
      return TRUE;
   }

error:
   sprintf(str, "Invalid parameter \"%s\"", value);
   seq_error(str);
   return FALSE;
}

/*------------------------------------------------------------------*/

int concatenate(char *result, int size, char *value)
{
   char str[256], list[100][XNAME_LENGTH];
   int  i, n;
   
   n = strbreak(value, list, 100, ",", FALSE);
   
   result[0] = 0;
   for (i=0 ; i<n ; i++) {
      if (!eval_var(list[i], str, sizeof(str)))
          return FALSE;
      strlcat(result, str, size);
   }
   
   return TRUE;   
}

/*------------------------------------------------------------------*/

int eval_condition(const char *condition)
{
   int i;
   double value1, value2;
   char value1_str[256], value2_str[256], value1_var[256], value2_var[256], str[256], op[3];
   
   strcpy(str, condition);
   op[1] = op[2] = 0;
   value1 = value2 = 0;

   /* find value and operator */
   for (i = 0; i < (int)strlen(str) ; i++)
      if (strchr("<>=!&", str[i]) != NULL)
         break;
   strlcpy(value1_str, str, i+1);
   while (value1_str[strlen(value1_str)-1] == ' ')
      value1_str[strlen(value1_str)-1] = 0;
   op[0] = str[i];
   if (strchr("<>=!&", str[i+1]) != NULL)
      op[1] = str[++i];

   for (i++; str[i] == ' '; i++);
   strlcpy(value2_str, str + i, sizeof(value2_str));
   
   if (!eval_var(value1_str, value1_var, sizeof(value1_var)))
      return -1;
   if (!eval_var(value2_str, value2_var, sizeof(value2_var)))
      return -1;
   for (i=0 ; i<(int)strlen(value1_var) ; i++)
      if (!isdigit(value1_var[i]))
         break;
   if (i < (int)strlen(value1_var)) {
      // string comparison
      if (strcmp(op, "=") == 0)
         if (equal_ustring(value1_var, value2_var)) return 1;
      if (strcmp(op, "==") == 0)
         if (equal_ustring(value1_var, value2_var)) return 1;
      if (strcmp(op, "!=") == 0)
         if (!equal_ustring(value1_var, value2_var)) return 1;
      return -1;
   }
   
   // numberic comparison
   for (i=0 ; i<(int)strlen(value2_var) ; i++)
      if (isdigit(value2_var[i]))
         break;
   if (i == (int)strlen(value2_var))
      return -1;

   value1 = atof(value1_var);
   value2 = atof(value2_var);
   
   /* now do logical operation */
   if (strcmp(op, "=") == 0)
      if (value1 == value2) return 1;
   if (strcmp(op, "==") == 0)
      if (value1 == value2) return 1;
   if (strcmp(op, "!=") == 0)
      if (value1 != value2) return 1;
   if (strcmp(op, "<") == 0)
      if (value1 < value2) return 1;
   if (strcmp(op, ">") == 0)
      if (value1 > value2) return 1;
   if (strcmp(op, "<=") == 0)
      if (value1 <= value2) return 1;
   if (strcmp(op, ">=") == 0)
      if (value1 >= value2) return 1;
   if (strcmp(op, "&") == 0)
      if (((unsigned int)value1 & (unsigned int)value2) > 0) return 1;
   
   return 0;
}

/*------------------------------------------------------------------*/

void seq_start(HNDLE hDB, HNDLE hKey)
{
   /* start sequencer */
   seq.running = TRUE;
   seq.finished = FALSE;
   seq.paused = FALSE;
   seq.transition_request = FALSE;
   seq.wait_limit = 0;
   seq.wait_value = 0;
   seq.start_time = 0;
   seq.wait_type[0] = 0;
   for (int i=0 ; i<4 ; i++) {
      seq.loop_start_line[i] = 0;
      seq.sloop_start_line[i] = 0;
      seq.loop_end_line[i] = 0;
      seq.sloop_end_line[i] = 0;
      seq.loop_counter[i] = 0;
      seq.loop_n[i] = 0;
   }
   for (int i=0 ; i<4 ; i++) {
      seq.if_else_line[i] = 0;
      seq.if_endif_line[i] = 0;
      seq.subroutine_end_line[i] = 0;
      seq.subroutine_return_line[i] = 0;
      seq.subroutine_call_line[i] = 0;
      seq.ssubroutine_call_line[i] = 0;
      seq.subroutine_param[i][0] = 0;
   }
   seq.current_line_number = 1;
   seq.scurrent_line_number = 1;
   seq.if_index = 0;
   seq.stack_index = 0;
   seq.error[0] = 0;
   seq.error_line = 0;
   seq.serror_line = 0;
   seq.subdir[0] = 0;
   seq.subdir_end_line = 0;
   seq.subdir_not_notify = 0;
   db_set_record(hDB, hKey, &seq, sizeof(seq), 0);
}

void seq_set_paused(HNDLE hDB, HNDLE hKey, BOOL paused)
{
   seq.paused = paused;
   db_set_record(hDB, hKey, &seq, sizeof(seq), 0);
}

void seq_set_stop_after_run(HNDLE hDB, HNDLE hKey, BOOL stop_after_run)
{
   seq.stop_after_run = stop_after_run;
   db_set_record(hDB, hKey, &seq, sizeof(seq), 0);
}

void seq_stop(HNDLE hDB, HNDLE hKey)
{
   seq.running = FALSE;
   seq.finished = FALSE;
   seq.paused = FALSE;
   seq.wait_limit = 0;
   seq.wait_value = 0;
   seq.wait_type[0] = 0;
   for (int i=0 ; i<4 ; i++) {
      seq.loop_start_line[i] = 0;
      seq.loop_end_line[i] = 0;
      seq.loop_counter[i] = 0;
      seq.loop_n[i] = 0;
   }
   seq.stop_after_run = FALSE;
   seq.subdir[0] = 0;
   
   /* stop run if not already stopped */
   char str[256];
   int state = 0;
   int size = sizeof(state);
   db_get_value(hDB, 0, "/Runinfo/State", &state, &size, TID_INT, FALSE);
   if (state != STATE_STOPPED)
      cm_transition(TR_STOP, 0, str, sizeof(str), TR_MTHREAD | TR_SYNC, FALSE);
   
   db_set_record(hDB, hKey, &seq, sizeof(seq), 0);
}


void seq_load(HNDLE hDB, HNDLE hKey, const char* filename)
{
   strlcpy(seq.filename, filename, sizeof(seq.filename));

   char str[256];
   strlcpy(str, seq.path, sizeof(str));
   if (strlen(str)>1 && str[strlen(str)-1] != DIR_SEPARATOR)
      strlcat(str, DIR_SEPARATOR_STR, sizeof(str));
   strlcat(str, seq.filename, sizeof(str));
   seq.error[0] = 0;
   seq.error_line = 0;
   seq.serror_line = 0;
   if (pnseq) {
      mxml_free_tree(pnseq);
      pnseq = NULL;
   }
   if (stristr(str, ".msl")) {
      if (msl_parse(str, seq.error, sizeof(seq.error), &seq.serror_line)) {
         strsubst(str, sizeof(str), ".msl", ".xml");
         pnseq = mxml_parse_file(str, seq.error, sizeof(seq.error), &seq.error_line);
      }
   } else
      pnseq = mxml_parse_file(str, seq.error, sizeof(seq.error), &seq.error_line);
   
   seq.finished = FALSE;
   db_set_record(hDB, hKey, &seq, sizeof(seq), 0);
}

void seq_save(HNDLE hDB, HNDLE hKey, char* str, int str_size)
{
   seq.error[0] = 0;
   if (pnseq) {
      mxml_free_tree(pnseq);
      pnseq = NULL;
   }
   seq.error_line = 0;
   seq.serror_line = 0;
   if (stristr(str, ".msl")) {
      if (msl_parse(str, seq.error, sizeof(seq.error), &seq.serror_line)) {
         strsubst(str, str_size, ".msl", ".xml");
         pnseq = mxml_parse_file(str, seq.error, sizeof(seq.error), &seq.error_line);
      }
   } else
      pnseq = mxml_parse_file(str, seq.error, sizeof(seq.error), &seq.error_line);
   
   db_set_record(hDB, hKey, &seq, sizeof(seq), 0);
}

int seq_loop_width(int i)
{
   int width = 0;
   if (seq.loop_n[i] <= 0)
      width = 0;
   else
      width = (int)(((double)seq.loop_counter[i]/seq.loop_n[i])*100+0.5);
   return width;
}

int seq_wait_width()
{
   int width = 0;
   if (seq.wait_value <= 0)
      width = 0;
   else
      width = (int)(((double)seq.wait_value/seq.wait_limit)*100+0.5);
   return width;
}

const char* seq_path() { return seq.path; }
const char* seq_filename() { return seq.filename; }
const char* seq_error() { return seq.error; }
int seq_running() { return seq.running; }
int seq_paused() { return seq.paused; }
int seq_stop_after_run() { return seq.stop_after_run; }
int seq_finished() { return seq.finished; }
int seq_current_line_number() { return seq.current_line_number; }
int seq_error_line() { return seq.error_line; }
int seq_serror_line() { return seq.serror_line; }

/*------------------------------------------------------------------*/

static void seq_watch(HNDLE hDB, HNDLE hKeyChanged, int index, void* info)
{
   int status;
   HNDLE hKey;
   
   cm_get_experiment_database(&hDB, NULL);

   status = db_find_key(hDB, 0, "/Sequencer/State", &hKey);
   if (status != DB_SUCCESS) {
      cm_msg(MERROR, "seq_watch", "Cannot find /Sequencer/State in ODB, db_find_key() status %d", status);
      return;
   }

   int size = sizeof(seq);
   status = db_get_record1(hDB, hKey, &seq, &size, 0, strcomb(sequencer_str));
   if (status != DB_SUCCESS) {
      cm_msg(MERROR, "seq_watch", "Cannot get /Sequencer/State from ODB, db_get_record1() status %d", status);
      return;
   }
}

/*------------------------------------------------------------------*/

void init_sequencer()
{
   int status;
   HNDLE hDB;
   HNDLE hKey;
   char str[256];
   
   cm_get_experiment_database(&hDB, NULL);

   status = db_check_record(hDB, 0, "/Sequencer/State", strcomb(sequencer_str), TRUE);
   if (status == DB_STRUCT_MISMATCH) {
      cm_msg(MERROR, "init_sequencer", "Sequencer error: mismatching /Sequencer/State structure, db_check_record() status %d", status);
      return;
   }

   status = db_find_key(hDB, 0, "/Sequencer/State", &hKey);
   if (status != DB_SUCCESS) {
      cm_msg(MERROR, "init_sequencer", "Sequencer error: Cannot find /Sequencer/State, db_find_key() status %d", status);
      return;
   }

   int size = sizeof(seq);
   status = db_get_record1(hDB, hKey, &seq, &size, 0, strcomb(sequencer_str));
   if (status != DB_SUCCESS) {
      cm_msg(MERROR, "init_sequencer", "Sequencer error: Cannot get /Sequencer/State, db_get_record1() status %d", status);
      return;
   }

   if (seq.path[0] == 0)
      if (!getcwd(seq.path, sizeof(seq.path)))
         seq.path[0] = 0;

   if (strlen(seq.path)>0 && seq.path[strlen(seq.path)-1] != DIR_SEPARATOR) {
      strlcat(seq.path, DIR_SEPARATOR_STR, sizeof(seq.path));
   }
   
   if (seq.filename[0]) {
      strlcpy(str, seq.path, sizeof(str));
      strlcat(str, seq.filename, sizeof(str));
      seq.error[0] = 0;
      seq.error_line = 0;
      seq.serror_line = 0;
      if (pnseq) {
         mxml_free_tree(pnseq);
         pnseq = NULL;
      }
      if (stristr(str, ".msl")) {
         if (msl_parse(str, seq.error, sizeof(seq.error), &seq.serror_line)) {
            strsubst(str, sizeof(str), ".msl", ".xml");
            pnseq = mxml_parse_file(str, seq.error, sizeof(seq.error), &seq.error_line);
         }
      } else
         pnseq = mxml_parse_file(str, seq.error, sizeof(seq.error), &seq.error_line);
   }
   
   seq.transition_request = FALSE;
   
   db_set_record(hDB, hKey, &seq, sizeof(seq), 0);
   
   status = db_watch(hDB, hKey, seq_watch, NULL);
   if (status != DB_SUCCESS) {
      cm_msg(MERROR, "init_sequencer", "Sequencer error: Cannot watch /Sequencer/State, db_watch() status %d", status);
      return;
   }
}

/*------------------------------------------------------------------*/

void sequencer()
{
   PMXML_NODE pn, pr, pt, pe;
   char odbpath[256], value[256], data[256], str[256], str1[256], name[32], op[32];
   char list[100][XNAME_LENGTH], *pc;
   int i, j, l, n, status, size, index, index1, index2, last_line, state, run_number, cont;
   HNDLE hDB, hKey, hKeySeq;
   KEY key;
   double d;
   float v;
   
   if (!seq.running || seq.paused || pnseq == NULL)
      return;
   
   cm_get_experiment_database(&hDB, NULL);
   db_find_key(hDB, 0, "/Sequencer/State", &hKeySeq);
   if (!hKeySeq)
      return;
   
   cm_msg_retrieve(1, str, sizeof(str));
   str[19] = 0;
   strcpy(seq.last_msg, str+11);
   
   pr = mxml_find_node(pnseq, "RunSequence");
   if (!pr) {
      seq_error("Cannot find <RunSequence> tag in XML file");
      return;
   }

   last_line = mxml_get_line_number_end(pr);
   
   /* check for Subroutine end */
   if (seq.stack_index > 0 && seq.current_line_number == seq.subroutine_end_line[seq.stack_index-1]) {
      seq.subroutine_end_line[seq.stack_index-1] = 0;
      seq.current_line_number = seq.subroutine_return_line[seq.stack_index-1];
      seq.stack_index --;
   }

   /* check for last line of script */
   if (seq.current_line_number > last_line) {
      seq.running = FALSE;
      seq.finished = TRUE;
      db_set_record(hDB, hKeySeq, &seq, sizeof(seq), 0);
      cm_msg(MTALK, "sequencer", "Sequencer is finished.");
      return;
   }
   
   /* check for loop end */
   for (i=3 ; i>=0 ; i--)
      if (seq.loop_start_line[i] > 0)
         break;
   if (i >= 0) {
      if (seq.current_line_number == seq.loop_end_line[i]) {
         if (seq.loop_counter[i] == seq.loop_n[i]) {
            seq.loop_counter[i] = 0;
            seq.loop_start_line[i] = 0;
            seq.sloop_start_line[i] = 0;
            seq.loop_end_line[i] = 0;
            seq.sloop_end_line[i] = 0;
            seq.loop_n[i] = 0;
            seq.current_line_number++;
         } else {
            pn = mxml_get_node_at_line(pnseq, seq.loop_start_line[i]);
            if (mxml_get_attribute(pn, "var")) {
               strlcpy(name, mxml_get_attribute(pn, "var"), sizeof(name));
               if (mxml_get_attribute(pn, "values")) {
                  strlcpy(data, mxml_get_attribute(pn, "values"), sizeof(data));
                  strbreak(data, list, 100, ",", FALSE);
                  if (!eval_var(list[seq.loop_counter[i]], value, sizeof(value)))
                     return;
               } else if (mxml_get_attribute(pn, "n")) {
                  sprintf(value, "%d", seq.loop_counter[i]+1);
               }
               sprintf(str, "/Sequencer/Variables/%s", name);
               size = strlen(value)+1;
               if (size < 32)
                  size = 32;
               db_set_value(hDB, 0, str, value, size, 1, TID_STRING);
            }
            seq.loop_counter[i]++;
            seq.current_line_number = seq.loop_start_line[i]+1;
         }
         db_set_record(hDB, hKeySeq, &seq, sizeof(seq), 0);
         return;
      }
   }
   
   /* check for end of "if" statement */
   if (seq.if_index > 0 && seq.current_line_number == seq.if_endif_line[seq.if_index-1]) {
      seq.if_index --;
      seq.if_line[seq.if_index] = 0;
      seq.if_else_line[seq.if_index] = 0;
      seq.if_endif_line[seq.if_index] = 0;
      seq.current_line_number++;
      db_set_record(hDB, hKeySeq, &seq, sizeof(seq), 0);
      return;
   }
   
   /* check for ODBSubdir end */
   if (seq.current_line_number == seq.subdir_end_line) {
      seq.subdir_end_line = 0;
      seq.subdir[0] = 0;
      seq.subdir_not_notify = FALSE;
   }
   
   /* find node belonging to current line */
   pn = mxml_get_node_at_line(pnseq, seq.current_line_number);
   if (!pn) {
      seq.current_line_number++;
      db_set_record(hDB, hKeySeq, &seq, sizeof(seq), 0);
      return;
   }
   
   /* set MSL line from current element */
   if (mxml_get_attribute(pn, "l"))
      seq.scurrent_line_number = atoi(mxml_get_attribute(pn, "l"));
   
   if (equal_ustring(mxml_get_name(pn), "PI") || 
       equal_ustring(mxml_get_name(pn), "RunSequence") ||
       equal_ustring(mxml_get_name(pn), "Comment")) {
      // just skip
      seq.current_line_number++;
   }
   
   /*---- ODBSubdir ----*/
   else if (equal_ustring(mxml_get_name(pn), "ODBSubdir")) {
      if (!mxml_get_attribute(pn, "path")) {
         seq_error("Missing attribute \"path\"");
      } else {
         strlcpy(seq.subdir, mxml_get_attribute(pn, "path"), sizeof(seq.subdir));
         if (mxml_get_attribute(pn, "notify"))
            seq.subdir_not_notify = !atoi(mxml_get_attribute(pn, "notify"));
         seq.subdir_end_line = mxml_get_line_number_end(pn);
         seq.current_line_number++;
      }
   }
   
   /*---- ODBSet ----*/
   else if (equal_ustring(mxml_get_name(pn), "ODBSet")) {
      if (!mxml_get_attribute(pn, "path")) {
         seq_error("Missing attribute \"path\"");
      } else {
         strlcpy(odbpath, seq.subdir, sizeof(odbpath));
         if (strlen(odbpath) > 0 && odbpath[strlen(odbpath)-1] != '/')
            strlcat(odbpath, "/", sizeof(odbpath));
         strlcat(odbpath, mxml_get_attribute(pn, "path"), sizeof(odbpath));
         
         /* check if index is supplied */
         index1 = index2 = 0;
         if (odbpath[strlen(odbpath) - 1] == ']') {
            if (strchr(odbpath, '[')) {
               if (*(strchr(odbpath, '[') + 1) == '*')
                  index1 = -1;
               else if (strchr((strchr(odbpath, '[') + 1), '.') ||
                        strchr((strchr(odbpath, '[') + 1), '-')) {
                  index1 = atoi(strchr(odbpath, '[') + 1);
                  pc = strchr(odbpath, '[') + 1;
                  while (*pc != '.' && *pc != '-')
                     pc++;
                  while (*pc == '.' || *pc == '-')
                     pc++;
                  index2 = atoi(pc);
               } else {
                  if (*(strchr(odbpath, '[') + 1) == '$') {
                     strlcpy(str, strchr(odbpath, '[') + 1, sizeof(str));
                     if (strchr(str, ']'))
                        *strchr(str, ']') = 0;
                     if (!eval_var(str, value, sizeof(value)))
                        return;
                     index1 = atoi(value);
                  } else
                     index1 = atoi(strchr(odbpath, '[') + 1);
               }
            }
            *strchr(odbpath, '[') = 0;
         }

         if (!eval_var(mxml_get_value(pn), value, sizeof(value)))
            return;
         status = db_find_key(hDB, 0, odbpath, &hKey);
         if (status != DB_SUCCESS) {
            sprintf(str, "Cannot find ODB key \"%s\"", odbpath);
            seq_error(str);
            return;
         } else {
            db_get_key(hDB, hKey, &key);
            size = sizeof(data);
            db_sscanf(value, data, &size, 0, key.type);
            
            int notify = TRUE;
            if (seq.subdir_not_notify)
               notify = FALSE;
            if (mxml_get_attribute(pn, "notify"))
               notify = atoi(mxml_get_attribute(pn, "notify"));

            if (key.num_values > 1 && index1 == -1) {
               for (i=0 ; i<key.num_values ; i++)
                  status = db_set_data_index2(hDB, hKey, data, key.item_size, i, key.type, notify);
            } else if (key.num_values > 1 && index2 > index1) {
               for (i=index1; i<key.num_values && i<=index2; i++)
                  status = db_set_data_index2(hDB, hKey, data, key.item_size, i, key.type, notify);
            } else
               status = db_set_data_index2(hDB, hKey, data, key.item_size, index1, key.type, notify);
            
            if (status != DB_SUCCESS) {
               sprintf(str, "Cannot set ODB key \"%s\"", odbpath);
               seq_error(str);
               return;
            }
            
            size = sizeof(seq);
            db_get_record1(hDB, hKeySeq, &seq, &size, 0, strcomb(sequencer_str)); // could have changed seq tree
            seq.current_line_number++;
         }
      }
   }
   
   /*---- ODBGet ----*/
   else if (equal_ustring(mxml_get_name(pn), "ODBGet")) {
      if (!mxml_get_attribute(pn, "path")) {
         seq_error("Missing attribute \"path\"");
      } else {
         strlcpy(odbpath, seq.subdir, sizeof(odbpath));
         if (strlen(odbpath) > 0 && odbpath[strlen(odbpath)-1] != '/')
            strlcat(odbpath, "/", sizeof(odbpath));
         strlcat(odbpath, mxml_get_attribute(pn, "path"), sizeof(odbpath));
         
         /* check if index is supplied */
         index1 = 0;
         if (odbpath[strlen(odbpath) - 1] == ']') {
            if (strchr(odbpath, '[')) {
             
               if (*(strchr(odbpath, '[') + 1) == '$') {
                  strlcpy(str, strchr(odbpath, '[') + 1, sizeof(str));
                  if (strchr(str, ']'))
                     *strchr(str, ']') = 0;
                  if (!eval_var(str, value, sizeof(value)))
                     return;
                  index1 = atoi(value);
               } else
                  index1 = atoi(strchr(odbpath, '[') + 1);
            }
            *strchr(odbpath, '[') = 0;
         }
         
         strlcpy(name, mxml_get_value(pn), sizeof(name));
         status = db_find_key(hDB, 0, odbpath, &hKey);
         if (status != DB_SUCCESS) {
            sprintf(str, "Cannot find ODB key \"%s\"", odbpath);
            seq_error(str);
            return;
         } else {
            db_get_key(hDB, hKey, &key);
            size = sizeof(data);

            status = db_get_data_index(hDB, hKey, data, &size, index1, key.type);
            if (key.type == TID_BOOL)
               strlcpy(value, *((int*)data) > 0 ? "1" : "0", sizeof(value));
            else
               db_sprintf(value, data, size, 0, key.type);
            
            sprintf(str, "/Sequencer/Variables/%s", name);
            size = strlen(value)+1;
            if (size < 32)
               size = 32;
            db_set_value(hDB, 0, str, value, size, 1, TID_STRING);
            
            size = sizeof(seq);
            db_get_record1(hDB, hKeySeq, &seq, &size, 0, strcomb(sequencer_str)); // could have changed seq tree
            seq.current_line_number = mxml_get_line_number_end(pn)+1;
         }
      }
   }

   /*---- ODBInc ----*/
   else if (equal_ustring(mxml_get_name(pn), "ODBInc")) {
      if (!mxml_get_attribute(pn, "path")) {
         seq_error("Missing attribute \"path\"");
      } else {
         strlcpy(odbpath, seq.subdir, sizeof(odbpath));
         if (strlen(odbpath) > 0 && odbpath[strlen(odbpath)-1] != '/')
            strlcat(odbpath, "/", sizeof(odbpath));
         strlcat(odbpath, mxml_get_attribute(pn, "path"), sizeof(odbpath));
         if (strchr(odbpath, '[')) {
            index = atoi(strchr(odbpath, '[')+1);
            *strchr(odbpath, '[') = 0;
         } else
            index = 0;
         if (!eval_var(mxml_get_value(pn), value, sizeof(value)))
            return;
         status = db_find_key(hDB, 0, odbpath, &hKey);
         if (status != DB_SUCCESS) {
            sprintf(str, "Cannot find ODB key \"%s\"", odbpath);
            seq_error(str);
         } else {
            db_get_key(hDB, hKey, &key);
            size = sizeof(data);
            db_get_data_index(hDB, hKey, data, &size, index, key.type);
            db_sprintf(str, data, size, 0, key.type);
            d = atof(str);
            d += atof(value);
            sprintf(str, "%lg", d);
            size = sizeof(data);
            db_sscanf(str, data, &size, 0, key.type);
            
            int notify = TRUE;
            if (seq.subdir_not_notify)
               notify = FALSE;
            if (mxml_get_attribute(pn, "notify"))
               notify = atoi(mxml_get_attribute(pn, "notify"));
            
            if (key.item_size < 32)
               key.item_size = 32;
            db_set_data_index2(hDB, hKey, data, key.item_size, index, key.type, notify);
            seq.current_line_number++;
         }
      }
   }
   
   /*---- RunDescription ----*/
   else if (equal_ustring(mxml_get_name(pn), "RunDescription")) {
      db_set_value(hDB, 0, "/Experiment/Run Parameters/Run Description", mxml_get_value(pn), 256, 1, TID_STRING);
      seq.current_line_number++;
   }
   
   /*---- Script ----*/
   else if (equal_ustring(mxml_get_name(pn), "Script")) {
      sprintf(str, "%s", mxml_get_value(pn));
      
      if (mxml_get_attribute(pn, "params")) {
         strlcpy(data, mxml_get_attribute(pn, "params"), sizeof(data));
         n = strbreak(data, list, 100, ",", FALSE);
         for (i=0 ; i<n ; i++) {
            if (!eval_var(list[i], value, sizeof(value)))
               return;
            strlcat(str, " ", sizeof(str));
            strlcat(str, value, sizeof(str));
         }
      }
      ss_system(str);
      seq.current_line_number++;
   }
   
   /*---- Transition ----*/
   else if (equal_ustring(mxml_get_name(pn), "Transition")) {
      if (equal_ustring(mxml_get_value(pn), "Start")) {
         if (!seq.transition_request) {
            seq.transition_request = TRUE;
            size = sizeof(state);
            db_get_value(hDB, 0, "/Runinfo/State", &state, &size, TID_INT, FALSE);
            if (state != STATE_RUNNING) {
               size = sizeof(run_number);
               db_get_value(hDB, 0, "/Runinfo/Run number", &run_number, &size, TID_INT, FALSE);
               status = cm_transition(TR_START, run_number+1, str, sizeof(str), TR_MTHREAD | TR_SYNC, FALSE);
               if (status != CM_SUCCESS) {
                  sprintf(str, "Cannot start run: %s", str);
                  seq_error(str);
               }
            }
         } else {
            // Wait until transition has finished
            size = sizeof(state);
            db_get_value(hDB, 0, "/Runinfo/State", &state, &size, TID_INT, FALSE);
            if (state == STATE_RUNNING) {
               seq.transition_request = FALSE;
               seq.current_line_number++;
            }
         }
      } else if (equal_ustring(mxml_get_value(pn), "Stop")) {
         if (!seq.transition_request) {
            seq.transition_request = TRUE;
            size = sizeof(state);
            db_get_value(hDB, 0, "/Runinfo/State", &state, &size, TID_INT, FALSE);
            if (state != STATE_STOPPED) {
               status = cm_transition(TR_STOP, 0, str, sizeof(str), TR_MTHREAD | TR_SYNC, FALSE);
               if (status == CM_DEFERRED_TRANSITION) {
                  // do nothing
               } else if (status != CM_SUCCESS) {
                  sprintf(str, "Cannot stop run: %s", str);
                  seq_error(str);
               }
            }
         } else {
            // Wait until transition has finished
            size = sizeof(state);
            db_get_value(hDB, 0, "/Runinfo/State", &state, &size, TID_INT, FALSE);
            if (state == STATE_STOPPED) {
               seq.transition_request = FALSE;
               
               if (seq.stop_after_run) {
                  seq.stop_after_run = FALSE;
                  seq.running = FALSE;
                  seq.finished = TRUE;
                  cm_msg(MTALK, "sequencer", "Sequencer is finished.");
               } else
                  seq.current_line_number++;
               
               db_set_record(hDB, hKeySeq, &seq, sizeof(seq), 0);
            }
         }
      } else {
         sprintf(str, "Invalid transition \"%s\"", mxml_get_value(pn));
         seq_error(str);
         return;
      }
   }
   
   /*---- Wait ----*/
   else if (equal_ustring(mxml_get_name(pn), "Wait")) {
      if (equal_ustring(mxml_get_attribute(pn, "for"), "Events")) {
         if (!eval_var(mxml_get_value(pn), str, sizeof(str)))
            return;
         n = atoi(str);
         seq.wait_limit = (float)n;
         strcpy(seq.wait_type, "Events");
         size = sizeof(d);
         db_get_value(hDB, 0, "/Equipment/Trigger/Statistics/Events sent", &d, &size, TID_DOUBLE, FALSE);
         seq.wait_value = (float)d;
         if (d >= n) {
            seq.current_line_number = mxml_get_line_number_end(pn) + 1;
            seq.wait_limit = 0;
            seq.wait_value = 0;
            seq.wait_type[0] = 0;
         }
         seq.wait_value = (float)d;
      } else  if (equal_ustring(mxml_get_attribute(pn, "for"), "ODBValue")) {
         if (!eval_var(mxml_get_value(pn), str1, sizeof(str1)))
            return;
         v = (float)atof(str1);
         seq.wait_limit = v;
         strcpy(seq.wait_type, "ODB");
         if (!mxml_get_attribute(pn, "path")) {
            seq_error("\"path\" must be given for ODB values");
            return;
         } else {
            strlcpy(odbpath, mxml_get_attribute(pn, "path"), sizeof(odbpath));
            if (strchr(odbpath, '[')) {
               index = atoi(strchr(odbpath, '[')+1);
               *strchr(odbpath, '[') = 0;
            } else
               index = 0;
            status = db_find_key(hDB, 0, odbpath, &hKey);
            if (status != DB_SUCCESS) {
               sprintf(str, "Cannot find ODB key \"%s\"", odbpath);
               seq_error(str);
               return;
            } else {
               if (mxml_get_attribute(pn, "op"))
                  strlcpy(op, mxml_get_attribute(pn, "op"), sizeof(op));
               else
                  strcpy(op, "!=");
               strlcat(seq.wait_type, op, sizeof(seq.wait_type));

               db_get_key(hDB, hKey, &key);
               size = sizeof(data);
               db_get_data_index(hDB, hKey, data, &size, index, key.type);
               if (key.type == TID_BOOL)
                  strlcpy(str, *((int*)data) > 0 ? "1" : "0", sizeof(str));
               else
                  db_sprintf(str, data, size, 0, key.type);
               cont = FALSE;
               seq.wait_value = (float)atof(str);
               if (equal_ustring(op, ">=")) {
                  cont = (seq.wait_value >= seq.wait_limit);
               } else if (equal_ustring(op, ">")) {
                  cont = (seq.wait_value > seq.wait_limit);
               } else if (equal_ustring(op, "<=")) {
                  cont = (seq.wait_value <= seq.wait_limit);
               } else if (equal_ustring(op, "<")) {
                  cont = (seq.wait_value < seq.wait_limit);
               } else if (equal_ustring(op, "==")) {
                  cont = (seq.wait_value == seq.wait_limit);
               } else if (equal_ustring(op, "!=")) {
                  cont = (seq.wait_value != seq.wait_limit);
               } else {
                  sprintf(str, "Invalid comaprison \"%s\"", op);
                  seq_error(str);
                  return;
               }
               
               if (cont) {
                  seq.current_line_number = mxml_get_line_number_end(pn) + 1;
                  seq.wait_limit = 0;
                  seq.wait_value = 0;
                  seq.wait_type[0] = 0;
               }
            }
         }
      } else if (equal_ustring(mxml_get_attribute(pn, "for"), "Seconds")) {
         if (!eval_var(mxml_get_value(pn), str, sizeof(str)))
            return;
         n = atoi(str);
         seq.wait_limit = (float)n;
         strcpy(seq.wait_type, "Seconds");
         if (seq.start_time == 0) {
            seq.start_time = ss_time();
            seq.wait_value = 0;
         } else {
            seq.wait_value = (float)(ss_time() - seq.start_time);
            if (seq.wait_value > seq.wait_limit)
               seq.wait_value = seq.wait_limit;
         }
         if (ss_time() - seq.start_time > (DWORD)seq.wait_limit) {
            seq.current_line_number++;
            seq.start_time = 0;
            seq.wait_limit = 0;
            seq.wait_value = 0;
            seq.wait_type[0] = 0;
         }
      } else {
         sprintf(str, "Invalid wait attribute \"%s\"", mxml_get_attribute(pn, "for"));
         seq_error(str);
      }
   }
   
   /*---- Loop start ----*/
   else if (equal_ustring(mxml_get_name(pn), "Loop")) {
      for (i=0 ; i<4 ; i++)
         if (seq.loop_start_line[i] == 0)
            break;
      if (i == 4) {
         seq_error("Maximum loop nesting exceeded");
         return;
      }
      seq.loop_start_line[i] = seq.current_line_number;
      seq.loop_end_line[i] = mxml_get_line_number_end(pn);
      if (mxml_get_attribute(pn, "l"))
         seq.sloop_start_line[i] = atoi(mxml_get_attribute(pn, "l"));
      if (mxml_get_attribute(pn, "le"))
         seq.sloop_end_line[i] = atoi(mxml_get_attribute(pn, "le"));
      seq.loop_counter[i] = 1;

      if (mxml_get_attribute(pn, "n")) {
         if (equal_ustring(mxml_get_attribute(pn, "n"), "infinite"))
            seq.loop_n[i] = -1;
         else {
            if (!eval_var(mxml_get_attribute(pn, "n"), value, sizeof(value)))
               return;
            seq.loop_n[i] = atoi(value);
         }
         strlcpy(value, "1", sizeof(value));
      } else if (mxml_get_attribute(pn, "values")) {
         strlcpy(data, mxml_get_attribute(pn, "values"), sizeof(data));
         seq.loop_n[i] = strbreak(data, list, 100, ",", FALSE);
         if (!eval_var(list[0], value, sizeof(value)))
            return;
      } else {
         seq_error("Missing \"var\" or \"n\" attribute");
         return;
      }

      if (mxml_get_attribute(pn, "var")) {
         strlcpy(name, mxml_get_attribute(pn, "var"), sizeof(name));
         sprintf(str, "/Sequencer/Variables/%s", name);
         size = strlen(value)+1;
         if (size < 32)
            size = 32;
         db_set_value(hDB, 0, str, value, size, 1, TID_STRING);
      }

      seq.current_line_number++;
   }
   
   /*---- If ----*/
   else if (equal_ustring(mxml_get_name(pn), "If")) {
      
      if (seq.if_index == 4) {
         seq_error("Maximum number of nexted if..endif exceeded");
         return;
      }
      
      // store if, else and endif lines
      seq.if_line[seq.if_index] = seq.current_line_number;
      seq.if_endif_line[seq.if_index] = mxml_get_line_number_end(pn);

      seq.if_else_line[seq.if_index] = 0;
      for (j=seq.current_line_number+1 ; j<mxml_get_line_number_end(pn)+1 ; j++) {
         pe = mxml_get_node_at_line(pnseq, j);
         if (pe && equal_ustring(mxml_get_name(pe), "Else")) {
            seq.if_else_line[seq.if_index] = j;
            break;
         }
      }
      
      strlcpy(str, mxml_get_attribute(pn, "condition"), sizeof(str));
      i = eval_condition(str);
      if (i < 0) {
         seq_error("Invalid number in comparison");
         return;
      }
      
      if (i == 1)
         seq.current_line_number++;
      else if (seq.if_else_line[seq.if_index])
         seq.current_line_number = seq.if_else_line[seq.if_index]+1;
      else
         seq.current_line_number = seq.if_endif_line[seq.if_index];
      
      seq.if_index++;
   }

   /*---- Else ----*/
   else if (equal_ustring(mxml_get_name(pn), "Else")) {
      // goto next "Endif"
      if (seq.if_index == 0) {
         seq_error("Unexpected Else");
         return;
      }
      seq.current_line_number = seq.if_endif_line[seq.if_index-1];
   }

   /*---- Goto ----*/
   else if (equal_ustring(mxml_get_name(pn), "Goto")) {
      if (!mxml_get_attribute(pn, "line") && !mxml_get_attribute(pn, "sline")) {
         seq_error("Missing line number");
         return;
      }
      if (mxml_get_attribute(pn, "line")) {
         if (!eval_var(mxml_get_attribute(pn, "line"), str, sizeof(str)))
            return;
         seq.current_line_number = atoi(str);
      }
      if (mxml_get_attribute(pn, "sline")) {
         if (!eval_var(mxml_get_attribute(pn, "sline"), str, sizeof(str)))
            return;
         for (i=0 ; i<last_line ; i++) {
            pt = mxml_get_node_at_line(pnseq, i);
            if (pt && mxml_get_attribute(pt, "l")) {
               l = atoi(mxml_get_attribute(pt, "l"));
               if (atoi(str) == l) {
                  seq.current_line_number = i;
                  break;
               }
            }
         } 
      }
   }

   /*---- Library ----*/
   else if (equal_ustring(mxml_get_name(pn), "Library")) {
      // simply skip libraries
      seq.current_line_number = mxml_get_line_number_end(pn) + 1;
   }
   
   /*---- Subroutine ----*/
   else if (equal_ustring(mxml_get_name(pn), "Subroutine")) {
      // simply skip subroutines
      seq.current_line_number = mxml_get_line_number_end(pn) + 1;
   }

   /*---- Param ----*/
   else if (equal_ustring(mxml_get_name(pn), "Param")) {
      // simply skip parameters
      seq.current_line_number = mxml_get_line_number_end(pn) + 1;
   }

   /*---- Set ----*/
   else if (equal_ustring(mxml_get_name(pn), "Set")) {
      if (!mxml_get_attribute(pn, "name")) {
         seq_error("Missing variable name");
         return;
      }
      strlcpy(name, mxml_get_attribute(pn, "name"), sizeof(name));
      if (!eval_var(mxml_get_value(pn), value, sizeof(value)))
         return;
      sprintf(str, "/Sequencer/Variables/%s", name);
      size = strlen(value)+1;
      if (size < 32)
         size = 32;
      db_set_value(hDB, 0, str, value, size, 1, TID_STRING);
      
      // check if variable is used in loop
      for (i=3 ; i>=0 ; i--)
         if (seq.loop_start_line[i] > 0) {
            pr = mxml_get_node_at_line(pnseq, seq.loop_start_line[i]);
            if (mxml_get_attribute(pr, "var")) {
               if (equal_ustring(mxml_get_attribute(pr, "var"), name))
                  seq.loop_counter[i] = atoi(value);
            }
         }
      
      seq.current_line_number = mxml_get_line_number_end(pn)+1;
   }

   /*---- Message ----*/
   else if (equal_ustring(mxml_get_name(pn), "Message")) {
      if (!eval_var(mxml_get_value(pn), value, sizeof(value)))
         return;
      if (!seq.message_wait)
         strlcpy(seq.message, value, sizeof(seq.message));
      if (mxml_get_attribute(pn, "wait")) {
         if (atoi(mxml_get_attribute(pn, "wait")) == 1) {
            seq.message_wait = TRUE;
            db_set_record(hDB, hKeySeq, &seq, sizeof(seq), 0);
            if (seq.message[0] != 0)
               return; // don't increment line number until message is cleared from within browser
            seq.message_wait = FALSE;
         }
      }
      seq.current_line_number = mxml_get_line_number_end(pn)+1;
   }

   /*---- Cat ----*/
   else if (equal_ustring(mxml_get_name(pn), "Cat")) {
      if (!mxml_get_attribute(pn, "name")) {
         seq_error("Missing variable name");
         return;
      }
      strlcpy(name, mxml_get_attribute(pn, "name"), sizeof(name));
      if (!concatenate(value, sizeof(value), mxml_get_value(pn)))
         return;
      sprintf(str, "/Sequencer/Variables/%s", name);
      size = strlen(value)+1;
      if (size < 32)
         size = 32;
      db_set_value(hDB, 0, str, value, size, 1, TID_STRING);
      
      seq.current_line_number = mxml_get_line_number_end(pn)+1;
   }

   /*---- Call ----*/
   else if (equal_ustring(mxml_get_name(pn), "Call")) {
      if (seq.stack_index == 4) {
         seq_error("Maximum subroutine level exceeded");
         return;
      } else {
         // put current line number on stack
         seq.subroutine_call_line[seq.stack_index] = mxml_get_line_number_end(pn);
         seq.ssubroutine_call_line[seq.stack_index] = atoi(mxml_get_attribute(pn, "l"));
         seq.subroutine_return_line[seq.stack_index] = mxml_get_line_number_end(pn)+1;
         
         // search subroutine
         for (i=1 ; i<mxml_get_line_number_end(mxml_find_node(pnseq, "RunSequence")) ; i++) {
            pt = mxml_get_node_at_line(pnseq, i);
            if (pt) {
               if (equal_ustring(mxml_get_name(pt), "Subroutine")) {
                  if (equal_ustring(mxml_get_attribute(pt, "name"), mxml_get_attribute(pn, "name"))) {
                     // put routine end line on end stack
                     seq.subroutine_end_line[seq.stack_index] = mxml_get_line_number_end(pt);
                     // go to first line of subroutine
                     seq.current_line_number = mxml_get_line_number_start(pt) + 1;
                     // put parameter(s) on stack
                     if (mxml_get_value(pn))
                        strlcpy(seq.subroutine_param[seq.stack_index], mxml_get_value(pn), 256);
                     // increment stack
                     seq.stack_index ++;
                     break;
                  }
               }
            }
         }
         if (i == mxml_get_line_number_end(mxml_find_node(pnseq, "RunSequence"))) {
            sprintf(str, "Subroutine '%s' not found", mxml_get_attribute(pn, "name"));
            seq_error(str);
         }
      }
   }
   
   /*---- <unknown> ----*/   
   else {
      sprintf(str, "Unknown statement \"%s\"", mxml_get_name(pn));
      seq_error(str);
   }
   
   /* set MSL line from current element */
   pn = mxml_get_node_at_line(pnseq, seq.current_line_number);
   if (pn) { 
      /* check if node belongs to library */
      pt = mxml_get_parent(pn);
      while (pt) {
         if (equal_ustring(mxml_get_name(pt), "Library"))
            break;
         pt = mxml_get_parent(pt);
      }
      if (pt)
         seq.scurrent_line_number = -1;
      else if(mxml_get_attribute(pn, "l"))
         seq.scurrent_line_number = atoi(mxml_get_attribute(pn, "l"));
   }

   /* update current line number */
   db_set_record(hDB, hKeySeq, &seq, sizeof(seq), 0);
}

/**dox***************************************************************/
/** @} *//* end of alfunctioncode */

