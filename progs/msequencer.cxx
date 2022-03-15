/********************************************************************\

  Name:         msequencer.cxx
  Created by:   Stefan Ritt

  Contents:     MIDAS sequencer engine

\********************************************************************/

#include "midas.h"
#include "msystem.h"
#include "mvodb.h"
#include "mxml.h"
#include "sequencer.h"
#include "strlcpy.h"
#include "tinyexpr.h"
#include "odbxx.h"
#include <assert.h>
#include <iostream>
#include <sstream>
#include <string.h>
#include <vector>

#define XNAME_LENGTH 256

/**dox***************************************************************/
/** @file sequencer.cxx
The Midas Sequencer file
*/

/** @defgroup seqfunctioncode Sequencer Functions
 */

/**dox***************************************************************/
/** @addtogroup seqfunctioncode
 *
 *  @{  */

/*------------------------------------------------------------------*/

SEQUENCER_STR(sequencer_str);
SEQUENCER seq;
PMXML_NODE pnseq = NULL;

MVOdb *gOdb = NULL;

/*------------------------------------------------------------------*/

char *stristr(const char *str, const char *pattern) {
   char c1, c2, *ps, *pp;

   if (str == NULL || pattern == NULL)
      return NULL;

   while (*str) {
      ps = (char *) str;
      pp = (char *) pattern;
      c1 = *ps;
      c2 = *pp;
      if (toupper(c1) == toupper(c2)) {
         while (*pp) {
            c1 = *ps;
            c2 = *pp;

            if (toupper(c1) != toupper(c2))
               break;

            ps++;
            pp++;
         }

         if (!*pp)
            return (char *) str;
      }
      str++;
   }

   return NULL;
}

/*------------------------------------------------------------------*/

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

static std::string toString(int v) {
   char buf[256];
   sprintf(buf, "%d", v);
   return buf;
}

static std::string qtoString(int v) {
   char buf[256];
   sprintf(buf, "\"%d\"", v);
   return buf;
}

static std::string q(const char *s) {
   return "\"" + std::string(s) + "\"";
}

/*------------------------------------------------------------------*/

bool is_valid_number(const char *str) {
   std::string s(str);
   std::stringstream ss;
   ss << s;
   double num = 0;
   ss >> num;
   if (ss.good())
      return false;
   else if (num == 0 && s[0] != 0)
      return false;
   else if (s[0] == 0)
      return false;
   return true;
}

/*------------------------------------------------------------------*/

void seq_error(SEQUENCER &seq, const char *str) {
   int status;
   HNDLE hDB, hKey;

   strlcpy(seq.error, str, sizeof(seq.error));
   seq.error_line = seq.current_line_number;
   seq.serror_line = seq.scurrent_line_number;
   seq.running = FALSE;
   seq.transition_request = FALSE;

   cm_get_experiment_database(&hDB, NULL);
   status = db_find_key(hDB, 0, "/Sequencer/State", &hKey);
   if (status != DB_SUCCESS)
      return;
   status = db_set_record(hDB, hKey, &seq, sizeof(seq), 0);
   if (status != DB_SUCCESS)
      return;

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

/*------------------------------------------------------------------*/

std::string eval_var(SEQUENCER &seq, std::string value) {
   std::string result;

   result = value;

   // replace all $... with value
   int i1, i2;
   std::string vsubst;
   while ((i1 = result.find("$")) != std::string::npos) {
      std::string s = result.substr(i1 + 1);
      if (std::isdigit(s[0]) && std::stoi(s) > 0) {
         // find end of number
         for (i2 = i1 + 1; std::isdigit(result[i2]);)
            i2++;

         // replace all $<number> with subroutine parameters
         int index = std::stoi(s);
         if (seq.stack_index > 0) {
            std::istringstream f(seq.subroutine_param[seq.stack_index - 1]);
            std::vector<std::string> param;
            std::string sp;
            while (std::getline(f, sp, ','))
               param.push_back(sp);
            if (index == 0 || index > param.size())
               throw "Parameter $" + std::to_string(index) + " not found";
            vsubst = param[index - 1];
            if (vsubst[0] == '$')
               vsubst = eval_var(seq, vsubst);
         } else
            throw "Parameter $" + std::to_string(index) + " not found";
      } else {
         // find end of string
         for (i2 = i1 + 1; std::isalpha(result[i2]) || result[i2] == '_';)
            i2++;
         s = s.substr(0, i2 - i1 - 1);
         try {
            midas::odb o("/Sequencer/Variables/" + s);
            vsubst = o;
         } catch (...) {
            throw "ODB variable \"" + s + " not found";
         }
      }

      result = result.substr(0, i1) + vsubst + result.substr(i2);
   }

   int error;
   double r = te_interp(result.c_str(), &error);
   if (error > 0) {
      // check if result is only a string
      if (!std::isdigit(result[0]) && result[0] != '-')
         return result;

      throw "Error in expression \"" + result + "\" position " + std::to_string(error - 1);
   }

   if (r == (int) r)
      return std::to_string((int) r);

   return std::to_string(r);
}

/*------------------------------------------------------------------*/

int concatenate(SEQUENCER &seq, char *result, int size, char *value) {
   char list[100][XNAME_LENGTH];
   int i, n;

   n = strbreak(value, list, 100, ",", FALSE);

   result[0] = 0;
   for (i = 0; i < n; i++) {
      std::string str = eval_var(seq, std::string(list[i]));
      strlcat(result, str.c_str(), size);
   }

   return TRUE;
}

/*------------------------------------------------------------------*/

int eval_condition(SEQUENCER &seq, const char *condition) {
   int i;
   double value1, value2;
   char value1_str[256], value2_str[256], str[256], op[3], *p;
   std::string value1_var, value2_var;

   // strip leading and trailing space
   p = (char *)condition;
   while (*p == ' ')
      p++;
   strcpy(str, p);

   // strip any comment '#'
   if (strchr(str, '#'))
      *strchr(str, '#') = 0;

   while (strlen(str) > 0 && (str[strlen(str)-1] == ' '))
      str[strlen(str)-1] = 0;

   // strip enclosing '()'
   if (str[0] == '(') {
      strlcpy(value1_str, str+1, sizeof(value1_str));
      strlcpy(str, value1_str, sizeof(str));
      if (strlen(str) > 0 && str[strlen(str)-1] == ')')
         str[strlen(str)-1] = 0;
   }

   op[1] = op[2] = 0;

   /* find value and operator */
   for (i = 0; i < (int) strlen(str); i++)
      if (strchr("<>=!&", str[i]) != NULL)
         break;
   strlcpy(value1_str, str, i + 1);
   while (value1_str[strlen(value1_str) - 1] == ' ')
      value1_str[strlen(value1_str) - 1] = 0;
   op[0] = str[i];
   if (strchr("<>=!&", str[i + 1]) != NULL)
      op[1] = str[++i];

   for (i++; str[i] == ' '; i++);
   strlcpy(value2_str, str + i, sizeof(value2_str));

   value1_var = eval_var(seq, value1_str);
   value2_var = eval_var(seq, value2_str);

   if (!is_valid_number(value1_var.c_str()) || !is_valid_number(value2_var.c_str())) {
      // string comparison
      if (strcmp(op, "=") == 0)
         return equal_ustring(value1_var.c_str(), value2_var.c_str()) ? 1 : 0;
      if (strcmp(op, "==") == 0)
         return equal_ustring(value1_var.c_str(), value2_var.c_str()) ? 1 : 0;
      if (strcmp(op, "!=") == 0)
         return equal_ustring(value1_var.c_str(), value2_var.c_str()) ? 0 : 1;
      // invalid operator for string comparisons
      return -1;
   }

   // numeric comparison
   value1 = std::stod(value1_var);
   value2 = std::stod(value2_var);

   /* now do logical operation */
   if (strcmp(op, "=") == 0)
      if (value1 == value2)
         return 1;
   if (strcmp(op, "==") == 0)
      if (value1 == value2)
         return 1;
   if (strcmp(op, "!=") == 0)
      if (value1 != value2)
         return 1;
   if (strcmp(op, "<") == 0)
      if (value1 < value2)
         return 1;
   if (strcmp(op, ">") == 0)
      if (value1 > value2)
         return 1;
   if (strcmp(op, "<=") == 0)
      if (value1 <= value2)
         return 1;
   if (strcmp(op, ">=") == 0)
      if (value1 >= value2)
         return 1;
   if (strcmp(op, "&") == 0)
      if (((unsigned int) value1 & (unsigned int) value2) > 0)
         return 1;

   return 0;
}

/*------------------------------------------------------------------*/

static BOOL
msl_parse(HNDLE hDB, MVOdb *odb, const char *filename, const char *xml_filename, char *error, int error_size,
          int *error_line) {
   char str[256], *buf, *pl, *pe;
   char list[100][XNAME_LENGTH], list2[100][XNAME_LENGTH], **lines;
   int i, j, n, size, n_lines, endl, line, nest, incl, library;
   std::string xml;
   char *msl_include, *xml_include, *include_error;
   int include_error_size;
   BOOL include_status;

   int fhin = open(filename, O_RDONLY | O_TEXT);
   if (fhin < 0) {
      sprintf(error, "Cannot open \"%s\", errno %d (%s)", filename, errno, strerror(errno));
      return FALSE;
   }
   FILE *fout = fopen(xml_filename, "wt");
   if (fout == NULL) {
      sprintf(error, "Cannot write to \"%s\", fopen() errno %d (%s)", xml_filename, errno, strerror(errno));
      return FALSE;
   }
   if (fhin > 0 && fout) {
      size = (int) lseek(fhin, 0, SEEK_END);
      lseek(fhin, 0, SEEK_SET);
      buf = (char *) malloc(size + 1);
      size = (int) read(fhin, buf, size);
      buf[size] = 0;
      close(fhin);

      /* look for any includes */
      lines = (char **) malloc(sizeof(char *));
      incl = 0;
      pl = buf;
      library = FALSE;
      for (n_lines = 0; *pl; n_lines++) {
         lines = (char **) realloc(lines, sizeof(char *) * (n_lines + 1));
         lines[n_lines] = pl;
         if (strchr(pl, '\n')) {
            pe = strchr(pl, '\n');
            *pe = 0;
            if (*(pe - 1) == '\r') {
               *(pe - 1) = 0;
            }
            pe++;
         } else
            pe = pl + strlen(pl);
         strlcpy(str, pl, sizeof(str));
         pl = pe;
         strbreak(str, list, 100, ", ", FALSE);
         if (equal_ustring(list[0], "include")) {
            if (!incl) {
               fprintf(fout, "<?xml version=\"1.0\" encoding=\"ISO-8859-1\"?>\n");
               fprintf(fout, "<!DOCTYPE RunSequence [\n");
               xml += "<?xml version=\"1.0\" encoding=\"ISO-8859-1\"?>\n";
               xml += "<!DOCTYPE RunSequence [\n";
               incl = 1;
            }

            //if a path is given, use filename as entity reference
            char *reference = strrchr(list[1], '/');
            if (reference)
               reference++;
            else
               reference = list[1];

            fprintf(fout, "  <!ENTITY %s SYSTEM \"%s.xml\">\n", reference, list[1]);
            xml += "  <!ENTITY ";
            xml += reference;
            xml += " SYSTEM \"";
            xml += list[1];
            xml += ".xml\">\n";

            //recurse
            size = strlen(list[1]) + 1 + 4;
            msl_include = (char *) malloc(size);
            xml_include = (char *) malloc(size);
            strlcpy(msl_include, list[1], size);
            strlcpy(xml_include, list[1], size);
            strlcat(msl_include, ".msl", size);
            strlcat(xml_include, ".xml", size);

            include_error = error + strlen(error);
            include_error_size = error_size - strlen(error);

            include_status = msl_parse(hDB, odb, msl_include, xml_include, include_error, include_error_size,
                                       error_line);
            free(msl_include);
            free(xml_include);

            if (!include_status) {
               // report the errror on CALL line instead of the one in included file
               *error_line = n_lines + 1;
               return FALSE;
            }
         }
         if (equal_ustring(list[0], "library")) {
            fprintf(fout, "<Library name=\"%s\">\n", list[1]);
            xml += "<Library name=\"";
            xml += list[1];
            xml += "\">\n";
            library = TRUE;
         }
      }
      if (incl) {
         fprintf(fout, "]>\n");
         xml += "]>\n";
      } else if (!library) {
         fprintf(fout, "<?xml version=\"1.0\" encoding=\"ISO-8859-1\"?>\n");
         xml += "<?xml version=\"1.0\" encoding=\"ISO-8859-1\"?>\n";
      }

      /* parse rest of file */
      if (!library) {
         fprintf(fout,
                 "<RunSequence xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" xsi:noNamespaceSchemaLocation=\"\">\n");
         xml += "<RunSequence xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" xsi:noNamespaceSchemaLocation=\"\">\n";
      }

      std::vector<std::string> slines;
      for (line = 0; line < n_lines; line++) {
         slines.push_back(lines[line]);
      }

      odb->WSA("Sequencer/Script/Lines", slines, 0);

      /* clear all variables */
      midas::odb::delete_key("/Sequencer/Variables");
      midas::odb::delete_key("/Sequencer/Param");

      for (line = 0; line < n_lines; line++) {
         char *p = lines[line];
         while (*p == ' ')
            p++;
         strlcpy(list[0], p, sizeof(list[0]));
         if (strchr(list[0], ' '))
            *strchr(list[0], ' ') = 0;
         p += strlen(list[0]);
         n = strbreak(p + 1, &list[1], 99, ",", FALSE) + 1;

         /* remove any comment */
         for (i = 0; i < n; i++) {
            if (list[i][0] == '#') {
               for (j = i; j < n; j++)
                  list[j][0] = 0;
               break;
            }
         }

         /* check for variable assignment */
         char eq[1024];
         strlcpy(eq, lines[line], sizeof(eq));
         if (strchr(eq, '#'))
            *strchr(eq, '#') = 0;
         for (i = 0, n = 0; i < strlen(eq); i++)
            if (eq[i] == '=')
               n++;
         if (n == 1 && eq[0] != '=') {
            // equation found
            strlcpy(list[0], "SET", sizeof(list[0]));
            p = eq;
            while (*p == ' ')
               p++;
            strlcpy(list[1], p, sizeof(list[1]));
            *strchr(list[1], '=') = 0;
            if (strchr(list[1], ' '))
               *strchr(list[1], ' ') = 0;
            p = strchr(eq, '=')+1;
            while (*p == ' ')
               p++;
            strlcpy(list[2], p, sizeof(list[2]));
            while (strlen(list[2]) > 0 && list[2][strlen(list[2])-1] == ' ')
               list[2][strlen(list[2])-1] = 0;
         }

         if (equal_ustring(list[0], "library")) {

         } else if (equal_ustring(list[0], "include")) {
            //if a path is given, use filename as entity reference
            char *reference = strrchr(list[1], '/');
            if (reference)
               reference++;
            else
               reference = list[1];

            fprintf(fout, "&%s;\n", reference);
            xml += "&";
            xml += reference;
            xml += ";\n";

         } else if (equal_ustring(list[0], "call")) {
            fprintf(fout, "<Call l=\"%d\" name=\"%s\">", line + 1, list[1]);
            xml += "<Call l=" + qtoString(line + 1) + " name=" + q(list[1]) + ">";
            for (i = 2; i < 100 && list[i][0]; i++) {
               if (i > 2) {
                  fprintf(fout, ",");
                  xml += ",";
               }
               fprintf(fout, "%s", list[i]);
               xml += list[i];
            }
            fprintf(fout, "</Call>\n");
            xml += "</Call>\n";

         } else if (equal_ustring(list[0], "cat")) {
            fprintf(fout, "<Cat l=\"%d\" name=\"%s\">", line + 1, list[1]);
            xml += "<Cat l=" + qtoString(line + 1) + " name=" + q(list[1]) + ">";
            for (i = 2; i < 100 && list[i][0]; i++) {
               if (i > 2) {
                  fprintf(fout, ",");
                  xml += ",";
               }
               fprintf(fout, "\"%s\"", list[i]);
               xml += q(list[i]);
            }
            fprintf(fout, "</Cat>\n");
            xml += "</Cat>\n";

         } else if (equal_ustring(list[0], "comment")) {
            fprintf(fout, "<Comment l=\"%d\">%s</Comment>\n", line + 1, list[1]);
            xml += "<Comment l=" + qtoString(line + 1) + ">" + list[1] + "</Comment>\n";

         } else if (equal_ustring(list[0], "goto")) {
            fprintf(fout, "<Goto l=\"%d\" sline=\"%s\" />\n", line + 1, list[1]);
            xml += "<Goto l=" + qtoString(line + 1) + " sline=" + q(list[1]) + " />\n";

         } else if (equal_ustring(list[0], "if")) {
            fprintf(fout, "<If l=\"%d\" condition=\"", line + 1);
            xml += "<If l=" + qtoString(line + 1) + " condition=\"";
            for (i = 1; i < 100 && list[i][0] && stricmp(list[i], "THEN") != 0; i++) {
               fprintf(fout, "%s", list[i]);
               xml += list[i];
            }
            fprintf(fout, "\">\n");
            xml += "\">\n";

         } else if (equal_ustring(list[0], "else")) {
            fprintf(fout, "<Else />\n");
            xml += "<Else />\n";

         } else if (equal_ustring(list[0], "endif")) {
            fprintf(fout, "</If>\n");
            xml += "</If>\n";

         } else if (equal_ustring(list[0], "loop")) {
            /* find end of loop */
            for (i = line, nest = 0; i < n_lines; i++) {
               strbreak(lines[i], list2, 100, ", ", FALSE);
               if (equal_ustring(list2[0], "loop"))
                  nest++;
               if (equal_ustring(list2[0], "endloop")) {
                  nest--;
                  if (nest == 0)
                     break;
               }
            }
            if (i < n_lines)
               endl = i + 1;
            else
               endl = line + 1;
            if (list[2][0] == 0) {
               fprintf(fout, "<Loop l=\"%d\" le=\"%d\" n=\"%s\">\n", line + 1, endl, list[1]);
               xml += "<Loop l=" + qtoString(line + 1) + " le=" + qtoString(endl) + " n=" + q(list[1]) + ">\n";
            } else if (list[3][0] == 0) {
               fprintf(fout, "<Loop l=\"%d\" le=\"%d\" var=\"%s\" n=\"%s\">\n", line + 1, endl, list[1], list[2]);
               xml += "<Loop l=" + qtoString(line + 1) + " le=" + qtoString(endl) + " var=" + q(list[1]) + " n=" +
                      q(list[2]) + ">\n";
            } else {
               fprintf(fout, "<Loop l=\"%d\" le=\"%d\" var=\"%s\" values=\"", line + 1, endl, list[1]);
               xml += "<Loop l=" + qtoString(line + 1) + " le=" + qtoString(endl) + " var=" + q(list[1]) + " values=\"";
               for (i = 2; i < 100 && list[i][0]; i++) {
                  if (i > 2) {
                     fprintf(fout, ",");
                     xml += ",";
                  }
                  fprintf(fout, "%s", list[i]);
                  xml += list[i];
               }
               fprintf(fout, "\">\n");
               xml += "\">\n";
            }
         } else if (equal_ustring(list[0], "endloop")) {
            fprintf(fout, "</Loop>\n");
            xml += "</Loop>\n";

         } else if (equal_ustring(list[0], "message")) {
            fprintf(fout, "<Message l=\"%d\"%s>%s</Message>\n", line + 1, list[2][0] == '1' ? " wait=\"1\"" : "",
                    list[1]);
            xml += "<Message l=" + qtoString(line + 1);
            if (list[2][0] == '1')
               xml += " wait=\"1\"";
            xml += ">";
            xml += list[1];
            xml += "</Message>\n";

         } else if (equal_ustring(list[0], "odbinc")) {
            if (list[2][0] == 0)
               strlcpy(list[2], "1", 2);
            fprintf(fout, "<ODBInc l=\"%d\" path=\"%s\">%s</ODBInc>\n", line + 1, list[1], list[2]);
            xml += "<ODBInc l=" + qtoString(line + 1) + " path=" + q(list[1]) + ">" + list[2] + "</ODBInc>\n";

         } else if (equal_ustring(list[0], "odbcreate")) {
            if (list[3][0]) {
               fprintf(fout, "<ODBCreate l=\"%d\" size=\"%s\" path=\"%s\" type=\"%s\"></ODBCreate>\n", line + 1,
                       list[3], list[1], list[2]);
               xml += "<ODBCreate l=" + qtoString(line + 1) + " size=" + q(list[3]) + " path=" + q(list[1]) + " type=" +
                      q(list[2]) + "></ODBCreate>\n";
            } else {
               fprintf(fout, "<ODBCreate l=\"%d\" path=\"%s\" type=\"%s\"></ODBCreate>\n", line + 1, list[1], list[2]);
               xml += "<ODBCreate l=" + qtoString(line + 1) + " path=" + q(list[1]) + " type=" + q(list[1]) +
                      "></ODBCreate>\n";
            }

         } else if (equal_ustring(list[0], "odbdelete")) {
            fprintf(fout, "<ODBDelete l=\"%d\">%s</ODBDelete>\n", line + 1, list[1]);
            xml += "<ODBDelete l=" + qtoString(line + 1) + ">" + list[1] + "</ODBDelete>\n";

         } else if (equal_ustring(list[0], "odbset")) {
            if (list[3][0]) {
               fprintf(fout, "<ODBSet l=\"%d\" notify=\"%s\" path=\"%s\">%s</ODBSet>\n", line + 1, list[3], list[1],
                       list[2]);
               xml += "<ODBSet l=" + qtoString(line + 1) + " notify=" + q(list[3]) + " path=" + q(list[1]) + ">" +
                      list[2] + "</ODBSet>\n";
            } else {
               fprintf(fout, "<ODBSet l=\"%d\" path=\"%s\">%s</ODBSet>\n", line + 1, list[1], list[2]);
               xml += "<ODBSet l=" + qtoString(line + 1) + " path=" + q(list[1]) + ">" + list[2] + "</ODBSet>\n";
            }

         } else if (equal_ustring(list[0], "odbload")) {
            if (list[2][0]) {
               fprintf(fout, "<ODBLoad l=\"%d\" path=\"%s\">%s</ODBLoad>\n", line + 1, list[2], list[1]);
               xml += "<ODBLoad l=" + qtoString(line + 1) + " path=" + q(list[2]) + ">" + list[1] + "</ODBLoad>\n";
            } else {
               fprintf(fout, "<ODBLoad l=\"%d\">%s</ODBLoad>\n", line + 1, list[1]);
               xml += "<ODBLoad l=" + qtoString(line + 1) + ">" + list[1] + "</ODBLoad>\n";
            }

         } else if (equal_ustring(list[0], "odbget")) {
            fprintf(fout, "<ODBGet l=\"%d\" path=\"%s\">%s</ODBGet>\n", line + 1, list[1], list[2]);
            xml += "<ODBGet l=" + qtoString(line + 1) + " path=" + q(list[1]) + ">" + list[2] + "</ODBGet>\n";

         } else if (equal_ustring(list[0], "odbsubdir")) {
            if (list[2][0]) {
               fprintf(fout, "<ODBSubdir l=\"%d\" notify=\"%s\" path=\"%s\">\n", line + 1, list[2], list[1]);
               xml += "<ODBSubdir l=" + qtoString(line + 1) + " notify=" + q(list[2]) + " path=" + q(list[1]) + ">\n";
            } else {
               fprintf(fout, "<ODBSubdir l=\"%d\" path=\"%s\">\n", line + 1, list[1]);
               xml += "<ODBSubdir l=" + qtoString(line + 1) + " path=" + q(list[1]) + ">\n";
            }
         } else if (equal_ustring(list[0], "endodbsubdir")) {
            fprintf(fout, "</ODBSubdir>\n");
            xml += "</ODBSubdir>\n";

         } else if (equal_ustring(list[0], "param")) {
            if (list[2][0] == 0) {
               fprintf(fout, "<Param l=\"%d\" name=\"%s\" />\n", line + 1, list[1]);
               xml += "<Param l=" + qtoString(line + 1) + " name=" + q(list[1]) + " />\n";
               std::string v;
               odb->RS((std::string("Sequencer/Param/Value/") + list[1]).c_str(), &v, true);
               odb->RS((std::string("Sequencer/Variables/") + list[1]).c_str(), &v, true);
            } else if (!list[3][0] && equal_ustring(list[2], "bool")) {
               fprintf(fout, "<Param l=\"%d\" name=\"%s\" type=\"bool\" />\n", line + 1, list[1]);
               xml += "<Param l=" + qtoString(line + 1) + " name=" + q(list[1]) + " type=\"bool\" />\n";
               bool v = false;
               odb->RB((std::string("Sequencer/Param/Value/") + list[1]).c_str(), &v, true);
               std::string s;
               odb->RS((std::string("Sequencer/Variables/") + list[1]).c_str(), &s, true);
            } else if (!list[3][0]) {
               fprintf(fout, "<Param l=\"%d\" name=\"%s\" comment=\"%s\" />\n", line + 1, list[1], list[2]);
               xml += "<Param l=" + qtoString(line + 1) + " name=" + q(list[1]) + " comment=" + q(list[2]) + " />\n";
               std::string v;
               odb->RS((std::string("Sequencer/Param/Value/") + list[1]).c_str(), &v, true);
               odb->RS((std::string("Sequencer/Variables/") + list[1]).c_str(), &v, true);
               odb->WS((std::string("Sequencer/Param/Comment/") + list[1]).c_str(), list[2]);
            } else {
               fprintf(fout, "<Param l=\"%d\" name=\"%s\" comment=\"%s\" options=\"", line + 1, list[1], list[2]);
               xml += "<Param l=" + qtoString(line + 1) + " name=" + q(list[1]) + " comment=" + q(list[2]) +
                      " options=\"";
               std::string v;
               odb->RS((std::string("Sequencer/Param/Value/") + list[1]).c_str(), &v, true);
               odb->RS((std::string("Sequencer/Variables/") + list[1]).c_str(), &v, true);
               odb->WS((std::string("Sequencer/Param/Comment/") + list[1]).c_str(), list[2]);
               std::vector<std::string> options;
               for (i = 3; i < 100 && list[i][0]; i++) {
                  if (i > 3) {
                     fprintf(fout, ",");
                     xml += ",";
                  }
                  fprintf(fout, "%s", list[i]);
                  xml += list[i];
                  options.push_back(list[i]);
               }
               fprintf(fout, "\" />\n");
               xml += "\" />\n";
               odb->WSA((std::string("Sequencer/Param/Options/") + list[1]).c_str(), options, 0);
            }

         } else if (equal_ustring(list[0], "rundescription")) {
            fprintf(fout, "<RunDescription l=\"%d\">%s</RunDescription>\n", line + 1, list[1]);
            xml += "<RunDescription l=" + qtoString(line + 1) + ">" + list[1] + "</RunDescription>\n";

         } else if (equal_ustring(list[0], "script")) {
            if (list[2][0] == 0) {
               fprintf(fout, "<Script l=\"%d\">%s</Script>\n", line + 1, list[1]);
               xml += "<Script l=" + qtoString(line + 1) + ">" + list[1] + "</Script>\n";
            } else {
               fprintf(fout, "<Script l=\"%d\" params=\"", line + 1);
               xml += "<Script l=" + qtoString(line + 1) + " params=\"";
               for (i = 2; i < 100 && list[i][0]; i++) {
                  if (i > 2) {
                     fprintf(fout, ",");
                     xml += ",";
                  }
                  fprintf(fout, "%s", list[i]);
                  xml += list[i];
               }
               fprintf(fout, "\">%s</Script>\n", list[1]);
               xml += "\">";
               xml += list[1];
               xml += "</Script>\n";
            }

         } else if (equal_ustring(list[0], "set")) {
            fprintf(fout, "<Set l=\"%d\" name=\"%s\">%s</Set>\n", line + 1, list[1], list[2]);
            xml += "<Set l=" + qtoString(line + 1) + " name=" + q(list[1]) + ">" + list[2] + "</Set>\n";

         } else if (equal_ustring(list[0], "subroutine")) {
            fprintf(fout, "\n<Subroutine l=\"%d\" name=\"%s\">\n", line + 1, list[1]);
            xml += "\n<Subroutine l=" + qtoString(line + 1) + " name=" + q(list[1]) + ">\n";

         } else if (equal_ustring(list[0], "endsubroutine")) {
            fprintf(fout, "</Subroutine>\n");
            xml += "</Subroutine>\n";

         } else if (equal_ustring(list[0], "transition")) {
            fprintf(fout, "<Transition l=\"%d\">%s</Transition>\n", line + 1, list[1]);
            xml += "<Transition l=" + qtoString(line + 1) + ">" + list[1] + "</Transition>\n";

         } else if (equal_ustring(list[0], "wait")) {
            if (!list[2][0]) {
               fprintf(fout, "<Wait l=\"%d\" for=\"seconds\">%s</Wait>\n", line + 1, list[1]);
               xml += "<Wait l=" + qtoString(line + 1) + " for=\"seconds\">" + list[1] + "</Wait>\n";
            } else if (!list[3][0]) {
               fprintf(fout, "<Wait l=\"%d\" for=\"%s\">%s</Wait>\n", line + 1, list[1], list[2]);
               xml += "<Wait l=" + qtoString(line + 1) + " for=" + q(list[1]) + ">" + list[2] + "</Wait>\n";
            } else {
               fprintf(fout, "<Wait l=\"%d\" for=\"%s\" path=\"%s\" op=\"%s\">%s</Wait>\n", line + 1, list[1], list[2],
                       list[3], list[4]);
               xml += "<Wait l=" + qtoString(line + 1) + " for=" + q(list[1]) + " path=" + q(list[2]) + " op=" +
                      q(list[3]) + ">" + list[4] + "</Wait>\n";
            }

         } else if (list[0][0] == 0 || list[0][0] == '#') {
            /* skip empty or outcommented lines */
         } else {
            sprintf(error, "Invalid command \"%s\"", list[0]);
            *error_line = line + 1;
            return FALSE;
         }
      }

      free(lines);
      free(buf);
      if (library) {
         fprintf(fout, "\n</Library>\n");
         xml += "\n</Library>\n";
      } else {
         fprintf(fout, "</RunSequence>\n");
         xml += "</RunSequence>\n";
      }
      fclose(fout);

      odb->WS("Sequencer/Script/XML", xml.c_str());

      std::string tmpxml = std::string(xml_filename) + ".odb";
      FILE *fp = fopen(tmpxml.c_str(), "w");
      if (fp) {
         fprintf(fp, "%s", xml.c_str());
         fclose(fp);
      }

   } else {
      sprintf(error, "File error on \"%s\"", filename);
      return FALSE;
   }

   return TRUE;
}

/*------------------------------------------------------------------*/

void seq_read(SEQUENCER *seq) {
   int status;
   HNDLE hDB, hKey;
   status = cm_get_experiment_database(&hDB, NULL);
   status = db_find_key(hDB, 0, "/Sequencer/State", &hKey);
   if (status != DB_SUCCESS) {
      cm_msg(MERROR, "seq_read", "Cannot find /Sequencer/State in ODB, db_find_key() status %d", status);
      return;
   }

   SEQUENCER_STR(sequencer_str);
   int size = sizeof(SEQUENCER);
   status = db_get_record1(hDB, hKey, seq, &size, 0, strcomb1(sequencer_str).c_str());
   if (status != DB_SUCCESS) {
      cm_msg(MERROR, "seq_read", "Cannot get /Sequencer/State from ODB, db_get_record1() status %d", status);
      return;
   }
}

void seq_write(const SEQUENCER &seq) {
   int status;
   HNDLE hDB, hKey;
   status = cm_get_experiment_database(&hDB, NULL);
   status = db_find_key(hDB, 0, "/Sequencer/State", &hKey);
   if (status != DB_SUCCESS) {
      cm_msg(MERROR, "seq_write", "Cannot find /Sequencer/State in ODB, db_find_key() status %d", status);
      return;
   }
   status = db_set_record(hDB, hKey, (void *) &seq, sizeof(SEQUENCER), 0);
   if (status != DB_SUCCESS) {
      cm_msg(MERROR, "seq_write", "Cannot write to ODB /Sequencer/State, db_set_record() status %d", status);
      return;
   }
}

/*------------------------------------------------------------------*/

void seq_clear(SEQUENCER &seq) {
   seq.running = FALSE;
   seq.finished = FALSE;
   seq.paused = FALSE;
   seq.transition_request = FALSE;
   seq.wait_limit = 0;
   seq.wait_value = 0;
   seq.start_time = 0;
   seq.wait_type[0] = 0;
   for (int i = 0; i < 4; i++) {
      seq.loop_start_line[i] = 0;
      seq.sloop_start_line[i] = 0;
      seq.loop_end_line[i] = 0;
      seq.sloop_end_line[i] = 0;
      seq.loop_counter[i] = 0;
      seq.loop_n[i] = 0;
   }
   for (int i = 0; i < 4; i++) {
      seq.if_else_line[i] = 0;
      seq.if_endif_line[i] = 0;
      seq.subroutine_end_line[i] = 0;
      seq.subroutine_return_line[i] = 0;
      seq.subroutine_call_line[i] = 0;
      seq.ssubroutine_call_line[i] = 0;
      seq.subroutine_param[i][0] = 0;
   }
   seq.current_line_number = 0;
   seq.scurrent_line_number = 0;
   seq.if_index = 0;
   seq.stack_index = 0;
   seq.error[0] = 0;
   seq.error_line = 0;
   seq.serror_line = 0;
   seq.subdir[0] = 0;
   seq.subdir_end_line = 0;
   seq.subdir_not_notify = 0;
   seq.message[0] = 0;
   seq.message_wait = FALSE;
   seq.stop_after_run = FALSE;
}

/*------------------------------------------------------------------*/

static void seq_start() {
   //SEQUENCER seq;

   seq_read(&seq);

   seq_clear(seq);

   if (!pnseq) {
      strlcpy(seq.error, "Cannot start script, no script loaded", sizeof(seq.error));
      seq_write(seq);
      return;
   }

   /* start sequencer */
   seq.running = TRUE;
   seq.current_line_number = 1;
   seq.scurrent_line_number = 1;
   seq_write(seq);
}

/*------------------------------------------------------------------*/

static void seq_stop() {
   printf("seq_stop!\n");

   //SEQUENCER seq;

   seq_read(&seq);

   seq_clear(seq);
   seq.finished = TRUE;

   seq_write(seq);

   /* stop run if not already stopped */
   char str[256];
   int state = 0;
   gOdb->RI("Runinfo/State", &state);
   if (state != STATE_STOPPED)
      cm_transition(TR_STOP, 0, str, sizeof(str), TR_MTHREAD | TR_SYNC, FALSE);
}

/*------------------------------------------------------------------*/

static void seq_open_file(HNDLE hDB, const char *str, SEQUENCER &seq) {
   seq.new_file = FALSE;
   seq.error[0] = 0;
   seq.error_line = 0;
   seq.serror_line = 0;
   if (pnseq) {
      mxml_free_tree(pnseq);
      pnseq = NULL;
   }
   gOdb->WS("Sequencer/Script/XML", "");
   gOdb->WS("Sequencer/Script/Lines", "");

   if (stristr(str, ".msl")) {
      int size = strlen(str) + 1;
      char *xml_filename = (char *) malloc(size);
      strlcpy(xml_filename, str, size);
      strsubst(xml_filename, size, ".msl", ".xml");
      //printf("Parsing MSL sequencer file: %s to XML sequencer file %s\n", str, xml_filename);
      if (msl_parse(hDB, gOdb, str, xml_filename, seq.error, sizeof(seq.error), &seq.serror_line)) {
         //printf("Loading XML sequencer file: %s\n", xml_filename);
         pnseq = mxml_parse_file(xml_filename, seq.error, sizeof(seq.error), &seq.error_line);
      } else {
         //printf("Error in MSL sequencer file \"%s\" line %d, error: %s\n", str, seq.serror_line, seq.error);
      }
      free(xml_filename);
   } else {
      //printf("Loading XML sequencer file: %s\n", str);
      pnseq = mxml_parse_file(str, seq.error, sizeof(seq.error), &seq.error_line);
   }
}

/*------------------------------------------------------------------*/

static void seq_watch(HNDLE hDB, HNDLE hKeyChanged, int index, void *info) {
   char str[256];
   //SEQUENCER seq;

   seq_read(&seq);

   if (seq.new_file) {
      strlcpy(str, seq.path, sizeof(str));
      if (strlen(str) > 1 && str[strlen(str) - 1] != DIR_SEPARATOR)
         strlcat(str, DIR_SEPARATOR_STR, sizeof(str));
      strlcat(str, seq.filename, sizeof(str));

      //printf("Load file %s\n", str);

      seq_open_file(hDB, str, seq);

      seq_clear(seq);

      seq_write(seq);
   }
}

static void seq_watch_command(HNDLE hDB, HNDLE hKeyChanged, int index, void *info) {
   bool start_script = false;
   bool stop_immediately = false;
   bool load_new_file = false;

   gOdb->RB("Sequencer/Command/Start script", &start_script);
   gOdb->RB("Sequencer/Command/Stop immediately", &stop_immediately);
   gOdb->RB("Sequencer/Command/Load new file", &load_new_file);

   if (load_new_file) {
      std::string filename;
      gOdb->RS("Sequencer/Command/Load filename", &filename);
      gOdb->WB("Sequencer/Command/Load new file", false);

      if (filename.find("..") != std::string::npos) {
         strlcpy(seq.error, "Cannot load \"", sizeof(seq.error));
         strlcat(seq.error, filename.c_str(), sizeof(seq.error));
         strlcat(seq.error, "\": file names with \"..\" is not permitted", sizeof(seq.error));
         seq_write(seq);
      } else if (filename.find(".msl") == std::string::npos) {
         strlcpy(seq.error, "Cannot load \"", sizeof(seq.error));
         strlcat(seq.error, filename.c_str(), sizeof(seq.error));
         strlcat(seq.error, "\": file name should end with \".msl\"", sizeof(seq.error));
         seq_write(seq);
      } else {
         strlcpy(seq.filename, filename.c_str(), sizeof(seq.filename));
         std::string path = cm_expand_env(seq.path);
         if (path.length() > 0) {
            path += "/";
         }
         path += filename;
         HNDLE hDB;
         cm_get_experiment_database(&hDB, NULL);
         seq_clear(seq);
         seq_open_file(hDB, path.c_str(), seq);
         seq_write(seq);
      }
   }

   if (start_script) {
      gOdb->WB("Sequencer/Command/Start script", false);

      bool seq_running = false;
      gOdb->RB("Sequencer/State/running", &seq_running);

      if (!seq_running) {
         seq_start();
      } else {
         printf("sequencer is already running!\n");
      }
   }

   if (stop_immediately) {
      gOdb->WB("Sequencer/Command/Stop immediately", false);

      seq_stop();

      cm_msg(MTALK, "sequencer", "Sequencer is finished by \"stop immediately\".");
   }
}

/*------------------------------------------------------------------*/

//performs array index extraction including sequencer variables
void seq_array_index(char *odbpath, int *index1, int *index2) {
   char str[256];
   *index1 = *index2 = 0;
   if (odbpath[strlen(odbpath) - 1] == ']') {
      if (strchr(odbpath, '[')) {
         //check for sequencer variables
         if (*(strchr(odbpath, '[') + 1) == '$') {
            strlcpy(str, strchr(odbpath, '[') + 1, sizeof(str));
            if (strchr(str, ']'))
               *strchr(str, ']') = 0;
            *index1 = std::stoi(eval_var(seq, str));

            *strchr(odbpath, '[') = 0;
         } else {
            //standard expansion
            strarrayindex(odbpath, index1, index2);
         }
      }
   }
}

/*------------------------------------------------------------------*/

//set all matching keys to a value
int set_all_matching(HNDLE hDB, HNDLE hBaseKey, char *odbpath, char *value, int index1, int index2, int notify) {
   int status, size;
   char data[256];
   KEY key;

   std::vector<HNDLE> keys;
   status = db_find_keys(hDB, hBaseKey, odbpath, keys);

   if (status != DB_SUCCESS)
      return status;

   for (HNDLE hKey: keys) {
      db_get_key(hDB, hKey, &key);
      size = sizeof(data);
      db_sscanf(value, data, &size, 0, key.type);

      if (key.num_values > 1 && index1 == -1) {
         for (int i = 0; i < key.num_values; i++)
            status = db_set_data_index1(hDB, hKey, data, key.item_size, i, key.type, notify);
      } else if (key.num_values > 1 && index2 > index1) {
         for (int i = index1; i < key.num_values && i <= index2; i++)
            status = db_set_data_index1(hDB, hKey, data, key.item_size, i, key.type, notify);
      } else
         status = db_set_data_index1(hDB, hKey, data, key.item_size, index1, key.type, notify);

      if (status != DB_SUCCESS) {
         return status;
      }
   }

   return DB_SUCCESS;
}

/*------------------------------------------------------------------*/

void sequencer() {
   PMXML_NODE pn, pr, pt, pe;
   char odbpath[256], value[256], data[256], str[1024], name[32], op[32];
   char list[100][XNAME_LENGTH];
   int i, j, l, n, status, size, index1, index2, state, run_number, cont;
   HNDLE hDB, hKey, hKeySeq;
   KEY key;
   double d;
   float v;

   if (!seq.running || seq.paused) {
      ss_sleep(10);
      return;
   }

   if (pnseq == NULL) {
      seq_stop();
      strlcpy(seq.error, "No script loaded", sizeof(seq.error));
      seq_write(seq);
      ss_sleep(10);
      return;
   }

   cm_get_experiment_database(&hDB, NULL);
   db_find_key(hDB, 0, "/Sequencer/State", &hKeySeq);
   if (!hKeySeq)
      return;

   // NB seq.last_msg is not used anywhere.
   //cm_msg_retrieve(1, str, sizeof(str));
   //str[19] = 0;
   //strcpy(seq.last_msg, str+11);

   pr = mxml_find_node(pnseq, "RunSequence");
   if (!pr) {
      seq_error(seq, "Cannot find <RunSequence> tag in XML file");
      return;
   }

   int last_line = mxml_get_line_number_end(pr);

   /* check for Subroutine end */
   if (seq.stack_index > 0 && seq.current_line_number == seq.subroutine_end_line[seq.stack_index - 1]) {
      size = sizeof(seq);
      db_get_record(hDB, hKeySeq, &seq, &size, 0);
      seq.subroutine_end_line[seq.stack_index - 1] = 0;
      seq.current_line_number = seq.subroutine_return_line[seq.stack_index - 1];
      seq.subroutine_return_line[seq.stack_index - 1] = 0;
      seq.subroutine_call_line[seq.stack_index - 1] = 0;
      seq.ssubroutine_call_line[seq.stack_index - 1] = 0;
      seq.stack_index--;
      db_set_record(hDB, hKeySeq, &seq, sizeof(seq), 0);
      return;
   }

   /* check for last line of script */
   if (seq.current_line_number > last_line) {
      size = sizeof(seq);
      db_get_record(hDB, hKeySeq, &seq, &size, 0);
      seq_clear(seq);
      seq.finished = TRUE;
      seq_write(seq);

      cm_msg(MTALK, "sequencer", "Sequencer is finished.");
      return;
   }

   /* check for loop end */
   for (i = 3; i >= 0; i--)
      if (seq.loop_start_line[i] > 0)
         break;
   if (i >= 0) {
      if (seq.current_line_number == seq.loop_end_line[i]) {
         size = sizeof(seq);
         db_get_record(hDB, hKeySeq, &seq, &size, 0);

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
                  strlcpy(value, eval_var(seq, list[seq.loop_counter[i]]).c_str(), sizeof(value));
               } else if (mxml_get_attribute(pn, "n")) {
                  sprintf(value, "%d", seq.loop_counter[i] + 1);
               }
               sprintf(str, "/Sequencer/Variables/%s", name);
               size = strlen(value) + 1;
               if (size < 32)
                  size = 32;
               db_set_value(hDB, 0, str, value, size, 1, TID_STRING);
            }
            seq.loop_counter[i]++;
            seq.current_line_number = seq.loop_start_line[i] + 1;
         }
         db_set_record(hDB, hKeySeq, &seq, sizeof(seq), 0);
         return;
      }
   }

   /* check for end of "if" statement */
   if (seq.if_index > 0 && seq.current_line_number == seq.if_endif_line[seq.if_index - 1]) {
      size = sizeof(seq);
      db_get_record(hDB, hKeySeq, &seq, &size, 0);
      seq.if_index--;
      seq.if_line[seq.if_index] = 0;
      seq.if_else_line[seq.if_index] = 0;
      seq.if_endif_line[seq.if_index] = 0;
      seq.current_line_number++;
      db_set_record(hDB, hKeySeq, &seq, sizeof(seq), 0);
      return;
   }

   /* check for ODBSubdir end */
   if (seq.current_line_number == seq.subdir_end_line) {
      size = sizeof(seq);
      db_get_record(hDB, hKeySeq, &seq, &size, 0);
      seq.subdir_end_line = 0;
      seq.subdir[0] = 0;
      seq.subdir_not_notify = FALSE;
      db_set_record(hDB, hKeySeq, &seq, sizeof(seq), 0);
      return;
   }

   /* find node belonging to current line */
   pn = mxml_get_node_at_line(pnseq, seq.current_line_number);
   if (!pn) {
      size = sizeof(seq);
      db_get_record(hDB, hKeySeq, &seq, &size, 0);
      seq.current_line_number++;
      db_set_record(hDB, hKeySeq, &seq, sizeof(seq), 0);
      return;
   }

   /* set MSL line from current element */
   if (mxml_get_attribute(pn, "l"))
      seq.scurrent_line_number = atoi(mxml_get_attribute(pn, "l"));

   // out-comment following lines for debug output
//   midas::odb o("/Sequencer/Script/Lines");
//   std::string s = o[seq.scurrent_line_number-1];
//   printf("%3d: %s\n", seq.scurrent_line_number, s.c_str());

   if (equal_ustring(mxml_get_name(pn), "PI") || equal_ustring(mxml_get_name(pn), "RunSequence") ||
       equal_ustring(mxml_get_name(pn), "Comment")) {
      // just skip
      seq.current_line_number++;
   }

      /*---- ODBSubdir ----*/
   else if (equal_ustring(mxml_get_name(pn), "ODBSubdir")) {
      if (!mxml_get_attribute(pn, "path")) {
         seq_error(seq, "Missing attribute \"path\"");
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
         seq_error(seq, "Missing attribute \"path\"");
      } else {
         strlcpy(odbpath, seq.subdir, sizeof(odbpath));
         if (strlen(odbpath) > 0 && odbpath[strlen(odbpath) - 1] != '/')
            strlcat(odbpath, "/", sizeof(odbpath));
         strlcat(odbpath, mxml_get_attribute(pn, "path"), sizeof(odbpath));

         int notify = TRUE;
         if (seq.subdir_not_notify)
            notify = FALSE;
         if (mxml_get_attribute(pn, "notify"))
            notify = atoi(mxml_get_attribute(pn, "notify"));

         index1 = index2 = 0;
         seq_array_index(odbpath, &index1, &index2);

         strlcpy(value, eval_var(seq, mxml_get_value(pn)).c_str(), sizeof(value));

         status = set_all_matching(hDB, 0, odbpath, value, index1, index2, notify);

         if (status == DB_SUCCESS) {
            size = sizeof(seq);
            db_get_record1(hDB, hKeySeq, &seq, &size, 0, strcomb1(sequencer_str).c_str());// could have changed seq tree
            seq.current_line_number++;
         } else if (status == DB_NO_KEY) {
            sprintf(str, "ODB key \"%s\" not found", odbpath);
            seq_error(seq, str);
         } else {
            //something went really wrong
            sprintf(str, "Internal error %d", status);
            seq_error(seq, str);
            return;
         }
      }
   }

      /*---- ODBLoad ----*/
   else if (equal_ustring(mxml_get_name(pn), "ODBLoad")) {
      if (mxml_get_value(pn)[0] == '/') {
         //absolute path
         strlcpy(value, mxml_get_value(pn), sizeof(value));

      } else if (mxml_get_value(pn)[0] == '$') {
         //path relative to the one set in /Sequencer/Path
         strlcpy(value, seq.path, sizeof(value));
         strlcat(value, mxml_get_value(pn), sizeof(value));
         *strchr(value, '$') = '/';

      } else {
         //relative path to msl file
         strlcpy(value, seq.path, sizeof(value));
         strlcat(value, seq.filename, sizeof(value));
         char *fullpath = strrchr(value, '/');
         if (fullpath)
            *(++fullpath) = '\0';
         strlcat(value, mxml_get_value(pn), sizeof(value));
      }

      //if path attribute is given
      if (mxml_get_attribute(pn, "path")) {
         strlcpy(odbpath, seq.subdir, sizeof(odbpath));
         if (strlen(odbpath) > 0 && odbpath[strlen(odbpath) - 1] != '/')
            strlcat(odbpath, "/", sizeof(odbpath));
         strlcat(odbpath, mxml_get_attribute(pn, "path"), sizeof(odbpath));

         //load at that key, if exists
         status = db_find_key(hDB, 0, odbpath, &hKey);
         if (status != DB_SUCCESS) {
            char errorstr[512];
            sprintf(errorstr, "Cannot find ODB key \"%s\"", odbpath);
            seq_error(seq, errorstr);
            return;
         } else {
            status = db_load(hDB, hKey, value, FALSE);
         }
      } else {
         //otherwise load at root
         status = db_load(hDB, 0, value, FALSE);
      }

      if (status == DB_SUCCESS) {
         size = sizeof(seq);
         db_get_record1(hDB, hKeySeq, &seq, &size, 0, strcomb1(sequencer_str).c_str());// could have changed seq tree
         seq.current_line_number++;
      } else if (status == DB_FILE_ERROR) {
         sprintf(str, "Error reading file \"%s\"", value);
         seq_error(seq, str);
      } else {
         //something went really wrong
         seq_error(seq, "Internal error loading ODB file!");
         return;
      }
   }

      /*---- ODBGet ----*/
   else if (equal_ustring(mxml_get_name(pn), "ODBGet")) {
      if (!mxml_get_attribute(pn, "path")) {
         seq_error(seq, "Missing attribute \"path\"");
      } else {
         strlcpy(odbpath, seq.subdir, sizeof(odbpath));
         if (strlen(odbpath) > 0 && odbpath[strlen(odbpath) - 1] != '/')
            strlcat(odbpath, "/", sizeof(odbpath));
         strlcat(odbpath, mxml_get_attribute(pn, "path"), sizeof(odbpath));

         /* check if index is supplied */
         index1 = index2 = 0;
         seq_array_index(odbpath, &index1, &index2);

         strlcpy(name, mxml_get_value(pn), sizeof(name));
         status = db_find_key(hDB, 0, odbpath, &hKey);
         if (status != DB_SUCCESS) {
            char errorstr[512];
            sprintf(errorstr, "Cannot find ODB key \"%s\"", odbpath);
            seq_error(seq, errorstr);
            return;
         } else {
            db_get_key(hDB, hKey, &key);
            size = sizeof(data);

            status = db_get_data_index(hDB, hKey, data, &size, index1, key.type);
            if (key.type == TID_BOOL)
               strlcpy(value, *((int *) data) > 0 ? "1" : "0", sizeof(value));
            else
               db_sprintf(value, data, size, 0, key.type);

            sprintf(str, "/Sequencer/Variables/%s", name);
            size = strlen(value) + 1;
            if (size < 32)
               size = 32;
            db_set_value(hDB, 0, str, value, size, 1, TID_STRING);

            size = sizeof(seq);
            db_get_record1(hDB, hKeySeq, &seq, &size, 0, strcomb1(sequencer_str).c_str());// could have changed seq tree
            seq.current_line_number = mxml_get_line_number_end(pn) + 1;
         }
      }
   }

      /*---- ODBInc ----*/
   else if (equal_ustring(mxml_get_name(pn), "ODBInc")) {
      if (!mxml_get_attribute(pn, "path")) {
         seq_error(seq, "Missing attribute \"path\"");
      } else {
         strlcpy(odbpath, seq.subdir, sizeof(odbpath));
         if (strlen(odbpath) > 0 && odbpath[strlen(odbpath) - 1] != '/')
            strlcat(odbpath, "/", sizeof(odbpath));
         strlcat(odbpath, mxml_get_attribute(pn, "path"), sizeof(odbpath));
         index1 = index2 = 0;
         seq_array_index(odbpath, &index1, &index2);

         strlcpy(value, eval_var(seq, mxml_get_value(pn)).c_str(), sizeof(value));

         status = db_find_key(hDB, 0, odbpath, &hKey);
         if (status != DB_SUCCESS) {
            char errorstr[512];
            sprintf(errorstr, "Cannot find ODB key \"%s\"", odbpath);
            seq_error(seq, errorstr);
         } else {
            db_get_key(hDB, hKey, &key);
            size = sizeof(data);
            db_get_data_index(hDB, hKey, data, &size, index1, key.type);
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

            db_set_data_index1(hDB, hKey, data, key.item_size, index1, key.type, notify);
            seq.current_line_number++;
         }
      }
   }

      /*---- ODBDelete ----*/
   else if (equal_ustring(mxml_get_name(pn), "ODBDelete")) {
      strlcpy(odbpath, seq.subdir, sizeof(odbpath));
      if (strlen(odbpath) > 0 && odbpath[strlen(odbpath) - 1] != '/')
         strlcat(odbpath, "/", sizeof(odbpath));
      strlcat(odbpath, mxml_get_value(pn), sizeof(odbpath));

      status = db_find_key(hDB, 0, odbpath, &hKey);
      if (status != DB_SUCCESS) {
         char errorstr[512];
         sprintf(errorstr, "Cannot find ODB key \"%s\"", odbpath);
         seq_error(seq, errorstr);
      } else {
         status = db_delete_key(hDB, hKey, FALSE);
         if (status != DB_SUCCESS) {
            char errorstr[512];
            sprintf(errorstr, "Cannot delete ODB key \"%s\"", odbpath);
            seq_error(seq, errorstr);
         } else
            seq.current_line_number++;
      }
   }

      /*---- ODBCreate ----*/
   else if (equal_ustring(mxml_get_name(pn), "ODBCreate")) {
      if (!mxml_get_attribute(pn, "path")) {
         seq_error(seq, "Missing attribute \"path\"");
      } else if (!mxml_get_attribute(pn, "type")) {
         seq_error(seq, "Missing attribute \"type\"");
      } else {
         strlcpy(odbpath, seq.subdir, sizeof(odbpath));
         if (strlen(odbpath) > 0 && odbpath[strlen(odbpath) - 1] != '/')
            strlcat(odbpath, "/", sizeof(odbpath));
         strlcat(odbpath, mxml_get_attribute(pn, "path"), sizeof(odbpath));

         /* get TID */
         int tid;
         for (tid = 0; tid < TID_LAST; tid++) {
            if (equal_ustring(rpc_tid_name(tid), mxml_get_attribute(pn, "type")))
               break;
         }

         if (tid == TID_LAST)
            seq_error(seq, "Type must be one of UINT8,INT8,UINT16,INT16,UINT32,INT32,BOOL,FLOAT,DOUBLE,STRING");
         else {

            status = db_find_key(hDB, 0, odbpath, &hKey);
            if (status == DB_SUCCESS) {
               db_get_key(hDB, hKey, &key);
               if (key.type != tid) {
                  db_delete_key(hDB, hKey, FALSE);
                  status = db_create_key(hDB, 0, odbpath, tid);
               }
            } else
               status = db_create_key(hDB, 0, odbpath, tid);

            if (status != DB_SUCCESS && status != DB_CREATED) {
               char errorstr[512];
               sprintf(errorstr, "Cannot createODB key \"%s\", error code %d", odbpath, status);
               seq_error(seq, errorstr);
            } else {
               status = db_find_key(hDB, 0, odbpath, &hKey);
               if (mxml_get_attribute(pn, "size") && atoi(mxml_get_attribute(pn, "size")) > 0)
                  db_set_num_values(hDB, hKey, atoi(mxml_get_attribute(pn, "size")));

               seq.current_line_number++;
            }
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
         for (i = 0; i < n; i++) {

            strlcpy(value, eval_var(seq, mxml_get_value(pn)).c_str(), sizeof(value));

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
            db_get_value(hDB, 0, "/Runinfo/State", &state, &size, TID_INT32, FALSE);
            if (state != STATE_RUNNING) {
               size = sizeof(run_number);
               db_get_value(hDB, 0, "/Runinfo/Run number", &run_number, &size, TID_INT32, FALSE);
               status = cm_transition(TR_START, run_number + 1, str, sizeof(str), TR_MTHREAD | TR_SYNC, FALSE);
               if (status != CM_SUCCESS) {
                  char errorstr[1500];
                  sprintf(errorstr, "Cannot start run: %s", str);
                  seq_error(seq, errorstr);
               }
            }
         } else {
            // Wait until transition has finished
            size = sizeof(state);
            db_get_value(hDB, 0, "/Runinfo/State", &state, &size, TID_INT32, FALSE);
            if (state == STATE_RUNNING) {
               seq.transition_request = FALSE;
               seq.current_line_number++;
            }
         }
      } else if (equal_ustring(mxml_get_value(pn), "Stop")) {
         if (!seq.transition_request) {
            seq.transition_request = TRUE;
            size = sizeof(state);
            db_get_value(hDB, 0, "/Runinfo/State", &state, &size, TID_INT32, FALSE);
            if (state != STATE_STOPPED) {
               status = cm_transition(TR_STOP, 0, str, sizeof(str), TR_MTHREAD | TR_SYNC, FALSE);
               if (status == CM_DEFERRED_TRANSITION) {
                  // do nothing
               } else if (status != CM_SUCCESS) {
                  char errorstr[1500];
                  sprintf(errorstr, "Cannot stop run: %s", str);
                  seq_error(seq, errorstr);
               }
            }
         } else {
            // Wait until transition has finished
            size = sizeof(state);
            db_get_value(hDB, 0, "/Runinfo/State", &state, &size, TID_INT32, FALSE);
            if (state == STATE_STOPPED) {
               size = sizeof(seq);
               db_get_record(hDB, hKeySeq, &seq, &size, 0);

               seq.transition_request = FALSE;

               if (seq.stop_after_run) {
                  seq.stop_after_run = FALSE;
                  seq.running = FALSE;
                  seq.finished = TRUE;
                  seq_stop();
                  cm_msg(MTALK, "sequencer", "Sequencer is finished by \"stop after current run\".");
               } else {
                  seq.current_line_number++;
               }

               db_set_record(hDB, hKeySeq, &seq, sizeof(seq), 0);
            }
         }
      } else {
         sprintf(str, "Invalid transition \"%s\"", mxml_get_value(pn));
         seq_error(seq, str);
         return;
      }
   }

      /*---- Wait ----*/
   else if (equal_ustring(mxml_get_name(pn), "Wait")) {
      if (equal_ustring(mxml_get_attribute(pn, "for"), "Events")) {
         n = std::stoi(eval_var(seq, mxml_get_value(pn)));
         seq.wait_limit = (float) n;
         strcpy(seq.wait_type, "Events");
         size = sizeof(d);
         db_get_value(hDB, 0, "/Equipment/Trigger/Statistics/Events sent", &d, &size, TID_DOUBLE, FALSE);
         seq.wait_value = (float) d;
         if (d >= n) {
            seq.current_line_number = mxml_get_line_number_end(pn) + 1;
            seq.wait_limit = 0;
            seq.wait_value = 0;
            seq.wait_type[0] = 0;
         }
         seq.wait_value = (float) d;
      } else if (equal_ustring(mxml_get_attribute(pn, "for"), "ODBValue")) {
         v = (float) std::stod(eval_var(seq, mxml_get_value(pn)));
         seq.wait_limit = v;
         strcpy(seq.wait_type, "ODB");
         if (!mxml_get_attribute(pn, "path")) {
            seq_error(seq, "\"path\" must be given for ODB values");
            return;
         } else {
            strlcpy(odbpath, mxml_get_attribute(pn, "path"), sizeof(odbpath));
            index1 = index2 = 0;
            seq_array_index(odbpath, &index1, &index2);
            status = db_find_key(hDB, 0, odbpath, &hKey);
            if (status != DB_SUCCESS) {
               char errorstr[512];
               sprintf(errorstr, "Cannot find ODB key \"%s\"", odbpath);
               seq_error(seq, errorstr);
               return;
            } else {
               if (mxml_get_attribute(pn, "op"))
                  strlcpy(op, mxml_get_attribute(pn, "op"), sizeof(op));
               else
                  strcpy(op, "!=");
               strlcat(seq.wait_type, op, sizeof(seq.wait_type));

               db_get_key(hDB, hKey, &key);
               size = sizeof(data);
               db_get_data_index(hDB, hKey, data, &size, index1, key.type);
               if (key.type == TID_BOOL)
                  strlcpy(str, *((int *) data) > 0 ? "1" : "0", sizeof(str));
               else
                  db_sprintf(str, data, size, 0, key.type);
               cont = FALSE;
               seq.wait_value = (float) atof(str);
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
                  seq_error(seq, str);
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
         seq.wait_limit = (float) std::stoi(eval_var(seq, mxml_get_value(pn)));;
         strcpy(seq.wait_type, "Seconds");
         if (seq.start_time == 0) {
            seq.start_time = ss_time();
            seq.wait_value = 0;
         } else {
            seq.wait_value = (float) (ss_time() - seq.start_time);
            if (seq.wait_value > seq.wait_limit)
               seq.wait_value = seq.wait_limit;
         }
         if (ss_time() - seq.start_time > (DWORD) seq.wait_limit) {
            seq.current_line_number++;
            seq.start_time = 0;
            seq.wait_limit = 0;
            seq.wait_value = 0;
            seq.wait_type[0] = 0;
         }
      } else {
         sprintf(str, "Invalid wait attribute \"%s\"", mxml_get_attribute(pn, "for"));
         seq_error(seq, str);
      }

      // sleep to keep the CPU from consuming 100%
      ss_sleep(100);
   }

      /*---- Loop start ----*/
   else if (equal_ustring(mxml_get_name(pn), "Loop")) {
      for (i = 0; i < 4; i++)
         if (seq.loop_start_line[i] == 0)
            break;
      if (i == 4) {
         seq_error(seq, "Maximum loop nesting exceeded");
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
            seq.loop_n[i] = std::stoi(eval_var(seq, mxml_get_attribute(pn, "n")));
         }
         strlcpy(value, "1", sizeof(value));
      } else if (mxml_get_attribute(pn, "values")) {
         strlcpy(data, mxml_get_attribute(pn, "values"), sizeof(data));
         seq.loop_n[i] = strbreak(data, list, 100, ",", FALSE);
         strlcpy(value, eval_var(seq, list[0]).c_str(), sizeof(value));
      } else {
         seq_error(seq, "Missing \"var\" or \"n\" attribute");
         return;
      }

      if (mxml_get_attribute(pn, "var")) {
         strlcpy(name, mxml_get_attribute(pn, "var"), sizeof(name));
         sprintf(str, "/Sequencer/Variables/%s", name);
         size = strlen(value) + 1;
         if (size < 32)
            size = 32;
         db_set_value(hDB, 0, str, value, size, 1, TID_STRING);
      }

      seq.current_line_number++;
   }

      /*---- If ----*/
   else if (equal_ustring(mxml_get_name(pn), "If")) {

      if (seq.if_index == 4) {
         seq_error(seq, "Maximum number of nexted if..endif exceeded");
         return;
      }

      // store if, else and endif lines
      seq.if_line[seq.if_index] = seq.current_line_number;
      seq.if_endif_line[seq.if_index] = mxml_get_line_number_end(pn);

      seq.if_else_line[seq.if_index] = 0;
      for (j = seq.current_line_number + 1; j < mxml_get_line_number_end(pn) + 1; j++) {
         pe = mxml_get_node_at_line(pnseq, j);
         if (pe && equal_ustring(mxml_get_name(pe), "Else")) {
            seq.if_else_line[seq.if_index] = j;
            break;
         }
      }

      strlcpy(str, mxml_get_attribute(pn, "condition"), sizeof(str));
      i = eval_condition(seq, str);
      if (i < 0) {
         seq_error(seq, "Invalid number in comparison");
         return;
      }

      if (i == 1)
         seq.current_line_number++;
      else if (seq.if_else_line[seq.if_index])
         seq.current_line_number = seq.if_else_line[seq.if_index] + 1;
      else
         seq.current_line_number = seq.if_endif_line[seq.if_index];

      seq.if_index++;
   }

      /*---- Else ----*/
   else if (equal_ustring(mxml_get_name(pn), "Else")) {
      // goto next "Endif"
      if (seq.if_index == 0) {
         seq_error(seq, "Unexpected Else");
         return;
      }
      seq.current_line_number = seq.if_endif_line[seq.if_index - 1];
   }

      /*---- Goto ----*/
   else if (equal_ustring(mxml_get_name(pn), "Goto")) {
      if (!mxml_get_attribute(pn, "line") && !mxml_get_attribute(pn, "sline")) {
         seq_error(seq, "Missing line number");
         return;
      }
      if (mxml_get_attribute(pn, "line")) {
         seq.current_line_number = std::stoi(eval_var(seq, mxml_get_attribute(pn, "line")));
      }
      if (mxml_get_attribute(pn, "sline")) {
         strlcpy(str, eval_var(seq, mxml_get_attribute(pn, "sline")).c_str(), sizeof(str));
         for (i = 0; i < last_line; i++) {
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
         seq_error(seq, "Missing variable name");
         return;
      }
      strlcpy(name, mxml_get_attribute(pn, "name"), sizeof(name));
      strlcpy(value, eval_var(seq, mxml_get_value(pn)).c_str(), sizeof(value));
      sprintf(str, "/Sequencer/Variables/%s", name);
      size = strlen(value) + 1;
      if (size < 32)
         size = 32;
      db_set_value(hDB, 0, str, value, size, 1, TID_STRING);

      // check if variable is used in loop
      for (i = 3; i >= 0; i--)
         if (seq.loop_start_line[i] > 0) {
            pr = mxml_get_node_at_line(pnseq, seq.loop_start_line[i]);
            if (mxml_get_attribute(pr, "var")) {
               if (equal_ustring(mxml_get_attribute(pr, "var"), name))
                  seq.loop_counter[i] = atoi(value);
            }
         }

      seq.current_line_number = mxml_get_line_number_end(pn) + 1;
   }

      /*---- Message ----*/
   else if (equal_ustring(mxml_get_name(pn), "Message")) {
      if (strchr(mxml_get_value(pn), '$')) // evaluate message string if $ present
         strlcpy(value, eval_var(seq, mxml_get_value(pn)).c_str(), sizeof(value));
      else
         strlcpy(value, mxml_get_value(pn), sizeof(value)); // treast message as sting
      const char *wait_attr = mxml_get_attribute(pn, "wait");
      bool wait = false;
      if (wait_attr)
         wait = (atoi(wait_attr) == 1);

      if (!wait) {
         // message with no wait: set seq.message and move on. we do not care if web page clears it
         strlcpy(seq.message, value, sizeof(seq.message));
         seq.message_wait = FALSE;
         db_set_record(hDB, hKeySeq, &seq, sizeof(seq), 0);
      } else {
         // message with wait

         // if message_wait not set, we are here for the first time
         if (!seq.message_wait) {
            strlcpy(seq.message, value, sizeof(seq.message));
            seq.message_wait = TRUE;
            db_set_record(hDB, hKeySeq, &seq, sizeof(seq), 0);
            // wait
            return;
         } else {
            // message_wait is set, we have been here before

            // if web page did not clear the message, keep waiting
            if (seq.message[0] != 0) {
               // wait
               return;
            }

            // web page cleared the message, we are done with waiting
            seq.message_wait = false;
         }
      }

      seq.current_line_number = mxml_get_line_number_end(pn) + 1;
   }

      /*---- Cat ----*/
   else if (equal_ustring(mxml_get_name(pn), "Cat")) {
      if (!mxml_get_attribute(pn, "name")) {
         seq_error(seq, "Missing variable name");
         return;
      }
      strlcpy(name, mxml_get_attribute(pn, "name"), sizeof(name));
      if (!concatenate(seq, value, sizeof(value), mxml_get_value(pn)))
         return;
      sprintf(str, "/Sequencer/Variables/%s", name);
      size = strlen(value) + 1;
      if (size < 32)
         size = 32;
      db_set_value(hDB, 0, str, value, size, 1, TID_STRING);

      seq.current_line_number = mxml_get_line_number_end(pn) + 1;
   }

      /*---- Call ----*/
   else if (equal_ustring(mxml_get_name(pn), "Call")) {
      if (seq.stack_index == 4) {
         seq_error(seq, "Maximum subroutine level exceeded");
         return;
      } else {
         // put current line number on stack
         seq.subroutine_call_line[seq.stack_index] = mxml_get_line_number_end(pn);
         seq.ssubroutine_call_line[seq.stack_index] = atoi(mxml_get_attribute(pn, "l"));
         seq.subroutine_return_line[seq.stack_index] = mxml_get_line_number_end(pn) + 1;

         // search subroutine
         for (i = 1; i < mxml_get_line_number_end(mxml_find_node(pnseq, "RunSequence")); i++) {
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
                     seq.stack_index++;
                     break;
                  }
               }
            }
         }
         if (i == mxml_get_line_number_end(mxml_find_node(pnseq, "RunSequence"))) {
            sprintf(str, "Subroutine '%s' not found", mxml_get_attribute(pn, "name"));
            seq_error(seq, str);
         }
      }
   }

      /*---- <unknown> ----*/
   else {
      sprintf(str, "Unknown statement \"%s\"", mxml_get_name(pn));
      seq_error(seq, str);
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
      else if (mxml_get_attribute(pn, "l"))
         seq.scurrent_line_number = atoi(mxml_get_attribute(pn, "l"));
   }

   /* get steering parameters, since they might have been changed in between */
   SEQUENCER seq1;
   size = sizeof(seq1);
   db_get_record(hDB, hKeySeq, &seq1, &size, 0);
   seq.running = seq1.running;
   seq.finished = seq1.finished;
   seq.paused = seq1.paused;
   seq.stop_after_run = seq1.stop_after_run;
   strlcpy(seq.message, seq1.message, sizeof(seq.message));

   /* update current line number */
   db_set_record(hDB, hKeySeq, &seq, sizeof(seq), 0);
}

/*------------------------------------------------------------------*/

void init_sequencer() {
   int status;
   HNDLE hDB;
   HNDLE hKey;
   char str[256];
   //SEQUENCER seq;
   SEQUENCER_STR(sequencer_str);

   cm_get_experiment_database(&hDB, NULL);

   status = db_check_record(hDB, 0, "/Sequencer/State", strcomb1(sequencer_str).c_str(), TRUE);
   if (status == DB_STRUCT_MISMATCH) {
      cm_msg(MERROR, "init_sequencer",
             "Sequencer error: mismatching /Sequencer/State structure, db_check_record() status %d", status);
      return;
   }

   status = db_find_key(hDB, 0, "/Sequencer/State", &hKey);
   if (status != DB_SUCCESS) {
      cm_msg(MERROR, "init_sequencer", "Sequencer error: Cannot find /Sequencer/State, db_find_key() status %d",
             status);
      return;
   }

   int size = sizeof(seq);
   status = db_get_record1(hDB, hKey, &seq, &size, 0, strcomb1(sequencer_str).c_str());
   if (status != DB_SUCCESS) {
      cm_msg(MERROR, "init_sequencer", "Sequencer error: Cannot get /Sequencer/State, db_get_record1() status %d",
             status);
      return;
   }

   if (seq.path[0] == 0) {
      // NOTE: this code must match identical code in mhttpd!
      const char *s = getenv("MIDASSYS");
      if (s) {
         strlcpy(seq.path, s, sizeof(seq.path));
         strlcat(seq.path, "/examples/sequencer/", sizeof(seq.path));
      } else {
         strlcpy(seq.path, cm_get_path().c_str(), sizeof(seq.path));
      }
   }

   if (strlen(seq.path) > 0 && seq.path[strlen(seq.path) - 1] != DIR_SEPARATOR) {
      strlcat(seq.path, DIR_SEPARATOR_STR, sizeof(seq.path));
   }

   if (seq.filename[0]) {
      strlcpy(str, seq.path, sizeof(str));
      strlcat(str, seq.filename, sizeof(str));
      seq_open_file(hDB, str, seq);
   }

   seq.transition_request = FALSE;

   db_set_record(hDB, hKey, &seq, sizeof(seq), 0);

   status = db_watch(hDB, hKey, seq_watch, NULL);
   if (status != DB_SUCCESS) {
      cm_msg(MERROR, "init_sequencer", "Sequencer error: Cannot watch /Sequencer/State, db_watch() status %d", status);
      return;
   }

   bool b = false;
   gOdb->RB("Sequencer/Command/Start script", &b, true);
   b = false;
   gOdb->RB("Sequencer/Command/Stop immediately", &b, true);
   b = false;
   gOdb->RB("Sequencer/Command/Load new file", &b, true);
   std::string s;
   gOdb->RS("Sequencer/Command/Load filename", &s, true);

   status = db_find_key(hDB, 0, "/Sequencer/Command", &hKey);
   if (status != DB_SUCCESS) {
      cm_msg(MERROR, "init_sequencer", "Sequencer error: Cannot find /Sequencer/Command, db_find_key() status %d",
             status);
      return;
   }

   status = db_watch(hDB, hKey, seq_watch_command, NULL);
   if (status != DB_SUCCESS) {
      cm_msg(MERROR, "init_sequencer", "Sequencer error: Cannot watch /Sequencer/Command, db_watch() status %d",
             status);
      return;
   }
}

/*------------------------------------------------------------------*/

int main(int argc, const char *argv[]) {
   int daemon = FALSE;
   int status, ch;
   char midas_hostname[256];
   char midas_expt[256];

   setbuf(stdout, NULL);
   setbuf(stderr, NULL);
#ifdef SIGPIPE
   /* avoid getting killed by "Broken pipe" signals */
   signal(SIGPIPE, SIG_IGN);
#endif

   /* get default from environment */
   cm_get_environment(midas_hostname, sizeof(midas_hostname), midas_expt, sizeof(midas_expt));

   /* parse command line parameters */
   for (int i = 1; i < argc; i++) {
      if (argv[i][0] == '-' && argv[i][1] == 'D') {
         daemon = TRUE;
      } else if (argv[i][0] == '-') {
         if (i + 1 >= argc || argv[i + 1][0] == '-')
            goto usage;
         if (argv[i][1] == 'h')
            strlcpy(midas_hostname, argv[++i], sizeof(midas_hostname));
         else if (argv[i][1] == 'e')
            strlcpy(midas_expt, argv[++i], sizeof(midas_hostname));
      } else {
         usage:
         printf("usage: %s [-h Hostname[:port]] [-e Experiment] [-D]\n\n", argv[0]);
         printf("       -e experiment to connect to\n");
         printf("       -h connect to midas server (mserver) on given host\n");
         printf("       -D become a daemon\n");
         return 0;
      }
   }

   if (daemon) {
      printf("Becoming a daemon...\n");
      ss_daemon_init(FALSE);
   }

#ifdef OS_LINUX
   /* write PID file */
   FILE *f = fopen("/var/run/mhttpd.pid", "w");
   if (f != NULL) {
      fprintf(f, "%d", ss_getpid());
      fclose(f);
   }
#endif

   /*---- connect to experiment ----*/
   status = cm_connect_experiment1(midas_hostname, midas_expt, "Sequencer", NULL,
                                   DEFAULT_ODB_SIZE, DEFAULT_WATCHDOG_TIMEOUT);
   if (status == CM_WRONG_PASSWORD)
      return 1;
   else if (status == DB_INVALID_HANDLE) {
      std::string s = cm_get_error(status);
      puts(s.c_str());
   } else if (status != CM_SUCCESS) {
      std::string s = cm_get_error(status);
      puts(s.c_str());
      return 1;
   }

   HNDLE hDB;
   cm_get_experiment_database(&hDB, NULL);
   gOdb = MakeMidasOdb(hDB);

   init_sequencer();

   printf("Sequencer started. Stop with \"!\"\n");

   // if any commands are active, process them now
   seq_watch_command(hDB, 0, 0, NULL);

   /* initialize ss_getchar */
   ss_getchar(0);

   /* main loop */
   do {
      try {
         sequencer();
      } catch (std::string &msg) {
         seq_error(seq, msg.c_str());
      } catch (const char *msg) {
         seq_error(seq, msg);
      }

      status = cm_yield(0);

      ch = 0;
      while (ss_kbhit()) {
         ch = ss_getchar(0);
         if (ch == -1)
            ch = getchar();

         if ((char) ch == '!')
            break;
      }

   } while (status != RPC_SHUTDOWN && ch != '!');

   /* reset terminal */
   ss_getchar(TRUE);

   /* close network connection to server */
   cm_disconnect_experiment();

   return 0;
}

/*------------------------------------------------------------------*/

/**dox***************************************************************/
/** @} */ /* end of alfunctioncode */

/* emacs
 * Local Variables:
 * tab-width: 8
 * c-basic-offset: 3
 * indent-tabs-mode: nil
 * End:
 */
