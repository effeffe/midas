/********************************************************************\

  Name:         ODB.C
  Created by:   Stefan Ritt

  Contents:     MIDAS online database functions

  $Id$

\********************************************************************/

#undef NDEBUG // midas required assert() to be always enabled

/**dox***************************************************************/
/** @file odb.c
The Online Database file
*/

/** @defgroup odbfunctionc ODB Functions (db_xxx)
 */

/**dox***************************************************************/
/** @addtogroup odbfunctionc
*
 *  @{  */

/**dox***************************************************************/
#ifndef DOXYGEN_SHOULD_SKIP_THIS

#include "midas.h"
#include "msystem.h"
#include "mxml.h"
#include "git-revision.h"

#ifndef HAVE_STRLCPY
#include "strlcpy.h"
#endif
#include <assert.h>
#include <signal.h>
#include <math.h>

#define CHECK_OPEN_RECORD 1

/*------------------------------------------------------------------*/

/********************************************************************\
*                                                                    *
*                 db_xxx  -  Database Functions                      *
*                                                                    *
\********************************************************************/

/* Globals */

static DATABASE *_database;
static INT _database_entries = 0;

static RECORD_LIST *_record_list;
static INT _record_list_entries = 0;

static WATCH_LIST *_watch_list;
static INT _watch_list_entries = 0;

INT db_save_xml_key(HNDLE hDB, HNDLE hKey, INT level, MXML_WRITER * writer);

/*------------------------------------------------------------------*/

#ifdef LOCAL_ROUTINES
typedef struct db_err_msg_struct db_err_msg;
static void db_msg(db_err_msg** msg, INT message_type, const char *filename, INT line, const char *routine, const char *format, ...) MATTRPRINTF(6,7);
static void db_print_msg(const db_err_msg* msg);
static void db_flush_msg(db_err_msg** msg);
static INT db_find_key_locked(const DATABASE_HEADER *pheader, HNDLE hKey, const char *key_name, HNDLE * subhKey, db_err_msg** msg);
static INT db_get_path_locked(const DATABASE_HEADER* pheader, HNDLE hKey, char *path, INT buf_size);
#endif // LOCAL_ROUTINES

/*------------------------------------------------------------------*/

/********************************************************************\
*                                                                    *
*            db_msg_xxx error message handling                       *
*                                                                    *
\********************************************************************/

struct db_err_msg_struct
{
   db_err_msg *next;
   int message_type;
   char filename[256];
   int line;
   char routine[256];
   char text[1];
};

static db_err_msg* _last_error_message = NULL; // for debuging core dumps

void db_print_msg(const db_err_msg* msg)
{
   while (msg != NULL) {
      printf("db_err_msg: %p, next %p, type %d, file \'%s:%d\', function \'%s\': %s\n", msg, msg->next, msg->message_type, msg->filename, msg->line, msg->routine, msg->text);
      msg = msg->next;
   }
}

void db_msg(db_err_msg** msgp, INT message_type, const char *filename, INT line, const char *routine, const char *format, ...)
{
   va_list argptr;
   char message[1000];

   /* print argument list into message */
   va_start(argptr, format);
   vsnprintf(message, sizeof(message)-1, format, argptr);
   va_end(argptr);
   message[sizeof(message)-1] = 0; // ensure string is NUL-terminated

   int len = strlen(message)+1;
   int size = sizeof(db_err_msg) + len;

   db_err_msg* msg = (db_err_msg*)malloc(size);

   msg->next = NULL;
   msg->message_type = message_type;
   strlcpy(msg->filename, filename, sizeof(msg->filename));
   msg->line = line;
   strlcpy(msg->routine, routine, sizeof(msg->routine));
   memcpy(&msg->text[0], message, len);

   _last_error_message = msg;

   //printf("new message:\n");
   //db_print_msg(msg);

   if (*msgp == NULL) {
      *msgp = msg;
      return;
   }

   // append new message to the end of the list
   db_err_msg *m = (*msgp);
   while (m->next != NULL) {
      m = m->next;
   }
   assert(m->next == NULL);
   m->next = msg;

   //printf("Message list with added new message:\n");
   //db_print_msg(*msgp);
   return;
}

void db_flush_msg(db_err_msg** msgp)
{
   db_err_msg *msg = *msgp;
   *msgp = NULL;

   if (/* DISABLES CODE */ (0)) {
      printf("db_flush_msg: %p\n", msg);
      db_print_msg(msg);
   }

   while (msg != NULL) {
      cm_msg(msg->message_type, msg->filename, msg->line, msg->routine, "%s", msg->text);
      db_err_msg* next = msg->next;
      free(msg);
      msg = next;
   }
}

/*------------------------------------------------------------------*/

/********************************************************************\
*                                                                    *
*            Shared Memory Allocation                                *
*                                                                    *
\********************************************************************/

static int validate_free_key(DATABASE_HEADER * pheader, int free_key)
{
   if (free_key <= 0)
      return 0;

   if (free_key > pheader->key_size)
      return 0;

   return 1;
}

/*------------------------------------------------------------------*/
static void *malloc_key(DATABASE_HEADER * pheader, INT size, const char* caller)
{
   FREE_DESCRIP *pfree, *pfound, *pprev = NULL;

   if (size == 0)
      return NULL;

   /* quadword alignment for alpha CPU */
   size = ALIGN8(size);

   if (!validate_free_key(pheader, pheader->first_free_key)) {
      return NULL;
   }

   /* search for free block */
   pfree = (FREE_DESCRIP *) ((char *) pheader + pheader->first_free_key);

   while (pfree->size < size && pfree->next_free) {
      if (!validate_free_key(pheader, pfree->next_free)) {
         return NULL;
      }
      pprev = pfree;
      pfree = (FREE_DESCRIP *) ((char *) pheader + pfree->next_free);
   }

   /* return if not enough memory */
   if (pfree->size < size)
      return 0;

   pfound = pfree;

   /* if found block is first in list, correct pheader */
   if (pfree == (FREE_DESCRIP *) ((char *) pheader + pheader->first_free_key)) {
      if (size < pfree->size) {
         /* free block is only used partially */
         pheader->first_free_key += size;
         pfree = (FREE_DESCRIP *) ((char *) pheader + pheader->first_free_key);

         pfree->size = pfound->size - size;
         pfree->next_free = pfound->next_free;
      } else {
         /* free block is used totally */
         pheader->first_free_key = pfree->next_free;
      }
   } else {
      /* check if free block is used totally */
      if (pfound->size - size < (int) sizeof(FREE_DESCRIP)) {
         /* skip block totally */
         pprev->next_free = pfound->next_free;
      } else {
         /* decrease free block */
         pfree = (FREE_DESCRIP *) ((char *) pfound + size);

         pfree->size = pfound->size - size;
         pfree->next_free = pfound->next_free;

         pprev->next_free = (POINTER_T) pfree - (POINTER_T) pheader;
      }
   }

   assert((void*)pfound != (void*)pheader);

   memset(pfound, 0, size);

   return pfound;
}

/*------------------------------------------------------------------*/
static void free_key(DATABASE_HEADER * pheader, void *address, INT size)
{
   FREE_DESCRIP *pfree, *pprev, *pnext;

   if (size == 0)
      return;

   assert(address != pheader);

   /* quadword alignment for alpha CPU */
   size = ALIGN8(size);

   pfree = (FREE_DESCRIP *) address;
   pprev = NULL;

   /* clear current block */
   memset(address, 0, size);

   /* if key comes before first free block, adjust pheader */
   if ((POINTER_T) address - (POINTER_T) pheader < pheader->first_free_key) {
      pfree->size = size;
      pfree->next_free = pheader->first_free_key;
      pheader->first_free_key = (POINTER_T) address - (POINTER_T) pheader;
   } else {
      /* find last free block before current block */
      pprev = (FREE_DESCRIP *) ((char *) pheader + pheader->first_free_key);

      while (pprev->next_free < (POINTER_T) address - (POINTER_T) pheader) {
         if (pprev->next_free <= 0) {
            cm_msg(MERROR, "free_key", "database is corrupted: pprev=%p, pprev->next_free=%d", pprev, pprev->next_free);
            return;
         }
         pprev = (FREE_DESCRIP *) ((char *) pheader + pprev->next_free);
      }

      pfree->size = size;
      pfree->next_free = pprev->next_free;

      pprev->next_free = (POINTER_T) pfree - (POINTER_T) pheader;
   }

   /* try to melt adjacent free blocks after current block */
   pnext = (FREE_DESCRIP *) ((char *) pheader + pfree->next_free);
   if ((POINTER_T) pnext == (POINTER_T) pfree + pfree->size) {
      pfree->size += pnext->size;
      pfree->next_free = pnext->next_free;

      memset(pnext, 0, pnext->size);
   }

   /* try to melt adjacent free blocks before current block */
   if (pprev && pprev->next_free == (POINTER_T) pprev - (POINTER_T) pheader + pprev->size) {
      pprev->size += pfree->size;
      pprev->next_free = pfree->next_free;

      memset(pfree, 0, pfree->size);
   }
}

static int validate_free_data(DATABASE_HEADER * pheader, int free_data)
{
   if (free_data <= 0)
      return 0;

   if (free_data < (int)sizeof(DATABASE_HEADER)) {
      //printf("validate_free_data: failed: %d is inside the database header 0..%d\n", free_data, (int)sizeof(DATABASE_HEADER));
      return 0;
   }

   if (free_data < (int)sizeof(DATABASE_HEADER) + pheader->key_size) {
      //printf("validate_free_data: failed: %d is inside key space %d..%d\n", free_data, (int)sizeof(DATABASE_HEADER), (int)sizeof(DATABASE_HEADER) + pheader->key_size);
      return 0;
   }

   if (free_data > (int)sizeof(DATABASE_HEADER) + pheader->key_size + pheader->data_size) {
      //printf("validate_free_data: failed: %d is beyound end of odb %d+%d+%d = %d\n", free_data, (int)sizeof(DATABASE_HEADER), pheader->key_size, pheader->data_size, (int)sizeof(DATABASE_HEADER) + pheader->key_size + pheader->data_size);
      return 0;
   }

   return 1;
}

/*------------------------------------------------------------------*/
static void *malloc_data(DATABASE_HEADER * pheader, INT size)
{
   if (size == 0)
      return NULL;

   assert(size > 0);

   /* quadword alignment for alpha CPU */
   size = ALIGN8(size);

   /* smallest allocation size is 8 bytes to make sure we can always create a new FREE_DESCRIP in free_data() */
   assert(size >= (int)sizeof(FREE_DESCRIP));

   if (!validate_free_data(pheader, pheader->first_free_data)) {
      return NULL;
   }

   /* search for free block */
   FREE_DESCRIP *pfree = (FREE_DESCRIP *) ((char *) pheader + pheader->first_free_data);
   FREE_DESCRIP *pprev = NULL;
   FREE_DESCRIP *pfound = NULL;

   while (1) {
      //printf("malloc_data: pprev %p,  pfree %p, next %d, size %d, want %d\n", pprev, pfree, pfree->next_free, pfree->size, size);

      if (pfree->size >= size) {
         // we will use this block
         pfound = pfree;
         break;
      }

      if (!pfree->next_free) {
         // no more free blocks
         return NULL;
      }

      if (!validate_free_data(pheader, pfree->next_free)) {
         // next_free is invalid
         //printf("malloc_data: pprev %p,  pfree %p, next %d, size %d, next is invalid\n", pprev, pfree, pfree->next_free, pfree->size);
         return NULL;
      }
      pprev = pfree;
      pfree = (FREE_DESCRIP *) ((char *) pheader + pfree->next_free);
   }

   //printf("malloc_data: pprev %p, pfound %p, size %d, want %d\n", pprev, pfound, pfound->size, size);

   assert(pfound != NULL);
   assert(size <= pfound->size);

   /* if found block is first in list, correct pheader */
   if (!pprev) {
      if (size < pfree->size) {
         /* free block is only used partially */
         pheader->first_free_data += size;
         pfree = (FREE_DESCRIP *) ((char *) pheader + pheader->first_free_data);

         pfree->size = pfound->size - size;
         pfree->next_free = pfound->next_free;
      } else {
         /* free block is used totally */
         pheader->first_free_data = pfree->next_free;
      }
   } else {
      /* check if free block is used totally */
      if (pfound->size - size < (int) sizeof(FREE_DESCRIP)) {
         /* delete this free block from the free blocks chain */
         pprev->next_free = pfound->next_free;
      } else {
         /* decrease free block */
         pfree = (FREE_DESCRIP *) ((char *) pfound + size);

         pfree->size = pfound->size - size;
         pfree->next_free = pfound->next_free;

         pprev->next_free = (POINTER_T) pfree - (POINTER_T) pheader;
      }
   }

   assert((void*)pfound != (void*)pheader);

   /* zero memeory */
   memset(pfound, 0, size);

   return pfound;
}

/*------------------------------------------------------------------*/
static int free_data(DATABASE_HEADER * pheader, void *address, INT size, const char* caller)
{
   if (size == 0)
      return DB_SUCCESS;

   assert(address != pheader);

   /* quadword alignment for alpha CPU */
   size = ALIGN8(size);

   /* smallest allocation size is 8 bytes to make sure we can always create a new FREE_DESCRIP in free_data() */
   assert(size >= (int)sizeof(FREE_DESCRIP));

   FREE_DESCRIP *pprev = NULL;
   FREE_DESCRIP *pfree = (FREE_DESCRIP *) address;
   int pfree_offset = (POINTER_T) address - (POINTER_T) pheader;

   /* clear current block */
   memset(address, 0, size);

   if (pheader->first_free_data == 0) {
      /* if free list is empty, create the first free block, adjust pheader */
      pfree->size = size;
      pfree->next_free = 0;
      pheader->first_free_data = pfree_offset;
      /* nothing else to do */
      return DB_SUCCESS;
   } else if ((POINTER_T) address - (POINTER_T) pheader < pheader->first_free_data) {
      /* if data comes before first free block, create new free block, adjust pheader */
      pfree->size = size;
      pfree->next_free = pheader->first_free_data;
      pheader->first_free_data = pfree_offset;
      /* maybe merge next free block into the new free block */
      //printf("free_data: created new first free block, maybe merge with old first free block\n");
   } else {
      /* find last free block before current block */
      pprev = (FREE_DESCRIP *) ((char *) pheader + pheader->first_free_data);

      while (pprev->next_free < pfree_offset) {
         if (pprev->next_free == 0) {
            /* add new free block at the end of the chain of free blocks */
            //printf("free_data: adding new free block at the very end\n");
            break;
         }
         if (!validate_free_data(pheader, pprev->next_free)) {
            cm_msg(MERROR, "free_data", "database is corrupted: pprev=%p, pprev->next_free=%d in free_data(%p,%p,%d) from %s", pprev, pprev->next_free, pheader, address, size, caller);
            return DB_CORRUPTED;
         }

         pprev = (FREE_DESCRIP *) ((char *) pheader + pprev->next_free);
      }

      pfree->size = size;
      pfree->next_free = pprev->next_free;

      pprev->next_free = pfree_offset;
   }

   /* try to melt adjacent free blocks after current block */
   FREE_DESCRIP *pnext = (FREE_DESCRIP *) ((char *) pheader + pfree->next_free);
   if ((POINTER_T) pnext == (POINTER_T) pfree + pfree->size) {
      //printf("free_data: merging first and second free block\n");
      pfree->size += pnext->size;
      pfree->next_free = pnext->next_free;

      memset(pnext, 0, pnext->size);
   }

   /* try to melt adjacent free blocks before current block */
   if (pprev && pprev->next_free == (POINTER_T) pprev - (POINTER_T) pheader + pprev->size) {
      //printf("free_data: merging pprev and pfree\n");
      pprev->size += pfree->size;
      pprev->next_free = pfree->next_free;

      memset(pfree, 0, pfree->size);
   }

   return DB_SUCCESS;
}

/*------------------------------------------------------------------*/
static void *realloc_data(DATABASE_HEADER * pheader, void *address, INT old_size, INT new_size, const char* caller)
{
   void *tmp = NULL;

   if (old_size) {
      int status;
      tmp = malloc(old_size);
      if (tmp == NULL) {
         cm_msg(MERROR, "realloc_data", "cannot malloc(%d), called from %s", old_size, caller);
         return NULL;
      }

      memcpy(tmp, address, old_size);

      status = free_data(pheader, address, old_size, caller);
      if (status != DB_SUCCESS) {
         free(tmp);
         cm_msg(MERROR, "realloc_data", "cannot free_data(%p, %d), called from %s", address, old_size, caller);
         return NULL;
      }
   }

   void *pnew = malloc_data(pheader, new_size);

   if (!pnew) {
      if (tmp)
         free(tmp);
      cm_msg(MERROR, "realloc_data", "cannot malloc_data(%d), called from %s", new_size, caller);
      return NULL;
   }

   if (old_size) {
      memcpy(pnew, tmp, old_size < new_size ? old_size : new_size);
      free(tmp);
   }

   return pnew;
}

/*------------------------------------------------------------------*/
char *strcomb(const char **list)
/* convert list of strings into single string to be used by db_paste() */
{
   INT i, j;
   static char *str = NULL;

   /* counter number of chars */
   for (i = 0, j = 0; list[i]; i++)
      j += strlen(list[i]) + 1;
   j += 1;

   if (str == NULL)
      str = (char *) malloc(j);
   else
      str = (char *) realloc(str, j);

   str[0] = 0;
   for (i = 0; list[i]; i++) {
      strcat(str, list[i]);
      strcat(str, "\n");
   }

   return str;
}

/*------------------------------------------------------------------*/

std::string strcomb1(const char **list)
/* convert list of strings into single string to be used by db_paste() */
{
   std::string s;

   for (int i = 0; list[i]; i++) {
      s += list[i];
      s += "\n";
   }

   return s;
}

/*------------------------------------------------------------------*/

struct print_key_info_buf
{
   int alloc_size;
   int used;
   char* buf;
};

static void add_to_buf(struct print_key_info_buf* buf, const char* s)
{
   int len = strlen(s);
   if (buf->used + len + 10 > buf->alloc_size) {
      int new_size = 1024 + 2*buf->alloc_size + len;
      //printf("realloc %d->%d, used %d, adding %d\n", buf->alloc_size, new_size, buf->used, len);
      buf->buf = (char*)realloc(buf->buf, new_size);
      assert(buf->buf != NULL);
      buf->alloc_size = new_size;
   }

   memcpy(buf->buf + buf->used, s, len);
   buf->used += len;
   buf->buf[buf->used] = 0; // zero-terminate the string
}

static INT print_key_info(HNDLE hDB, HNDLE hKey, KEY * pkey, INT level, void *info)
{
   struct print_key_info_buf* buf = (struct print_key_info_buf*)info;
   int i;

   char str[256];

   sprintf(str, "%08X  %08X  %04X    ",
           (int) (hKey - sizeof(DATABASE_HEADER)),
           (int) (pkey->data - sizeof(DATABASE_HEADER)), (int) pkey->total_size);

   assert(strlen(str)+10 < sizeof(str));

   for (i = 0; i < level; i++)
      strcat(str, "  ");

   assert(strlen(str)+10 < sizeof(str));

   strcat(str, pkey->name);
   strcat(str, "\n");

   assert(strlen(str)+10 < sizeof(str));

   //printf("str [%s]\n", str);

   add_to_buf(buf, str);

   return SUCCESS;
}

static bool db_validate_key_offset(const DATABASE_HEADER * pheader, int offset);
static bool db_validate_data_offset(const DATABASE_HEADER * pheader, int offset);

INT db_show_mem(HNDLE hDB, char **result, BOOL verbose)
{
   INT total_size_key, total_size_data;

   struct print_key_info_buf buf;
   buf.buf = NULL;
   buf.used = 0;
   buf.alloc_size = 0;

   db_lock_database(hDB);

   DATABASE_HEADER *pheader = _database[hDB - 1].database_header;

   char str[256];

   sprintf(str, "Database header size is 0x%04X, all following values are offset by this!\n", (int)sizeof(DATABASE_HEADER));
   add_to_buf(&buf, str);
   sprintf(str, "Key area  0x00000000 - 0x%08X, size %d bytes\n",  pheader->key_size - 1, pheader->key_size);
   add_to_buf(&buf, str);
   sprintf(str, "Data area 0x%08X - 0x%08X, size %d bytes\n\n",    pheader->key_size, pheader->key_size + pheader->data_size, pheader->data_size);
   add_to_buf(&buf, str);

   add_to_buf(&buf, "Keylist:\n");
   add_to_buf(&buf, "--------\n");
   total_size_key = 0;

   if (!db_validate_key_offset(pheader, pheader->first_free_key)) {
      add_to_buf(&buf, "ODB is corrupted: pheader->first_free_key is invalid\n");
      db_unlock_database(hDB);
      if (result) {
         *result = buf.buf;
      } else {
         free(buf.buf);
      }
      return DB_CORRUPTED;
   }

   FREE_DESCRIP *pfree = (FREE_DESCRIP *) ((char *) pheader + pheader->first_free_key);

   while ((POINTER_T) pfree != (POINTER_T) pheader) {
      total_size_key += pfree->size;
      sprintf(str, "Free block at 0x%08X, size 0x%08X, next 0x%08X\n",
              (int) ((POINTER_T) pfree - (POINTER_T) pheader - sizeof(DATABASE_HEADER)),
              pfree->size, pfree->next_free ? (int) (pfree->next_free - sizeof(DATABASE_HEADER)) : 0);
      add_to_buf(&buf, str);
      if (!db_validate_key_offset(pheader, pfree->next_free)) {
         add_to_buf(&buf, "ODB is corrupted: next_free is invalid!");
         break;
      }
      pfree = (FREE_DESCRIP *) ((char *) pheader + pfree->next_free);
   }

   sprintf(str, "\nFree Key area: %d bytes out of %d bytes\n", total_size_key, pheader->key_size);
   add_to_buf(&buf, str);
   
   add_to_buf(&buf, "\nData:\n");
   add_to_buf(&buf, "-----\n");
   total_size_data = 0;

   if (!db_validate_data_offset(pheader, pheader->first_free_data)) {
      add_to_buf(&buf, "ODB is corrupted: pheader->first_free_data is invalid\n");
      db_unlock_database(hDB);
      if (result) {
         *result = buf.buf;
      } else {
         free(buf.buf);
      }
      return DB_CORRUPTED;
   }

   pfree = (FREE_DESCRIP *) ((char *) pheader + pheader->first_free_data);

   while ((POINTER_T) pfree != (POINTER_T) pheader) {
      total_size_data += pfree->size;
      sprintf(str, "Free block at 0x%08X, size 0x%08X, next 0x%08X\n",
              (int) ((POINTER_T) pfree - (POINTER_T) pheader - sizeof(DATABASE_HEADER)),
              pfree->size, pfree->next_free ? (int) (pfree->next_free - sizeof(DATABASE_HEADER)) : 0);
      add_to_buf(&buf, str);
      if (!db_validate_data_offset(pheader, pfree->next_free)) {
         add_to_buf(&buf, "ODB is corrupted: next_free is invalid!");
         break;
      }
      pfree = (FREE_DESCRIP *) ((char *) pheader + pfree->next_free);
   }

   sprintf(str, "\nFree Data area: %d bytes out of %d bytes\n", total_size_data, pheader->data_size);
   add_to_buf(&buf, str);

   sprintf(str, "\nFree: %1d (%1.1lf%%) keylist, %1d (%1.1lf%%) data\n",
           total_size_key,
           100 * (double) total_size_key / pheader->key_size,
           total_size_data, 100 * (double) total_size_data / pheader->data_size);
   add_to_buf(&buf, str);

   if (verbose) {
      add_to_buf(&buf, "\n\n");
      add_to_buf(&buf, "Key       Data      Size\n");
      add_to_buf(&buf, "------------------------\n");
      db_scan_tree(hDB, pheader->root_key, 0, print_key_info, &buf);
   }

   db_unlock_database(hDB);

   if (result) {
      *result = buf.buf;
   } else {
      free(buf.buf);
   }

   return DB_SUCCESS;
}

INT db_get_free_mem(HNDLE hDB, INT *key_size, INT *data_size)
{
   DATABASE_HEADER *pheader;
   FREE_DESCRIP *pfree;
   
   *data_size = 0;
   *key_size = 0;

   db_lock_database(hDB);
   
   pheader = _database[hDB - 1].database_header;
   
   pfree = (FREE_DESCRIP *) ((char *) pheader + pheader->first_free_key);
   
   while ((POINTER_T) pfree != (POINTER_T) pheader) {
      *key_size += pfree->size;
      pfree = (FREE_DESCRIP *) ((char *) pheader + pfree->next_free);
   }
   
   *data_size = 0;
   pfree = (FREE_DESCRIP *) ((char *) pheader + pheader->first_free_data);
   
   while ((POINTER_T) pfree != (POINTER_T) pheader) {
      *data_size += pfree->size;
      pfree = (FREE_DESCRIP *) ((char *) pheader + pfree->next_free);
   }

   db_unlock_database(hDB);
   return DB_SUCCESS;
}


// Method to check if a given string is valid UTF-8.  Returns 1 if it is.
// This method was taken from stackoverflow user Christoph, specifically
// http://stackoverflow.com/questions/1031645/how-to-detect-utf-8-in-plain-c
static bool is_utf8(const char * string)
{
    if(!string)
        return false;

    const unsigned char * bytes = (const unsigned char *)string;
    while(*bytes)
    {
        if( (// ASCII
             // use bytes[0] <= 0x7F to allow ASCII control characters
                bytes[0] == 0x09 ||
                bytes[0] == 0x0A ||
                bytes[0] == 0x0D ||
                (0x20 <= bytes[0] && bytes[0] <= 0x7E)
            )
        ) {
            bytes += 1;
            continue;
        }

        if( (// non-overlong 2-byte
                (0xC2 <= bytes[0] && bytes[0] <= 0xDF) &&
                (0x80 <= bytes[1] && bytes[1] <= 0xBF)
            )
        ) {
            bytes += 2;
            continue;
        }

        if( (// excluding overlongs
                bytes[0] == 0xE0 &&
                (0xA0 <= bytes[1] && bytes[1] <= 0xBF) &&
                (0x80 <= bytes[2] && bytes[2] <= 0xBF)
            ) ||
            (// straight 3-byte
                ((0xE1 <= bytes[0] && bytes[0] <= 0xEC) ||
                    bytes[0] == 0xEE ||
                    bytes[0] == 0xEF) &&
                (0x80 <= bytes[1] && bytes[1] <= 0xBF) &&
                (0x80 <= bytes[2] && bytes[2] <= 0xBF)
            ) ||
            (// excluding surrogates
                bytes[0] == 0xED &&
                (0x80 <= bytes[1] && bytes[1] <= 0x9F) &&
                (0x80 <= bytes[2] && bytes[2] <= 0xBF)
            )
        ) {
            bytes += 3;
            continue;
        }

        if( (// planes 1-3
                bytes[0] == 0xF0 &&
                (0x90 <= bytes[1] && bytes[1] <= 0xBF) &&
                (0x80 <= bytes[2] && bytes[2] <= 0xBF) &&
                (0x80 <= bytes[3] && bytes[3] <= 0xBF)
            ) ||
            (// planes 4-15
                (0xF1 <= bytes[0] && bytes[0] <= 0xF3) &&
                (0x80 <= bytes[1] && bytes[1] <= 0xBF) &&
                (0x80 <= bytes[2] && bytes[2] <= 0xBF) &&
                (0x80 <= bytes[3] && bytes[3] <= 0xBF)
            ) ||
            (// plane 16
                bytes[0] == 0xF4 &&
                (0x80 <= bytes[1] && bytes[1] <= 0x8F) &&
                (0x80 <= bytes[2] && bytes[2] <= 0xBF) &&
                (0x80 <= bytes[3] && bytes[3] <= 0xBF)
            )
        ) {
            bytes += 4;
            continue;
        }
        
        //printf("is_utf8: string [%s], not utf8 at offset %d, byte %d, [%s]\n", string, (int)((char*)bytes-(char*)string), (int)(0xFF&bytes[0]), bytes);
        //abort();

        return false;
    }

    return true;
}

static BOOL utfCheckEnvVar = 0;
static BOOL checkUtfValidString = 0;

/*------------------------------------------------------------------*/
static int db_validate_name(const char* name, int maybe_path, const char* caller_name)
{
   //printf("db_validate_name [%s] length %d, maybe_path %d from %s\n", name, (int)strlen(name), maybe_path, caller_name);

   if (name == NULL) {
      cm_msg(MERROR, "db_validate_name", "Invalid name passed to %s: should not be NULL", caller_name);
      return DB_INVALID_NAME;
   }

   if (strlen(name) < 1) {
      cm_msg(MERROR, "db_validate_name", "Invalid name passed to %s: should not be an empty string", caller_name);
      return DB_INVALID_NAME;
   }

   if (strchr(name, '[')) {
      cm_msg(MERROR, "db_validate_name", "Invalid name \"%s\" passed to %s: should not contain \"[\"", name, caller_name);
      return DB_INVALID_NAME;
   }
   
   if (strchr(name, ']')) {
      cm_msg(MERROR, "db_validate_name", "Invalid name \"%s\" passed to %s: should not contain \"[\"", name, caller_name);
      return DB_INVALID_NAME;
   }
   
   // Disabled check for UTF-8 compatible names. 
   // Check can be disabled by having an environment variable called "MIDAS_INVALID_STRING_IS_OK"
   // Check the environment variable only first time
   if(!utfCheckEnvVar){
      if (getenv("MIDAS_INVALID_STRING_IS_OK")){
         checkUtfValidString = 0;
      }else{
         checkUtfValidString = 1;
      }
      utfCheckEnvVar = 1;
   }
   
   if (checkUtfValidString && !is_utf8(name)) {
      cm_msg(MERROR, "db_validate_name", "Invalid name \"%s\" passed to %s: UTF-8 incompatible string", name,caller_name);
      return DB_INVALID_NAME;
   }
   
   if (!maybe_path) {
      if (strchr(name, '/')) {
         cm_msg(MERROR, "db_validate_name", "Invalid name \"%s\" passed to %s: should not contain \"/\"", name, caller_name);
         return DB_INVALID_NAME;
      }

      if (strlen(name) >= NAME_LENGTH) {
         cm_msg(MERROR, "db_validate_name", "Invalid name \"%s\" passed to %s: length %d should be less than %d", name, caller_name, (int)strlen(name), NAME_LENGTH);
         return DB_INVALID_NAME;
      }
   }

   //if (strcmp(name, "test")==0)
   //return DB_INVALID_NAME;

   return DB_SUCCESS;
}

/*------------------------------------------------------------------*/
static bool db_validate_key_offset(const DATABASE_HEADER * pheader, int offset)
/* check if key offset lies in valid range */
{
   if (offset != 0 && offset < (int) sizeof(DATABASE_HEADER))
      return false;

   if (offset > (int) sizeof(DATABASE_HEADER) + pheader->key_size)
      return false;

   return true;
}

static bool db_validate_data_offset(const DATABASE_HEADER * pheader, int offset)
/* check if data offset lies in valid range */
{
   if (offset != 0 && offset < (int) sizeof(DATABASE_HEADER))
      return false;

   if (offset > (int) sizeof(DATABASE_HEADER) + pheader->key_size + pheader->data_size)
      return false;

   return true;
}

static bool db_validate_hkey(const DATABASE_HEADER * pheader, HNDLE hKey)
{
   if (hKey == 0) {
      cm_msg(MERROR, "db_validate_hkey", "Error: invalid zero hkey %d", hKey);
      return false;
   }
   if (!db_validate_key_offset(pheader, hKey)) {
      cm_msg(MERROR, "db_validate_hkey", "Error: invalid hkey %d", hKey);
      return false;
   }
   return true;
}

static bool db_validate_pkey(const DATABASE_HEADER * pheader, const KEY* pkey)
{
   /* check key type */
   if (pkey->type <= 0 || pkey->type >= TID_LAST) {
      return false;
   }
   return true;
}

static const KEY* db_get_pkey(const DATABASE_HEADER* pheader, HNDLE hKey, int* pstatus, const char* caller, db_err_msg **msg)
{
   BOOL hKey_is_root_key = FALSE;

   if (!hKey) {
      hKey_is_root_key = TRUE;
      hKey = pheader->root_key;
   }

   /* check if hKey argument is correct */
   if (!db_validate_hkey(pheader, hKey)) {
      if (pstatus)
         *pstatus = DB_INVALID_HANDLE;
      return NULL;
   }

   const KEY* pkey = (const KEY *) ((char *) pheader + hKey);

   if (pkey->type < 1 || pkey->type >= TID_LAST) {
      DWORD tid = pkey->type;
      if (hKey_is_root_key) {
         db_msg(msg, MERROR, caller, "root_key hkey %d invalid key type %d, database root directory is corrupted", hKey, tid);
         if (pstatus)
            *pstatus = DB_CORRUPTED;
         return NULL;
      } else {
         char str[MAX_ODB_PATH];
         db_get_path_locked(pheader, hKey, str, sizeof(str));
         db_msg(msg, MERROR, caller, "hkey %d path \"%s\" invalid key type %d", hKey, str, tid);
      }
      if (pstatus)
         *pstatus = DB_NO_KEY;
      return NULL;
   }

   if (pkey->name[0] == 0) {
      char str[MAX_ODB_PATH];
      db_get_path_locked(pheader, hKey, str, sizeof(str));
      db_msg(msg, MERROR, caller, "hkey %d path \"%s\" invalid name \"%s\" is empty", hKey, str, pkey->name);
      if (pstatus)
         *pstatus = DB_NO_KEY;
      return NULL;
   }

   return pkey;
}

static const KEYLIST* db_get_pkeylist(const DATABASE_HEADER* pheader, HNDLE hKey, const KEY* pkey, const char* caller, db_err_msg **msg)
{
   if (pkey->type != TID_KEY) {
      char str[MAX_ODB_PATH];
      db_get_path_locked(pheader, hKey, str, sizeof(str));
      db_msg(msg, MERROR, caller, "hkey %d path \"%s\" unexpected call to db_get_pkeylist(), not a subdirectory, pkey->type %d", hKey, str, pkey->type);
      return NULL;
   }

   if (!hKey) {
      hKey = pheader->root_key;
   }

   if (!db_validate_data_offset(pheader, pkey->data)) {
      char str[MAX_ODB_PATH];
      db_get_path_locked(pheader, hKey, str, sizeof(str));
      db_msg(msg, MERROR, caller, "hkey %d path \"%s\" invalid pkey->data %d", hKey, str, pkey->data);
      return NULL;
   }

   const KEYLIST *pkeylist = (const KEYLIST *) ((char *) pheader + pkey->data);

   if (0 && pkeylist->parent != hKey) {
      char str[MAX_ODB_PATH];
      db_get_path_locked(pheader, hKey, str, sizeof(str));
      db_msg(msg, MERROR, caller, "hkey %d path \"%s\" invalid pkeylist->parent %d should be hkey %d", hKey, str, pkeylist->parent, hKey);
      return NULL;
   }

   if (pkeylist->first_key == 0 && pkeylist->num_keys != 0) {
      char str[MAX_ODB_PATH];
      db_get_path_locked(pheader, hKey, str, sizeof(str));
      db_msg(msg, MERROR, caller, "hkey %d path \"%s\" invalid pkeylist->first_key %d should be non zero for num_keys %d", hKey, str, pkeylist->first_key, pkeylist->num_keys);
      return NULL;
   }

   return pkeylist;
}

static bool db_validate_and_repair_key(DATABASE_HEADER * pheader, int recurse, const char *path, HNDLE parenthkeylist, HNDLE hkey, KEY * pkey)
{
   int status;
   static time_t t_min = 0, t_max;
   bool flag = true;

   //printf("path \"%s\", parenthkey %d, hkey %d, pkey->name \"%s\", type %d\n", path, parenthkeylist, hkey, pkey->name, pkey->type);

   if (hkey==0 || !db_validate_key_offset(pheader, hkey)) {
      cm_msg(MERROR, "db_validate_key", "Warning: hkey %d, path \"%s\", invalid hkey", hkey, path);
      return false;
   }

   /* check key type */
   if (pkey->type <= 0 || pkey->type >= TID_LAST) {
      cm_msg(MERROR, "db_validate_key", "Warning: hkey %d, path \"%s\", name \"%s\", invalid key type %d", hkey, path, pkey->name, pkey->type);
      return false;
   }

   /* check key name */
   status = db_validate_name(pkey->name, FALSE, "db_validate_key");
   if (status != DB_SUCCESS) {
      char newname[NAME_LENGTH];
      sprintf(newname, "%p", pkey);
      cm_msg(MERROR, "db_validate_key", "Warning: hkey %d, path \"%s\": invalid name \"%s\" replaced with \"%s\"", hkey, path, pkey->name, newname);
      strlcpy(pkey->name, newname, sizeof(pkey->name));
      flag = false;
      //return false;
   }

   /* check parent */
   if (pkey->parent_keylist != parenthkeylist) {
      cm_msg(MERROR, "db_validate_key", "Warning: hkey %d, path \"%s\", name \"%s\", invalid parent_keylist %d should be %d", hkey, path, pkey->name, pkey->parent_keylist, parenthkeylist);
      return false;
   }

   if (!db_validate_data_offset(pheader, pkey->data)) {
      cm_msg(MERROR, "db_validate_key", "Warning: hkey %d, path \"%s\", invalid data offset 0x%08X is invalid", hkey, path, pkey->data - (int)sizeof(DATABASE_HEADER));
      return false;
   }

   /* check key sizes */
   if ((pkey->total_size < 0) || (pkey->total_size > pheader->data_size)) {
      cm_msg(MERROR, "db_validate_key", "Warning: hkey %d, path \"%s\", invalid pkey->total_size %d", hkey, path, pkey->total_size);
      return false;
   }

   if ((pkey->item_size < 0) || (pkey->item_size > pheader->data_size)) {
      cm_msg(MERROR, "db_validate_key", "Warning: hkey %d, path \"%s\", invalid pkey->item_size: %d", hkey, path, pkey->item_size);
      return false;
   }

   if ((pkey->num_values < 0) || (pkey->num_values > pheader->data_size)) {
      cm_msg(MERROR, "db_validate_key", "Warning: hkey %d, path \"%s\", invalid pkey->num_values: %d", hkey, path, pkey->num_values);
      return false;
   }

   /* check and correct key size */
   if (pkey->total_size != pkey->item_size * pkey->num_values) {
      cm_msg(MINFO, "db_validate_key", "Warning: hkey %d, path \"%s\", corrected pkey->total_size from %d to %d*%d=%d", hkey, path, pkey->total_size, pkey->item_size, pkey->num_values, pkey->item_size * pkey->num_values);
      pkey->total_size = pkey->item_size * pkey->num_values;
      flag = false;
   }

   /* check and correct key size */
   if (pkey->data == 0 && pkey->total_size != 0) {
      cm_msg(MINFO, "db_validate_key", "Warning: hkey %d, path \"%s\", pkey->data is zero, corrected pkey->num_values %d and pkey->total_size %d to be zero, should be zero", hkey, path, pkey->num_values, pkey->total_size);
      pkey->num_values = 0;
      pkey->total_size = 0;
      flag = false;
   }

   if (pkey->type == TID_STRING || pkey->type == TID_LINK) {
      const char* s = (char*)pheader + pkey->data;
      if (!is_utf8(s)) {
         cm_msg(MERROR, "db_validate_key", "Warning: hkey %d, path \"%s\", string value is not valid UTF-8", hkey, path);
         //flag = false;
      }
   }

   /* check for empty link */
   if (pkey->type == TID_LINK) {
      // minimum symlink length is 3 bytes:
      // one byte "/"
      // one byte odb entry name
      // one byte "\0"
      if (pkey->total_size <= 2) {
         cm_msg(MERROR, "db_validate_key", "Warning: hkey %d, path \"%s\", TID_LINK is an empty link", hkey, path);
      }
      flag = false;
      //return false;
   }

   /* check for too long link */
   if (pkey->type == TID_LINK) {
      if (pkey->total_size >= MAX_ODB_PATH) {
         cm_msg(MERROR, "db_validate_key", "Warning: hkey %d, path \"%s\", TID_LINK length %d exceeds MAX_ODB_PATH %d", hkey, path, pkey->total_size, MAX_ODB_PATH);
      }
      flag = false;
      //return false;
   }

   /* check for link loop */
   if (pkey->type == TID_LINK) {
      const char* link = (char*)pheader + pkey->data;
      int link_len = strlen(link);
      int path_len = strlen(path);
      if (link_len == path_len) {
         // check for link to itself
         if (equal_ustring(link, path)) {
            cm_msg(MERROR, "db_validate_key", "Warning: hkey %d, path \"%s\", TID_LINK to \"%s\" is a link to itself", hkey, path, link);
         }
      } else if (link_len < path_len) {
         // check for link to the "path" subdirectory
         char tmp[MAX_ODB_PATH];
         memcpy(tmp, path, link_len);
         tmp[link_len] = 0;
         if (equal_ustring(link, tmp) && path[link_len] == DIR_SEPARATOR) {
            cm_msg(MERROR, "db_validate_key", "Warning: hkey %d, path \"%s\", TID_LINK to \"%s\" is a loop", hkey, path, link);
         }
      }
      flag = false;
      //return false;
   }

   /* check access mode */
   if ((pkey->access_mode & ~(MODE_READ | MODE_WRITE | MODE_DELETE | MODE_EXCLUSIVE | MODE_ALLOC))) {
      cm_msg(MERROR, "db_validate_key", "Warning: hkey %d, path \"%s\", invalid pkey->access_mode %d", hkey, path, pkey->access_mode);
      flag = false;
      //return false;
   }

   /* check access time, consider valid if within +- 10 years */
   if (t_min == 0) {
      t_min = ss_time() - 3600 * 24 * 365 * 10;
      t_max = ss_time() + 3600 * 24 * 365 * 10;
   }

   if (pkey->last_written > 0 && (pkey->last_written < t_min || pkey->last_written > t_max)) {
      cm_msg(MERROR, "db_validate_key", "Warning: hkey %d, path \"%s\", invalid pkey->last_written time %d", hkey, path, pkey->last_written);
      flag = false;
      //return false;
   }

   if (pkey->type == TID_KEY) {
      bool pkeylist_ok = true;
      db_err_msg* msg = NULL;
      const KEYLIST *pkeylist = db_get_pkeylist(pheader, pkey->data, pkey, "db_validate_key", &msg);

      if (!pkeylist) {
         db_flush_msg(&msg);
         cm_msg(MERROR, "db_validate_key", "Warning: hkey %d, path \"%s\", invalid pkey->data %d", hkey, path, pkey->data);
         flag = false;
      } else {
         if (pkeylist->parent != hkey) {
            cm_msg(MERROR, "db_validate_key", "Warning: hkey %d, path \"%s\", TID_KEY invalid pkeylist->parent %d is not hkey %d", hkey, path, pkeylist->parent, hkey);
            flag = false;
            pkeylist_ok = false;
         }

         if (pkeylist->num_keys < 0 || pkeylist->num_keys > pheader->key_size) {
            cm_msg(MERROR, "db_validate_key", "Warning: hkey %d, path \"%s\", TID_KEY invalid pkeylist->num_keys %d", hkey, path, pkeylist->num_keys);
            flag = false;
            pkeylist_ok = false;
         }
         
         if (pkeylist->num_keys == 0 && pkeylist->first_key == 0) {
            // empty key
         } else if (pkeylist->first_key == 0 || !db_validate_key_offset(pheader, pkeylist->first_key)) {
            cm_msg(MERROR, "db_validate_key", "Warning: hkey %d, path \"%s\", TID_KEY invalid pkeylist->first_key %d", hkey, path, pkeylist->first_key);
            flag = false;
            pkeylist_ok = false;
         }
         
         if (pkeylist_ok) {
            //printf("hkey %d, path \"%s\", pkey->data %d, pkeylist parent %d, num_keys %d, first_key %d: ", hkey, path, pkey->data, pkeylist->parent, pkeylist->num_keys, pkeylist->first_key);
            
            int subhkey = pkeylist->first_key;

            int count = 0;
            while (subhkey != 0) {
               db_err_msg* msg = NULL;
               KEY* subpkey = (KEY*)db_get_pkey(pheader, subhkey, NULL, "db_validate_key", &msg);
               if (!subpkey) {
                  db_flush_msg(&msg);
                  pkeylist_ok = false;
                  flag = false;
                  break;
               }

               std::string buf;
               buf += path;
               buf += "/";
               buf += subpkey->name;
               
               //printf("pkey %p, next %d, name [%s], path %s\n", subpkey, subpkey->next_key, subpkey->name, buf.c_str());
               
               if (recurse) {
                  flag &= db_validate_and_repair_key(pheader, recurse + 1, buf.c_str(), pkey->data, subhkey, subpkey);
               }

               count++;
               subhkey = subpkey->next_key;
            }

            if (count != pkeylist->num_keys) {
               cm_msg(MERROR, "db_validate_key", "Warning: hkey %d, path \"%s\", TID_KEY mismatch of pkeylist->num_keys %d against key chain length %d", hkey, path, pkeylist->num_keys, count);
               flag = false;
               pkeylist_ok = false;
            }
         }
      }
   }

   return flag;
}

/*------------------------------------------------------------------*/
static void db_validate_sizes()
{
   /* validate size of data structures (miscompiled, 32/64 bit mismatch, etc */

   if (0) {
#define S(x) printf("assert(sizeof(%-20s) == %6d);\n", #x, (int)sizeof(x))
      // basic data types
      S(char *);
      S(char);
      S(int);
      S(long int);
      S(float);
      S(double);
      S(BOOL);
      S(WORD);
      S(DWORD);
      S(INT);
      S(POINTER_T);
      S(midas_thread_t);
      // data buffers
      S(EVENT_REQUEST);
      S(BUFFER_CLIENT);
      S(BUFFER_HEADER);
      // history files
      S(HIST_RECORD);
      S(DEF_RECORD);
      S(INDEX_RECORD);
      S(TAG);
      // ODB shared memory structures
      S(KEY);
      S(KEYLIST);
      S(OPEN_RECORD);
      S(DATABASE_CLIENT);
      S(DATABASE_HEADER);
      // misc structures
      S(EVENT_HEADER);
      S(RUNINFO);
      S(EQUIPMENT_INFO);
      S(EQUIPMENT_STATS);
      S(BANK_HEADER);
      S(BANK);
      S(BANK32);
      S(ANA_OUTPUT_INFO);
      S(PROGRAM_INFO);
      S(ALARM_CLASS);
      S(ALARM);
      //S(CHN_SETTINGS);
      //S(CHN_STATISTICS);
#undef S
   }

#if 0
   EQUIPMENT_INFO eq;
   printf("EQUIPMENT_INFO offset of event_id: %d\n", (int)((char*)&eq.event_id - (char*)&eq));
   printf("EQUIPMENT_INFO offset of eq_type: %d\n", (int)((char*)&eq.eq_type - (char*)&eq));
   printf("EQUIPMENT_INFO offset of event_limit: %d\n", (int)((char*)&eq.event_limit - (char*)&eq));
   printf("EQUIPMENT_INFO offset of num_subevents: %d\n", (int)((char*)&eq.num_subevents - (char*)&eq));
   printf("EQUIPMENT_INFO offset of status: %d\n", (int)((char*)&eq.status - (char*)&eq));
   printf("EQUIPMENT_INFO offset of hidden: %d\n", (int)((char*)&eq.hidden - (char*)&eq));
#endif
   
#ifdef OS_LINUX
   assert(sizeof(EVENT_REQUEST) == 16); // ODB v3
   assert(sizeof(BUFFER_CLIENT) == 256);
   assert(sizeof(BUFFER_HEADER) == 16444);
   assert(sizeof(HIST_RECORD) == 20);
   assert(sizeof(DEF_RECORD) == 40);
   assert(sizeof(INDEX_RECORD) == 12);
   assert(sizeof(TAG) == 40);
   assert(sizeof(KEY) == 68);
   assert(sizeof(KEYLIST) == 12);
   assert(sizeof(OPEN_RECORD) == 8);
   assert(sizeof(DATABASE_CLIENT) == 2112);
   assert(sizeof(DATABASE_HEADER) == 135232);
   assert(sizeof(EVENT_HEADER) == 16);
   //assert(sizeof(EQUIPMENT_INFO) == 696); has been moved to dynamic checking inside mhttpd.c
   assert(sizeof(EQUIPMENT_STATS) == 24);
   assert(sizeof(BANK_HEADER) == 8);
   assert(sizeof(BANK) == 8);
   assert(sizeof(BANK32) == 12);
   assert(sizeof(ANA_OUTPUT_INFO) == 792);
   assert(sizeof(PROGRAM_INFO) == 316);
   assert(sizeof(ALARM_CLASS) == 348);
   assert(sizeof(ALARM) == 452);
   //assert(sizeof(CHN_SETTINGS) == 648); // ODB v3
   //assert(sizeof(CHN_STATISTICS) == 56);        // ODB v3
#endif
}

typedef struct {
   DATABASE_HEADER * pheader;
   int max_keys;
   int num_keys;
   HNDLE* hkeys;
   int* counts;
   int* modes;
   int num_modified;
} UPDATE_OPEN_RECORDS;

static int db_update_open_record_locked(HNDLE hDB, HNDLE hKey, KEY* xkey, INT level, void* voidp)
{
   int found = 0;
   int count = 0;
   int status;
   int k;
   UPDATE_OPEN_RECORDS *uorp = (UPDATE_OPEN_RECORDS *)voidp;
   char path[MAX_ODB_PATH];

   if (!hKey)
      hKey = uorp->pheader->root_key;

   for (k=0; k<uorp->num_keys; k++)
      if (uorp->hkeys[k] == hKey) {
         found = 1;
         count = uorp->counts[k];
         break;
      }

   if (xkey->notify_count == 0 && !found)
      return DB_SUCCESS; // no open record here

   status = db_get_path_locked(uorp->pheader, hKey, path, sizeof(path));
   if (status != DB_SUCCESS)
      return DB_SUCCESS;

   if (!db_validate_hkey(uorp->pheader, hKey)) {
      cm_msg(MINFO, "db_update_open_record", "Invalid hKey %d", hKey);
      return DB_SUCCESS;
   }

   KEY* pkey = (KEY *) ((char *) uorp->pheader + hKey);

   //printf("path [%s], type %d, notify_count %d\n", path, pkey->type, pkey->notify_count);

   // extra check: are we looking at the same key?
   assert(xkey->notify_count == pkey->notify_count);

#if 0
   printf("%s, notify_count %d, found %d, our count %d\n", path, pkey->notify_count, found, count);
#endif
   
   if (pkey->notify_count==0 && found) {
      cm_msg(MINFO, "db_update_open_record", "Added missing open record flag to \"%s\"", path);
      pkey->notify_count = count;
      uorp->num_modified++;
      return DB_SUCCESS;
   }

   if (pkey->notify_count!=0 && !found) {
      cm_msg(MINFO, "db_update_open_record", "Removed open record flag from \"%s\"", path);
      pkey->notify_count = 0;
      uorp->num_modified++;

      if (pkey->access_mode | MODE_EXCLUSIVE) {
         status = db_set_mode(hDB, hKey, (WORD) (pkey->access_mode & ~MODE_EXCLUSIVE), 2);
         if (status != DB_SUCCESS) {
            cm_msg(MERROR, "db_update_open_record", "Cannot remove exclusive access mode from \"%s\", db_set_mode() status %d", path, status);
            return DB_SUCCESS;
         }
         cm_msg(MINFO, "db_update_open_record", "Removed exclusive access mode from \"%s\"", path);
      }
      return DB_SUCCESS;
   }

   if (pkey->notify_count != uorp->counts[k]) {
      cm_msg(MINFO, "db_update_open_record", "Updated notify_count of \"%s\" from %d to %d", path, pkey->notify_count, count);
      pkey->notify_count = count;
      uorp->num_modified++;
      return DB_SUCCESS;
   }

   return DB_SUCCESS;
}

static int db_validate_open_records(HNDLE hDB)
{
   UPDATE_OPEN_RECORDS uor;
   DATABASE_HEADER * pheader;
   int i, j, k;

   if (hDB > _database_entries || hDB <= 0) {
      cm_msg(MERROR, "db_validate_open_records", "invalid database handle");
      return DB_INVALID_HANDLE;
   }

   uor.max_keys = MAX_CLIENTS*MAX_OPEN_RECORDS;
   uor.num_keys = 0;
   uor.hkeys = (HNDLE*)calloc(uor.max_keys, sizeof(HNDLE));
   uor.counts = (int*)calloc(uor.max_keys, sizeof(int));
   uor.modes = (int*)calloc(uor.max_keys, sizeof(int));
   uor.num_modified = 0;

   assert(uor.hkeys != NULL);
   assert(uor.counts != NULL);
   assert(uor.modes != NULL);

   db_lock_database(hDB);

   pheader = _database[hDB - 1].database_header;

   uor.pheader = pheader;

   for (i = 0; i < pheader->max_client_index; i++) {
      DATABASE_CLIENT* pclient = &pheader->client[i];
      for (j = 0; j < pclient->max_index; j++)
         if (pclient->open_record[j].handle) {
            int found = 0;
            for (k=0; k<uor.num_keys; k++) {
               if (uor.hkeys[k] == pclient->open_record[j].handle) {
                  uor.counts[k]++;
                  found = 1;
                  break;
               }
            }
            if (!found) {
               uor.hkeys[uor.num_keys] = pclient->open_record[j].handle;
               uor.counts[uor.num_keys] = 1;
               uor.modes[uor.num_keys] = pclient->open_record[j].access_mode;
               uor.num_keys++;
            }
         }
   }

#if 0
   for (i=0; i<uor.num_keys; i++)
      printf("index %d, handle %d, count %d, access mode %d\n", i, uor.hkeys[i], uor.counts[i], uor.modes[i]);
#endif
   
   db_scan_tree(hDB, 0, 0, db_update_open_record_locked, &uor);

   if (uor.num_modified) {
      cm_msg(MINFO, "db_validate_open_records", "Corrected %d ODB entries", uor.num_modified);
   }

   db_unlock_database(hDB);

   free(uor.hkeys);
   free(uor.counts);
   free(uor.modes);

   return DB_SUCCESS;
}

/*------------------------------------------------------------------*/
static bool db_validate_and_repair_db_locked(DATABASE_HEADER * pheader)
{
   int total_size_key = 0;
   int total_size_data = 0;
   double ratio;
   FREE_DESCRIP *pfree;
   bool flag = true;

   /* validate size of data structures (miscompiled, 32/64 bit mismatch, etc */

   db_validate_sizes();

   /* validate the key free list */

   if (!db_validate_key_offset(pheader, pheader->first_free_key)) {
      cm_msg(MERROR, "db_validate_db", "Error: database corruption, invalid pheader->first_free_key 0x%08X", pheader->first_free_key - (int)sizeof(DATABASE_HEADER));
      return false;
   }

   pfree = (FREE_DESCRIP *) ((char *) pheader + pheader->first_free_key);

   while ((POINTER_T) pfree != (POINTER_T) pheader) {

      if (pfree->next_free != 0 && !db_validate_key_offset(pheader, pfree->next_free)) {
         cm_msg(MERROR, "db_validate_db", "Warning: database corruption, invalid key area next_free 0x%08X", pfree->next_free - (int)sizeof(DATABASE_HEADER));
         flag = false;
         break;
      }

      total_size_key += pfree->size;
      FREE_DESCRIP *nextpfree = (FREE_DESCRIP *) ((char *) pheader + pfree->next_free);

      if (pfree->next_free != 0 && nextpfree == pfree) {
         cm_msg(MERROR, "db_validate_db", "Warning: database corruption, key area next_free 0x%08X is same as current free %p, truncating the free list", pfree->next_free, pfree - (int)sizeof(DATABASE_HEADER));
         pfree->next_free = 0;
         flag = false;
         break;
         //return false;
      }

      pfree = nextpfree;
   }

   ratio = ((double) (pheader->key_size - total_size_key)) / ((double) pheader->key_size);
   if (ratio > 0.9)
      cm_msg(MERROR, "db_validate_db", "Warning: database key area is %.0f%% full", ratio * 100.0);

   if (total_size_key > pheader->key_size) {
      cm_msg(MERROR, "db_validate_db", "Error: database corruption, total_key_size 0x%08X bigger than pheader->key_size 0x%08X", total_size_key, pheader->key_size);
      flag = false;
   }

   /* validate the data free list */

   if (!db_validate_data_offset(pheader, pheader->first_free_data)) {
      cm_msg(MERROR, "db_validate_db", "Error: database corruption, invalid pheader->first_free_data 0x%08X", pheader->first_free_data - (int)sizeof(DATABASE_HEADER));
      return false;
   }

   pfree = (FREE_DESCRIP *) ((char *) pheader + pheader->first_free_data);

   while ((POINTER_T) pfree != (POINTER_T) pheader) {

      if (pfree->next_free != 0 && !db_validate_data_offset(pheader, pfree->next_free)) {
         cm_msg(MERROR, "db_validate_db", "Warning: database corruption, invalid data area next_free 0x%08X", pfree->next_free - (int)sizeof(DATABASE_HEADER));
         flag = false;
         break;
         //return false;
      }

      total_size_data += pfree->size;
      FREE_DESCRIP *nextpfree = (FREE_DESCRIP *) ((char *) pheader + pfree->next_free);

      if (pfree->next_free != 0 && nextpfree == pfree) {
         cm_msg(MERROR, "db_validate_db", "Warning: database corruption, data area next_free 0x%08X is same as current free %p, truncating the free list", pfree->next_free, pfree - (int)sizeof(DATABASE_HEADER));
         pfree->next_free = 0;
         flag = false;
         break;
         //return false;
      }

      pfree = nextpfree;
   }

   ratio = ((double) (pheader->data_size - total_size_data)) / ((double) pheader->data_size);
   if (ratio > 0.9)
      cm_msg(MERROR, "db_validate_db", "Warning: database data area is %.0f%% full", ratio * 100.0);

   if (total_size_data > pheader->data_size) {
      cm_msg(MERROR, "db_validate_db", "Error: database corruption, total_size_data 0x%08X bigger than pheader->data_size 0x%08X", total_size_key, pheader->data_size);
      flag = false;
      //return false;
   }

   /* validate the tree of keys, starting from the root key */

   if (!db_validate_key_offset(pheader, pheader->root_key)) {
      cm_msg(MERROR, "db_validate_db", "Error: database corruption, pheader->root_key 0x%08X is invalid", pheader->root_key - (int)sizeof(DATABASE_HEADER));
      return false;
   }

   flag &= db_validate_and_repair_key(pheader, 1, "", 0, pheader->root_key, (KEY *) ((char *) pheader + pheader->root_key));

   if (!flag) {
      cm_msg(MERROR, "db_validate_db", "Error: ODB corruption detected, maybe repaired");
   }

   return flag;
}

/**dox***************************************************************/
#endif                          /* DOXYGEN_SHOULD_SKIP_THIS */

/********************************************************************/
/**
Open an online database
@param database_name     Database name.
@param database_size     Initial size of database if not existing
@param client_name       Name of this application
@param hDB          ODB handle obtained via cm_get_experiment_database().
@return DB_SUCCESS, DB_CREATED, DB_INVALID_NAME, DB_NO_MEMORY,
        DB_MEMSIZE_MISMATCH, DB_NO_SEMAPHORE, DB_INVALID_PARAM,
        RPC_NET_ERROR
*/
INT db_open_database(const char *xdatabase_name, INT database_size, HNDLE * hDB, const char *client_name)
{
   if (rpc_is_remote())
      return rpc_call(RPC_DB_OPEN_DATABASE, xdatabase_name, database_size, hDB, client_name);

#ifdef LOCAL_ROUTINES
   {
   INT i, status;
   HNDLE handle;
   DATABASE_CLIENT *pclient;
   BOOL shm_created;
   DATABASE_HEADER *pheader;
   KEY *pkey;
   KEYLIST *pkeylist;
   FREE_DESCRIP *pfree;
   BOOL call_watchdog;
   DWORD timeout;
   char database_name[NAME_LENGTH];

   /* restrict name length */
   strlcpy(database_name, xdatabase_name, NAME_LENGTH);

   if (database_size < 0 || database_size > 10E7) {
      cm_msg(MERROR, "db_open_database", "invalid database size");
      return DB_INVALID_PARAM;
   }

   if (strlen(client_name) >= NAME_LENGTH) {
      cm_msg(MERROR, "db_open_database", "client name \'%s\' is longer than %d characters", client_name, NAME_LENGTH-1);
      return DB_INVALID_PARAM;
   }

   if (strchr(client_name, '/') != NULL) {
      cm_msg(MERROR, "db_open_database", "client name \'%s\' should not contain the slash \'/\' character", client_name);
      return DB_INVALID_PARAM;
   }

   /* allocate new space for the new database descriptor */
   if (_database_entries == 0) {
      _database = (DATABASE *) malloc(sizeof(DATABASE));
      memset(_database, 0, sizeof(DATABASE));
      if (_database == NULL) {
         *hDB = 0;
         return DB_NO_MEMORY;
      }

      _database_entries = 1;
      i = 0;
   } else {
      /* check if database already open */
      for (i = 0; i < _database_entries; i++)
         if (_database[i].attached && equal_ustring(_database[i].name, database_name)) {
            /* check if database belongs to this thread */
            *hDB = i + 1;
            return DB_SUCCESS;
         }

      /* check for a deleted entry */
      for (i = 0; i < _database_entries; i++)
         if (!_database[i].attached)
            break;

      /* if not found, create new one */
      if (i == _database_entries) {
         _database = (DATABASE *) realloc(_database, sizeof(DATABASE) * (_database_entries + 1));
         memset(&_database[_database_entries], 0, sizeof(DATABASE));

         _database_entries++;
         if (_database == NULL) {
            _database_entries--;
            *hDB = 0;
            return DB_NO_MEMORY;
         }
      }
   }

   handle = (HNDLE) i;

   /* open shared memory region */
   void* shm_adr = NULL;
   size_t shm_size = 0;
   HNDLE shm_handle;
   
   status = ss_shm_open(database_name, sizeof(DATABASE_HEADER) + 2 * ALIGN8(database_size / 2), &shm_adr, &shm_size, &shm_handle, TRUE);

   if (status == SS_NO_MEMORY || status == SS_FILE_ERROR) {
      *hDB = 0;
      return DB_INVALID_NAME;
   }

   _database[handle].shm_adr  = shm_adr;
   _database[handle].shm_size = shm_size;
   _database[handle].shm_handle = shm_handle;

   _database[handle].database_header = (DATABASE_HEADER *) shm_adr;

   /* shortcut to header */
   pheader = _database[handle].database_header;

   /* save name */
   strcpy(_database[handle].name, database_name);

   shm_created = (status == SS_CREATED);

   /* clear memeory for debugging */
   /* memset(pheader, 0, sizeof(DATABASE_HEADER) + 2*ALIGN8(database_size/2)); */

   if (shm_created && pheader->name[0] == 0) {
      /* setup header info if database was created */
      memset(pheader, 0, sizeof(DATABASE_HEADER) + 2 * ALIGN8(database_size / 2));

      strcpy(pheader->name, database_name);
      pheader->version = DATABASE_VERSION;
      pheader->key_size = ALIGN8(database_size / 2);
      pheader->data_size = ALIGN8(database_size / 2);
      pheader->root_key = sizeof(DATABASE_HEADER);
      pheader->first_free_key = sizeof(DATABASE_HEADER);
      pheader->first_free_data = sizeof(DATABASE_HEADER) + pheader->key_size;

      /* set up free list */
      pfree = (FREE_DESCRIP *) ((char *) pheader + pheader->first_free_key);
      pfree->size = pheader->key_size;
      pfree->next_free = 0;

      pfree = (FREE_DESCRIP *) ((char *) pheader + pheader->first_free_data);
      pfree->size = pheader->data_size;
      pfree->next_free = 0;

      /* create root key */
      pkey = (KEY *) malloc_key(pheader, sizeof(KEY), "db_open_database_A");
      assert(pkey);

      /* set key properties */
      pkey->type = TID_KEY;
      pkey->num_values = 1;
      pkey->access_mode = MODE_READ | MODE_WRITE | MODE_DELETE;
      strcpy(pkey->name, "root");
      pkey->parent_keylist = 0;

      /* create keylist */
      pkeylist = (KEYLIST *) malloc_key(pheader, sizeof(KEYLIST), "db_open_database_B");
      assert(pkeylist);

      /* store keylist in data field */
      pkey->data = (POINTER_T) pkeylist - (POINTER_T) pheader;
      pkey->item_size = sizeof(KEYLIST);
      pkey->total_size = sizeof(KEYLIST);

      pkeylist->parent = (POINTER_T) pkey - (POINTER_T) pheader;
      pkeylist->num_keys = 0;
      pkeylist->first_key = 0;
   }

   /* check database version */
   if (pheader->version != DATABASE_VERSION) {
      cm_msg(MERROR, "db_open_database",
             "Different database format: Shared memory is %d, program is %d", pheader->version, DATABASE_VERSION);
      return DB_VERSION_MISMATCH;
   }

   /* check root key */
   if (!db_validate_key_offset(pheader, pheader->root_key)) {
      cm_msg(MERROR, "db_open_database", "Invalid, incompatible or corrupted database: root key offset %d is invalid", pheader->root_key);
      return DB_VERSION_MISMATCH;
   } else {
      pkey = (KEY*)((char*)pheader + pheader->root_key);

      if (pkey->type != TID_KEY) {
         cm_msg(MERROR, "db_open_database", "Invalid, incompatible or corrupted database: root key type %d is not TID_KEY", pkey->type);
         return DB_VERSION_MISMATCH;
      }

      if (strcmp(pkey->name, "root") != 0) {
         cm_msg(MERROR, "db_open_database", "Invalid, incompatible or corrupted database: root key name \"%s\" is not \"root\"", pkey->name);
         return DB_VERSION_MISMATCH;
      }

      // what if we are connecting to an incompatible ODB?
      // A call to db_validate_and_repair_key() maybe will
      // corrupt it here. But we have no choice,
      // if we skip it here and continue,
      // db_validate_and_repair_db() will call it later anyway... K.O.
      
      if (!db_validate_and_repair_key(pheader, 0, "", 0, pheader->root_key, pkey)) {
         cm_msg(MERROR, "db_open_database", "Invalid, incompatible or corrupted database: root key is invalid");
         return DB_VERSION_MISMATCH;
      }
   }

   /* set default mutex and semaphore timeout */
   _database[handle].timeout = 10000;

   /* create mutexes for the database */
   status = ss_mutex_create(&_database[handle].mutex, TRUE);
   if (status != SS_SUCCESS && status != SS_CREATED) {
      *hDB = 0;
      return DB_NO_SEMAPHORE;
   }

   /* create semaphore for the database */
   status = ss_semaphore_create(database_name, &(_database[handle].semaphore));
   if (status != SS_SUCCESS && status != SS_CREATED) {
      *hDB = 0;
      return DB_NO_SEMAPHORE;
   }
   _database[handle].lock_cnt = 0;

   _database[handle].protect = FALSE;
   _database[handle].protect_read = FALSE;
   _database[handle].protect_write = FALSE;

   /* first lock database */
   status = db_lock_database(handle + 1);
   if (status != DB_SUCCESS)
      return status;

   /* we have the database locked, without write protection */

   /*
    Now we have a DATABASE_HEADER, so let's setup a CLIENT
    structure in that database. The information there can also
    be seen by other processes.
    */

   /*
    update the client count
    */
   pheader->num_clients = 0;
   pheader->max_client_index = 0;
   for (i = 0; i < MAX_CLIENTS; i++) {
      if (pheader->client[i].pid == 0)
         continue;
      pheader->num_clients++;
      pheader->max_client_index = i + 1;
   }

   /*fprintf(stderr,"num_clients: %d, max_client: %d\n",pheader->num_clients,pheader->max_client_index); */

   /* remove dead clients */
   for (i = 0; i < MAX_CLIENTS; i++) {
      if (pheader->client[i].pid == 0)
         continue;
      if (!ss_pid_exists(pheader->client[i].pid)) {
         char client_name_tmp[NAME_LENGTH];
         int client_pid;

         strlcpy(client_name_tmp, pheader->client[i].name, sizeof(client_name_tmp));
         client_pid = pheader->client[i].pid;

         // removed: /* decrement notify_count for open records and clear exclusive mode */
         // open records are corrected later, by db_validate_open_records()

         /* clear entry from client structure in database header */
         memset(&(pheader->client[i]), 0, sizeof(DATABASE_CLIENT));

         cm_msg(MERROR, "db_open_database", "Removed ODB client \'%s\', index %d because process pid %d does not exists", client_name_tmp, i, client_pid);
      }
   }

   /*
    Look for empty client slot
    */
   for (i = 0; i < MAX_CLIENTS; i++)
      if (pheader->client[i].pid == 0)
         break;

   if (i == MAX_CLIENTS) {
      db_unlock_database(handle + 1);
      *hDB = 0;
      cm_msg(MERROR, "db_open_database", "maximum number of clients exceeded");
      return DB_NO_SLOT;
   }

   /* store slot index in _database structure */
   _database[handle].client_index = i;

   /*
    Save the index of the last client of that database so that later only
    the clients 0..max_client_index-1 have to be searched through.
    */
   pheader->num_clients++;
   if (i + 1 > pheader->max_client_index)
      pheader->max_client_index = i + 1;

   /* setup database header and client structure */
   pclient = &pheader->client[i];

   memset(pclient, 0, sizeof(DATABASE_CLIENT));
   /* use client name previously set by bm_set_name */
   strlcpy(pclient->name, client_name, sizeof(pclient->name));
   pclient->pid = ss_getpid();
   pclient->num_open_records = 0;

   ss_suspend_get_odb_port(&pclient->port);

   pclient->last_activity = ss_millitime();

   cm_get_watchdog_params(&call_watchdog, &timeout);
   pclient->watchdog_timeout = timeout;

   /* check ODB for corruption */
   if (!db_validate_and_repair_db_locked(pheader)) {
      /* do not treat corrupted odb as a fatal error- allow the user
       to preceed at own risk- the database is already corrupted,
       so no further harm can possibly be made. */
      /*
       db_unlock_database(handle + 1);
       *hDB = 0;
       return DB_CORRUPTED;
       */
   }

   /* setup _database entry */
   _database[handle].database_data = _database[handle].database_header + 1;
   _database[handle].attached = TRUE;
   _database[handle].protect = FALSE;
   _database[handle].protect_read = FALSE;
   _database[handle].protect_write = FALSE;

   *hDB = (handle + 1);

   status = db_validate_open_records(handle + 1);
   if (status != DB_SUCCESS) {
      db_unlock_database(handle + 1);
      cm_msg(MERROR, "db_open_database", "Error: db_validate_open_records() status %d", status);
      return status;
   }

   db_unlock_database(handle + 1);

   if (shm_created)
      return DB_CREATED;
   }
#endif                          /* LOCAL_ROUTINES */

   return DB_SUCCESS;
}

/********************************************************************/
/**
Close a database
@param   hDB          ODB handle obtained via cm_get_experiment_database().
@return DB_SUCCESS, DB_INVALID_HANDLE, RPC_NET_ERROR
*/
INT db_close_database(HNDLE hDB)
{
   if (rpc_is_remote())
      return rpc_call(RPC_DB_CLOSE_DATABASE, hDB);

#ifdef LOCAL_ROUTINES
   else {
      DATABASE_HEADER *pheader;
      DATABASE_CLIENT *pclient;
      INT idx, destroy_flag, i, j;
      char xname[256];

      if (hDB > _database_entries || hDB <= 0) {
         cm_msg(MERROR, "db_close_database", "invalid database handle");
         return DB_INVALID_HANDLE;
      }

      /*
         Check if database was opened by current thread. This is necessary
         in the server process where one thread may not close the database
         of other threads.
       */

      /* first lock database */
      db_lock_database(hDB);

      idx = _database[hDB - 1].client_index;
      pheader = _database[hDB - 1].database_header;
      pclient = &pheader->client[idx];

      if (!_database[hDB - 1].attached) {
         db_unlock_database(hDB);
         cm_msg(MERROR, "db_close_database", "invalid database handle");
         return DB_INVALID_HANDLE;
      }

      db_allow_write_locked(&_database[hDB-1], "db_close_database");

      /* close all open records */
      for (i = 0; i < pclient->max_index; i++)
         if (pclient->open_record[i].handle)
            db_remove_open_record(hDB, pclient->open_record[i].handle, FALSE);

      /* mark entry in _database as empty */
      _database[hDB - 1].attached = FALSE;

      /* clear entry from client structure in database header */
      memset(&(pheader->client[idx]), 0, sizeof(DATABASE_CLIENT));

      /* calculate new max_client_index entry */
      for (i = MAX_CLIENTS - 1; i >= 0; i--)
         if (pheader->client[i].pid != 0)
            break;
      pheader->max_client_index = i + 1;

      /* count new number of clients */
      for (i = MAX_CLIENTS - 1, j = 0; i >= 0; i--)
         if (pheader->client[i].pid != 0)
            j++;
      pheader->num_clients = j;

      destroy_flag = (pheader->num_clients == 0);

      /* flush shared memory to disk */
      ss_shm_flush(pheader->name, _database[hDB - 1].shm_adr, _database[hDB - 1].shm_size, _database[hDB - 1].shm_handle);

      strlcpy(xname, pheader->name, sizeof(xname));

      /* unmap shared memory, delete it if we are the last */
      ss_shm_close(xname, _database[hDB - 1].shm_adr, _database[hDB - 1].shm_size, _database[hDB - 1].shm_handle, destroy_flag);

      pheader = NULL; // after ss_shm_close(), pheader points nowhere
      _database[hDB - 1].database_header = NULL; // ditto

      /* unlock database */
      db_unlock_database(hDB);

      /* delete semaphore */
      ss_semaphore_delete(_database[hDB - 1].semaphore, destroy_flag);

      /* update _database_entries */
      if (hDB == _database_entries)
         _database_entries--;

      if (_database_entries > 0)
         _database = (DATABASE *) realloc(_database, sizeof(DATABASE) * (_database_entries));
      else {
         free(_database);
         _database = NULL;
      }

      /* if we are the last one, also delete other semaphores */
      if (destroy_flag) {
         extern INT _semaphore_elog, _semaphore_alarm, _semaphore_history, _semaphore_msg;

         if (_semaphore_elog)
            ss_semaphore_delete(_semaphore_elog, TRUE);
         if (_semaphore_alarm)
            ss_semaphore_delete(_semaphore_alarm, TRUE);
         if (_semaphore_history)
            ss_semaphore_delete(_semaphore_history, TRUE);
         if (_semaphore_msg)
            ss_semaphore_delete(_semaphore_msg, TRUE);
      }

   }
#endif                          /* LOCAL_ROUTINES */

   return DB_SUCCESS;
}

/**dox***************************************************************/
#ifndef DOXYGEN_SHOULD_SKIP_THIS

/*------------------------------------------------------------------*/
INT db_flush_database(HNDLE hDB)
/********************************************************************\

  Routine: db_flush_database

  Purpose: Flushes the shared memory of a database to its disk file.

  Input:
    HNDLE  hDB              Handle to the database, which is used as
                            an index to the _database array.

  Output:
    none

  Function value:
    DB_SUCCESS              Successful completion
    DB_INVALID_HANDLE       Database handle is invalid
    RPC_NET_ERROR           Network error

\********************************************************************/
{
   if (rpc_is_remote())
      return rpc_call(RPC_DB_FLUSH_DATABASE, hDB);

#ifdef LOCAL_ROUTINES
   else {
      DATABASE_HEADER *pheader;

      if (hDB > _database_entries || hDB <= 0) {
         cm_msg(MERROR, "db_close_database", "invalid database handle");
         return DB_INVALID_HANDLE;
      }

      /*
         Check if database was opened by current thread. This is necessary
         in the server process where one thread may not close the database
         of other threads.
       */

      db_lock_database(hDB);
      pheader = _database[hDB - 1].database_header;

      if (!_database[hDB - 1].attached) {
         db_unlock_database(hDB);
         cm_msg(MERROR, "db_close_database", "invalid database handle");
         return DB_INVALID_HANDLE;
      }

      /* flush shared memory to disk */
      ss_shm_flush(pheader->name, _database[hDB - 1].shm_adr, _database[hDB - 1].shm_size, _database[hDB - 1].shm_handle);
      db_unlock_database(hDB);

   }
#endif                          /* LOCAL_ROUTINES */

   return DB_SUCCESS;
}

/*------------------------------------------------------------------*/
INT db_close_all_databases(void)
/********************************************************************\

  Routine: db_close_all_databases

  Purpose: Close all open databases and open records

  Input:
    none

  Output:
    none

  Function value:
    DB_SUCCESS              Successful completion

\********************************************************************/
{
   INT status;

   if (rpc_is_remote()) {
      status = rpc_call(RPC_DB_CLOSE_ALL_DATABASES);
      if (status != DB_SUCCESS)
         return status;
   }

   db_close_all_records();
   db_unwatch_all();

#ifdef LOCAL_ROUTINES
   {
      INT i;

      for (i = _database_entries; i > 0; i--)
         db_close_database(i);
   }
#endif                          /* LOCAL_ROUTINES */
   
   return DB_SUCCESS;
}

/*------------------------------------------------------------------*/
INT db_set_client_name(HNDLE hDB, const char *client_name)
/********************************************************************\

  Routine: db_set_client_name

  Purpose: Set client name for a database. Used by cm_connect_experiment
           if a client name is duplicate and changed.

  Input:
    INT  hDB                Handle to database
    char *client_name       Name of this application

  Output:

  Function value:
    DB_SUCCESS              Successful completion
    RPC_NET_ERROR           Network error

\********************************************************************/
{
   if (rpc_is_remote())
      return rpc_call(RPC_DB_SET_CLIENT_NAME, hDB, client_name);

#ifdef LOCAL_ROUTINES
   {
      DATABASE_HEADER *pheader;
      DATABASE_CLIENT *pclient;
      INT idx;

      idx = _database[hDB - 1].client_index;
      pheader = _database[hDB - 1].database_header;
      pclient = &pheader->client[idx];

      strcpy(pclient->name, client_name);
   }
#endif                          /* LOCAL_ROUTINES */

   return DB_SUCCESS;
}

/**dox***************************************************************/
#endif                          /* DOXYGEN_SHOULD_SKIP_THIS */

/********************************************************************/
/**
Lock a database for exclusive access via system semaphore calls.
@param hDB   Handle to the database to lock
@return DB_SUCCESS, DB_INVALID_HANDLE, DB_TIMEOUT
*/

INT db_lock_database(HNDLE hDB)
{
#ifdef LOCAL_ROUTINES
   int status;

   if (hDB > _database_entries || hDB <= 0) {
      cm_msg(MERROR, "db_lock_database", "invalid database handle %d, aborting...", hDB);
      abort();
      return DB_INVALID_HANDLE;
   }

   /* obtain access mutex in multi-thread applications */
   status = ss_mutex_wait_for(_database[hDB - 1].mutex, _database[hDB - 1].timeout);
   if (status != SS_SUCCESS) {
      cm_msg(MERROR, "db_lock_database", "internal error: cannot obtain access mutex, aborting...");
      abort();
   }

   /* protect this function against recursive call from signal handlers */
   if (_database[hDB - 1].inside_lock_unlock) {
      fprintf(stderr, "db_lock_database: Detected recursive call to db_{lock,unlock}_database() while already inside db_{lock,unlock}_database(). Maybe this is a call from a signal handler. Cannot continue, aborting...\n");
      abort();
   }

   _database[hDB - 1].inside_lock_unlock = 1;

   //static int x = 0;
   //x++;
   //if (x > 5000) {
   //   printf("inside db_lock_database(), press Ctrl-C now!\n");
   //   sleep(5);
   //}

   // test recursive locking
   // static int out=0;
   // out++;
   // printf("HERE %d!\n", out);
   // if (out>10) abort();
   // db_lock_database(hDB);
   // printf("OUT %d!\n", out);

   if (_database[hDB - 1].lock_cnt == 0) {
      _database[hDB - 1].lock_cnt = 1;
      /* wait max. 5 minutes for semaphore (required if locking process is being debugged) */
      status = ss_semaphore_wait_for(_database[hDB - 1].semaphore, _database[hDB - 1].timeout);
      if (status == SS_TIMEOUT) {
         cm_msg(MERROR, "db_lock_database", "timeout obtaining lock for database, exiting...");
         abort();
      }
      if (status != SS_SUCCESS) {
         cm_msg(MERROR, "db_lock_database", "cannot lock database, ss_semaphore_wait_for() status %d, aborting...", status);
         abort();
      }
   } else {
      _database[hDB - 1].lock_cnt++; // we have already the lock (recursive call), so just increase counter
   }

#ifdef CHECK_LOCK_COUNT
   {
      char str[256];

      sprintf(str, "db_lock_database, lock_cnt=%d", _database[hDB - 1].lock_cnt);
      ss_stack_history_entry(str);
   }
#endif

   if (_database[hDB - 1].protect) {
      if (_database[hDB - 1].database_header == NULL) {
         int status;
         assert(!_database[hDB - 1].protect_read);
         assert(!_database[hDB - 1].protect_write);
         status = ss_shm_unprotect(_database[hDB - 1].shm_handle, &_database[hDB - 1].shm_adr, _database[hDB - 1].shm_size, TRUE, FALSE, "db_lock_database");
         if (status != SS_SUCCESS) {
            cm_msg(MERROR, "db_lock_database", "ss_shm_unprotect(TRUE,FALSE) failed with status %d, aborting...", status);
            cm_msg_flush_buffer();
            abort();
         }
         _database[hDB - 1].database_header = (DATABASE_HEADER *) _database[hDB - 1].shm_adr;
         _database[hDB - 1].protect_read = TRUE;
         _database[hDB - 1].protect_write = FALSE;
      }
   }

   _database[hDB - 1].inside_lock_unlock = 0;

#endif                          /* LOCAL_ROUTINES */
   
   return DB_SUCCESS;
}

#ifdef LOCAL_ROUTINES
INT db_allow_write_locked(DATABASE* p, const char* caller_name)
{
   assert(p);
   if (p->protect && !p->protect_write) {
      int status;
      assert(p->lock_cnt > 0);
      assert(p->database_header != NULL);
      assert(p->protect_read);
      status = ss_shm_unprotect(p->shm_handle, &p->shm_adr, p->shm_size, TRUE, TRUE, caller_name);
      if (status != SS_SUCCESS) {
         cm_msg(MERROR, "db_allow_write_locked", "ss_shm_unprotect(TRUE,TRUE) failed with status %d, aborting...", status);
         cm_msg_flush_buffer();
         abort();
      }
      p->database_header = (DATABASE_HEADER *) p->shm_adr;
      p->protect_read = TRUE;
      p->protect_write = TRUE;
   }
   return DB_SUCCESS;
}
#endif                          /* LOCAL_ROUTINES */

/********************************************************************/
/**
Unlock a database via system semaphore calls.
@param hDB   Handle to the database to unlock
@return DB_SUCCESS, DB_INVALID_HANDLE
*/
INT db_unlock_database(HNDLE hDB)
{
#ifdef LOCAL_ROUTINES

   if (hDB > _database_entries || hDB <= 0) {
      cm_msg(MERROR, "db_unlock_database", "invalid database handle %d", hDB);
      return DB_INVALID_HANDLE;
   }
#ifdef CHECK_LOCK_COUNT
   {
      char str[256];

      sprintf(str, "db_unlock_database, lock_cnt=%d", _database[hDB - 1].lock_cnt);
      ss_stack_history_entry(str);
   }
#endif

   /* protect this function against recursive call from signal handlers */
   if (_database[hDB - 1].inside_lock_unlock) {
      fprintf(stderr, "db_unlock_database: Detected recursive call to db_{lock,unlock}_database() while already inside db_{lock,unlock}_database(). Maybe this is a call from a signal handler. Cannot continue, aborting...\n");
      abort();
   }

   _database[hDB - 1].inside_lock_unlock = 1;

   //static int x = 0;
   //x++;
   //if (x > 5000) {
   //   printf("inside db_unlock_database(), press Ctrl-C now!\n");
   //   sleep(5);
   //}

   if (_database[hDB - 1].lock_cnt == 1) {
      ss_semaphore_release(_database[hDB - 1].semaphore);

      if (_database[hDB - 1].protect && _database[hDB - 1].database_header) {
         int status;
         assert(_database[hDB - 1].protect_read);
         assert(_database[hDB - 1].database_header);
         _database[hDB - 1].database_header = NULL;
         status = ss_shm_protect(_database[hDB - 1].shm_handle, _database[hDB - 1].shm_adr, _database[hDB - 1].shm_size);
         if (status != SS_SUCCESS) {
            cm_msg(MERROR, "db_unlock_database", "ss_shm_protect() failed with status %d, aborting...", status);
            cm_msg_flush_buffer();
            abort();
         }
         _database[hDB - 1].protect_read = FALSE;
         _database[hDB - 1].protect_write = FALSE;
      }
   }

   assert(_database[hDB - 1].lock_cnt > 0);
   _database[hDB - 1].lock_cnt--;

   _database[hDB - 1].inside_lock_unlock = 0;

   /* release mutex for multi-thread applications */
   ss_mutex_release(_database[hDB - 1].mutex);

#endif                          /* LOCAL_ROUTINES */
   return DB_SUCCESS;
}

/********************************************************************/
#if 0
INT db_get_lock_cnt(HNDLE hDB)
{
#ifdef LOCAL_ROUTINES

   /* return zero if no ODB is open or we run remotely */
   if (_database_entries == 0)
      return 0;

   if (hDB > _database_entries || hDB <= 0) {
      cm_msg(MERROR, "db_get_lock_cnt", "invalid database handle %d, aborting...", hDB);
      fprintf(stderr, "db_get_lock_cnt: invalid database handle %d, aborting...\n", hDB);
      abort();
      return DB_INVALID_HANDLE;
   }

   return _database[hDB - 1].lock_cnt;
#else
   return 0;
#endif
}
#endif

INT db_set_lock_timeout(HNDLE hDB, int timeout_millisec)
{
#ifdef LOCAL_ROUTINES

   /* return zero if no ODB is open or we run remotely */
   if (_database_entries == 0)
      return 0;

   if (hDB > _database_entries || hDB <= 0) {
      cm_msg(MERROR, "db_set_lock_timeout", "invalid database handle %d, aborting...", hDB);
      fprintf(stderr, "db_set_lock_timeout: invalid database handle %d, aborting...\n", hDB);
      abort();
      return DB_INVALID_HANDLE;
   }

   if (timeout_millisec > 0) {
      _database[hDB - 1].timeout = timeout_millisec;
   }

   return _database[hDB - 1].timeout;
#else
   return 0;
#endif
}

/**
Update last activity time
*/
INT db_update_last_activity(DWORD millitime)
{
   int pid = ss_getpid();
   int i;
   for (i = 0; i < _database_entries; i++) {
      if (_database[i].attached) {
         int must_unlock = 0;
         if (_database[i].protect) {
            must_unlock = 1;
            db_lock_database(i + 1);
            db_allow_write_locked(&_database[i], "db_update_last_activity");
         }
         assert(_database[i].database_header);
         /* update the last_activity entry to show that we are alive */
         int j;
         for (j=0; j<_database[i].database_header->max_client_index; j++) {
            DATABASE_CLIENT* pdbclient = _database[i].database_header->client + j;
            //printf("client %d pid %d vs our pid %d\n", j, pdbclient->pid, pid);
            if (pdbclient->pid == pid) {
               pdbclient->last_activity = millitime;
            }
         }
         if (must_unlock) {
            db_unlock_database(i + 1);
         }
      }
   }
   return DB_SUCCESS;
}

void db_cleanup(const char *who, DWORD actual_time, BOOL wrong_interval)
{
   int status;
   int i;
   /* check online databases */
   for (i = 0; i < _database_entries; i++) {
      if (_database[i].attached) {
         int must_unlock = 0;
         if (_database[i].protect) {
            must_unlock = 1;
            db_lock_database(i + 1);
            db_allow_write_locked(&_database[i], "db_cleanup");
         }
         assert(_database[i].database_header);
         /* update the last_activity entry to show that we are alive */
         DATABASE_HEADER* pdbheader = _database[i].database_header;
         DATABASE_CLIENT* pdbclient = pdbheader->client;
         pdbclient[_database[i].client_index].last_activity = actual_time;

         /* don't check other clients if interval is stange */
         if (wrong_interval) {
            if (must_unlock) {
               db_unlock_database(i + 1);
            }
            continue;
         }

         /* now check other clients */
         int j;
         for (j = 0; j < pdbheader->max_client_index; j++, pdbclient++) {
            int client_pid = pdbclient->pid;
            if (client_pid == 0)
               continue;
            BOOL dead = !ss_pid_exists(client_pid);
            /* If client process has no activity, clear its buffer entry. */
            if (dead ||
                (pdbclient->watchdog_timeout > 0 &&
                 actual_time - pdbclient->last_activity > pdbclient->watchdog_timeout)
                ) {
               
               db_lock_database(i + 1);

               /* now make again the check with the buffer locked */
               actual_time = ss_millitime();
               if (dead ||
                   (pdbclient->watchdog_timeout &&
                    actual_time > pdbclient->last_activity &&
                    actual_time - pdbclient->last_activity > pdbclient->watchdog_timeout)
                   ) {

                  db_allow_write_locked(&_database[i], "db_cleanup");
                  
                  if (dead) {
                     cm_msg(MINFO, "db_cleanup", "Client \'%s\' on database \'%s\' removed by db_cleanup called by %s because pid %d does not exist", pdbclient->name, pdbheader->name, who, client_pid);
                  } else {
                     cm_msg(MINFO, "db_cleanup", "Client \'%s\' (PID %d) on database \'%s\' removed by db_cleanup called by %s (idle %1.1lfs,TO %1.0lfs)",
                            pdbclient->name, client_pid, pdbheader->name,
                            who,
                            (actual_time - pdbclient->last_activity) / 1000.0,
                            pdbclient->watchdog_timeout / 1000.0);
                  }

                  /* decrement notify_count for open records and clear exclusive mode */
                  int k;
                  for (k = 0; k < pdbclient->max_index; k++)
                     if (pdbclient->open_record[k].handle) {
                        KEY* pkey = (KEY *) ((char *) pdbheader + pdbclient->open_record[k].handle);
                        if (pkey->notify_count > 0)
                           pkey->notify_count--;

                        if (pdbclient->open_record[k].access_mode & MODE_WRITE)
                           db_set_mode(i + 1, pdbclient->open_record[k].handle, (WORD) (pkey->access_mode & ~MODE_EXCLUSIVE), 2);
                     }

                  status = cm_delete_client_info(i + 1, client_pid);
                  if (status != CM_SUCCESS)
                     cm_msg(MERROR, "db_cleanup", "Cannot delete client info for client \'%s\', pid %d from database \'%s\', cm_delete_client_info() status %d", pdbclient->name, client_pid, pdbheader->name, status);

                  /* clear entry from client structure in buffer header */
                  memset(&(pdbheader->client[j]), 0, sizeof(DATABASE_CLIENT));

                  /* calculate new max_client_index entry */
                  for (k = MAX_CLIENTS - 1; k >= 0; k--)
                     if (pdbheader->client[k].pid != 0)
                        break;
                  pdbheader->max_client_index = k + 1;

                  /* count new number of clients */
                  int nc;
                  for (k = MAX_CLIENTS - 1, nc = 0; k >= 0; k--)
                     if (pdbheader->client[k].pid != 0)
                        nc++;
                  pdbheader->num_clients = nc;
               }

               db_unlock_database(i + 1);
            }
         }
         if (must_unlock) {
            db_unlock_database(i + 1);
         }
      }
   }
}

void db_cleanup2(const char* client_name, int ignore_timeout, DWORD actual_time,  const char *who)
{
   /* check online databases */
   int i;
   for (i = 0; i < _database_entries; i++) {
      if (_database[i].attached) {
         /* update the last_activity entry to show that we are alive */
         
         db_lock_database(i + 1);
         
         db_allow_write_locked(&_database[i], "db_cleanup2");
         
         DATABASE_HEADER* pdbheader = _database[i].database_header;
         DATABASE_CLIENT* pdbclient = pdbheader->client;
         pdbclient[_database[i].client_index].last_activity = ss_millitime();
         
         /* now check other clients */
         int j;
         for (j = 0; j < pdbheader->max_client_index; j++, pdbclient++)
            if (j != _database[i].client_index && pdbclient->pid &&
                (client_name == NULL || client_name[0] == 0
                 || strncmp(pdbclient->name, client_name, strlen(client_name)) == 0)) {
               int client_pid = pdbclient->pid;
               BOOL dead = !ss_pid_exists(client_pid);
               DWORD interval;
               if (ignore_timeout)
                  interval = 2 * WATCHDOG_INTERVAL;
               else
                  interval = pdbclient->watchdog_timeout;
               
               /* If client process has no activity, clear its buffer entry. */
               
               if (dead || (interval > 0 && ss_millitime() - pdbclient->last_activity > interval)) {
                  int bDeleted = FALSE;
                  
                  /* now make again the check with the buffer locked */
                  if (dead || (interval > 0 && ss_millitime() - pdbclient->last_activity > interval)) {
                     if (dead) {
                        cm_msg(MINFO, "db_cleanup2", "Client \'%s\' on \'%s\' removed by db_cleanup2 called by %s because pid %d does not exist",
                               pdbclient->name, pdbheader->name,
                               who,
                               client_pid);
                     } else {
                        cm_msg(MINFO, "db_cleanup2", "Client \'%s\' on \'%s\' removed by db_cleanup2 called by %s (idle %1.1lfs,TO %1.0lfs)",
                               pdbclient->name, pdbheader->name,
                               who,
                               (ss_millitime() - pdbclient->last_activity) / 1000.0, interval / 1000.0);
                     }
                     
                     /* decrement notify_count for open records and clear exclusive mode */
                     int k;
                     for (k = 0; k < pdbclient->max_index; k++)
                        if (pdbclient->open_record[k].handle) {
                          KEY* pkey = (KEY *) ((char *) pdbheader + pdbclient->open_record[k].handle);
                           if (pkey->notify_count > 0)
                              pkey->notify_count--;
                           
                           if (pdbclient->open_record[k].access_mode & MODE_WRITE)
                              db_set_mode(i + 1, pdbclient->open_record[k].handle, (WORD) (pkey->access_mode & ~MODE_EXCLUSIVE), 2);
                        }
                     
                     /* clear entry from client structure in buffer header */
                     memset(&(pdbheader->client[j]), 0, sizeof(DATABASE_CLIENT));
                     
                     /* calculate new max_client_index entry */
                     for (k = MAX_CLIENTS - 1; k >= 0; k--)
                        if (pdbheader->client[k].pid != 0)
                           break;
                     pdbheader->max_client_index = k + 1;
                     
                     /* count new number of clients */
                     int nc;
                     for (k = MAX_CLIENTS - 1, nc = 0; k >= 0; k--)
                        if (pdbheader->client[k].pid != 0)
                           nc++;
                     pdbheader->num_clients = nc;
                     
                     bDeleted = TRUE;
                  }
                  
                  
                  /* delete client entry after unlocking db */
                  if (bDeleted) {
                     int status;
                     status = cm_delete_client_info(i + 1, client_pid);
                     if (status != CM_SUCCESS)
                        cm_msg(MERROR, "db_cleanup2", "cannot delete client info, cm_delete_client_into() status %d", status);
                     
                     pdbheader = _database[i].database_header;
                     pdbclient = pdbheader->client;
                     
                     /* go again though whole list */
                     j = 0;
                  }
               }
            }
         
         db_unlock_database(i + 1);
      }
   }
}

void db_set_watchdog_params(DWORD timeout)
{
   /* set watchdog flag of all open databases */
   int i;
   for (i = _database_entries; i > 0; i--) {
      DATABASE_HEADER *pheader;
      DATABASE_CLIENT *pclient;
      INT idx;
      
      db_lock_database(i);
      
      idx = _database[i - 1].client_index;
      pheader = _database[i - 1].database_header;
      pclient = &pheader->client[idx];
      
      if (!_database[i - 1].attached) {
         db_unlock_database(i);
         continue;
      }
      
      db_allow_write_locked(&_database[i-1], "db_set_watchdog_params");
      
      /* clear entry from client structure in buffer header */
      pclient->watchdog_timeout = timeout;
      
      /* show activity */
      pclient->last_activity = ss_millitime();
      
      db_unlock_database(i);
   }
}

/********************************************************************/
/**
Return watchdog information about specific client
@param    hDB              ODB handle
@param    client_name     ODB client name
@param    timeout         Timeout for this application in seconds
@param    last            Last time watchdog was called in msec
@return   CM_SUCCESS, CM_NO_CLIENT, DB_INVALID_HANDLE
*/

INT db_get_watchdog_info(HNDLE hDB, const char *client_name, DWORD * timeout, DWORD * last)
{
   DATABASE_HEADER *pheader;
   DATABASE_CLIENT *pclient;
   INT i;
   
   if (hDB > _database_entries || hDB <= 0) {
      cm_msg(MERROR, "db_get_watchdog_info", "invalid database handle");
      return DB_INVALID_HANDLE;
   }
   
   if (!_database[hDB - 1].attached) {
      cm_msg(MERROR, "db_get_watchdog_info", "invalid database handle");
      return DB_INVALID_HANDLE;
   }
   
   /* lock database */
   db_lock_database(hDB);
   
   pheader = _database[hDB - 1].database_header;
   pclient = pheader->client;
   
   /* find client */
   for (i = 0; i < pheader->max_client_index; i++, pclient++)
      if (pclient->pid && equal_ustring(pclient->name, client_name)) {
         *timeout = pclient->watchdog_timeout;
         *last = ss_millitime() - pclient->last_activity;
         db_unlock_database(hDB);
         return DB_SUCCESS;
      }
   
   *timeout = *last = 0;

   db_unlock_database(hDB);

   return CM_NO_CLIENT;
}

/********************************************************************/
/**
Check if a client with a /system/client/xxx entry has
a valid entry in the ODB client table. If not, remove
that client from the /system/client tree.
@param   hDB               Handle to online database
@param   hKeyClient        Handle to client key
@return  CM_SUCCESS, CM_NO_CLIENT
*/
INT db_check_client(HNDLE hDB, HNDLE hKeyClient)
{
   KEY key;
   DATABASE_HEADER *pheader;
   DATABASE_CLIENT *pclient;
   INT i, client_pid, status, dead = 0, found = 0;
   char name[NAME_LENGTH];
   
   db_lock_database(hDB);
   
   status = db_get_key(hDB, hKeyClient, &key);
   if (status != DB_SUCCESS)
      return CM_NO_CLIENT;
   
   client_pid = atoi(key.name);
   
   name[0] = 0;
   i = sizeof(name);
   status = db_get_value(hDB, hKeyClient, "Name", name, &i, TID_STRING, FALSE);
   
   //fprintf(stderr, "db_check_client: hkey %d, status %d, pid %d, name \'%s\', my name %s\n", hKeyClient, status, client_pid, name, _client_name);
   
   if (status != DB_SUCCESS) {
      db_unlock_database(hDB);
      return CM_NO_CLIENT;
   }
   
   if (_database[hDB - 1].attached) {
      pheader = _database[hDB - 1].database_header;
      pclient = pheader->client;
      
      /* loop through clients */
      for (i = 0; i < pheader->max_client_index; i++, pclient++)
         if (pclient->pid == client_pid) {
            found = 1;
            break;
         }
      
      if (found) {
         /* check that the client is still running: PID still exists */
         if (!ss_pid_exists(client_pid)) {
            dead = 1;
         }
      }
      
      if (!found || dead) {
         /* client not found : delete ODB stucture */
         
         status = cm_delete_client_info(hDB, client_pid);
         
         if (status != CM_SUCCESS)
            cm_msg(MERROR, "db_check_client", "Cannot delete client info for client \'%s\', pid %d, cm_delete_client_info() status %d", name, client_pid, status);
         else if (!found)
            cm_msg(MINFO, "db_check_client", "Deleted entry \'/System/Clients/%d\' for client \'%s\' because it is not connected to ODB", client_pid, name);
         else if (dead)
            cm_msg(MINFO, "db_check_client", "Deleted entry \'/System/Clients/%d\' for client \'%s\' because process pid %d does not exists", client_pid, name, client_pid);
         
         db_unlock_database(hDB);
         
         return CM_NO_CLIENT;
      }
   }
   
   db_unlock_database(hDB);

   return DB_SUCCESS;
}

/********************************************************************/
/**
Protect a database for read/write access outside of the \b db_xxx functions
@param hDB          ODB handle obtained via cm_get_experiment_database().
@return DB_SUCCESS, DB_INVALID_HANDLE
*/
INT db_protect_database(HNDLE hDB)
{
   if (rpc_is_remote())
      return DB_SUCCESS;

#ifdef LOCAL_ROUTINES
   if (hDB > _database_entries || hDB <= 0) {
      cm_msg(MERROR, "db_protect_database", "invalid database handle %d", hDB);
      return DB_INVALID_HANDLE;
   }

   _database[hDB - 1].protect = TRUE;
   ss_shm_protect(_database[hDB - 1].shm_handle, _database[hDB - 1].database_header, _database[hDB - 1].shm_size);
   _database[hDB - 1].database_header = NULL;
#endif                          /* LOCAL_ROUTINES */
   return DB_SUCCESS;
}

/*---- helper routines ---------------------------------------------*/

const char *extract_key(const char *key_list, char *key_name, int key_name_length)
{
   int i = 0;

   if (*key_list == '/')
      key_list++;

   while (*key_list && *key_list != '/' && ++i < key_name_length)
      *key_name++ = *key_list++;
   *key_name = 0;

   return key_list;
}

BOOL equal_ustring(const char *str1, const char *str2)
{
   if (str1 == NULL && str2 != NULL)
      return FALSE;
   if (str1 != NULL && str2 == NULL)
      return FALSE;
   if (str1 == NULL && str2 == NULL)
      return TRUE;
   if (strlen(str1) != strlen(str2))
      return FALSE;

   while (*str1)
      if (toupper(*str1++) != toupper(*str2++))
         return FALSE;

   if (*str2)
      return FALSE;

   return TRUE;
}

BOOL ends_with_ustring(const char *str, const char *suffix)
{
   int len_str = strlen(str);
   int len_suffix = strlen(suffix);

   // suffix is longer than the string
   if (len_suffix > len_str)
      return FALSE;

   return equal_ustring(str + len_str - len_suffix, suffix);
}

/********************************************************************/
/**
Create a new key in a database
@param hDB          ODB handle obtained via cm_get_experiment_database().
@param hKey  Key handle to start with, 0 for root
@param key_name    Name of key in the form "/key/key/key"
@param type        Type of key, one of TID_xxx (see @ref Midas_Data_Types)
@return DB_SUCCESS, DB_INVALID_HANDLE, DB_INVALID_PARAM, DB_FULL, DB_KEY_EXIST, DB_NO_ACCESS
*/
INT db_create_key(HNDLE hDB, HNDLE hKey, const char *key_name, DWORD type)
{

   if (rpc_is_remote())
      return rpc_call(RPC_DB_CREATE_KEY, hDB, hKey, key_name, type);

#ifdef LOCAL_ROUTINES
   {
      DATABASE_HEADER *pheader;
      KEYLIST *pkeylist;
      KEY *pkey, *pprev_key, *pkeyparent;
      const char *pkey_name;
      INT i;
      int status;

      if (hDB > _database_entries || hDB <= 0) {
         cm_msg(MERROR, "db_create_key", "invalid database handle");
         return DB_INVALID_HANDLE;
      }

      if (!_database[hDB - 1].attached) {
         cm_msg(MERROR, "db_create_key", "invalid database handle");
         return DB_INVALID_HANDLE;
      }

      status = db_validate_name(key_name, TRUE, "db_create_key");
      if (status != DB_SUCCESS)
         return status;

      /* check type */
      if (type <= 0 || type >= TID_LAST) {
         char str[MAX_ODB_PATH];
         db_get_path(hDB, hKey, str, sizeof(str));
         cm_msg(MERROR, "db_create_key", "invalid key type %d to create \'%s\' in \'%s\'", type, key_name, str);
         return DB_INVALID_PARAM;
      }

      /* lock database */
      db_lock_database(hDB);

      pheader = _database[hDB - 1].database_header;
      if (!hKey)
         hKey = pheader->root_key;

      /* check if hKey argument is correct */
      if (!db_validate_hkey(pheader, hKey)) {
         db_unlock_database(hDB);
         return DB_INVALID_HANDLE;
      }
      
      pkey = (KEY *) ((char *) pheader + hKey);

      db_allow_write_locked(&_database[hDB-1], "db_create_key");

      if (pkey->type != TID_KEY) {
         DWORD xtid = pkey->type;
         db_unlock_database(hDB);
         char str[MAX_ODB_PATH];
         db_get_path(hDB, hKey, str, sizeof(str));
         cm_msg(MERROR, "db_create_key", "cannot create \'%s\' in \'%s\' tid is %d, not a directory", key_name, str, xtid);
         return DB_NO_KEY;
      }
      pkeylist = (KEYLIST *) ((char *) pheader + pkey->data);

      pkey_name = key_name;
      do {
         // key name is limited to NAME_LENGTH, but buffer has to be slightly longer
         // to prevent truncation before db_validate_name checks for correct length. K.O.
         char str[NAME_LENGTH+100];

         /* extract single key from key_name */
         pkey_name = extract_key(pkey_name, str, sizeof(str));

         status = db_validate_name(str, FALSE, "db_create_key");
         if (status != DB_SUCCESS) {
            db_unlock_database(hDB);
            return status;
         }

         /* do not allow empty names, like '/dir/dir//dir/' */
         if (str[0] == 0) {
            db_unlock_database(hDB);
            return DB_INVALID_PARAM;
         }

         /* check if parent or current directory */
         if (strcmp(str, "..") == 0) {
            if (pkey->parent_keylist) {
               pkeylist = (KEYLIST *) ((char *) pheader + pkey->parent_keylist);
               // FIXME: validate pkeylist->parent
               pkey = (KEY *) ((char *) pheader + pkeylist->parent);
            }
            continue;
         }
         if (strcmp(str, ".") == 0)
            continue;

         /* check if key is in keylist */
         // FIXME: validate pkeylist->first_key
         pkey = (KEY *) ((char *) pheader + pkeylist->first_key);
         pprev_key = NULL;

         for (i = 0; i < pkeylist->num_keys; i++) {
            if (!db_validate_key_offset(pheader, pkey->next_key)) {
               int pkey_next_key = pkey->next_key;
               db_unlock_database(hDB);
               char str[MAX_ODB_PATH];
               db_get_path(hDB, hKey, str, sizeof(str));
               cm_msg(MERROR, "db_create_key", "Error: database corruption, key \"%s\", next_key 0x%08X, while creating \'%s\' in \'%s\'", key_name, pkey_next_key - (int)sizeof(DATABASE_HEADER), key_name, str);
               return DB_CORRUPTED;
            }

            if (equal_ustring(str, pkey->name))
               break;

            pprev_key = pkey;
            pkey = (KEY *) ((char *) pheader + pkey->next_key); // FIXME: pkey->next_key could be zero
         }

         if (i == pkeylist->num_keys) {
            /* not found: create new key */

            /* check parent for write access */
            // FIXME: validate pkeylist->parent
            pkeyparent = (KEY *) ((char *) pheader + pkeylist->parent);
            if (!(pkeyparent->access_mode & MODE_WRITE) || (pkeyparent->access_mode & MODE_EXCLUSIVE)) {
               db_unlock_database(hDB);
               return DB_NO_ACCESS;
            }

            pkeylist->num_keys++;

            if (*pkey_name == '/' || type == TID_KEY) {
               /* create new key with keylist */
               pkey = (KEY *) malloc_key(pheader, sizeof(KEY), "db_create_key_A");

               if (pkey == NULL) {
                  db_unlock_database(hDB);
                  char str[MAX_ODB_PATH];
                  db_get_path(hDB, hKey, str, sizeof(str));
                  cm_msg(MERROR, "db_create_key", "online database full while creating \'%s\'", str);
                  return DB_FULL;
               }

               /* append key to key list */
               if (pprev_key)
                  pprev_key->next_key = (POINTER_T) pkey - (POINTER_T) pheader;
               else
                  pkeylist->first_key = (POINTER_T) pkey - (POINTER_T) pheader;

               /* set key properties */
               pkey->type = TID_KEY;
               pkey->num_values = 1;
               pkey->access_mode = MODE_READ | MODE_WRITE | MODE_DELETE;
               strlcpy(pkey->name, str, sizeof(pkey->name));
               pkey->parent_keylist = (POINTER_T) pkeylist - (POINTER_T) pheader;

               /* find space for new keylist */
               pkeylist = (KEYLIST *) malloc_key(pheader, sizeof(KEYLIST), "db_create_key_B");

               if (pkeylist == NULL) {
                  db_unlock_database(hDB);
                  char str[MAX_ODB_PATH];
                  db_get_path(hDB, hKey, str, sizeof(str));
                  cm_msg(MERROR, "db_create_key", "online database full while creating \'%s\' in \'%s'", key_name, str);
                  return DB_FULL;
               }

               /* store keylist in data field */
               pkey->data = (POINTER_T) pkeylist - (POINTER_T) pheader;
               pkey->item_size = sizeof(KEYLIST);
               pkey->total_size = sizeof(KEYLIST);

               pkeylist->parent = (POINTER_T) pkey - (POINTER_T) pheader;
               pkeylist->num_keys = 0;
               pkeylist->first_key = 0;
            } else {
               /* create new key with data */
               pkey = (KEY *) malloc_key(pheader, sizeof(KEY), "db_create_key_C");

               if (pkey == NULL) {
                  db_unlock_database(hDB);
                  char str[MAX_ODB_PATH];
                  db_get_path(hDB, hKey, str, sizeof(str));
                  cm_msg(MERROR, "db_create_key", "online database full while creating \'%s\'", str);
                  return DB_FULL;
               }

               /* append key to key list */
               if (pprev_key)
                  pprev_key->next_key = (POINTER_T) pkey - (POINTER_T) pheader;
               else
                  pkeylist->first_key = (POINTER_T) pkey - (POINTER_T) pheader;

               pkey->type = type;
               pkey->num_values = 1;
               pkey->access_mode = MODE_READ | MODE_WRITE | MODE_DELETE;
               strlcpy(pkey->name, str, sizeof(pkey->name));
               pkey->parent_keylist = (POINTER_T) pkeylist - (POINTER_T) pheader;

               /* zero data */
               if (type != TID_STRING && type != TID_LINK) {
                  pkey->item_size = rpc_tid_size(type);
                  pkey->data = (POINTER_T) malloc_data(pheader, pkey->item_size);
                  pkey->total_size = pkey->item_size;

                  if (pkey->data == 0) {
                     pkey->total_size = 0;
                     db_unlock_database(hDB);
                     char str[MAX_ODB_PATH];
                     db_get_path(hDB, hKey, str, sizeof(str));
                     cm_msg(MERROR, "db_create_key", "online database full while creating \'%s\' in \'%s\'", key_name, str);
                     return DB_FULL;
                  }

                  pkey->data -= (POINTER_T) pheader;
               } else {
                  /* first data is empty */
                  pkey->item_size = 0;
                  pkey->total_size = 0;
                  pkey->data = 0;
               }
            }
         } else {
            /* key found: descend */

            /* resolve links */
            if (pkey->type == TID_LINK && pkey_name[0]) {
               /* copy destination, strip '/' */
               strcpy(str, (char *) pheader + pkey->data);
               if (str[strlen(str) - 1] == '/')
                  str[strlen(str) - 1] = 0;

               /* append rest of key name */
               strcat(str, pkey_name);

               db_unlock_database(hDB);

               return db_create_key(hDB, 0, str, type);
            }

            if (!(*pkey_name == '/')) {
               DWORD xtid = pkey->type;
               db_unlock_database(hDB);

               if (xtid != type) {
                  char path[MAX_ODB_PATH];
                  db_get_path(hDB, hKey, path, sizeof(path));
                  cm_msg(MERROR, "db_create_key", "object of type %d already exists while creating \'%s\' of type %d in \'%s\'", xtid, key_name, type, path);
               }

               return DB_KEY_EXIST;
            }

            if (pkey->type != TID_KEY) {
               db_unlock_database(hDB);
               char path[MAX_ODB_PATH];
               db_get_path(hDB, hKey, path, sizeof(path));
               cm_msg(MERROR, "db_create_key", "path element \"%s\" in \"%s\" is not a subdirectory while creating \'%s\' in \'%s\'", str, key_name, key_name, path);
               return DB_KEY_EXIST;
            }

            pkeylist = (KEYLIST *) ((char *) pheader + pkey->data);
         }
      } while (*pkey_name == '/');

      db_unlock_database(hDB);
   }
#endif                          /* LOCAL_ROUTINES */

   return DB_SUCCESS;
}

/********************************************************************/
/**
Create a link to a key or set the destination of and existing link.
@param hDB           ODB handle obtained via cm_get_experiment_database().
@param hKey          Key handle to start with, 0 for root
@param link_name     Name of key in the form "/key/key/key"
@param destination   Destination of link in the form "/key/key/key"
@return DB_SUCCESS, DB_INVALID_HANDLE, DB_FULL, DB_KEY_EXIST, DB_NO_ACCESS, DB_INVALID_NAME
*/
INT db_create_link(HNDLE hDB, HNDLE hKey, const char *link_name, const char *destination)
{
   HNDLE hkey;
   int status;

   if (rpc_is_remote())
      return rpc_call(RPC_DB_CREATE_LINK, hDB, hKey, link_name, destination);

   if (destination == NULL) {
      cm_msg(MERROR, "db_create_link", "link destination name is NULL");
      return DB_INVALID_NAME;
   }

   if (destination[0] != '/') {
      cm_msg(MERROR, "db_create_link", "link destination name \'%s\' should start with \'/\', relative links are forbidden", destination);
      return DB_INVALID_NAME;
   }

   if (strlen(destination) < 1) {
      cm_msg(MERROR, "db_create_link", "link destination name \'%s\' is too short", destination);
      return DB_INVALID_NAME;
   }

   if ((destination[0] == '/') && (destination[1] == 0)) {
      cm_msg(MERROR, "db_create_link", "links to \"/\" are forbidden");
      return DB_INVALID_NAME;
   }

   /* check if destination exists */
   status = db_find_key(hDB, hKey, destination, &hkey);
   if (status != DB_SUCCESS) {
      cm_msg(MERROR, "db_create_link", "Link destination \"%s\" does not exist", destination);
      return DB_NO_KEY;
   }

   //printf("db_create_link: [%s] hkey %d\n", destination, hkey);

   return db_set_value(hDB, hKey, link_name, destination, strlen(destination) + 1, 1, TID_LINK);
}

/********************************************************************/
/**
Delete a subtree, using level information (only called internally by db_delete_key())
@internal
@param hDB          ODB handle obtained via cm_get_experiment_database().
@param hKey  Key handle to start with, 0 for root
@param level            Recursion level, must be zero when
@param follow_links     Follow links when TRUE called from a user routine
@return DB_SUCCESS, DB_INVALID_HANDLE, DB_OPEN_RECORD
*/
INT db_delete_key1(HNDLE hDB, HNDLE hKey, INT level, BOOL follow_links)
{
#ifdef LOCAL_ROUTINES
   {
      DATABASE_HEADER *pheader;
      KEYLIST *pkeylist;
      KEY *pkey, *pnext_key, *pkey_tmp;
      HNDLE hKeyLink;
      BOOL deny_delete;
      INT status;
      BOOL locked = FALSE;

      if (hDB > _database_entries || hDB <= 0) {
         cm_msg(MERROR, "db_delete_key1", "invalid database handle");
         return DB_INVALID_HANDLE;
      }

      if (!_database[hDB - 1].attached) {
         cm_msg(MERROR, "db_delete_key1", "invalid database handle");
         return DB_INVALID_HANDLE;
      }

      if (hKey < (int) sizeof(DATABASE_HEADER)) {
         cm_msg(MERROR, "db_delete_key1", "invalid key handle");
         return DB_INVALID_HANDLE;
      }

      /* lock database at the top level */
      if (level == 0) {
         db_lock_database(hDB);
         locked = TRUE;
      }

      pheader = _database[hDB - 1].database_header;

      /* check if hKey argument is correct */
      if (!db_validate_hkey(pheader, hKey)) {
         if (locked)
            db_unlock_database(hDB);
         return DB_INVALID_HANDLE;
      }

      pkey = (KEY *) ((char *) pheader + hKey);

      /* check if someone has opened key or parent */
      if (level == 0)
         do {
#ifdef CHECK_OPEN_RECORD
            if (pkey->notify_count) {
               if (locked)
                  db_unlock_database(hDB);
               return DB_OPEN_RECORD;
            }
#endif
            if (pkey->parent_keylist == 0)
               break;

            pkeylist = (KEYLIST *) ((char *) pheader + pkey->parent_keylist);
            // FIXME: validate pkeylist->parent
            pkey = (KEY *) ((char *) pheader + pkeylist->parent);
         } while (TRUE);

      pkey = (KEY *) ((char *) pheader + hKey);
      pkeylist = (KEYLIST *) ((char *) pheader + pkey->data);

      deny_delete = FALSE;

      /* first recures subtree for keys */
      if (pkey->type == TID_KEY && pkeylist->first_key) {
         pkey = (KEY *) ((char *) pheader + pkeylist->first_key);

         do {
            pnext_key = (KEY *) (POINTER_T) pkey->next_key; // FIXME: what is this casting of hKey to pointer?

            status = db_delete_key1(hDB, (POINTER_T) pkey - (POINTER_T) pheader, level + 1, follow_links);

            if (status == DB_NO_ACCESS)
               deny_delete = TRUE;

            if (pnext_key) {
               // FIXME: validate pnext_key
               pkey = (KEY *) ((char *) pheader + (POINTER_T) pnext_key);
            }
         } while (pnext_key);
      }

      /* follow links if requested */
      if (pkey->type == TID_LINK && follow_links) {
         status = db_find_key1(hDB, 0, (char *) pheader + pkey->data, &hKeyLink);
         if (status == DB_SUCCESS && follow_links < 100)
            db_delete_key1(hDB, hKeyLink, level + 1, follow_links + 1);

         if (follow_links == 100)
            cm_msg(MERROR, "db_delete_key1", "try to delete cyclic link");
      }

      /* check if hKey argument is correct */
      if (!db_validate_hkey(pheader, hKey)) {
         if (locked)
            db_unlock_database(hDB);
         return DB_INVALID_HANDLE;
      }

      pkey = (KEY *) ((char *) pheader + hKey);

      /* return if key was already deleted by cyclic link */
      if (pkey->parent_keylist == 0) {
         if (locked)
            db_unlock_database(hDB);
         return DB_SUCCESS;
      }

      /* now delete key */
      if (hKey != pheader->root_key) {
         if (!(pkey->access_mode & MODE_DELETE) || deny_delete) {
            if (locked)
               db_unlock_database(hDB);
            return DB_NO_ACCESS;
         }
#ifdef CHECK_OPEN_RECORD
         if (pkey->notify_count) {
            if (locked)
               db_unlock_database(hDB);
            return DB_OPEN_RECORD;
         }
#endif
         db_allow_write_locked(&_database[hDB - 1], "db_delete_key1");

         /* delete key data */
         if (pkey->type == TID_KEY)
            free_key(pheader, (char *) pheader + pkey->data, pkey->total_size);
         else
            free_data(pheader, (char *) pheader + pkey->data, pkey->total_size, "db_delete_key1");

         /* unlink key from list */
         pnext_key = (KEY *) (POINTER_T) pkey->next_key;
         pkeylist = (KEYLIST *) ((char *) pheader + pkey->parent_keylist);

         if ((KEY *) ((char *) pheader + pkeylist->first_key) == pkey) {
            /* key is first in list */
            pkeylist->first_key = (POINTER_T) pnext_key;
         } else {
            /* find predecessor */
            pkey_tmp = (KEY *) ((char *) pheader + pkeylist->first_key);
            while ((KEY *) ((char *) pheader + pkey_tmp->next_key) != pkey)
               pkey_tmp = (KEY *) ((char *) pheader + pkey_tmp->next_key);
            pkey_tmp->next_key = (POINTER_T) pnext_key;
         }

         /* delete key */
         free_key(pheader, pkey, sizeof(KEY));
         pkeylist->num_keys--;
      }

      if (locked)
         db_unlock_database(hDB);
   }
#endif                          /* LOCAL_ROUTINES */

   return DB_SUCCESS;
}

/********************************************************************/
/**
Delete a subtree in a database starting from a key (including this key).
\code
...
    status = db_find_link(hDB, 0, str, &hkey);
    if (status != DB_SUCCESS)
    {
      cm_msg(MINFO,"my_delete"," "Cannot find key %s", str);
      return;
    }

    status = db_delete_key(hDB, hkey, FALSE);
    if (status != DB_SUCCESS)
    {
      cm_msg(MERROR,"my_delete"," "Cannot delete key %s", str);
      return;
    }
  ...
\endcode
@param hDB          ODB handle obtained via cm_get_experiment_database().
@param hKey         for key where search starts, zero for root.
@param follow_links Follow links when TRUE.
@return DB_SUCCESS, DB_INVALID_HANDLE, DB_NO_ACCESS, DB_OPEN_RECORD
*/
INT db_delete_key(HNDLE hDB, HNDLE hKey, BOOL follow_links)
{
   if (rpc_is_remote())
      return rpc_call(RPC_DB_DELETE_KEY, hDB, hKey, follow_links);

   return db_delete_key1(hDB, hKey, 0, follow_links);
}

#ifdef LOCAL_ROUTINES
static INT db_find_key_locked(const DATABASE_HEADER *pheader, HNDLE hKey, const char *key_name, HNDLE * subhKey, db_err_msg **msg)
{
   int status;
   const KEY* pkey = db_get_pkey(pheader, hKey, &status, "db_find_key", msg);
   if (!pkey) {
      if (subhKey)
         *subhKey = 0;
      return status;
   }

   if (pkey->type != TID_KEY) {
      DWORD tid = pkey->type;
      char str[MAX_ODB_PATH];
      db_get_path_locked(pheader, hKey, str, sizeof(str));
      *subhKey = 0;
      db_msg(msg, MERROR, "db_find_key", "hkey %d path \"%s\" tid %d is not a directory", hKey, str, tid);
      return DB_NO_KEY;
   }
   
   if (key_name[0] == 0 || strcmp(key_name, "/") == 0) {
      if (!(pkey->access_mode & MODE_READ)) {
         *subhKey = 0;
         return DB_NO_ACCESS;
      }

      *subhKey = (POINTER_T) pkey - (POINTER_T) pheader;

      return DB_SUCCESS;
   }

   const KEYLIST *pkeylist = db_get_pkeylist(pheader, hKey, pkey, "db_find_key", msg);
   if (!pkeylist) {
      *subhKey = 0;
      return DB_CORRUPTED;
   }

   const char *pkey_name = key_name;
   do {
      char str[MAX_ODB_PATH];
         
      /* extract single subkey from key_name */
      pkey_name = extract_key(pkey_name, str, sizeof(str));

      /* strip trailing '[n]' */
      if (strchr(str, '[') && str[strlen(str) - 1] == ']')
         *strchr(str, '[') = 0;

      /* check if parent or current directory */
      if (strcmp(str, "..") == 0) {
         if (pkey->parent_keylist) {
            pkeylist = (KEYLIST *) ((char *) pheader + pkey->parent_keylist);
            // FIXME: validate pkeylist->parent
            pkey = (KEY *) ((char *) pheader + pkeylist->parent);
         }
         continue;
      }
      if (strcmp(str, ".") == 0)
         continue;

      /* check if key is in keylist */
      // FIXME: validate pkeylist->first_key
      pkey = (KEY *) ((char *) pheader + pkeylist->first_key);

      int i;
      for (i = 0; i < pkeylist->num_keys; i++) {
         if (pkey->name[0] == 0 || !db_validate_key_offset(pheader, pkey->next_key)) {
            int pkey_next_key = pkey->next_key;
            db_msg(msg, MERROR, "db_find_key", "Error: database corruption, key \"%s\", next_key 0x%08X is invalid", key_name, pkey_next_key - (int)sizeof(DATABASE_HEADER));
            *subhKey = 0;
            return DB_CORRUPTED;
         }

         if (equal_ustring(str, pkey->name))
            break;

         pkey = (KEY *) ((char *) pheader + pkey->next_key); // FIXME: pkey->next_key could be zero
      }

      if (i == pkeylist->num_keys) {
         *subhKey = 0;
         return DB_NO_KEY;
      }

      /* resolve links */
      if (pkey->type == TID_LINK) {
         /* copy destination, strip '/' */
         strlcpy(str, (char *) pheader + pkey->data, sizeof(str));
         if (str[strlen(str) - 1] == '/')
            str[strlen(str) - 1] = 0;

         /* if link is pointer to array index, return link instead of destination */
         if (str[strlen(str) - 1] == ']')
            break;

         /* append rest of key name if existing */
         if (pkey_name[0]) {
            strlcat(str, pkey_name, sizeof(str));
            return db_find_key_locked(pheader, 0, str, subhKey, msg);
         } else {
            /* if last key in chain is a link, return its destination */
            int status = db_find_key_locked(pheader, 0, str, subhKey, msg);
            if (status == DB_NO_KEY)
               return DB_INVALID_LINK;
            return status;
         }
      }

      /* key found: check if last in chain */
      if (*pkey_name == '/') {
         if (pkey->type != TID_KEY) {
            *subhKey = 0;
            return DB_NO_KEY;
         }
      }

      /* descend one level */
      pkeylist = (KEYLIST *) ((char *) pheader + pkey->data);

   } while (*pkey_name == '/' && *(pkey_name + 1));

   *subhKey = (POINTER_T) pkey - (POINTER_T) pheader;

   return DB_SUCCESS;
}
#endif /* LOCAL_ROUTINES */

/********************************************************************/
/**
Returns key handle for a key with a specific name.

Keys can be accessed by their name including the directory
or by a handle. A key handle is an internal offset to the shared memory
where the ODB lives and allows a much faster access to a key than via its
name.

The function db_find_key() must be used to convert a key name to a handle.
Most other database functions use this key handle in various operations.
\code
HNDLE hkey, hsubkey;
// use full name, start from root
db_find_key(hDB, 0, "/Runinfo/Run number", &hkey);
// start from subdirectory
db_find_key(hDB, 0, "/Runinfo", &hkey);
db_find_key(hdb, hkey, "Run number", &hsubkey);
\endcode
@param hDB          ODB handle obtained via cm_get_experiment_database().
@param hKey Handle for key where search starts, zero for root.
@param key_name Name of key to search, can contain directories.
@param subhKey Returned handle of key, zero if key cannot be found.
@return DB_SUCCESS, DB_INVALID_HANDLE, DB_NO_ACCESS, DB_NO_KEY
*/
INT db_find_key(HNDLE hDB, HNDLE hKey, const char *key_name, HNDLE * subhKey)
{
   if (rpc_is_remote())
      return rpc_call(RPC_DB_FIND_KEY, hDB, hKey, key_name, subhKey);

#ifdef LOCAL_ROUTINES
   {
      DATABASE_HEADER *pheader;
      INT status;

      *subhKey = 0;

      if (hDB > _database_entries || hDB <= 0) {
         cm_msg(MERROR, "db_find_key", "invalid database handle");
         return DB_INVALID_HANDLE;
      }

      if (!_database[hDB - 1].attached) {
         cm_msg(MERROR, "db_find_key", "invalid database handle");
         return DB_INVALID_HANDLE;
      }

      db_err_msg *msg = NULL;

      db_lock_database(hDB);

      pheader = _database[hDB - 1].database_header;
      
      status = db_find_key_locked(pheader, hKey, key_name, subhKey, &msg);
   
      db_unlock_database(hDB);

      if (msg)
         db_flush_msg(&msg);

      return status;
   }
#endif /* LOCAL_ROUTINES */

   return DB_SUCCESS;
}

/**dox***************************************************************/
#ifndef DOXYGEN_SHOULD_SKIP_THIS

/*------------------------------------------------------------------*/
INT db_find_key1(HNDLE hDB, HNDLE hKey, const char *key_name, HNDLE * subhKey)
/********************************************************************\

  Routine: db_find_key1

  Purpose: Same as db_find_key, but without DB locking

  Input:
    HNDLE  bufer_handle     Handle to the database
    HNDLE  hKey             Key handle to start the search
    char   *key_name        Name of key in the form "/key/key/key"

  Output:
    INT    *handle          Key handle

  Function value:
    DB_SUCCESS              Successful completion
    DB_INVALID_HANDLE       Database handle is invalid
    DB_NO_KEY               Key doesn't exist
    DB_NO_ACCESS            No access to read key

\********************************************************************/
{
   if (rpc_is_remote())
      return rpc_call(RPC_DB_FIND_KEY, hDB, hKey, key_name, subhKey);

#ifdef LOCAL_ROUTINES
   {
      DATABASE_HEADER *pheader;
      KEYLIST *pkeylist;
      KEY *pkey;
      const char *pkey_name;
      INT i;

      *subhKey = 0;

      if (hDB > _database_entries || hDB <= 0) {
         cm_msg(MERROR, "db_find_key", "invalid database handle");
         return DB_INVALID_HANDLE;
      }

      if (!_database[hDB - 1].attached) {
         cm_msg(MERROR, "db_find_key", "invalid database handle");
         return DB_INVALID_HANDLE;
      }

      pheader = _database[hDB - 1].database_header;
      if (!hKey)
         hKey = pheader->root_key;

      /* check if hKey argument is correct */
      if (!db_validate_hkey(pheader, hKey)) {
         return DB_INVALID_HANDLE;
      }

      pkey = (KEY *) ((char *) pheader + hKey);

      if (pkey->type != TID_KEY) {
         cm_msg(MERROR, "db_find_key", "key has no subkeys");
         *subhKey = 0;
         return DB_NO_KEY;
      }
      pkeylist = (KEYLIST *) ((char *) pheader + pkey->data);

      if (key_name[0] == 0 || strcmp(key_name, "/") == 0) {
         if (!(pkey->access_mode & MODE_READ)) {
            *subhKey = 0;
            return DB_NO_ACCESS;
         }

         *subhKey = (POINTER_T) pkey - (POINTER_T) pheader;

         return DB_SUCCESS;
      }

      pkey_name = key_name;
      do {
         char str[MAX_ODB_PATH];

         /* extract single subkey from key_name */
         pkey_name = extract_key(pkey_name, str, sizeof(str));

         /* check if parent or current directory */
         if (strcmp(str, "..") == 0) {
            if (pkey->parent_keylist) {
               pkeylist = (KEYLIST *) ((char *) pheader + pkey->parent_keylist);
               // FIXME: validate pkeylist->parent
               pkey = (KEY *) ((char *) pheader + pkeylist->parent);
            }
            continue;
         }
         if (strcmp(str, ".") == 0)
            continue;

         /* check if key is in keylist */
         // FIXME: validate pkeylist->first_key
         pkey = (KEY *) ((char *) pheader + pkeylist->first_key);

         for (i = 0; i < pkeylist->num_keys; i++) {
            if (equal_ustring(str, pkey->name))
               break;

            // FIXME: validate pkey->next_key
            pkey = (KEY *) ((char *) pheader + pkey->next_key);
         }

         if (i == pkeylist->num_keys) {
            *subhKey = 0;
            return DB_NO_KEY;
         }

         /* resolve links */
         if (pkey->type == TID_LINK) {
            /* copy destination, strip '/' */
            strcpy(str, (char *) pheader + pkey->data);
            if (str[strlen(str) - 1] == '/')
               str[strlen(str) - 1] = 0;

            /* append rest of key name if existing */
            if (pkey_name[0]) {
               strcat(str, pkey_name);
               return db_find_key1(hDB, 0, str, subhKey);
            } else {
               /* if last key in chain is a link, return its destination */
               return db_find_link1(hDB, 0, str, subhKey);
            }
         }

         /* key found: check if last in chain */
         if (*pkey_name == '/') {
            if (pkey->type != TID_KEY) {
               *subhKey = 0;
               return DB_NO_KEY;
            }
         }

         /* descend one level */
         pkeylist = (KEYLIST *) ((char *) pheader + pkey->data);

      } while (*pkey_name == '/' && *(pkey_name + 1));

      *subhKey = (POINTER_T) pkey - (POINTER_T) pheader;
   }
#endif                          /* LOCAL_ROUTINES */

   return DB_SUCCESS;
}

/*------------------------------------------------------------------*/
INT db_find_link(HNDLE hDB, HNDLE hKey, const char *key_name, HNDLE * subhKey)
/********************************************************************\

  Routine: db_find_link

  Purpose: Find a key or link by name and return its handle
           (internal address). The only difference of this routine
           compared with db_find_key is that if the LAST key in
           the chain is a link, it is NOT evaluated. Links not being
           the last in the chain are evaluated.

  Input:
    HNDLE  bufer_handle     Handle to the database
    HNDLE  hKey       Key handle to start the search
    char   *key_name        Name of key in the form "/key/key/key"

  Output:
    INT    *handle          Key handle

  Function value:
    DB_SUCCESS              Successful completion
    DB_INVALID_HANDLE       Database handle is invalid
    DB_NO_KEY               Key doesn't exist
    DB_NO_ACCESS            No access to read key

\********************************************************************/
{
   if (rpc_is_remote())
      return rpc_call(RPC_DB_FIND_LINK, hDB, hKey, key_name, subhKey);

#ifdef LOCAL_ROUTINES
   {
      DATABASE_HEADER *pheader;
      KEYLIST *pkeylist;
      KEY *pkey;
      const char *pkey_name;
      INT i;

      *subhKey = 0;

      if (hDB > _database_entries || hDB <= 0) {
         cm_msg(MERROR, "db_find_link", "Invalid database handle");
         return DB_INVALID_HANDLE;
      }

      if (!_database[hDB - 1].attached) {
         cm_msg(MERROR, "db_find_link", "invalid database handle");
         return DB_INVALID_HANDLE;
      }

      db_lock_database(hDB);

      pheader = _database[hDB - 1].database_header;
      if (!hKey)
         hKey = pheader->root_key;

      /* check if hKey argument is correct */
      if (!db_validate_hkey(pheader, hKey)) {
         db_unlock_database(hDB);
         return DB_INVALID_HANDLE;
      }

      pkey = (KEY *) ((char *) pheader + hKey);

      if (pkey->type != TID_KEY) {
         db_unlock_database(hDB);
         cm_msg(MERROR, "db_find_link", "key has no subkeys");
         return DB_NO_KEY;
      }
      pkeylist = (KEYLIST *) ((char *) pheader + pkey->data);

      if (key_name[0] == 0 || strcmp(key_name, "/") == 0) {
         if (!(pkey->access_mode & MODE_READ)) {
            *subhKey = 0;
            db_unlock_database(hDB);
            return DB_NO_ACCESS;
         }

         *subhKey = (POINTER_T) pkey - (POINTER_T) pheader;

         db_unlock_database(hDB);
         return DB_SUCCESS;
      }

      pkey_name = key_name;
      do {
         char str[MAX_ODB_PATH];

         /* extract single subkey from key_name */
         pkey_name = extract_key(pkey_name, str, sizeof(str));

         /* check if parent or current directory */
         if (strcmp(str, "..") == 0) {
            if (pkey->parent_keylist) {
               pkeylist = (KEYLIST *) ((char *) pheader + pkey->parent_keylist);
               // FIXME: validate pkeylist->parent
               pkey = (KEY *) ((char *) pheader + pkeylist->parent);
            }
            continue;
         }
         if (strcmp(str, ".") == 0)
            continue;

         /* check if key is in keylist */
         // FIXME: validate pkeylist->first_key
         pkey = (KEY *) ((char *) pheader + pkeylist->first_key);

         for (i = 0; i < pkeylist->num_keys; i++) {
            if (!db_validate_key_offset(pheader, pkey->next_key)) {
               int pkey_next_key = pkey->next_key;
               db_unlock_database(hDB);
               cm_msg(MERROR, "db_find_link", "Warning: database corruption, key \"%s\", next_key 0x%08X is invalid", key_name, pkey_next_key - (int)sizeof(DATABASE_HEADER));
               *subhKey = 0;
               return DB_CORRUPTED;
            }

            if (equal_ustring(str, pkey->name))
               break;

            pkey = (KEY *) ((char *) pheader + pkey->next_key); // FIXME: pkey->next_key could be zero
         }

         if (i == pkeylist->num_keys) {
            *subhKey = 0;
            db_unlock_database(hDB);
            return DB_NO_KEY;
         }

         /* resolve links if not last in chain */
         if (pkey->type == TID_LINK && *pkey_name == '/') {
            /* copy destination, strip '/' */
            strcpy(str, (char *) pheader + pkey->data);
            if (str[strlen(str) - 1] == '/')
               str[strlen(str) - 1] = 0;

            /* append rest of key name */
            strcat(str, pkey_name);
            db_unlock_database(hDB);
            return db_find_link(hDB, 0, str, subhKey);
         }

         /* key found: check if last in chain */
         if ((*pkey_name == '/')) {
            if (pkey->type != TID_KEY) {
               *subhKey = 0;
               db_unlock_database(hDB);
               return DB_NO_KEY;
            }
         }

         /* descend one level */
         pkeylist = (KEYLIST *) ((char *) pheader + pkey->data);

      } while (*pkey_name == '/' && *(pkey_name + 1));

      *subhKey = (POINTER_T) pkey - (POINTER_T) pheader;

      db_unlock_database(hDB);
   }
#endif                          /* LOCAL_ROUTINES */

   return DB_SUCCESS;
}

/*------------------------------------------------------------------*/
INT db_find_link1(HNDLE hDB, HNDLE hKey, const char *key_name, HNDLE * subhKey)
/********************************************************************\

  Routine: db_find_link1

  Purpose: Same ad db_find_link, but without DB locking

  Input:
    HNDLE  bufer_handle     Handle to the database
    HNDLE  hKey       Key handle to start the search
    char   *key_name        Name of key in the form "/key/key/key"

  Output:
    INT    *handle          Key handle

  Function value:
    DB_SUCCESS              Successful completion
    DB_INVALID_HANDLE       Database handle is invalid
    DB_NO_KEY               Key doesn't exist
    DB_NO_ACCESS            No access to read key

\********************************************************************/
{
   if (rpc_is_remote())
      return rpc_call(RPC_DB_FIND_LINK, hDB, hKey, key_name, subhKey);

#ifdef LOCAL_ROUTINES
   {
      DATABASE_HEADER *pheader;
      KEYLIST *pkeylist;
      KEY *pkey;
      const char *pkey_name;
      INT i;

      *subhKey = 0;

      if (hDB > _database_entries || hDB <= 0) {
         cm_msg(MERROR, "db_find_link", "Invalid database handle");
         return DB_INVALID_HANDLE;
      }

      if (!_database[hDB - 1].attached) {
         cm_msg(MERROR, "db_find_link", "invalid database handle");
         return DB_INVALID_HANDLE;
      }

      pheader = _database[hDB - 1].database_header;
      if (!hKey)
         hKey = pheader->root_key;

      /* check if hKey argument is correct */
      if (!db_validate_hkey(pheader, hKey)) {
         return DB_INVALID_HANDLE;
      }

      pkey = (KEY *) ((char *) pheader + hKey);

      if (pkey->type != TID_KEY) {
         cm_msg(MERROR, "db_find_link", "key has no subkeys");
         return DB_NO_KEY;
      }
      pkeylist = (KEYLIST *) ((char *) pheader + pkey->data);

      if (key_name[0] == 0 || strcmp(key_name, "/") == 0) {
         if (!(pkey->access_mode & MODE_READ)) {
            *subhKey = 0;
            return DB_NO_ACCESS;
         }

         *subhKey = (POINTER_T) pkey - (POINTER_T) pheader;

         return DB_SUCCESS;
      }

      pkey_name = key_name;
      do {
         char str[MAX_ODB_PATH];
         /* extract single subkey from key_name */
         pkey_name = extract_key(pkey_name, str, sizeof(str));

         /* check if parent or current directory */
         if (strcmp(str, "..") == 0) {
            if (pkey->parent_keylist) {
               pkeylist = (KEYLIST *) ((char *) pheader + pkey->parent_keylist);
               // FIXME: validate pkeylist->parent
               pkey = (KEY *) ((char *) pheader + pkeylist->parent);
            }
            continue;
         }
         if (strcmp(str, ".") == 0)
            continue;

         /* check if key is in keylist */
         // FIXME: validate pkeylist->first_key
         pkey = (KEY *) ((char *) pheader + pkeylist->first_key);

         for (i = 0; i < pkeylist->num_keys; i++) {
            if (!db_validate_key_offset(pheader, pkey->next_key)) {
               cm_msg(MERROR, "db_find_link1", "Warning: database corruption, key \"%s\", next_key 0x%08X is invalid", key_name, pkey->next_key - (int)sizeof(DATABASE_HEADER));
               *subhKey = 0;
               return DB_CORRUPTED;
            }

            if (equal_ustring(str, pkey->name))
               break;

            pkey = (KEY *) ((char *) pheader + pkey->next_key); // FIXME: pkey->next_key could be zero
         }

         if (i == pkeylist->num_keys) {
            *subhKey = 0;
            return DB_NO_KEY;
         }

         /* resolve links if not last in chain */
         if (pkey->type == TID_LINK && *pkey_name == '/') {
            /* copy destination, strip '/' */
            strcpy(str, (char *) pheader + pkey->data);
            if (str[strlen(str) - 1] == '/')
               str[strlen(str) - 1] = 0;

            /* append rest of key name */
            strcat(str, pkey_name);
            return db_find_link1(hDB, 0, str, subhKey);
         }

         /* key found: check if last in chain */
         if ((*pkey_name == '/')) {
            if (pkey->type != TID_KEY) {
               *subhKey = 0;
               return DB_NO_KEY;
            }
         }

         /* descend one level */
         pkeylist = (KEYLIST *) ((char *) pheader + pkey->data);

      } while (*pkey_name == '/' && *(pkey_name + 1));

      *subhKey = (POINTER_T) pkey - (POINTER_T) pheader;
   }
#endif                          /* LOCAL_ROUTINES */

   return DB_SUCCESS;
}

/*------------------------------------------------------------------*/
INT db_get_parent(HNDLE hDB, HNDLE hKey, HNDLE * parenthKey)
/********************************************************************\

  Routine: db_get_parent

  Purpose: return an handle to the parent key

  Input:
    HNDLE  bufer_handle     Handle to the database
    HNDLE  hKey       Key handle of the key

  Output:
    INT    *handle          Parent key handle

  Function value:
    DB_SUCCESS              Successful completion

\********************************************************************/
{
   if (rpc_is_remote())
      return rpc_call(RPC_DB_GET_PARENT, hDB, hKey, parenthKey);

#ifdef LOCAL_ROUTINES
   {

      DATABASE_HEADER *pheader;
      const KEY *pkey;

      if (hDB > _database_entries || hDB <= 0) {
         cm_msg(MERROR, "db_get_parent", "invalid database handle");
         return DB_INVALID_HANDLE;
      }

      if (!_database[hDB - 1].attached) {
         cm_msg(MERROR, "db_get_parent", "invalid database handle");
         return DB_INVALID_HANDLE;
      }

      if (hKey < (int) sizeof(DATABASE_HEADER)) {
         cm_msg(MERROR, "db_get_parent", "invalid key handle");
         return DB_INVALID_HANDLE;
      }

      db_lock_database(hDB);

      pheader = _database[hDB - 1].database_header;
      pkey = (const KEY *) ((char *) pheader + hKey);

      /* find parent key */
      const KEYLIST *pkeylist = (const KEYLIST *) ((char *) pheader + pkey->parent_keylist);

      if (!db_validate_hkey(pheader, pkeylist->parent)) {
         db_unlock_database(hDB);
         return DB_INVALID_HANDLE;
      }

      pkey = (const KEY *) ((char *) pheader + pkeylist->parent);

      *parenthKey = (POINTER_T) pkey - (POINTER_T) pheader;

      db_unlock_database(hDB);
   }
#endif

   return DB_SUCCESS;
}

/*------------------------------------------------------------------*/
INT db_scan_tree(HNDLE hDB, HNDLE hKey, INT level, INT(*callback) (HNDLE, HNDLE, KEY *, INT, void *), void *info)
/********************************************************************\

  Routine: db_scan_tree

  Purpose: Scan a subtree recursively and call 'callback' for each key

  Input:
    HNDLE  hDB              Handle to the database
    HNDLE  hKeyRoot         Key to start scan from, 0 for root
    INT    level            Recursion level
    void   *callback        Callback routine called with params:
                              hDB   Copy of hDB
                              hKey  Copy of hKey
                              key   Key associated with hKey
                              INT   Recursion level
                              info  Copy of *info
    void   *info            Optional data copied to callback routine

  Output:
    implicit via callback

  Function value:
    DB_SUCCESS              Successful completion
    <all error codes of db_get_key>

\********************************************************************/
{
   HNDLE hSubkey;
   KEY key;
   INT i, status;

   status = db_get_link(hDB, hKey, &key);
   if (status != DB_SUCCESS)
      return status;

   status = callback(hDB, hKey, &key, level, info);
   if (status == 0)
      return status;

   if (key.type == TID_KEY) {
      for (i = 0;; i++) {
         db_enum_link(hDB, hKey, i, &hSubkey);

         if (!hSubkey)
            break;

         db_scan_tree(hDB, hSubkey, level + 1, callback, info);
      }
   }

   return DB_SUCCESS;
}

/*------------------------------------------------------------------*/
INT db_scan_tree_link(HNDLE hDB, HNDLE hKey, INT level, void (*callback) (HNDLE, HNDLE, KEY *, INT, void *), void *info)
/********************************************************************\

  Routine: db_scan_tree_link

  Purpose: Scan a subtree recursively and call 'callback' for each key.
           Similar to db_scan_tree but without following links.

  Input:
    HNDLE  hDB              Handle to the database
    HNDLE  hKeyRoot         Key to start scan from, 0 for root
    INT    level            Recursion level
    void   *callback        Callback routine called with params:
                              hDB   Copy of hDB
                              hKey  Copy of hKey
                              key   Key associated with hKey
                              INT   Recursion level
                              info  Copy of *info
    void   *info            Optional data copied to callback routine

  Output:
    implicit via callback

  Function value:
    DB_SUCCESS              Successful completion
    <all error codes of db_get_key>

\********************************************************************/
{
   HNDLE hSubkey;
   KEY key;
   INT i, status;

   status = db_get_key(hDB, hKey, &key);
   if (status != DB_SUCCESS)
      return status;

   callback(hDB, hKey, &key, level, info);

   if (key.type == TID_KEY) {
      for (i = 0;; i++) {
         db_enum_link(hDB, hKey, i, &hSubkey);

         if (!hSubkey)
            break;

         db_scan_tree_link(hDB, hSubkey, level + 1, callback, info);
      }
   }

   return DB_SUCCESS;
}

#ifdef LOCAL_ROUTINES
/*------------------------------------------------------------------*/
static INT db_get_path_locked(const DATABASE_HEADER* pheader, HNDLE hKey, char *path, INT buf_size)
{
   if (!hKey)
      hKey = pheader->root_key;

   /* check if hKey argument is correct */
   if (!db_validate_hkey(pheader, hKey)) {
      return DB_INVALID_HANDLE;
   }

   const KEY* pkey = (const KEY *) ((char *) pheader + hKey);

   if (hKey == pheader->root_key) {
      strcpy(path, "/");
      return DB_SUCCESS;
   }

   *path = 0;
   do {
      if (!db_validate_pkey(pheader, pkey)) {
         return DB_INVALID_HANDLE;
      }

      char str[MAX_ODB_PATH];

      /* add key name in front of path */
      strcpy(str, path);
      strcpy(path, "/");
      strcat(path, pkey->name);

      if (strlen(path) + strlen(str) + 1 > (DWORD) buf_size) {
         *path = 0;
         return DB_NO_MEMORY;
      }
      strcat(path, str);

      if (!db_validate_hkey(pheader, pkey->parent_keylist)) {
         return DB_INVALID_HANDLE;
      }

      /* find parent key */
      const KEYLIST* pkeylist = (const KEYLIST *) ((char *) pheader + pkey->parent_keylist);

      if (!db_validate_hkey(pheader, pkeylist->parent)) {
         return DB_INVALID_HANDLE;
      }

      // FIXME: validate pkeylist->parent
      pkey = (const KEY *) ((char *) pheader + pkeylist->parent);
   } while (pkey->parent_keylist);

   return DB_SUCCESS;
}
#endif /* LOCAL_ROUTINES */

/*------------------------------------------------------------------*/
INT db_get_path(HNDLE hDB, HNDLE hKey, char *path, INT buf_size)
/********************************************************************\

  Routine: db_get_path

  Purpose: Get full path of a key

  Input:
    HNDLE  hDB              Handle to the database
    HNDLE  hKey             Key handle
    INT    buf_size         Maximum size of path buffer (including
                            trailing zero)

  Output:
    char   path[buf_size]   Path string

  Function value:
    DB_SUCCESS              Successful completion
    DB_INVALID_HANDLE       Database handle is invalid
    DB_NO_MEMORY            path buffer is to small to contain full
                            string

\********************************************************************/
{
   if (rpc_is_remote())
      return rpc_call(RPC_DB_GET_PATH, hDB, hKey, path, buf_size);

#ifdef LOCAL_ROUTINES
   {
      DATABASE_HEADER *pheader;
      INT status;

      if (hDB > _database_entries || hDB <= 0) {
         cm_msg(MERROR, "db_get_path", "invalid database handle");
         return DB_INVALID_HANDLE;
      }

      if (!_database[hDB - 1].attached) {
         cm_msg(MERROR, "db_get_path", "invalid database handle");
         return DB_INVALID_HANDLE;
      }

      db_lock_database(hDB);

      pheader = _database[hDB - 1].database_header;

      status = db_get_path_locked(pheader, hKey, path, buf_size);

      db_unlock_database(hDB);

      return status;
   }
#endif /* LOCAL_ROUTINES */

   return DB_SUCCESS;
}

/*------------------------------------------------------------------*/
int db_find_open_records(HNDLE hDB, HNDLE hKey, KEY * key, INT level, void *result)
{
#ifdef LOCAL_ROUTINES
   /* check if this key has notify count set */
   if (key->notify_count) {
      char line[256], path[80];
      DATABASE_HEADER *pheader;
      int i, j;
      int count = 0;

      db_get_path(hDB, hKey, path, sizeof(path));
      sprintf(line, "%s open %d times by ", path, key->notify_count);

      //printf("path [%s] key.name [%s]\n", path, key->name);

      db_lock_database(hDB);
      pheader = _database[hDB - 1].database_header;

      for (i = 0; i < pheader->max_client_index; i++) {
         DATABASE_CLIENT *pclient = &pheader->client[i];
         for (j = 0; j < pclient->max_index; j++)
            if (pclient->open_record[j].handle == hKey) {
               count++;
               sprintf(line + strlen(line), "\"%s\" ", pclient->name);
               //sprintf(line + strlen(line), ", handle %d, mode %d ", pclient->open_record[j].handle, pclient->open_record[j].access_mode);
            }
      }

      if (count < 1) {
         sprintf(line + strlen(line), "a deleted client");
      }

      strcat(line, "\n");
      strcat((char *) result, line);

      db_unlock_database(hDB);
   }
#endif                          /* LOCAL_ROUTINES */
   return DB_SUCCESS;
}

int db_fix_open_records(HNDLE hDB, HNDLE hKey, KEY * key, INT level, void *result)
{
#ifdef LOCAL_ROUTINES
   DATABASE_HEADER *pheader;
   DATABASE_CLIENT *pclient;
   INT i, j;
   char str[256];
   KEY *pkey;

   /* avoid compiler warning */
   i = level;

   /* check if this key has notify count set */
   if (key->notify_count) {
      db_lock_database(hDB);
      pheader = _database[hDB - 1].database_header;
      db_allow_write_locked(&_database[hDB - 1], "db_fix_open_records");

      for (i = 0; i < pheader->max_client_index; i++) {
         pclient = &pheader->client[i];
         for (j = 0; j < pclient->max_index; j++)
            if (pclient->open_record[j].handle == hKey)
               break;
         if (j < pclient->max_index)
            break;
      }
      if (i == pheader->max_client_index) {
         /* check if hKey argument is correct */
         if (!db_validate_hkey(pheader, hKey)) {
            db_unlock_database(hDB);
            return DB_SUCCESS;
         }

         /* reset notify count */
         pkey = (KEY *) ((char *) pheader + hKey);
         pkey->notify_count = 0;

         db_get_path(hDB, hKey, str, sizeof(str));
         strcat(str, " fixed\n");
         strcat((char *) result, str);
      }

      db_unlock_database(hDB);
   }
#endif                          /* LOCAL_ROUTINES */
   return DB_SUCCESS;
}

INT db_get_open_records(HNDLE hDB, HNDLE hKey, char *str, INT buf_size, BOOL fix)
/********************************************************************\

  Routine: db_get_open_records

  Purpose: Return a string with all open records

  Input:
    HNDLE  hDB              Handle to the database
    HNDLE  hKey             Key to start search from, 0 for root
    INT    buf_size         Size of string
    INT    fix              If TRUE, fix records which are open
                            but have no client belonging to it.

  Output:
    char   *str             Result string. Individual records are
                            separated with new lines.

  Function value:
    DB_SUCCESS              Successful completion

\********************************************************************/
{
   str[0] = 0;

   if (rpc_is_remote())
      return rpc_call(RPC_DB_GET_OPEN_RECORDS, hDB, hKey, str, buf_size);

   if (fix)
      db_scan_tree(hDB, hKey, 0, db_fix_open_records, str);
   else
      db_scan_tree(hDB, hKey, 0, db_find_open_records, str);

   return DB_SUCCESS;
}

/**dox***************************************************************/
#endif                          /* DOXYGEN_SHOULD_SKIP_THIS */

/********************************************************************/
/**
Set value of a single key.

The function sets a single value or a whole array to a ODB key.
Since the data buffer is of type void, no type checking can be performed by the
compiler. Therefore the type has to be explicitly supplied, which is checked
against the type stored in the ODB. key_name can contain the full path of a key
(like: "/Equipment/Trigger/Settings/Level1") while hkey is zero which refers
to the root, or hkey can refer to a sub-directory (like /Equipment/Trigger)
and key_name is interpreted relative to that directory like "Settings/Level1".
\code
INT level1;
  db_set_value(hDB, 0, "/Equipment/Trigger/Settings/Level1",
                          &level1, sizeof(level1), 1, TID_INT);
\endcode
@param hDB          ODB handle obtained via cm_get_experiment_database().
@param hKeyRoot Handle for key where search starts, zero for root.
@param key_name Name of key to search, can contain directories.
@param data Address of data.
@param data_size Size of data (in bytes).
@param num_values Number of data elements.
@param type Type of key, one of TID_xxx (see @ref Midas_Data_Types)
@return DB_SUCCESS, DB_INVALID_HANDLE, DB_NO_ACCESS, DB_TYPE_MISMATCH
*/
INT db_set_value(HNDLE hDB, HNDLE hKeyRoot, const char *key_name, const void *data,
                 INT data_size, INT num_values, DWORD type)
{
   if (rpc_is_remote())
      return rpc_call(RPC_DB_SET_VALUE, hDB, hKeyRoot, key_name, data, data_size, num_values, type);

#ifdef LOCAL_ROUTINES
   {
      DATABASE_HEADER *pheader;
      KEY *pkey;
      HNDLE hKey;
      INT status;

      if (num_values == 0)
         return DB_INVALID_PARAM;

      status = db_find_key(hDB, hKeyRoot, key_name, &hKey);
      if (status == DB_NO_KEY) {
         status = db_create_key(hDB, hKeyRoot, key_name, type);
         if (status != DB_SUCCESS && status != DB_CREATED)
            return status;
         status = db_find_link(hDB, hKeyRoot, key_name, &hKey);
      }

      if (status != DB_SUCCESS)
         return status;

      db_lock_database(hDB);
      pheader = _database[hDB - 1].database_header;

      /* get address from handle */
      pkey = (KEY *) ((char *) pheader + hKey); // NB: hKey comes from db_find_key(), assumed to be valid

      /* check for write access */
      if (!(pkey->access_mode & MODE_WRITE) || (pkey->access_mode & MODE_EXCLUSIVE)) {
         db_unlock_database(hDB);
         return DB_NO_ACCESS;
      }

      if (pkey->type != type) {
         int pkey_type = pkey->type;
         db_unlock_database(hDB);
         cm_msg(MERROR, "db_set_value", "\"%s\" is of type %s, not %s", key_name, rpc_tid_name(pkey_type), rpc_tid_name(type));
         return DB_TYPE_MISMATCH;
      }

      /* keys cannot contain data */
      if (pkey->type == TID_KEY) {
         db_unlock_database(hDB);
         cm_msg(MERROR, "db_set_value", "key cannot contain data");
         return DB_TYPE_MISMATCH;
      }

      if (data_size == 0) {
         db_unlock_database(hDB);
         cm_msg(MERROR, "db_set_value", "zero data size not allowed");
         return DB_TYPE_MISMATCH;
      }

      if (type != TID_STRING && type != TID_LINK && data_size != rpc_tid_size(type) * num_values) {
         db_unlock_database(hDB);
         cm_msg(MERROR, "db_set_value", "\"%s\" data_size %d does not match tid %d size %d times num_values %d", key_name, data_size, type, rpc_tid_size(type), num_values);
         return DB_TYPE_MISMATCH;
      }

      db_allow_write_locked(&_database[hDB-1], "db_set_value");

      /* resize data size if necessary */
      if (pkey->total_size != data_size) {
         pkey->data = (POINTER_T) realloc_data(pheader, (char *) pheader + pkey->data, pkey->total_size, data_size, "db_set_value");

         if (pkey->data == 0) {
            pkey->total_size = 0;
            db_unlock_database(hDB);
            cm_msg(MERROR, "db_set_value", "online database full");
            return DB_FULL;
         }

         pkey->data -= (POINTER_T) pheader;
         pkey->total_size = data_size;
      }

      /* set number of values */
      pkey->num_values = num_values;

      if (type == TID_STRING || type == TID_LINK)
         pkey->item_size = data_size / num_values;
      else
         pkey->item_size = rpc_tid_size(type);

      /* copy data */
      memcpy((char *) pheader + pkey->data, data, data_size);

      /* update time */
      pkey->last_written = ss_time();

      db_notify_clients(hDB, hKey, -1, TRUE);
      db_unlock_database(hDB);

   }
#endif                          /* LOCAL_ROUTINES */

   return DB_SUCCESS;
}

/********************************************************************/
/**
Set single value of an array.

The function sets a single value of an ODB key which is an array.
key_name can contain the full path of a key
(like: "/Equipment/Trigger/Settings/Level1") while hkey is zero which refers
to the root, or hkey can refer to a sub-directory (like /Equipment/Trigger)
and key_name is interpreted relative to that directory like "Settings/Level1".
\code
INT level1;
  db_set_value_index(hDB, 0, "/Equipment/Trigger/Settings/Level1",
                          &level1, sizeof(level1), 15, TID_INT, FALSE);
\endcode
@param hDB          ODB handle obtained via cm_get_experiment_database().
@param hKeyRoot Handle for key where search starts, zero for root.
@param key_name Name of key to search, can contain directories.
@param data Address of data.
@param data_size Size of data (in bytes).
@param index Array index of value.
@param type Type of key, one of TID_xxx (see @ref Midas_Data_Types)
@param truncate Truncate array to current index if TRUE
@return \<same as db_set_data_index\>
*/
INT db_set_value_index(HNDLE hDB, HNDLE hKeyRoot, const char *key_name, const void *data,
                       INT data_size, INT idx, DWORD type, BOOL trunc)
{
   int status;
   HNDLE hkey;

   status = db_find_key(hDB, hKeyRoot, key_name, &hkey);
   if (!hkey) {
      status = db_create_key(hDB, hKeyRoot, key_name, type);
      status = db_find_key(hDB, hKeyRoot, key_name, &hkey);
      if (status != DB_SUCCESS)
         return status;
   } else
      if (trunc) {
         status = db_set_num_values(hDB, hkey, idx + 1);
         if (status != DB_SUCCESS)
            return status;
      }

   return db_set_data_index(hDB, hkey, data, data_size, idx, type);
}

/********************************************************************/
/**
Get value of a single key.

The function returns single values or whole arrays which are contained
in an ODB key. Since the data buffer is of type void, no type checking can be
performed by the compiler. Therefore the type has to be explicitly supplied,
which is checked against the type stored in the ODB. key_name can contain the
full path of a key (like: "/Equipment/Trigger/Settings/Level1") while hkey is
zero which refers to the root, or hkey can refer to a sub-directory
(like: /Equipment/Trigger) and key_name is interpreted relative to that directory
like "Settings/Level1".
\code
INT level1, size;
  size = sizeof(level1);
  db_get_value(hDB, 0, "/Equipment/Trigger/Settings/Level1",
                                   &level1, &size, TID_INT, 0);
\endcode
@param hDB          ODB handle obtained via cm_get_experiment_database().
@param hKeyRoot Handle for key where search starts, zero for root.
@param key_name Name of key to search, can contain directories.
@param data Address of data.
@param buf_size Maximum buffer size on input, number of written bytes on return.
@param type Type of key, one of TID_xxx (see @ref Midas_Data_Types)
@param create If TRUE, create key if not existing
@return DB_SUCCESS, DB_INVALID_HANDLE, DB_NO_ACCESS, DB_TYPE_MISMATCH,
DB_TRUNCATED, DB_NO_KEY
*/
INT db_get_value(HNDLE hDB, HNDLE hKeyRoot, const char *key_name, void *data, INT * buf_size, DWORD type, BOOL create)
{
   if (rpc_is_remote())
      return rpc_call(RPC_DB_GET_VALUE, hDB, hKeyRoot, key_name, data, buf_size, type, create);

#ifdef LOCAL_ROUTINES
   {
      DATABASE_HEADER *pheader;
      HNDLE hkey;
      KEY *pkey;
      INT status, size, idx;
      char *p, path[256], keyname[256];

      if (hDB > _database_entries || hDB <= 0) {
         cm_msg(MERROR, "db_get_value", "invalid database handle %d", hDB);
         return DB_INVALID_HANDLE;
      }

      if (!_database[hDB - 1].attached) {
         cm_msg(MERROR, "db_get_value", "invalid database handle %d", hDB);
         return DB_INVALID_HANDLE;
      }

      /* check if key name contains index */
      strlcpy(keyname, key_name, sizeof(keyname));
      idx = -1;
      if (strchr(keyname, '[') && strchr(keyname, ']')) {
         for (p = strchr(keyname, '[') + 1; *p && *p != ']'; p++)
            if (!isdigit(*p))
               break;

         if (*p && *p == ']') {
            idx = atoi(strchr(keyname, '[') + 1);
            *strchr(keyname, '[') = 0;
         }
      }

      status = db_find_key(hDB, hKeyRoot, keyname, &hkey);
      if (status == DB_NO_KEY) {
         if (create) {
            db_create_key(hDB, hKeyRoot, keyname, type);
            status = db_find_key(hDB, hKeyRoot, keyname, &hkey);
            if (status != DB_SUCCESS)
               return status;

            /* get string size from data size */
            if (type == TID_STRING || type == TID_LINK)
               size = *buf_size;
            else
               size = rpc_tid_size(type);

            if (size == 0)
               return DB_TYPE_MISMATCH;

            /* set default value if key was created */
            status = db_set_value(hDB, hKeyRoot, keyname, data, *buf_size, *buf_size / size, type);
         } else
            return DB_NO_KEY;
      }

      if (status != DB_SUCCESS)
         return status;

      /* now lock database */
      db_lock_database(hDB);
      pheader = _database[hDB - 1].database_header;

      /* get address from handle */
      pkey = (KEY *) ((char *) pheader + hkey); // NB: hkey comes from db_find_key(), assumed to be valid

      /* check for correct type */
      if (pkey->type != (type)) {
         int pkey_type = pkey->type;
         db_unlock_database(hDB);
         cm_msg(MERROR, "db_get_value", "hkey %d entry \"%s\" is of type %s, not %s", hKeyRoot, keyname, rpc_tid_name(pkey_type), rpc_tid_name(type));
         return DB_TYPE_MISMATCH;
      }

      /* check for read access */
      if (!(pkey->access_mode & MODE_READ)) {
         db_unlock_database(hDB);
         cm_msg(MERROR, "db_get_value", "%s has no read access", keyname);
         return DB_NO_ACCESS;
      }

      /* check if buffer is too small */
      if ((idx == -1 && pkey->num_values * pkey->item_size > *buf_size) || (idx != -1 && pkey->item_size > *buf_size)) {
         int pkey_num_values = pkey->num_values;
         int pkey_item_size = pkey->item_size;
         memcpy(data, (char *) pheader + pkey->data, *buf_size);
         db_unlock_database(hDB);
         char path[MAX_ODB_PATH];
         db_get_path(hDB, hkey, path, sizeof(path));
         cm_msg(MERROR, "db_get_value", "buffer size %d too small, data size %dx%d, truncated for key \"%s\"", *buf_size, pkey_num_values, pkey_item_size, path);
         return DB_TRUNCATED;
      }

      /* check if index in boundaries */
      if (idx != -1 && idx >= pkey->num_values) {
         db_unlock_database(hDB);
         db_get_path(hDB, hkey, path, sizeof(path));
         cm_msg(MERROR, "db_get_value", "invalid index \"%d\" for key \"%s\"", idx, path);
         return DB_INVALID_PARAM;
      }

      /* copy key data */
      if (idx == -1) {
         memcpy(data, (char *) pheader + pkey->data, pkey->num_values * pkey->item_size);
         *buf_size = pkey->num_values * pkey->item_size;
      } else {
         memcpy(data, (char *) pheader + pkey->data + idx * pkey->item_size, pkey->item_size);
         *buf_size = pkey->item_size;
      }

      db_unlock_database(hDB);
   }
#endif                          /* LOCAL_ROUTINES */

   return DB_SUCCESS;
}

/********************************************************************/
/**
Enumerate subkeys from a key, follow links.

hkey must correspond to a valid ODB directory. The index is
usually incremented in a loop until the last key is reached. Information about the
sub-keys can be obtained with db_get_key(). If a returned key is of type
TID_KEY, it contains itself sub-keys. To scan a whole ODB sub-tree, the
function db_scan_tree() can be used.
\code
INT   i;
HNDLE hkey, hsubkey;
KEY   key;
  db_find_key(hdb, 0, "/Runinfo", &hkey);
  for (i=0 ; ; i++)
  {
   db_enum_key(hdb, hkey, i, &hsubkey);
   if (!hSubkey)
    break; // end of list reached
   // print key name
   db_get_key(hdb, hkey, &key);
   printf("%s\n", key.name);
  }
\endcode
@param hDB          ODB handle obtained via cm_get_experiment_database().
@param hKey          Handle for key where search starts, zero for root.
@param idx          Subkey index, sould be initially 0, then
                    incremented in each call until
                    *subhKey becomes zero and the function
                    returns DB_NO_MORE_SUBKEYS
@param subkey_handle Handle of subkey which can be used in
                    db_get_key() and db_get_data()
@return DB_SUCCESS, DB_INVALID_HANDLE, DB_NO_MORE_SUBKEYS
*/
INT db_enum_key(HNDLE hDB, HNDLE hKey, INT idx, HNDLE * subkey_handle)
{
   if (rpc_is_remote())
      return rpc_call(RPC_DB_ENUM_KEY, hDB, hKey, idx, subkey_handle);

#ifdef LOCAL_ROUTINES
   {
      DATABASE_HEADER *pheader;
      KEYLIST *pkeylist;
      KEY *pkey;
      INT i;
      char str[256];
      HNDLE parent;

      if (hDB > _database_entries || hDB <= 0) {
         cm_msg(MERROR, "db_enum_key", "invalid database handle");
         return DB_INVALID_HANDLE;
      }

      if (!_database[hDB - 1].attached) {
         cm_msg(MERROR, "db_enum_key", "invalid database handle");
         return DB_INVALID_HANDLE;
      }

      *subkey_handle = 0;

      /* first lock database */
      db_lock_database(hDB);

      pheader = _database[hDB - 1].database_header;
      if (!hKey)
         hKey = pheader->root_key;

      /* check if hKey argument is correct */
      if (!db_validate_hkey(pheader, hKey)) {
         db_unlock_database(hDB);
         return DB_INVALID_HANDLE;
      }

      pkey = (KEY *) ((char *) pheader + hKey);

      if (pkey->type != TID_KEY) {
         db_unlock_database(hDB);
         return DB_NO_MORE_SUBKEYS;
      }
      pkeylist = (KEYLIST *) ((char *) pheader + pkey->data);

      if (idx >= pkeylist->num_keys) {
         db_unlock_database(hDB);
         return DB_NO_MORE_SUBKEYS;
      }

      // FIXME: validate pkeylist->first_key
      pkey = (KEY *) ((char *) pheader + pkeylist->first_key);
      for (i = 0; i < idx; i++) {
         // FIXME: validate pkey->next_key
         pkey = (KEY *) ((char *) pheader + pkey->next_key);
      }

      /* resolve links */
      if (pkey->type == TID_LINK) {
         strcpy(str, (char *) pheader + pkey->data);

         /* no not resolve if link to array index */
         if (strlen(str) > 0 && str[strlen(str) - 1] == ']') {
            *subkey_handle = (POINTER_T) pkey - (POINTER_T) pheader;
            db_unlock_database(hDB);
            return DB_SUCCESS;
         }

         if (*str == '/') {
            /* absolute path */
            db_unlock_database(hDB);
            return db_find_key(hDB, 0, str, subkey_handle);
         } else {
            /* relative path */
            if (pkey->parent_keylist) {
               pkeylist = (KEYLIST *) ((char *) pheader + pkey->parent_keylist);
               parent = pkeylist->parent;
               db_unlock_database(hDB);
               return db_find_key(hDB, parent, str, subkey_handle);
            } else {
               db_unlock_database(hDB);
               return db_find_key(hDB, 0, str, subkey_handle);
            }
         }
      }

      *subkey_handle = (POINTER_T) pkey - (POINTER_T) pheader;
      db_unlock_database(hDB);
   }
#endif                          /* LOCAL_ROUTINES */

   return DB_SUCCESS;
}

/**dox***************************************************************/
#ifndef DOXYGEN_SHOULD_SKIP_THIS


/*------------------------------------------------------------------*/
INT db_enum_link(HNDLE hDB, HNDLE hKey, INT idx, HNDLE * subkey_handle)
/********************************************************************\

  Routine: db_enum_link

  Purpose: Enumerate subkeys from a key, don't follow links

  Input:
    HNDLE hDB               Handle to the database
    HNDLE hKey              Handle of key to enumerate, zero for the
                            root key
    INT   idx               Subkey index, sould be initially 0, then
                            incremented in each call until
                            *subhKey becomes zero and the function
                            returns DB_NO_MORE_SUBKEYS

  Output:
    HNDLE *subkey_handle    Handle of subkey which can be used in
                            db_get_key and db_get_data

  Function value:
    DB_SUCCESS              Successful completion
    DB_INVALID_HANDLE       Database handle is invalid
    DB_NO_MORE_SUBKEYS      Last subkey reached

\********************************************************************/
{
   if (rpc_is_remote())
      return rpc_call(RPC_DB_ENUM_LINK, hDB, hKey, idx, subkey_handle);

#ifdef LOCAL_ROUTINES
   {
      DATABASE_HEADER *pheader;
      KEYLIST *pkeylist;
      KEY *pkey;
      INT i;

      if (hDB > _database_entries || hDB <= 0) {
         cm_msg(MERROR, "db_enum_link", "invalid database handle");
         return DB_INVALID_HANDLE;
      }

      if (!_database[hDB - 1].attached) {
         cm_msg(MERROR, "db_enum_link", "invalid database handle");
         return DB_INVALID_HANDLE;
      }

      *subkey_handle = 0;

      /* first lock database */
      db_lock_database(hDB);

      pheader = _database[hDB - 1].database_header;
      if (!hKey)
         hKey = pheader->root_key;

      /* check if hKey argument is correct */
      if (!db_validate_hkey(pheader, hKey)) {
         db_unlock_database(hDB);
         return DB_INVALID_HANDLE;
      }

      pkey = (KEY *) ((char *) pheader + hKey);

      if (pkey->type != TID_KEY) {
         db_unlock_database(hDB);
         return DB_NO_MORE_SUBKEYS;
      }
      pkeylist = (KEYLIST *) ((char *) pheader + pkey->data);

      if (idx >= pkeylist->num_keys) {
         db_unlock_database(hDB);
         return DB_NO_MORE_SUBKEYS;
      }

      // FIXME: validate pkeylist->first_key
      pkey = (KEY *) ((char *) pheader + pkeylist->first_key);
      for (i = 0; i < idx; i++) {
         // FIXME: validate pkey->next_key
         pkey = (KEY *) ((char *) pheader + pkey->next_key);
      }

      *subkey_handle = (POINTER_T) pkey - (POINTER_T) pheader;
      db_unlock_database(hDB);
   }
#endif                          /* LOCAL_ROUTINES */

   return DB_SUCCESS;
}

/*------------------------------------------------------------------*/
INT db_get_next_link(HNDLE hDB, HNDLE hKey, HNDLE * subkey_handle)
/********************************************************************\

  Routine: db_get_next_link

  Purpose: Get next key in ODB after hKey

  Input:
    HNDLE hDB               Handle to the database
    HNDLE hKey              Handle of key to enumerate, zero for the
                            root key

  Output:
    HNDLE *subkey_handle    Handle of subkey which can be used in
                            db_get_key and db_get_data

  Function value:
    DB_SUCCESS              Successful completion
    DB_INVALID_HANDLE       Database handle is invalid
    DB_NO_MORE_SUBKEYS      Last subkey reached

\********************************************************************/
{
   if (rpc_is_remote())
      return rpc_call(RPC_DB_GET_NEXT_LINK, hDB, hKey, subkey_handle);

#ifdef LOCAL_ROUTINES
   {
      DATABASE_HEADER *pheader;
      KEYLIST *pkeylist;
      KEY *pkey;
      INT descent;

      if (hDB > _database_entries || hDB <= 0) {
         cm_msg(MERROR, "db_enum_link", "invalid database handle");
         return DB_INVALID_HANDLE;
      }

      if (!_database[hDB - 1].attached) {
         cm_msg(MERROR, "db_enum_link", "invalid database handle");
         return DB_INVALID_HANDLE;
      }

      *subkey_handle = 0;

      /* first lock database */
      db_lock_database(hDB);

      pheader = _database[hDB - 1].database_header;
      if (!hKey)
         hKey = pheader->root_key;

      /* check if hKey argument is correct */
      if (!db_validate_hkey(pheader, hKey)) {
         db_unlock_database(hDB);
         return DB_INVALID_HANDLE;
      }

      pkey = (KEY *) ((char *) pheader + hKey);

      descent = TRUE;
      do {
         if (pkey->type != TID_KEY || !descent) {
            if (pkey->next_key) {
               /* key has next key, return it */
               // FIXME: validate pkey->next_key
               pkey = (KEY *) ((char *) pheader + pkey->next_key);

               if (pkey->type != TID_KEY) {
                  *subkey_handle = (POINTER_T) pkey - (POINTER_T) pheader;
                  db_unlock_database(hDB);
                  return DB_SUCCESS;
               }

               /* key has subkeys, so descent on the next iteration */
               descent = TRUE;
            } else {
               if (pkey->parent_keylist == 0) {
                  /* return if we are back to the root key */
                  db_unlock_database(hDB);
                  return DB_NO_MORE_SUBKEYS;
               }

               /* key is last in list, traverse up */
               pkeylist = (KEYLIST *) ((char *) pheader + pkey->parent_keylist);

               // FIXME: validate pkeylist->parent
               pkey = (KEY *) ((char *) pheader + pkeylist->parent);
               descent = FALSE;
            }
         } else {
            if (descent) {
               /* find first subkey */
               pkeylist = (KEYLIST *) ((char *) pheader + pkey->data);

               if (pkeylist->num_keys == 0) {
                  /* if key has no subkeys, look for next key on this level */
                  descent = FALSE;
               } else {
                  /* get first subkey */
                  // FIXME: validate pkeylist->first_key
                  pkey = (KEY *) ((char *) pheader + pkeylist->first_key);

                  if (pkey->type != TID_KEY) {
                     *subkey_handle = (POINTER_T) pkey - (POINTER_T) pheader;
                     db_unlock_database(hDB);
                     return DB_SUCCESS;
                  }
               }
            }
         }

      } while (TRUE);
   }
#endif                          /* LOCAL_ROUTINES */

   return DB_SUCCESS;
}

/**dox***************************************************************/
#endif                          /* DOXYGEN_SHOULD_SKIP_THIS */

/********************************************************************/

#ifdef LOCAL_ROUTINES

static INT db_get_key_locked(const DATABASE_HEADER* pheader, HNDLE hKey, KEY * key, db_err_msg** msg)
{
   if (!hKey)
      hKey = pheader->root_key;

   /* check if hKey argument is correct */
   if (!db_validate_hkey(pheader, hKey)) {
      return DB_INVALID_HANDLE;
   }
   
   const KEY* pkey = (const KEY *) ((char *) pheader + hKey);
   
   if (pkey->type < 1 || pkey->type >= TID_LAST) {
      int pkey_type = pkey->type;
      db_msg(msg, MERROR, "db_get_key", "hkey %d invalid key type %d", hKey, pkey_type);
      return DB_INVALID_HANDLE;
   }
   
   /* check for link to array index */
   if (pkey->type == TID_LINK) {
      char link_name[256];
      strlcpy(link_name, (char *) pheader + pkey->data, sizeof(link_name));
      if (strlen(link_name) > 0 && link_name[strlen(link_name) - 1] == ']') {
         if (strchr(link_name, '[') == NULL)
            return DB_INVALID_LINK;

         HNDLE hkeylink;
         if (db_find_key_locked(pheader, 0, link_name, &hkeylink, msg) != DB_SUCCESS)
            return DB_INVALID_LINK;
         int status = db_get_key_locked(pheader, hkeylink, key, msg);
         key->num_values = 1;        // fake number of values
         return status;
      }
   }
   
   memcpy(key, pkey, sizeof(KEY));

   return DB_SUCCESS;
}
#endif /* LOCAL_ROUTINES */

/********************************************************************/
/**
Get key structure from a handle.

KEY structure has following format:
\code
typedef struct {
  DWORD         type;                 // TID_xxx type
  INT           num_values;           // number of values
  char          name[NAME_LENGTH];    // name of variable
  INT           data;                 // Address of variable (offset)
  INT           total_size;           // Total size of data block
  INT           item_size;            // Size of single data item
  WORD          access_mode;          // Access mode
  WORD          notify_count;         // Notify counter
  INT           next_key;             // Address of next key
  INT           parent_keylist;       // keylist to which this key belongs
  INT           last_written;         // Time of last write action
} KEY;
\endcode
Most of these values are used for internal purposes, the values which are of
public interest are type, num_values, and name. For keys which contain a
single value, num_values equals to one and total_size equals to
item_size. For keys which contain an array of strings (TID_STRING),
item_size equals to the length of one string.
\code
KEY   key;
HNDLE hkey;
db_find_key(hDB, 0, "/Runinfo/Run number", &hkey);
db_get_key(hDB, hkey, &key);
printf("The run number is of type %s\n", rpc_tid_name(key.type));
\endcode
@param hDB          ODB handle obtained via cm_get_experiment_database().
@param hKey Handle for key where search starts, zero for root.
@param key Pointer to KEY stucture.
@return DB_SUCCESS, DB_INVALID_HANDLE
*/
INT db_get_key(HNDLE hDB, HNDLE hKey, KEY * key)
{
   if (rpc_is_remote())
      return rpc_call(RPC_DB_GET_KEY, hDB, hKey, key);

#ifdef LOCAL_ROUTINES
   {
      DATABASE_HEADER *pheader;
      int status;

      if (hDB > _database_entries || hDB <= 0) {
         cm_msg(MERROR, "db_get_key", "invalid database handle");
         return DB_INVALID_HANDLE;
      }

      if (!_database[hDB - 1].attached) {
         cm_msg(MERROR, "db_get_key", "invalid database handle");
         return DB_INVALID_HANDLE;
      }

      if (hKey < (int) sizeof(DATABASE_HEADER) && hKey != 0) {
         cm_msg(MERROR, "db_get_key", "invalid key handle");
         return DB_INVALID_HANDLE;
      }

      db_err_msg *msg = NULL;

      db_lock_database(hDB);

      pheader = _database[hDB - 1].database_header;

      status = db_get_key_locked(pheader, hKey, key, &msg);

      db_unlock_database(hDB);

      if (msg)
         db_flush_msg(&msg);

      return status;
   }
#endif                          /* LOCAL_ROUTINES */

   return DB_SUCCESS;
}

/********************************************************************/
/**
Same as db_get_key, but it does not follow a link to an array index
@param hDB          ODB handle obtained via cm_get_experiment_database().
@param hKey Handle for key where search starts, zero for root.
@param key Pointer to KEY stucture.
@return DB_SUCCESS, DB_INVALID_HANDLE
*/
INT db_get_link(HNDLE hDB, HNDLE hKey, KEY * key)
{
   if (rpc_is_remote())
      return rpc_call(RPC_DB_GET_LINK, hDB, hKey, key);

#ifdef LOCAL_ROUTINES
   {
      DATABASE_HEADER *pheader;
      KEY *pkey;

      if (hDB > _database_entries || hDB <= 0) {
         cm_msg(MERROR, "db_get_link", "invalid database handle");
         return DB_INVALID_HANDLE;
      }

      if (!_database[hDB - 1].attached) {
         cm_msg(MERROR, "db_get_link", "invalid database handle");
         return DB_INVALID_HANDLE;
      }

      if (hKey < (int) sizeof(DATABASE_HEADER) && hKey != 0) {
         cm_msg(MERROR, "db_get_link", "invalid key handle");
         return DB_INVALID_HANDLE;
      }

      db_lock_database(hDB);

      pheader = _database[hDB - 1].database_header;

      if (!hKey)
         hKey = pheader->root_key;

      /* check if hKey argument is correct */
      if (!db_validate_hkey(pheader, hKey)) {
         db_unlock_database(hDB);
         return DB_INVALID_HANDLE;
      }

      pkey = (KEY *) ((char *) pheader + hKey);

      if (pkey->type < 1 || pkey->type >= TID_LAST) {
         int pkey_type = pkey->type;
         db_unlock_database(hDB);
         cm_msg(MERROR, "db_get_link", "hkey %d invalid key type %d", hKey, pkey_type);
         return DB_INVALID_HANDLE;
      }

      memcpy(key, pkey, sizeof(KEY));

      db_unlock_database(hDB);

   }
#endif                          /* LOCAL_ROUTINES */

   return DB_SUCCESS;
}

/********************************************************************/
/**
Get time when key was last modified
@param hDB          ODB handle obtained via cm_get_experiment_database().
@param hKey              Handle of key to operate on
@param delta             Seconds since last update
@return DB_SUCCESS, DB_INVALID_HANDLE
*/
INT db_get_key_time(HNDLE hDB, HNDLE hKey, DWORD * delta)
{
   if (rpc_is_remote())
      return rpc_call(RPC_DB_GET_KEY_TIME, hDB, hKey, delta);

#ifdef LOCAL_ROUTINES
   {
      DATABASE_HEADER *pheader;
      KEY *pkey;

      if (hDB > _database_entries || hDB <= 0) {
         cm_msg(MERROR, "db_get_key", "invalid database handle");
         return DB_INVALID_HANDLE;
      }

      if (!_database[hDB - 1].attached) {
         cm_msg(MERROR, "db_get_key", "invalid database handle");
         return DB_INVALID_HANDLE;
      }

      if (hKey < (int) sizeof(DATABASE_HEADER)) {
         cm_msg(MERROR, "db_get_key", "invalid key handle");
         return DB_INVALID_HANDLE;
      }

      db_lock_database(hDB);

      pheader = _database[hDB - 1].database_header;

      /* check if hKey argument is correct */
      if (!db_validate_hkey(pheader, hKey)) {
         db_unlock_database(hDB);
         return DB_INVALID_HANDLE;
      }

      pkey = (KEY *) ((char *) pheader + hKey);

      *delta = ss_time() - pkey->last_written;

      db_unlock_database(hDB);

   }
#endif                          /* LOCAL_ROUTINES */

   return DB_SUCCESS;
}

/********************************************************************/
/**
Get key info (separate values instead of structure)
@param hDB          ODB handle obtained via cm_get_experiment_database().
@param hKey              Handle of key to operate on
@param name             Key name
@param name_size        Size of the give name (done with sizeof())
@param type             Key type (see @ref Midas_Data_Types).
@param num_values       Number of values in key.
@param item_size        Size of individual key value (used for strings)
@return DB_SUCCESS, DB_INVALID_HANDLE
*/
INT db_get_key_info(HNDLE hDB, HNDLE hKey, char *name, INT name_size, INT * type, INT * num_values, INT * item_size)
{
   if (rpc_is_remote())
      return rpc_call(RPC_DB_GET_KEY_INFO, hDB, hKey, name, name_size, type, num_values, item_size);

#ifdef LOCAL_ROUTINES
   {
      DATABASE_HEADER *pheader;
      KEY *pkey;
      KEYLIST *pkeylist;

      if (hDB > _database_entries || hDB <= 0) {
         cm_msg(MERROR, "db_get_key_info", "invalid database handle");
         return DB_INVALID_HANDLE;
      }

      if (!_database[hDB - 1].attached) {
         cm_msg(MERROR, "db_get_key_info", "invalid database handle");
         return DB_INVALID_HANDLE;
      }

      if (hKey < (int) sizeof(DATABASE_HEADER)) {
         cm_msg(MERROR, "db_get_key_info", "invalid key handle");
         return DB_INVALID_HANDLE;
      }

      db_lock_database(hDB);

      pheader = _database[hDB - 1].database_header;

      /* check if hKey argument is correct */
      if (!db_validate_hkey(pheader, hKey)) {
         db_unlock_database(hDB);
         return DB_INVALID_HANDLE;
      }

      pkey = (KEY *) ((char *) pheader + hKey);

      if ((INT) strlen(pkey->name) + 1 > name_size) {
         /* truncate name */
         memcpy(name, pkey->name, name_size - 1);
         name[name_size] = 0;
      } else
         strcpy(name, pkey->name);

      /* convert "root" to "/" */
      if (strcmp(name, "root") == 0)
         strcpy(name, "/");

      *type = pkey->type;
      *num_values = pkey->num_values;
      *item_size = pkey->item_size;

      if (pkey->type == TID_KEY) {
         pkeylist = (KEYLIST *) ((char *) pheader + pkey->data);
         *num_values = pkeylist->num_keys;
      }

      db_unlock_database(hDB);
   }
#endif                          /* LOCAL_ROUTINES */

   return DB_SUCCESS;
}

/**dox***************************************************************/
#ifndef DOXYGEN_SHOULD_SKIP_THIS


/*------------------------------------------------------------------*/
INT db_rename_key(HNDLE hDB, HNDLE hKey, const char *name)
/********************************************************************\

  Routine: db_get_key

  Purpose: Rename a key

  Input:
    HNDLE hDB               Handle to the database
    HNDLE hKey              Handle of key
    char  *name             New key name

  Output:
    <none>

  Function value:
    DB_SUCCESS              Successful completion
    DB_INVALID_HANDLE       Database handle is invalid
    DB_INVALID_NAME         Key name contains '/'

\********************************************************************/
{
   if (rpc_is_remote())
      return rpc_call(RPC_DB_RENAME_KEY, hDB, hKey, name);

#ifdef LOCAL_ROUTINES
   {
      DATABASE_HEADER *pheader;
      KEY *pkey;
      int status;

      if (hDB > _database_entries || hDB <= 0) {
         cm_msg(MERROR, "db_rename_key", "invalid database handle");
         return DB_INVALID_HANDLE;
      }

      if (!_database[hDB - 1].attached) {
         cm_msg(MERROR, "db_rename_key", "invalid database handle");
         return DB_INVALID_HANDLE;
      }

      if (hKey < (int) sizeof(DATABASE_HEADER)) {
         cm_msg(MERROR, "db_rename_key", "invalid key handle");
         return DB_INVALID_HANDLE;
      }

      status = db_validate_name(name, FALSE, "db_rename_key");
      if (status != DB_SUCCESS)
         return status;

      if (name == NULL) {
         cm_msg(MERROR, "db_rename_key", "key name is NULL");
         return DB_INVALID_NAME;
      }

      if (strlen(name) < 1) {
         cm_msg(MERROR, "db_rename_key", "key name is too short");
         return DB_INVALID_NAME;
      }

      if (strchr(name, '/')) {
         cm_msg(MERROR, "db_rename_key", "key name may not contain \"/\"");
         return DB_INVALID_NAME;
      }

      db_lock_database(hDB);

      pheader = _database[hDB - 1].database_header;

      /* check if hKey argument is correct */
      if (!db_validate_hkey(pheader, hKey)) {
         db_unlock_database(hDB);
         return DB_INVALID_HANDLE;
      }

      pkey = (KEY *) ((char *) pheader + hKey);

      if (!pkey->type) {
         int pkey_type = pkey->type;
         db_unlock_database(hDB);
         cm_msg(MERROR, "db_rename_key", "hkey %d invalid key type %d", hKey, pkey_type);
         return DB_INVALID_HANDLE;
      }

      db_allow_write_locked(&_database[hDB - 1], "db_rename_key");

      strlcpy(pkey->name, name, NAME_LENGTH);

      db_unlock_database(hDB);

   }
#endif                          /* LOCAL_ROUTINES */

   return DB_SUCCESS;
}

/*------------------------------------------------------------------*/
INT db_reorder_key(HNDLE hDB, HNDLE hKey, INT idx)
/********************************************************************\

  Routine: db_reorder_key

  Purpose: Reorder key so that key hKey apprears at position 'index'
           in keylist (or at bottom if index<0)

  Input:
    HNDLE hDB               Handle to the database
    HNDLE hKey              Handle of key
    INT   idx               New positio of key in keylist

  Output:
    <none>

  Function value:
    DB_SUCCESS              Successful completion
    DB_INVALID_HANDLE       Database handle is invalid
    DB_NO_ACCESS            Key is locked for write
    DB_OPEN_RECORD          Key, subkey or parent key is open

\********************************************************************/
{
   if (rpc_is_remote())
      return rpc_call(RPC_DB_REORDER_KEY, hDB, hKey, idx);

#ifdef LOCAL_ROUTINES
   {
      DATABASE_HEADER *pheader;
      KEY *pkey, *pnext_key, *pkey_tmp;
      KEYLIST *pkeylist;
      INT i;

      if (hDB > _database_entries || hDB <= 0) {
         cm_msg(MERROR, "db_rename_key", "invalid database handle");
         return DB_INVALID_HANDLE;
      }

      if (!_database[hDB - 1].attached) {
         cm_msg(MERROR, "db_rename_key", "invalid database handle");
         return DB_INVALID_HANDLE;
      }

      if (hKey < (int) sizeof(DATABASE_HEADER)) {
         cm_msg(MERROR, "db_rename_key", "invalid key handle");
         return DB_INVALID_HANDLE;
      }

      db_lock_database(hDB);

      pheader = _database[hDB - 1].database_header;

      /* check if hKey argument is correct */
      if (!db_validate_hkey(pheader, hKey)) {
         db_unlock_database(hDB);
         return DB_INVALID_HANDLE;
      }

      pkey = (KEY *) ((char *) pheader + hKey);

      if (!pkey->type) {
         int pkey_type = pkey->type;
         db_unlock_database(hDB);
         cm_msg(MERROR, "db_reorder_key", "hkey %d invalid key type %d", hKey, pkey_type);
         return DB_INVALID_HANDLE;
      }

      if (!(pkey->access_mode & MODE_WRITE)) {
         db_unlock_database(hDB);
         return DB_NO_ACCESS;
      }

      /* check if someone has opened key or parent */
      do {
#ifdef CHECK_OPEN_RECORD
         if (pkey->notify_count) {
            db_unlock_database(hDB);
            return DB_OPEN_RECORD;
         }
#endif
         if (pkey->parent_keylist == 0)
            break;

         pkeylist = (KEYLIST *) ((char *) pheader + pkey->parent_keylist);
         // FIXME: validate pkeylist->parent
         pkey = (KEY *) ((char *) pheader + pkeylist->parent);
      } while (TRUE);
      
      db_allow_write_locked(&_database[hDB - 1], "db_reorder_key");
      
      pkey = (KEY *) ((char *) pheader + hKey); // NB: hKey is already validated
      pkeylist = (KEYLIST *) ((char *) pheader + pkey->parent_keylist);

      /* first remove key from list */
      pnext_key = (KEY *) (POINTER_T) pkey->next_key; // FIXME: what is this pointer cast?

      if ((KEY *) ((char *) pheader + pkeylist->first_key) == pkey) {
         /* key is first in list */
         pkeylist->first_key = (POINTER_T) pnext_key;
      } else {
         /* find predecessor */
         // FIXME: validate pkeylist->first_key
         pkey_tmp = (KEY *) ((char *) pheader + pkeylist->first_key);
         while ((KEY *) ((char *) pheader + pkey_tmp->next_key) != pkey) {
            // FIXME: validate pkey_tmp->next_key
            pkey_tmp = (KEY *) ((char *) pheader + pkey_tmp->next_key);
         }
         pkey_tmp->next_key = (POINTER_T) pnext_key;
      }

      /* add key to list at proper index */
      // FIXME: validate pkeylist->first_key
      pkey_tmp = (KEY *) ((char *) pheader + pkeylist->first_key);
      if (idx < 0 || idx >= pkeylist->num_keys - 1) {
         /* add at bottom */

         /* find last key */
         for (i = 0; i < pkeylist->num_keys - 2; i++) {
            // FIXME: validate pkey_tmp->next_key
            pkey_tmp = (KEY *) ((char *) pheader + pkey_tmp->next_key);
         }

         pkey_tmp->next_key = (POINTER_T) pkey - (POINTER_T) pheader;
         pkey->next_key = 0;
      } else {
         if (idx == 0) {
            /* add at top */
            pkey->next_key = pkeylist->first_key;
            pkeylist->first_key = (POINTER_T) pkey - (POINTER_T) pheader;
         } else {
            /* add at position index */
            for (i = 0; i < idx - 1; i++) {
               // FIXME: validate pkey_tmp->next_key
               pkey_tmp = (KEY *) ((char *) pheader + pkey_tmp->next_key);
            }

            pkey->next_key = pkey_tmp->next_key;
            pkey_tmp->next_key = (POINTER_T) pkey - (POINTER_T) pheader;
         }
      }

      db_unlock_database(hDB);

   }
#endif                          /* LOCAL_ROUTINES */

   return DB_SUCCESS;
}

/**dox***************************************************************/
#endif                          /* DOXYGEN_SHOULD_SKIP_THIS */


/********************************************************************/
/**
Get key data from a handle

The function returns single values or whole arrays which are contained
in an ODB key. Since the data buffer is of type void, no type checking can be
performed by the compiler. Therefore the type has to be explicitly supplied,
which is checked against the type stored in the ODB.
\code
  HNLDE hkey;
  INT   run_number, size;
  // get key handle for run number
  db_find_key(hDB, 0, "/Runinfo/Run number", &hkey);
  // return run number
  size = sizeof(run_number);
  db_get_data(hDB, hkey, &run_number, &size,TID_INT);
\endcode
@param hDB          ODB handle obtained via cm_get_experiment_database().
@param hKey         Handle for key where search starts, zero for root.
@param data         Pointer to the return data.
@param buf_size     Size of data buffer.
@param type         Type of key, one of TID_xxx (see @ref Midas_Data_Types).
@return DB_SUCCESS, DB_INVALID_HANDLE, DB_TRUNCATED, DB_TYPE_MISMATCH
*/
INT db_get_data(HNDLE hDB, HNDLE hKey, void *data, INT * buf_size, DWORD type)
{
   if (rpc_is_remote())
      return rpc_call(RPC_DB_GET_DATA, hDB, hKey, data, buf_size, type);

#ifdef LOCAL_ROUTINES
   {
      DATABASE_HEADER *pheader;
      KEY *pkey;
      char str[256];

      if (hDB > _database_entries || hDB <= 0) {
         cm_msg(MERROR, "db_get_data", "Invalid database handle");
         return DB_INVALID_HANDLE;
      }

      if (!_database[hDB - 1].attached) {
         cm_msg(MERROR, "db_get_data", "invalid database handle");
         return DB_INVALID_HANDLE;
      }

      if (hKey < (int) sizeof(DATABASE_HEADER)) {
         cm_msg(MERROR, "db_get_data", "invalid key handle");
         return DB_INVALID_HANDLE;
      }

      db_lock_database(hDB);

      pheader = _database[hDB - 1].database_header;

      /* check if hKey argument is correct */
      if (!db_validate_hkey(pheader, hKey)) {
         db_unlock_database(hDB);
         return DB_INVALID_HANDLE;
      }

      pkey = (KEY *) ((char *) pheader + hKey);

      /* check for read access */
      if (!(pkey->access_mode & MODE_READ)) {
         db_unlock_database(hDB);
         return DB_NO_ACCESS;
      }

      if (!pkey->type) {
         int pkey_type = pkey->type;
         db_unlock_database(hDB);
         cm_msg(MERROR, "db_get_data", "hkey %d invalid key type %d", hKey, pkey_type);
         return DB_INVALID_HANDLE;
      }

      /* follow links to array index */
      if (pkey->type == TID_LINK) {
         char link_name[256];
         int i;
         HNDLE hkey;
         KEY key;

         strlcpy(link_name, (char *) pheader + pkey->data, sizeof(link_name));
         if (strlen(link_name) > 0 && link_name[strlen(link_name) - 1] == ']') {
            db_unlock_database(hDB);
            if (strchr(link_name, '[') == NULL)
               return DB_INVALID_LINK;
            i = atoi(strchr(link_name, '[') + 1);
            *strchr(link_name, '[') = 0;
            if (db_find_key(hDB, 0, link_name, &hkey) != DB_SUCCESS)
               return DB_INVALID_LINK;
            db_get_key(hDB, hkey, &key);
            return db_get_data_index(hDB, hkey, data, buf_size, i, key.type);
         }
      }

      if (pkey->type != type) {
         int pkey_type = pkey->type;
         db_unlock_database(hDB);
         db_get_path(hDB, hKey, str, sizeof(str));
         cm_msg(MERROR, "db_get_data", "\"%s\" is of type %s, not %s", str, rpc_tid_name(pkey_type), rpc_tid_name(type));
         return DB_TYPE_MISMATCH;
      }

      /* keys cannot contain data */
      if (pkey->type == TID_KEY) {
         db_unlock_database(hDB);
         db_get_path(hDB, hKey, str, sizeof(str));
         cm_msg(MERROR, "db_get_data", "Key \"%s\" cannot contain data", str);
         return DB_TYPE_MISMATCH;
      }

      /* check if key has data */
      if (pkey->data == 0) {
         memset(data, 0, *buf_size);
         *buf_size = 0;
         db_unlock_database(hDB);
         return DB_SUCCESS;
      }

      /* check if buffer is too small */
      if (pkey->num_values * pkey->item_size > *buf_size) {
         int pkey_size = pkey->num_values * pkey->item_size;
         memcpy(data, (char *) pheader + pkey->data, *buf_size);
         db_unlock_database(hDB);
         char str[MAX_ODB_PATH];
         db_get_path(hDB, hKey, str, sizeof(str));
         cm_msg(MERROR, "db_get_data", "data for key \"%s\" truncated from %d to %d bytes", str, pkey_size, *buf_size);
         return DB_TRUNCATED;
      }

      /* copy key data */
      memcpy(data, (char *) pheader + pkey->data, pkey->num_values * pkey->item_size);
      *buf_size = pkey->num_values * pkey->item_size;

      db_unlock_database(hDB);

   }
#endif                          /* LOCAL_ROUTINES */

   return DB_SUCCESS;
}

/********************************************************************/
/**
Same as db_get_data, but do not follow a link to an array index

@param hDB          ODB handle obtained via cm_get_experiment_database().
@param hKey         Handle for key where search starts, zero for root.
@param data         Pointer to the return data.
@param buf_size     Size of data buffer.
@param type         Type of key, one of TID_xxx (see @ref Midas_Data_Types).
@return DB_SUCCESS, DB_INVALID_HANDLE, DB_TRUNCATED, DB_TYPE_MISMATCH
*/
INT db_get_link_data(HNDLE hDB, HNDLE hKey, void *data, INT * buf_size, DWORD type)
{
   if (rpc_is_remote())
      return rpc_call(RPC_DB_GET_LINK_DATA, hDB, hKey, data, buf_size, type);

#ifdef LOCAL_ROUTINES
   {
      DATABASE_HEADER *pheader;
      KEY *pkey;

      if (hDB > _database_entries || hDB <= 0) {
         cm_msg(MERROR, "db_get_data", "Invalid database handle");
         return DB_INVALID_HANDLE;
      }

      if (!_database[hDB - 1].attached) {
         cm_msg(MERROR, "db_get_data", "invalid database handle");
         return DB_INVALID_HANDLE;
      }

      if (hKey < (int) sizeof(DATABASE_HEADER)) {
         cm_msg(MERROR, "db_get_data", "invalid key handle");
         return DB_INVALID_HANDLE;
      }

      db_lock_database(hDB);

      pheader = _database[hDB - 1].database_header;

      /* check if hKey argument is correct */
      if (!db_validate_hkey(pheader, hKey)) {
         db_unlock_database(hDB);
         return DB_INVALID_HANDLE;
      }

      pkey = (KEY *) ((char *) pheader + hKey);

      /* check for read access */
      if (!(pkey->access_mode & MODE_READ)) {
         db_unlock_database(hDB);
         return DB_NO_ACCESS;
      }

      if (!pkey->type) {
         int pkey_type = pkey->type;
         db_unlock_database(hDB);
         cm_msg(MERROR, "db_get_data", "hkey %d invalid key type %d", hKey, pkey_type);
         return DB_INVALID_HANDLE;
      }

      if (pkey->type != type) {
         int pkey_type = pkey->type;
         char pkey_name[NAME_LENGTH];
         strlcpy(pkey_name, pkey->name, sizeof(pkey_name));
         db_unlock_database(hDB);
         cm_msg(MERROR, "db_get_data", "\"%s\" is of type %s, not %s", pkey_name, rpc_tid_name(pkey_type), rpc_tid_name(type));
         return DB_TYPE_MISMATCH;
      }

      /* keys cannot contain data */
      if (pkey->type == TID_KEY) {
         db_unlock_database(hDB);
         cm_msg(MERROR, "db_get_data", "Key cannot contain data");
         return DB_TYPE_MISMATCH;
      }

      /* check if key has data */
      if (pkey->data == 0) {
         memset(data, 0, *buf_size);
         *buf_size = 0;
         db_unlock_database(hDB);
         return DB_SUCCESS;
      }

      /* check if buffer is too small */
      if (pkey->num_values * pkey->item_size > *buf_size) {
         int pkey_size = pkey->num_values * pkey->item_size;
         memcpy(data, (char *) pheader + pkey->data, *buf_size);
         db_unlock_database(hDB);
         char str[MAX_ODB_PATH];
         db_get_path(hDB, hKey, str, sizeof(str));
         cm_msg(MERROR, "db_get_data", "data for key \"%s\" truncated from %d to %d bytes", str, pkey_size, *buf_size);
         return DB_TRUNCATED;
      }

      /* copy key data */
      memcpy(data, (char *) pheader + pkey->data, pkey->num_values * pkey->item_size);
      *buf_size = pkey->num_values * pkey->item_size;

      db_unlock_database(hDB);

   }
#endif                          /* LOCAL_ROUTINES */

   return DB_SUCCESS;
}

/**dox***************************************************************/
#ifndef DOXYGEN_SHOULD_SKIP_THIS

/*------------------------------------------------------------------*/
INT db_get_data1(HNDLE hDB, HNDLE hKey, void *data, INT * buf_size, DWORD type, INT * num_values)
/********************************************************************\

  Routine: db_get_data1

  Purpose: Get key data from a handle, return number of values

  Input:
    HNDLE  hDB              Handle to the database
    HNDLE  hKey             Handle of key
    INT    *buf_size        Size of data buffer
    DWORD  type             Type of data

  Output:
    void   *data            Key data
    INT    *buf_size        Size of key data
    INT    *num_values      Number of values

  Function value:
    DB_SUCCESS              Successful completion
    DB_INVALID_HANDLE       Database handle is invalid
    DB_TRUNCATED            Return buffer is smaller than key data
    DB_TYPE_MISMATCH        Type mismatch

\********************************************************************/
{
   if (rpc_is_remote())
      return rpc_call(RPC_DB_GET_DATA1, hDB, hKey, data, buf_size, type, num_values);

#ifdef LOCAL_ROUTINES
   {
      DATABASE_HEADER *pheader;
      KEY *pkey;

      if (hDB > _database_entries || hDB <= 0) {
         cm_msg(MERROR, "db_get_data", "Invalid database handle");
         return DB_INVALID_HANDLE;
      }

      if (!_database[hDB - 1].attached) {
         cm_msg(MERROR, "db_get_data", "invalid database handle");
         return DB_INVALID_HANDLE;
      }

      if (hKey < (int) sizeof(DATABASE_HEADER)) {
         cm_msg(MERROR, "db_get_data", "invalid key handle");
         return DB_INVALID_HANDLE;
      }

      db_lock_database(hDB);

      pheader = _database[hDB - 1].database_header;

      /* check if hKey argument is correct */
      if (!db_validate_hkey(pheader, hKey)) {
         db_unlock_database(hDB);
         return DB_INVALID_HANDLE;
      }

      pkey = (KEY *) ((char *) pheader + hKey);

      /* check for read access */
      if (!(pkey->access_mode & MODE_READ)) {
         db_unlock_database(hDB);
         return DB_NO_ACCESS;
      }

      if (!pkey->type) {
         int pkey_type = pkey->type;
         db_unlock_database(hDB);
         cm_msg(MERROR, "db_get_data", "hkey %d invalid key type %d", hKey, pkey_type);
         return DB_INVALID_HANDLE;
      }

      if (pkey->type != type) {
         int pkey_type = pkey->type;
         char pkey_name[NAME_LENGTH];
         strlcpy(pkey_name, pkey->name, sizeof(pkey_name));
         db_unlock_database(hDB);
         cm_msg(MERROR, "db_get_data", "\"%s\" is of type %s, not %s", pkey_name, rpc_tid_name(pkey_type), rpc_tid_name(type));
         return DB_TYPE_MISMATCH;
      }

      /* keys cannot contain data */
      if (pkey->type == TID_KEY) {
         db_unlock_database(hDB);
         cm_msg(MERROR, "db_get_data", "Key cannot contain data");
         return DB_TYPE_MISMATCH;
      }

      /* check if key has data */
      if (pkey->data == 0) {
         memset(data, 0, *buf_size);
         *buf_size = 0;
         db_unlock_database(hDB);
         return DB_SUCCESS;
      }

      /* check if buffer is too small */
      if (pkey->num_values * pkey->item_size > *buf_size) {
         int pkey_size = pkey->num_values * pkey->item_size;
         memcpy(data, (char *) pheader + pkey->data, *buf_size);
         db_unlock_database(hDB);
         char str[MAX_ODB_PATH];
         db_get_path(hDB, hKey, str, sizeof(str));
         cm_msg(MERROR, "db_get_data", "data for key \"%s\" truncated from %d to %d bytes", str, pkey_size, *buf_size);
         return DB_TRUNCATED;
      }

      /* copy key data */
      memcpy(data, (char *) pheader + pkey->data, pkey->num_values * pkey->item_size);
      *buf_size = pkey->num_values * pkey->item_size;
      *num_values = pkey->num_values;

      db_unlock_database(hDB);

   }
#endif                          /* LOCAL_ROUTINES */

   return DB_SUCCESS;
}

/**dox***************************************************************/
#endif                          /* DOXYGEN_SHOULD_SKIP_THIS */

/********************************************************************/
/**
returns a single value of keys containing arrays of values.

The function returns a single value of keys containing arrays of values.
@param hDB          ODB handle obtained via cm_get_experiment_database().
@param hKey         Handle for key where search starts, zero for root.
@param data         Size of data buffer.
@param buf_size     Return size of the record.
@param idx          Index of array [0..n-1].
@param type         Type of key, one of TID_xxx (see @ref Midas_Data_Types).
@return DB_SUCCESS, DB_INVALID_HANDLE, DB_TRUNCATED, DB_OUT_OF_RANGE
*/
INT db_get_data_index(HNDLE hDB, HNDLE hKey, void *data, INT * buf_size, INT idx, DWORD type)
{
   if (rpc_is_remote())
      return rpc_call(RPC_DB_GET_DATA_INDEX, hDB, hKey, data, buf_size, idx, type);

#ifdef LOCAL_ROUTINES
   {
      DATABASE_HEADER *pheader;
      KEY *pkey;
      char str[256];

      if (hDB > _database_entries || hDB <= 0) {
         cm_msg(MERROR, "db_get_data", "Invalid database handle");
         return DB_INVALID_HANDLE;
      }

      if (!_database[hDB - 1].attached) {
         cm_msg(MERROR, "db_get_data", "invalid database handle");
         return DB_INVALID_HANDLE;
      }

      if (hKey < (int) sizeof(DATABASE_HEADER)) {
         cm_msg(MERROR, "db_get_data", "invalid key handle");
         return DB_INVALID_HANDLE;
      }

      db_lock_database(hDB);

      pheader = _database[hDB - 1].database_header;

      /* check if hKey argument is correct */
      if (!db_validate_hkey(pheader, hKey)) {
         db_unlock_database(hDB);
         return DB_INVALID_HANDLE;
      }

      pkey = (KEY *) ((char *) pheader + hKey);

      /* check for read access */
      if (!(pkey->access_mode & MODE_READ)) {
         db_unlock_database(hDB);
         return DB_NO_ACCESS;
      }

      if (!pkey->type) {
         int pkey_type = pkey->type;
         db_unlock_database(hDB);
         cm_msg(MERROR, "db_get_data_index", "hkey %d invalid key type %d", hKey, pkey_type);
         return DB_INVALID_HANDLE;
      }

      if (pkey->type != type) {
         int pkey_type = pkey->type;
         char pkey_name[NAME_LENGTH];
         strlcpy(pkey_name, pkey->name, sizeof(pkey_name));
         db_unlock_database(hDB);
         cm_msg(MERROR, "db_get_data_index", "\"%s\" is of type %s, not %s", pkey_name, rpc_tid_name(pkey_type), rpc_tid_name(type));
         return DB_TYPE_MISMATCH;
      }

      /* keys cannot contain data */
      if (pkey->type == TID_KEY) {
         db_unlock_database(hDB);
         cm_msg(MERROR, "db_get_data_index", "Key cannot contain data");
         return DB_TYPE_MISMATCH;
      }

      /* check if key has data */
      if (pkey->data == 0) {
         memset(data, 0, *buf_size);
         *buf_size = 0;
         db_unlock_database(hDB);
         return DB_SUCCESS;
      }

      /* check if index in range */
      if (idx < 0 || idx >= pkey->num_values) {
         int pkey_num_values = pkey->num_values;
         memset(data, 0, *buf_size);
         db_unlock_database(hDB);

         db_get_path(hDB, hKey, str, sizeof(str));
         cm_msg(MERROR, "db_get_data_index", "index (%d) exceeds array length (%d) for key \"%s\"", idx, pkey_num_values, str);
         return DB_OUT_OF_RANGE;
      }

      /* check if buffer is too small */
      if (pkey->item_size > *buf_size) {
         int pkey_size = pkey->item_size;
         /* copy data */
         memcpy(data, (char *) pheader + pkey->data + idx * pkey->item_size, *buf_size);
         db_unlock_database(hDB);
         char str[MAX_ODB_PATH];
         db_get_path(hDB, hKey, str, sizeof(str));
         cm_msg(MERROR, "db_get_data_index", "data for key \"%s\" truncated from %d to %d bytes", str, pkey_size, *buf_size);
         return DB_TRUNCATED;
      }

      /* copy key data */
      memcpy(data, (char *) pheader + pkey->data + idx * pkey->item_size, pkey->item_size);
      *buf_size = pkey->item_size;

      db_unlock_database(hDB);

   }
#endif                          /* LOCAL_ROUTINES */

   return DB_SUCCESS;
}

/********************************************************************/
/**
Set key data from a handle. Adjust number of values if
previous data has different size.
\code
HNLDE hkey;
 INT   run_number;
 // get key handle for run number
 db_find_key(hDB, 0, "/Runinfo/Run number", &hkey);
 // set run number
 db_set_data(hDB, hkey, &run_number, sizeof(run_number),TID_INT);
\endcode
@param hDB          ODB handle obtained via cm_get_experiment_database().
@param hKey Handle for key where search starts, zero for root.
@param data Buffer from which data gets copied to.
@param buf_size Size of data buffer.
@param num_values Number of data values (for arrays).
@param type Type of key, one of TID_xxx (see @ref Midas_Data_Types).
@return DB_SUCCESS, DB_INVALID_HANDLE, DB_TRUNCATED
*/
INT db_set_data(HNDLE hDB, HNDLE hKey, const void *data, INT buf_size, INT num_values, DWORD type)
{
   if (rpc_is_remote())
      return rpc_call(RPC_DB_SET_DATA, hDB, hKey, data, buf_size, num_values, type);

#ifdef LOCAL_ROUTINES
   {
      DATABASE_HEADER *pheader;
      KEY *pkey;
      HNDLE hkeylink;
      int link_idx;
      char link_name[256];

      if (hDB > _database_entries || hDB <= 0) {
         cm_msg(MERROR, "db_set_data", "invalid database handle");
         return DB_INVALID_HANDLE;
      }

      if (!_database[hDB - 1].attached) {
         cm_msg(MERROR, "db_set_data", "invalid database handle");
         return DB_INVALID_HANDLE;
      }

      if (hKey < (int) sizeof(DATABASE_HEADER)) {
         cm_msg(MERROR, "db_set_data", "invalid key handle");
         return DB_INVALID_HANDLE;
      }

      if (num_values == 0)
         return DB_INVALID_PARAM;

      db_lock_database(hDB);

      pheader = _database[hDB - 1].database_header;

      /* check if hKey argument is correct */
      if (!db_validate_hkey(pheader, hKey)) {
         db_unlock_database(hDB);
         return DB_INVALID_HANDLE;
      }

      pkey = (KEY *) ((char *) pheader + hKey);

      /* check for write access */
      if (!(pkey->access_mode & MODE_WRITE) || (pkey->access_mode & MODE_EXCLUSIVE)) {
         db_unlock_database(hDB);
         return DB_NO_ACCESS;
      }

      /* check for link to array index */
      if (pkey->type == TID_LINK) {
         strlcpy(link_name, (char *) pheader + pkey->data, sizeof(link_name));
         if (strlen(link_name) > 0 && link_name[strlen(link_name) - 1] == ']') {
            db_unlock_database(hDB);
            if (strchr(link_name, '[') == NULL)
               return DB_INVALID_LINK;
            link_idx = atoi(strchr(link_name, '[') + 1);
            *strchr(link_name, '[') = 0;
            if (db_find_key(hDB, 0, link_name, &hkeylink) != DB_SUCCESS)
               return DB_INVALID_LINK;
            return db_set_data_index(hDB, hkeylink, data, buf_size, link_idx, type);
         }
      }

      if (pkey->type != type) {
         int pkey_type = pkey->type;
         char pkey_name[NAME_LENGTH];
         strlcpy(pkey_name, pkey->name, sizeof(pkey_name));
         db_unlock_database(hDB);
         cm_msg(MERROR, "db_set_data", "\"%s\" is of type %s, not %s", pkey_name, rpc_tid_name(pkey_type), rpc_tid_name(type));
         return DB_TYPE_MISMATCH;
      }

      /* keys cannot contain data */
      if (pkey->type == TID_KEY) {
         db_unlock_database(hDB);
         cm_msg(MERROR, "db_set_data", "Key cannot contain data");
         return DB_TYPE_MISMATCH;
      }

      db_allow_write_locked(&_database[hDB-1], "db_set_data");

      /* if no buf_size given (Java!), calculate it */
      if (buf_size == 0)
         buf_size = pkey->item_size * num_values;

      /* resize data size if necessary */
      if (pkey->total_size != buf_size) {
         pkey->data = (POINTER_T) realloc_data(pheader, (char *) pheader + pkey->data, pkey->total_size, buf_size, "db_set_data");

         if (pkey->data == 0) {
            pkey->total_size = 0;
            db_unlock_database(hDB);
            cm_msg(MERROR, "db_set_data", "online database full");
            return DB_FULL;
         }

         pkey->data -= (POINTER_T) pheader;
         pkey->total_size = buf_size;
      }

      /* set number of values */
      pkey->num_values = num_values;
      if (num_values)
         pkey->item_size = buf_size / num_values;

      /* copy data */
      memcpy((char *) pheader + pkey->data, data, buf_size);

      /* update time */
      pkey->last_written = ss_time();

      db_notify_clients(hDB, hKey, -1, TRUE);
      db_unlock_database(hDB);

   }
#endif                          /* LOCAL_ROUTINES */

   return DB_SUCCESS;
}

INT db_set_data1(HNDLE hDB, HNDLE hKey, const void *data, INT buf_size, INT num_values, DWORD type)
/*
 
 Same as db_set_data(), but do not notify hot-linked clients
 
 */
{
   if (rpc_is_remote())
      return rpc_call(RPC_DB_SET_DATA1, hDB, hKey, data, buf_size, num_values, type);
   
#ifdef LOCAL_ROUTINES
   {
   DATABASE_HEADER *pheader;
   KEY *pkey;
   HNDLE hkeylink;
   int link_idx;
   char link_name[256];
   
   if (hDB > _database_entries || hDB <= 0) {
      cm_msg(MERROR, "db_set_data1", "invalid database handle");
      return DB_INVALID_HANDLE;
   }
   
   if (!_database[hDB - 1].attached) {
      cm_msg(MERROR, "db_set_data1", "invalid database handle");
      return DB_INVALID_HANDLE;
   }
   
   if (hKey < (int) sizeof(DATABASE_HEADER)) {
      cm_msg(MERROR, "db_set_data1", "invalid key handle");
      return DB_INVALID_HANDLE;
   }
   
   if (num_values == 0)
      return DB_INVALID_PARAM;
   
   db_lock_database(hDB);
   
   pheader = _database[hDB - 1].database_header;
   
   /* check if hKey argument is correct */
   if (!db_validate_hkey(pheader, hKey)) {
      db_unlock_database(hDB);
      return DB_INVALID_HANDLE;
   }
   
   pkey = (KEY *) ((char *) pheader + hKey);

   /* check for write access */
   if (!(pkey->access_mode & MODE_WRITE) || (pkey->access_mode & MODE_EXCLUSIVE)) {
      db_unlock_database(hDB);
      return DB_NO_ACCESS;
   }
   
   /* check for link to array index */
   if (pkey->type == TID_LINK) {
      strlcpy(link_name, (char *) pheader + pkey->data, sizeof(link_name));
      if (strlen(link_name) > 0 && link_name[strlen(link_name) - 1] == ']') {
         db_unlock_database(hDB);
         if (strchr(link_name, '[') == NULL)
            return DB_INVALID_LINK;
         link_idx = atoi(strchr(link_name, '[') + 1);
         *strchr(link_name, '[') = 0;
         if (db_find_key(hDB, 0, link_name, &hkeylink) != DB_SUCCESS)
            return DB_INVALID_LINK;
         return db_set_data_index1(hDB, hkeylink, data, buf_size, link_idx, type, FALSE);
      }
   }
   
   if (pkey->type != type) {
      int pkey_type = pkey->type;
      char pkey_name[NAME_LENGTH];
      strlcpy(pkey_name, pkey->name, sizeof(pkey_name));
      db_unlock_database(hDB);
      cm_msg(MERROR, "db_set_data1", "\"%s\" is of type %s, not %s", pkey_name, rpc_tid_name(pkey_type), rpc_tid_name(type));
      return DB_TYPE_MISMATCH;
   }
   
   /* keys cannot contain data */
   if (pkey->type == TID_KEY) {
      db_unlock_database(hDB);
      cm_msg(MERROR, "db_set_data1", "Key cannot contain data");
      return DB_TYPE_MISMATCH;
   }
   
   db_allow_write_locked(&_database[hDB - 1], "db_set_data1");

   /* if no buf_size given (Java!), calculate it */
   if (buf_size == 0)
      buf_size = pkey->item_size * num_values;
   
   /* resize data size if necessary */
   if (pkey->total_size != buf_size) {
      pkey->data = (POINTER_T) realloc_data(pheader, (char *) pheader + pkey->data, pkey->total_size, buf_size, "db_set_data1");
      
      if (pkey->data == 0) {
         pkey->total_size = 0;
         db_unlock_database(hDB);
         cm_msg(MERROR, "db_set_data1", "online database full");
         return DB_FULL;
      }
      
      pkey->data -= (POINTER_T) pheader;
      pkey->total_size = buf_size;
   }
   
   /* set number of values */
   pkey->num_values = num_values;
   if (num_values)
      pkey->item_size = buf_size / num_values;
   
   /* copy data */
   memcpy((char *) pheader + pkey->data, data, buf_size);
   
   /* update time */
   pkey->last_written = ss_time();
   
   db_unlock_database(hDB);
   
   }
#endif                          /* LOCAL_ROUTINES */
   
   return DB_SUCCESS;
}

/********************************************************************/
/**
Same as db_set_data, but it does not follow a link to an array index
@param hDB          ODB handle obtained via cm_get_experiment_database().
@param hKey Handle for key where search starts, zero for root.
@param data Buffer from which data gets copied to.
@param buf_size Size of data buffer.
@param num_values Number of data values (for arrays).
@param type Type of key, one of TID_xxx (see @ref Midas_Data_Types).
@return DB_SUCCESS, DB_INVALID_HANDLE, DB_TRUNCATED
*/
INT db_set_link_data(HNDLE hDB, HNDLE hKey, const void *data, INT buf_size, INT num_values, DWORD type)
{
   if (rpc_is_remote())
      return rpc_call(RPC_DB_SET_LINK_DATA, hDB, hKey, data, buf_size, num_values, type);

#ifdef LOCAL_ROUTINES
   {
      DATABASE_HEADER *pheader;
      KEY *pkey;

      if (hDB > _database_entries || hDB <= 0) {
         cm_msg(MERROR, "db_set_data", "invalid database handle");
         return DB_INVALID_HANDLE;
      }

      if (!_database[hDB - 1].attached) {
         cm_msg(MERROR, "db_set_data", "invalid database handle");
         return DB_INVALID_HANDLE;
      }

      if (hKey < (int) sizeof(DATABASE_HEADER)) {
         cm_msg(MERROR, "db_set_data", "invalid key handle");
         return DB_INVALID_HANDLE;
      }

      if (num_values == 0)
         return DB_INVALID_PARAM;

      db_lock_database(hDB);

      pheader = _database[hDB - 1].database_header;

      /* check if hKey argument is correct */
      if (!db_validate_hkey(pheader, hKey)) {
         db_unlock_database(hDB);
         return DB_INVALID_HANDLE;
      }

      pkey = (KEY *) ((char *) pheader + hKey);

      /* check for write access */
      if (!(pkey->access_mode & MODE_WRITE) || (pkey->access_mode & MODE_EXCLUSIVE)) {
         db_unlock_database(hDB);
         return DB_NO_ACCESS;
      }

      if (pkey->type != type) {
         int pkey_type = pkey->type;
         char pkey_name[NAME_LENGTH];
         strlcpy(pkey_name, pkey->name, sizeof(pkey_name));
         db_unlock_database(hDB);
         cm_msg(MERROR, "db_set_link_data", "\"%s\" is of type %s, not %s", pkey_name, rpc_tid_name(pkey_type), rpc_tid_name(type));
         return DB_TYPE_MISMATCH;
      }

      /* keys cannot contain data */
      if (pkey->type == TID_KEY) {
         db_unlock_database(hDB);
         cm_msg(MERROR, "db_set_link_data", "Key cannot contain data");
         return DB_TYPE_MISMATCH;
      }

      /* if no buf_size given (Java!), calculate it */
      if (buf_size == 0)
         buf_size = pkey->item_size * num_values;

      db_allow_write_locked(&_database[hDB - 1], "db_set_link_data");

      /* resize data size if necessary */
      if (pkey->total_size != buf_size) {
         pkey->data = (POINTER_T) realloc_data(pheader, (char *) pheader + pkey->data, pkey->total_size, buf_size, "db_set_link_data");

         if (pkey->data == 0) {
            pkey->total_size = 0;
            db_unlock_database(hDB);
            cm_msg(MERROR, "db_set_link_data", "online database full");
            return DB_FULL;
         }

         pkey->data -= (POINTER_T) pheader;
         pkey->total_size = buf_size;
      }

      /* set number of values */
      pkey->num_values = num_values;
      if (num_values)
         pkey->item_size = buf_size / num_values;

      /* copy data */
      memcpy((char *) pheader + pkey->data, data, buf_size);

      /* update time */
      pkey->last_written = ss_time();

      db_notify_clients(hDB, hKey, -1, TRUE);
      db_unlock_database(hDB);

   }
#endif                          /* LOCAL_ROUTINES */

   return DB_SUCCESS;
}

/**dox***************************************************************/
#ifndef DOXYGEN_SHOULD_SKIP_THIS

/*------------------------------------------------------------------*/
INT db_set_num_values(HNDLE hDB, HNDLE hKey, INT num_values)
/********************************************************************\

  Routine: db_set_num_values

  Purpose: Set numbe of values in a key. Extend with zeros or truncate.

  Input:
    HNDLE  hDB              Handle to the database
    HNDLE  hKey             Handle of key
    INT    num_values       Number of data values

  Output:
    none

  Function value:
    DB_SUCCESS              Successful completion
    DB_INVALID_HANDLE       Database handle is invalid

\********************************************************************/
{
   if (rpc_is_remote())
      return rpc_call(RPC_DB_SET_NUM_VALUES, hDB, hKey, num_values);

#ifdef LOCAL_ROUTINES
   {
      DATABASE_HEADER *pheader;
      KEY *pkey;
      INT new_size;

      if (hDB > _database_entries || hDB <= 0) {
         cm_msg(MERROR, "db_set_num_values", "invalid database handle");
         return DB_INVALID_HANDLE;
      }

      if (!_database[hDB - 1].attached) {
         cm_msg(MERROR, "db_set_num_values", "invalid database handle");
         return DB_INVALID_HANDLE;
      }

      if (hKey < (int) sizeof(DATABASE_HEADER)) {
         cm_msg(MERROR, "db_set_num_values", "invalid key handle");
         return DB_INVALID_HANDLE;
      }

      if (num_values <= 0) {
         cm_msg(MERROR, "db_set_num_values", "invalid num_values %d", num_values);
         return DB_INVALID_PARAM;
      }

      if (num_values == 0)
         return DB_INVALID_PARAM;

      db_lock_database(hDB);

      pheader = _database[hDB - 1].database_header;

      /* check if hKey argument is correct */
      if (!db_validate_hkey(pheader, hKey)) {
         db_unlock_database(hDB);
         return DB_INVALID_HANDLE;
      }

      pkey = (KEY *) ((char *) pheader + hKey);

      /* check for write access */
      if (!(pkey->access_mode & MODE_WRITE) || (pkey->access_mode & MODE_EXCLUSIVE)) {
         db_unlock_database(hDB);
         return DB_NO_ACCESS;
      }

      /* keys cannot contain data */
      if (pkey->type == TID_KEY) {
         db_unlock_database(hDB);
         cm_msg(MERROR, "db_set_num_values", "Key cannot contain data");
         return DB_TYPE_MISMATCH;
      }

      if (pkey->total_size != pkey->item_size * pkey->num_values) {
         db_unlock_database(hDB);
         cm_msg(MERROR, "db_set_num_values", "Corrupted key");
         return DB_CORRUPTED;
      }

      if (pkey->item_size == 0) {
         db_unlock_database(hDB);
         cm_msg(MERROR, "db_set_num_values", "Cannot resize array with item_size equal to zero");
         return DB_INVALID_PARAM;
      }

      db_allow_write_locked(&_database[hDB - 1], "db_set_num_values");

      /* resize data size if necessary */
      if (pkey->num_values != num_values) {
         new_size = pkey->item_size * num_values;

         pkey->data = (POINTER_T) realloc_data(pheader, (char *) pheader + pkey->data, pkey->total_size, new_size, "db_set_num_values");

         if (pkey->data == 0) {
            pkey->total_size = 0;
            pkey->num_values = 0;
            db_unlock_database(hDB);
            cm_msg(MERROR, "db_set_num_values", "hkey %d, num_values %d, new_size %d, online database full", hKey, num_values, new_size);
            return DB_FULL;
         }

         pkey->data -= (POINTER_T) pheader;
         pkey->total_size = new_size;
         pkey->num_values = num_values;
      }

      /* update time */
      pkey->last_written = ss_time();

      db_notify_clients(hDB, hKey, -1, TRUE);
      db_unlock_database(hDB);

   }
#endif                          /* LOCAL_ROUTINES */

   return DB_SUCCESS;
}

/**dox***************************************************************/
#endif                          /* DOXYGEN_SHOULD_SKIP_THIS */

/********************************************************************/
/**
Set key data for a key which contains an array of values.

This function sets individual values of a key containing an array.
If the index is larger than the array size, the array is extended and the intermediate
values are set to zero.
@param hDB          ODB handle obtained via cm_get_experiment_database().
@param hKey Handle for key where search starts, zero for root.
@param data Pointer to single value of data.
@param data_size
@param idx Size of single data element.
@param type Type of key, one of TID_xxx (see @ref Midas_Data_Types).
@return DB_SUCCESS, DB_INVALID_HANDLE, DB_NO_ACCESS, DB_TYPE_MISMATCH
*/
INT db_set_data_index(HNDLE hDB, HNDLE hKey, const void *data, INT data_size, INT idx, DWORD type)
{
   if (rpc_is_remote())
      return rpc_call(RPC_DB_SET_DATA_INDEX, hDB, hKey, data, data_size, idx, type);

#ifdef LOCAL_ROUTINES
   {
      DATABASE_HEADER *pheader;
      KEY *pkey;
      char link_name[256], str[256];
      int link_idx;
      HNDLE hkeylink;

      if (hDB > _database_entries || hDB <= 0) {
         cm_msg(MERROR, "db_set_data_index", "invalid database handle");
         return DB_INVALID_HANDLE;
      }

      if (!_database[hDB - 1].attached) {
         cm_msg(MERROR, "db_set_data_index", "invalid database handle");
         return DB_INVALID_HANDLE;
      }

      if (hKey < (int) sizeof(DATABASE_HEADER)) {
         cm_msg(MERROR, "db_set_data_index", "invalid key handle");
         return DB_INVALID_HANDLE;
      }

      db_lock_database(hDB);

      pheader = _database[hDB - 1].database_header;

      /* check if hKey argument is correct */
      if (!db_validate_hkey(pheader, hKey)) {
         db_unlock_database(hDB);
         return DB_INVALID_HANDLE;
      }

      pkey = (KEY *) ((char *) pheader + hKey);

      /* check for write access */
      if (!(pkey->access_mode & MODE_WRITE) || (pkey->access_mode & MODE_EXCLUSIVE)) {
         db_unlock_database(hDB);
         return DB_NO_ACCESS;
      }

      /* check for link to array index */
      if (pkey->type == TID_LINK) {
         strlcpy(link_name, (char *) pheader + pkey->data, sizeof(link_name));
         if (strlen(link_name) > 0 && link_name[strlen(link_name) - 1] == ']') {
            db_unlock_database(hDB);
            if (strchr(link_name, '[') == NULL)
               return DB_INVALID_LINK;
            link_idx = atoi(strchr(link_name, '[') + 1);
            *strchr(link_name, '[') = 0;
            if (db_find_key(hDB, 0, link_name, &hkeylink) != DB_SUCCESS)
               return DB_INVALID_LINK;
            return db_set_data_index(hDB, hkeylink, data, data_size, link_idx, type);
         }
      }

      if (pkey->type != type) {
         int pkey_type = pkey->type;
         db_unlock_database(hDB);
         db_get_path(hDB, hKey, str, sizeof(str));
         cm_msg(MERROR, "db_set_data_index", "\"%s\" is of type %s, not %s", str, rpc_tid_name(pkey_type), rpc_tid_name(type));
         return DB_TYPE_MISMATCH;
      }

      /* keys cannot contain data */
      if (pkey->type == TID_KEY) {
         db_unlock_database(hDB);
         cm_msg(MERROR, "db_set_data_index", "key cannot contain data");
         return DB_TYPE_MISMATCH;
      }

      /* check for valid idx */
      if (idx < 0) {
         db_unlock_database(hDB);
         cm_msg(MERROR, "db_set_data_index", "invalid index %d", idx);
         return DB_FULL;
      }

      /* check for valid array element size: if new element size
         is different from existing size, ODB becomes corrupted */
      if (pkey->item_size != 0 && data_size != pkey->item_size) {
         int pkey_item_size = pkey->item_size;
         db_unlock_database(hDB);
         cm_msg(MERROR, "db_set_data_index", "invalid element data size %d, expected %d", data_size, pkey_item_size);
         return DB_TYPE_MISMATCH;
      }

      db_allow_write_locked(&_database[hDB-1], "db_set_data_index");

      /* increase data size if necessary */
      if (idx >= pkey->num_values || pkey->item_size == 0) {
         pkey->data = (POINTER_T) realloc_data(pheader, (char *) pheader + pkey->data, pkey->total_size, data_size * (idx + 1), "db_set_data_index_A");

         if (pkey->data == 0) {
            pkey->total_size = 0;
            pkey->num_values = 0;
            db_unlock_database(hDB);
            cm_msg(MERROR, "db_set_data_index", "online database full");
            return DB_FULL;
         }

         pkey->data -= (POINTER_T) pheader;
         if (!pkey->item_size)
            pkey->item_size = data_size;
         pkey->total_size = data_size * (idx + 1);
         pkey->num_values = idx + 1;
      }

      /* cut strings which are too long */
      if ((type == TID_STRING || type == TID_LINK) && (int) strlen((char *) data) + 1 > pkey->item_size)
         *((char *) data + pkey->item_size - 1) = 0;

      /* copy data */
      memcpy((char *) pheader + pkey->data + idx * pkey->item_size, data, pkey->item_size);

#if 0
      /* ensure strings are NUL terminated */
      if ((type == TID_STRING || type == TID_LINK)) {
         int len = strlen(data);
         int end_of_string = idx * pkey->item_size + pkey->item_size - 1;
         printf("db_set_data_index: len %d, item_size %d, idx %d, end_of_string %d, char %d\n", len, pkey->item_size, idx, end_of_string, ((char*) pheader + pkey->data)[end_of_string]);
         ((char*) pheader + pkey->data)[end_of_string] = 0;
      }
#endif

      /* update time */
      pkey->last_written = ss_time();

      db_notify_clients(hDB, hKey, idx, TRUE);
      db_unlock_database(hDB);

   }
#endif                          /* LOCAL_ROUTINES */

   return DB_SUCCESS;
}

/********************************************************************/
/**
Same as db_set_data_index, but does not follow links.

@param hDB          ODB handle obtained via cm_get_experiment_database().
@param hKey Handle for key where search starts, zero for root.
@param data Pointer to single value of data.
@param data_size
@param idx Size of single data element.
@param type Type of key, one of TID_xxx (see @ref Midas_Data_Types).
@return DB_SUCCESS, DB_INVALID_HANDLE, DB_NO_ACCESS, DB_TYPE_MISMATCH
*/
INT db_set_link_data_index(HNDLE hDB, HNDLE hKey, const void *data, INT data_size, INT idx, DWORD type)
{
   if (rpc_is_remote())
      return rpc_call(RPC_DB_SET_LINK_DATA_INDEX, hDB, hKey, data, data_size, idx, type);

#ifdef LOCAL_ROUTINES
   {
      DATABASE_HEADER *pheader;
      KEY *pkey;
      char str[256];

      if (hDB > _database_entries || hDB <= 0) {
         cm_msg(MERROR, "db_set_link_data_index", "invalid database handle");
         return DB_INVALID_HANDLE;
      }

      if (!_database[hDB - 1].attached) {
         cm_msg(MERROR, "db_set_link_data_index", "invalid database handle");
         return DB_INVALID_HANDLE;
      }

      if (hKey < (int) sizeof(DATABASE_HEADER)) {
         cm_msg(MERROR, "db_set_link_data_index", "invalid key handle");
         return DB_INVALID_HANDLE;
      }

      db_lock_database(hDB);

      pheader = _database[hDB - 1].database_header;

      /* check if hKey argument is correct */
      if (!db_validate_hkey(pheader, hKey)) {
         db_unlock_database(hDB);
         return DB_INVALID_HANDLE;
      }

      pkey = (KEY *) ((char *) pheader + hKey);

      /* check for write access */
      if (!(pkey->access_mode & MODE_WRITE) || (pkey->access_mode & MODE_EXCLUSIVE)) {
         db_unlock_database(hDB);
         return DB_NO_ACCESS;
      }

      if (pkey->type != type) {
         int pkey_type = pkey->type;
         db_unlock_database(hDB);
         db_get_path(hDB, hKey, str, sizeof(str));
         cm_msg(MERROR, "db_set_link_data_index", "\"%s\" is of type %s, not %s", str, rpc_tid_name(pkey_type), rpc_tid_name(type));
         return DB_TYPE_MISMATCH;
      }

      /* keys cannot contain data */
      if (pkey->type == TID_KEY) {
         db_unlock_database(hDB);
         cm_msg(MERROR, "db_set_link_data_index", "key cannot contain data");
         return DB_TYPE_MISMATCH;
      }

      /* check for valid array element size: if new element size
         is different from existing size, ODB becomes corrupted */
      if (pkey->item_size != 0 && data_size != pkey->item_size) {
         int pkey_item_size = pkey->item_size;
         db_unlock_database(hDB);
         cm_msg(MERROR, "db_set_link_data_index", "invalid element data size %d, expected %d", data_size, pkey_item_size);
         return DB_TYPE_MISMATCH;
      }

      db_allow_write_locked(&_database[hDB - 1], "db_set_link_data_index");

      /* increase data size if necessary */
      if (idx >= pkey->num_values || pkey->item_size == 0) {
         pkey->data = (POINTER_T) realloc_data(pheader, (char *) pheader + pkey->data, pkey->total_size, data_size * (idx + 1), "db_set_data_index_B");

         if (pkey->data == 0) {
            pkey->total_size = 0;
            pkey->num_values = 0;
            db_unlock_database(hDB);
            cm_msg(MERROR, "db_set_link_data_index", "online database full");
            return DB_FULL;
         }

         pkey->data -= (POINTER_T) pheader;
         if (!pkey->item_size)
            pkey->item_size = data_size;
         pkey->total_size = data_size * (idx + 1);
         pkey->num_values = idx + 1;
      }

      /* cut strings which are too long */
      if ((type == TID_STRING || type == TID_LINK) && (int) strlen((char *) data) + 1 > pkey->item_size)
         *((char *) data + pkey->item_size - 1) = 0;

      /* copy data */
      memcpy((char *) pheader + pkey->data + idx * pkey->item_size, data, pkey->item_size);

      /* update time */
      pkey->last_written = ss_time();

      db_notify_clients(hDB, hKey, idx, TRUE);
      db_unlock_database(hDB);

   }
#endif                          /* LOCAL_ROUTINES */

   return DB_SUCCESS;
}

/**dox***************************************************************/
#ifndef DOXYGEN_SHOULD_SKIP_THIS

/*------------------------------------------------------------------*/
INT db_set_data_index1(HNDLE hDB, HNDLE hKey, const void *data, INT data_size, INT idx, DWORD type, BOOL bNotify)
/********************************************************************\

  Routine: db_set_data_index1

  Purpose: Set key data for a key which contains an array of values.
           Optionally notify clients which have key open.

  Input:
    HNDLE  hDB              Handle to the database
    HNDLE  hKey             Handle of key to enumerate
    void   *data            Pointer to single value of data
    INT    data_size        Size of single data element
    INT    idx              Index of array to change [0..n-1]
    DWORD  type             Type of data
    BOOL   bNotify          If TRUE, notify clients

  Output:
    none

  Function value:
    DB_SUCCESS              Successful completion
    DB_INVALID_HANDLE       Database handle is invalid
    DB_TYPE_MISMATCH        Key was created with different type
    DB_NO_ACCESS            No write access

\********************************************************************/
{
   if (rpc_is_remote())
      return rpc_call(RPC_DB_SET_DATA_INDEX1, hDB, hKey, data, data_size, idx, type, bNotify);

#ifdef LOCAL_ROUTINES
   {
      DATABASE_HEADER *pheader;
      KEY *pkey;

      if (hDB > _database_entries || hDB <= 0) {
         cm_msg(MERROR, "db_set_data_index1", "invalid database handle");
         return DB_INVALID_HANDLE;
      }

      if (!_database[hDB - 1].attached) {
         cm_msg(MERROR, "db_set_data_index1", "invalid database handle");
         return DB_INVALID_HANDLE;
      }

      if (hKey < (int) sizeof(DATABASE_HEADER)) {
         cm_msg(MERROR, "db_set_data_index1", "invalid key handle");
         return DB_INVALID_HANDLE;
      }

      db_lock_database(hDB);

      pheader = _database[hDB - 1].database_header;

      /* check if hKey argument is correct */
      if (!db_validate_hkey(pheader, hKey)) {
         db_unlock_database(hDB);
         return DB_INVALID_HANDLE;
      }

      pkey = (KEY *) ((char *) pheader + hKey);

      /* check for write access */
      if (!(pkey->access_mode & MODE_WRITE) || (pkey->access_mode & MODE_EXCLUSIVE)) {
         db_unlock_database(hDB);
         return DB_NO_ACCESS;
      }

      if (pkey->type != type) {
         int pkey_type = pkey->type;
         char pkey_name[NAME_LENGTH];
         strlcpy(pkey_name, pkey->name, sizeof(pkey_name));
         db_unlock_database(hDB);
         cm_msg(MERROR, "db_set_data_index1", "\"%s\" is of type %s, not %s", pkey_name, rpc_tid_name(pkey_type), rpc_tid_name(type));
         return DB_TYPE_MISMATCH;
      }

      /* keys cannot contain data */
      if (pkey->type == TID_KEY) {
         db_unlock_database(hDB);
         cm_msg(MERROR, "db_set_data_index1", "key cannot contain data");
         return DB_TYPE_MISMATCH;
      }

      /* check for valid index */
      if (idx < 0) {
         db_unlock_database(hDB);
         cm_msg(MERROR, "db_set_data_index1", "invalid index");
         return DB_FULL;
      }

      db_allow_write_locked(&_database[hDB - 1], "db_set_data_index1");

      /* increase key size if necessary */
      if (idx >= pkey->num_values) {
         pkey->data = (POINTER_T) realloc_data(pheader, (char *) pheader + pkey->data, pkey->total_size, data_size * (idx + 1), "db_set_data_index1");

         if (pkey->data == 0) {
            pkey->total_size = 0;
            pkey->num_values = 0;
            db_unlock_database(hDB);
            cm_msg(MERROR, "db_set_data_index1", "online database full");
            return DB_FULL;
         }

         pkey->data -= (POINTER_T) pheader;
         if (!pkey->item_size)
            pkey->item_size = data_size;
         pkey->total_size = data_size * (idx + 1);
         pkey->num_values = idx + 1;
      }

      /* cut strings which are too long */
      if ((type == TID_STRING || type == TID_LINK) && (int) strlen((char *) data) + 1 > pkey->item_size)
         *((char *) data + pkey->item_size - 1) = 0;

      /* copy data */
      memcpy((char *) pheader + pkey->data + idx * pkey->item_size, data, pkey->item_size);

      /* update time */
      pkey->last_written = ss_time();

      if (bNotify)
         db_notify_clients(hDB, hKey, idx, TRUE);

      db_unlock_database(hDB);
   }
#endif                          /* LOCAL_ROUTINES */

   return DB_SUCCESS;
}

/*----------------------------------------------------------------------------*/

INT db_merge_data(HNDLE hDB, HNDLE hKeyRoot, const char *name, void *data, INT data_size, INT num_values, INT type)
/********************************************************************\

  Routine: db_merge_data

  Purpose: Merge an array with an ODB array. If the ODB array doesn't
           exist, create it and fill it with the array. If it exists,
           load it in the array. Adjust ODB array size if necessary.

  Input:
    HNDLE  hDB              Handle to the database
    HNDLE  hKeyRoot         Key handle to start with, 0 for root
    cha    *name            Key name relative to hKeyRoot
    void   *data            Pointer to data array
    INT    data_size        Size of data array
    INT    num_values       Number of values in array
    DWORD  type             Type of data

  Output:
    none

  Function value:
    <same as db_set_data>

\********************************************************************/
{
   HNDLE hKey;
   INT status, old_size;

   if (num_values == 0)
      return DB_INVALID_PARAM;

   status = db_find_key(hDB, hKeyRoot, name, &hKey);
   if (status != DB_SUCCESS) {
      db_create_key(hDB, hKeyRoot, name, type);
      status = db_find_key(hDB, hKeyRoot, name, &hKey);
      if (status != DB_SUCCESS)
         return status;
      status = db_set_data(hDB, hKey, data, data_size, num_values, type);
   } else {
      old_size = data_size;
      db_get_data(hDB, hKey, data, &old_size, type);
      status = db_set_data(hDB, hKey, data, data_size, num_values, type);
   }

   return status;
}

/*------------------------------------------------------------------*/
INT db_set_mode(HNDLE hDB, HNDLE hKey, WORD mode, BOOL recurse)
/********************************************************************\

  Routine: db_set_mode

  Purpose: Set access mode of key

  Input:
    HNDLE  hDB              Handle to the database
    HNDLE  hKey             Key handle
    DWORD  mode             Access mode, any or'ed combination of
                            MODE_READ, MODE_WRITE, MODE_EXCLUSIVE
                            and MODE_DELETE
    BOOL   recurse          Value of 0 (FALSE): do not recurse subtree,
                            value of 1 (TRUE): recurse subtree,
                            value of 2: recurse subtree, assume database is locked by caller.

  Output:
    none

  Function value:
    DB_SUCCESS              Successful completion
    DB_INVALID_HANDLE       Database handle is invalid

\********************************************************************/
{
   if (rpc_is_remote())
      return rpc_call(RPC_DB_SET_MODE, hDB, hKey, mode, recurse);

#ifdef LOCAL_ROUTINES
   {
      DATABASE_HEADER *pheader;
      KEYLIST *pkeylist;
      KEY *pkey, *pnext_key;
      HNDLE hKeyLink;
      BOOL locked = FALSE;

      if (hDB > _database_entries || hDB <= 0) {
         cm_msg(MERROR, "db_set_mode", "invalid database handle");
         return DB_INVALID_HANDLE;
      }

      if (!_database[hDB - 1].attached) {
         cm_msg(MERROR, "db_set_mode", "invalid database handle");
         return DB_INVALID_HANDLE;
      }

      if (recurse < 2) {
         db_lock_database(hDB);
         locked = TRUE;
      }

      pheader = _database[hDB - 1].database_header;
      if (!hKey)
         hKey = pheader->root_key;

      /* check if hKey argument is correct */
      if (!db_validate_hkey(pheader, hKey)) {
         if (locked)
            db_unlock_database(hDB);
         return DB_INVALID_HANDLE;
      }

      pkey = (KEY *) ((char *) pheader + hKey);

      db_allow_write_locked(&_database[hDB-1], "db_set_mode");

      pkeylist = (KEYLIST *) ((char *) pheader + pkey->data);

      if (pkey->type == TID_KEY && pkeylist->first_key && recurse) {
         /* first recurse subtree */
         // FIXME: validate pkeylist->first_key
         pkey = (KEY *) ((char *) pheader + pkeylist->first_key);

         do {
            pnext_key = (KEY *) (POINTER_T) pkey->next_key; // FIXME: what is this pointer cast?

            db_set_mode(hDB, (POINTER_T) pkey - (POINTER_T) pheader, mode, recurse + 1);

            if (pnext_key) {
               // FIXME: validate pnext_key
               pkey = (KEY *) ((char *) pheader + (POINTER_T) pnext_key);
            }
         } while (pnext_key);
      }

      pkey = (KEY *) ((char *) pheader + hKey); // NB: hKey is already validated

      /* resolve links */
      if (pkey->type == TID_LINK) {
         if (*((char *) pheader + pkey->data) == '/')
            db_find_key1(hDB, 0, (char *) pheader + pkey->data, &hKeyLink);
         else
            db_find_key1(hDB, hKey, (char *) pheader + pkey->data, &hKeyLink);
         if (hKeyLink)
            db_set_mode(hDB, hKeyLink, mode, recurse > 0);
         pheader = _database[hDB - 1].database_header;
         pkey = (KEY *) ((char *) pheader + hKey); // NB: hKey is already validated
      }

      /* now set mode */
      pkey->access_mode = mode;

      if (locked)
         db_unlock_database(hDB);
   }
#endif                          /* LOCAL_ROUTINES */

   return DB_SUCCESS;
}

/**dox***************************************************************/
#endif                          /* DOXYGEN_SHOULD_SKIP_THIS */

/********************************************************************/
/**
Load a branch of a database from an .ODB file.

This function is used by the ODBEdit command load. For a
description of the ASCII format, see db_copy(). Data can be loaded relative to
the root of the ODB (hkey equal zero) or relative to a certain key.
@param hDB          ODB handle obtained via cm_get_experiment_database().
@param hKeyRoot Handle for key where search starts, zero for root.
@param filename Filename of .ODB file.
@param bRemote If TRUE, the file is loaded by the server process on the
back-end, if FALSE, it is loaded from the current process
@return DB_SUCCESS, DB_INVALID_HANDLE, DB_FILE_ERROR
*/
INT db_load(HNDLE hDB, HNDLE hKeyRoot, const char *filename, BOOL bRemote)
{
   struct stat stat_buf;
   INT hfile, size, n, i, status;
   char *buffer;

   if (rpc_is_remote() && bRemote)
      return rpc_call(RPC_DB_LOAD, hDB, hKeyRoot, filename);

   /* open file */
   hfile = open(filename, O_RDONLY | O_TEXT, 0644);
   if (hfile == -1) {
      cm_msg(MERROR, "db_load", "file \"%s\" not found", filename);
      return DB_FILE_ERROR;
   }

   /* allocate buffer with file size */
   fstat(hfile, &stat_buf);
   size = stat_buf.st_size;
   buffer = (char *) malloc(size + 1);

   if (buffer == NULL) {
      cm_msg(MERROR, "db_load", "cannot allocate ODB load buffer");
      close(hfile);
      return DB_NO_MEMORY;
   }

   n = 0;

   do {
      i = read(hfile, buffer + n, size - n);
      if (i <= 0)
         break;
      n += i;
   } while (TRUE);

   buffer[n] = 0;

   if (strncmp(buffer, "<?xml version=\"1.0\"", 19) == 0) {
      status = db_paste_xml(hDB, hKeyRoot, buffer);
      if (status != DB_SUCCESS)
         printf("Error in file \"%s\"\n", filename);
   } else
      status = db_paste(hDB, hKeyRoot, buffer);

   close(hfile);
   free(buffer);

   return status;
}

/********************************************************************/
/**
Copy an ODB subtree in ASCII format to a buffer

This function converts the binary ODB contents to an ASCII.
The function db_paste() can be used to convert the ASCII representation back
to binary ODB contents. The functions db_load() and db_save() internally
use db_copy() and db_paste(). This function converts the binary ODB
contents to an ASCII representation of the form:
- For single value:
\code
[ODB path]
 key name = type : value
\endcode
- For strings:
\code
key name = STRING : [size] string contents
\endcode
- For arrayes (type can be BYTE, SBYTE, CHAR, WORD, SHORT, DWORD,
INT, BOOL, FLOAT, DOUBLE, STRING or LINK):
\code
key name = type[size] :
 [0] value0
 [1] value1
 [2] value2
 ...
\endcode
@param hDB          ODB handle obtained via cm_get_experiment_database().
@param hKey Handle for key where search starts, zero for root.
@param buffer ASCII buffer which receives ODB contents.
@param buffer_size Size of buffer, returns remaining space in buffer.
@param path Internal use only, must be empty ("").
@return DB_SUCCESS, DB_TRUNCATED, DB_NO_MEMORY
*/
INT db_copy(HNDLE hDB, HNDLE hKey, char *buffer, INT * buffer_size, const char *path)
{
   INT i, j, size, status;
   KEY key;
   HNDLE hSubkey;
   char full_path[MAX_ODB_PATH];
   char *data;
   char line[MAX_STRING_LENGTH * 2];
   BOOL bWritten;

   strlcpy(full_path, path, sizeof(full_path));

   bWritten = FALSE;

   /* first enumerate this level */
   for (i = 0;; i++) {
      db_enum_link(hDB, hKey, i, &hSubkey);

      if (i == 0 && !hSubkey) {
         /* If key has no subkeys, just write this key */
         status = db_get_link(hDB, hKey, &key);
         if (status != DB_SUCCESS)
            continue;
         size = key.total_size;
         data = (char *) malloc(size);
         if (data == NULL) {
            cm_msg(MERROR, "db_copy", "cannot allocate data buffer");
            return DB_NO_MEMORY;
         }
         line[0] = 0;

         if (key.type != TID_KEY) {
            status = db_get_link_data(hDB, hKey, data, &size, key.type);
            if (status != DB_SUCCESS)
               continue;
            if (key.num_values == 1) {
               sprintf(line, "%s = %s : ", key.name, rpc_tid_name(key.type));

               if (key.type == TID_STRING && strchr(data, '\n') != NULL) {
                  /* multiline string */
                  sprintf(line + strlen(line), "[====#$@$#====]\n");

                  /* copy line to buffer */
                  if ((INT) (strlen(line) + 1) > *buffer_size) {
                     free(data);
                     return DB_TRUNCATED;
                  }

                  strcpy(buffer, line);
                  buffer += strlen(line);
                  *buffer_size -= strlen(line);

                  /* copy multiple lines to buffer */
                  if (key.item_size > *buffer_size) {
                     free(data);
                     return DB_TRUNCATED;
                  }

                  strcpy(buffer, data);
                  buffer += strlen(data);
                  *buffer_size -= strlen(data);

                  strcpy(line, "\n====#$@$#====\n");
               } else {
                  char str[MAX_STRING_LENGTH]; // buffer for db_sprintf()
                  db_sprintf(str, data, key.item_size, 0, key.type);

                  if (key.type == TID_STRING || key.type == TID_LINK)
                     sprintf(line + strlen(line), "[%d] ", key.item_size);

                  sprintf(line + strlen(line), "%s\n", str);
               }
            } else {
               char str[MAX_STRING_LENGTH]; // buffer for db_sprintf()
               sprintf(line, "%s = %s[%d] :\n", key.name, rpc_tid_name(key.type), key.num_values);

               for (j = 0; j < key.num_values; j++) {
                  if (key.type == TID_STRING || key.type == TID_LINK)
                     sprintf(line + strlen(line), "[%d] ", key.item_size);
                  else
                     sprintf(line + strlen(line), "[%d] ", j);

                  db_sprintf(str, data, key.item_size, j, key.type);
                  sprintf(line + strlen(line), "%s\n", str);

                  /* copy line to buffer */
                  if ((INT) (strlen(line) + 1) > *buffer_size) {
                     free(data);
                     return DB_TRUNCATED;
                  }

                  strcpy(buffer, line);
                  buffer += strlen(line);
                  *buffer_size -= strlen(line);
                  line[0] = 0;
               }
            }
         }

         /* copy line to buffer */
         if ((INT) (strlen(line) + 1) > *buffer_size) {
            free(data);
            return DB_TRUNCATED;
         }

         strcpy(buffer, line);
         buffer += strlen(line);
         *buffer_size -= strlen(line);

         free(data);
         data = NULL;
      }

      if (!hSubkey)
         break;

      status = db_get_link(hDB, hSubkey, &key);
      if (status != DB_SUCCESS)
         continue;

      if (strcmp(key.name, "arr2") == 0)
         printf("\narr2\n");
      size = key.total_size;
      data = (char *) malloc(size);
      if (data == NULL) {
         cm_msg(MERROR, "db_copy", "cannot allocate data buffer");
         return DB_NO_MEMORY;
      }

      line[0] = 0;

      if (key.type == TID_KEY) {
         char str[MAX_ODB_PATH];

         /* new line */
         if (bWritten) {
            if (*buffer_size < 2) {
               free(data);
               return DB_TRUNCATED;
            }

            strcpy(buffer, "\n");
            buffer += 1;
            *buffer_size -= 1;
         }

         strcpy(str, full_path);
         if (str[0] && str[strlen(str) - 1] != '/')
            strcat(str, "/");
         strcat(str, key.name);

         /* recurse */
         status = db_copy(hDB, hSubkey, buffer, buffer_size, str);
         if (status != DB_SUCCESS) {
            free(data);
            return status;
         }

         buffer += strlen(buffer);
         bWritten = FALSE;
      } else {
         status = db_get_link_data(hDB, hSubkey, data, &size, key.type);
         if (status != DB_SUCCESS)
            continue;

         if (!bWritten) {
            if (path[0] == 0)
               sprintf(line, "[.]\n");
            else
               sprintf(line, "[%s]\n", path);
            bWritten = TRUE;
         }

         if (key.num_values == 1) {
            sprintf(line + strlen(line), "%s = %s : ", key.name, rpc_tid_name(key.type));

            if (key.type == TID_STRING && strchr(data, '\n') != NULL) {
               /* multiline string */
               sprintf(line + strlen(line), "[====#$@$#====]\n");

               /* ensure string limiter */
               data[size - 1] = 0;

               /* copy line to buffer */
               if ((INT) (strlen(line) + 1) > *buffer_size) {
                  free(data);
                  return DB_TRUNCATED;
               }

               strcpy(buffer, line);
               buffer += strlen(line);
               *buffer_size -= strlen(line);

               /* copy multiple lines to buffer */
               if (key.item_size > *buffer_size) {
                  free(data);
                  return DB_TRUNCATED;
               }

               strcpy(buffer, data);
               buffer += strlen(data);
               *buffer_size -= strlen(data);

               strcpy(line, "\n====#$@$#====\n");
            } else {
               char str[MAX_STRING_LENGTH]; // buffer for db_sprintf()
               
               db_sprintf(str, data, key.item_size, 0, key.type);

               if (key.type == TID_STRING || key.type == TID_LINK)
                  sprintf(line + strlen(line), "[%d] ", key.item_size);

               sprintf(line + strlen(line), "%s\n", str);
            }
         } else {
            sprintf(line + strlen(line), "%s = %s[%d] :\n", key.name, rpc_tid_name(key.type), key.num_values);

            for (j = 0; j < key.num_values; j++) {
               char str[MAX_STRING_LENGTH]; // buffer for db_sprintf()

               if (key.type == TID_STRING || key.type == TID_LINK)
                  sprintf(line + strlen(line), "[%d] ", key.item_size);
               else
                  sprintf(line + strlen(line), "[%d] ", j);

               db_sprintf(str, data, key.item_size, j, key.type);
               sprintf(line + strlen(line), "%s\n", str);

               /* copy line to buffer */
               if ((INT) (strlen(line) + 1) > *buffer_size) {
                  free(data);
                  return DB_TRUNCATED;
               }

               strcpy(buffer, line);
               buffer += strlen(line);
               *buffer_size -= strlen(line);
               line[0] = 0;
            }
         }

         /* copy line to buffer */
         if ((INT) (strlen(line) + 1) > *buffer_size) {
            free(data);
            return DB_TRUNCATED;
         }

         strcpy(buffer, line);
         buffer += strlen(line);
         *buffer_size -= strlen(line);
      }

      free(data);
      data = NULL;
   }

   if (bWritten) {
      if (*buffer_size < 2)
         return DB_TRUNCATED;

      strcpy(buffer, "\n");
      buffer += 1;
      *buffer_size -= 1;
   }

   return DB_SUCCESS;
}

/********************************************************************/
/**
Copy an ODB subtree in ASCII format from a buffer
@param hDB          ODB handle obtained via cm_get_experiment_database().
@param hKeyRoot Handle for key where search starts, zero for root.
@param buffer NULL-terminated buffer
@return DB_SUCCESS, DB_TRUNCATED, DB_NO_MEMORY
*/
INT db_paste(HNDLE hDB, HNDLE hKeyRoot, const char *buffer)
{
   char title[MAX_STRING_LENGTH]; // FIXME: no overflow, not sure if it should be MAX_ODB_PATH or longer. K.O.
   char *data;
   const char *pold;
   INT data_size, index;
   INT tid, i, j, n_data, string_length, status, size;
   HNDLE hKey;
   KEY root_key;

   title[0] = 0;

   if (hKeyRoot == 0)
      db_find_key(hDB, hKeyRoot, "", &hKeyRoot);

   db_get_key(hDB, hKeyRoot, &root_key);

   /* initial data size */
   data_size = 1000;
   data = (char *) malloc(data_size);
   if (data == NULL) {
      cm_msg(MERROR, "db_paste", "cannot allocate data buffer");
      return DB_NO_MEMORY;
   }

   do {
      char line[10*MAX_STRING_LENGTH];

      if (*buffer == 0)
         break;

      for (i = 0; *buffer != '\n' && *buffer && i < 10*MAX_STRING_LENGTH; i++)
         line[i] = *buffer++;

      if (i == 10*MAX_STRING_LENGTH) {
         line[10*MAX_STRING_LENGTH-1] = 0;
         cm_msg(MERROR, "db_paste", "line too long: %s...", line);
         free(data);
         return DB_TRUNCATED;
      }

      line[i] = 0;
      if (*buffer == '\n')
         buffer++;

      /* check if it is a section title */
      if (line[0] == '[') {
         /* extract title and append '/' */
         strlcpy(title, line + 1, sizeof(title));
         if (strchr(title, ']'))
            *strchr(title, ']') = 0;
         if (title[0] && title[strlen(title) - 1] != '/')
            strlcat(title, "/", sizeof(title));
      } else {
         /* valid data line if it includes '=' and no ';' */
         if (strchr(line, '=') && line[0] != ';') {
            char key_name[MAX_ODB_PATH];
            char test_str[MAX_ODB_PATH];
            char data_str[MAX_STRING_LENGTH + 50]; // FIXME: not sure if this should be max line length. K.O.

            /* copy type info and data */
            char* pline = strrchr(line, '=') + 1;
            while (strstr(line, ": [") != NULL && strstr(line, ": [") < pline) {
               pline -= 2;
               while (*pline != '=' && pline > line)
                  pline--;
               pline++;
            }
            while (*pline == ' ')
               pline++;
            strlcpy(data_str, pline, sizeof(data_str));

            /* extract key name */
            *strrchr(line, '=') = 0;
            while (strstr(line, ": [") && strchr(line, '='))
               *strrchr(line, '=') = 0;

            pline = &line[strlen(line) - 1];
            while (*pline == ' ')
               *pline-- = 0;

            key_name[0] = 0;
            if (title[0] != '.')
               strlcpy(key_name, title, sizeof(key_name));

            strlcat(key_name, line, sizeof(key_name));

            /* evaluate type info */
            strlcpy(line, data_str, sizeof(line));
            if (strchr(line, ' '))
               *strchr(line, ' ') = 0;

            n_data = 1;
            if (strchr(line, '[')) {
               n_data = atol(strchr(line, '[') + 1);
               *strchr(line, '[') = 0;
            }

            for (tid = 0; tid < TID_LAST; tid++)
               if (strcmp(rpc_tid_name(tid), line) == 0)
                  break;

            string_length = 0;

            if (tid == TID_LAST)
               cm_msg(MERROR, "db_paste", "found unknown data type \"%s\" in ODB file", line);
            else {
               /* skip type info */
               char* pc = data_str;
               while (*pc != ' ' && *pc)
                  pc++;
               while ((*pc == ' ' || *pc == ':') && *pc)
                  pc++;

               //strlcpy(data_str, pc, sizeof(data_str)); // MacOS 10.9 does not permit strlcpy() of overlapping strings
               assert(strlen(pc) < sizeof(data_str)); // "pc" points at a substring inside "data_str"
               memmove(data_str, pc, strlen(pc)+1);

               if (n_data > 1) {
                  data_str[0] = 0;
                  if (!*buffer)
                     break;

                  for (j = 0; *buffer != '\n' && *buffer; j++)
                     data_str[j] = *buffer++;
                  data_str[j] = 0;
                  if (*buffer == '\n')
                     buffer++;
               }

               for (i = 0; i < n_data; i++) {
                  /* strip trailing \n */
                  char* pc = &data_str[strlen(data_str) - 1];
                  while (*pc == '\n' || *pc == '\r')
                     *pc-- = 0;

                  if (tid == TID_STRING || tid == TID_LINK) {
                     if (!string_length) {
                        if (data_str[1] == '=')
                           string_length = -1;
                        else
                           string_length = atoi(data_str + 1);
                        if (string_length > MAX_STRING_LENGTH) {
                           string_length = MAX_STRING_LENGTH;
                           cm_msg(MERROR, "db_paste", "found string exceeding MAX_STRING_LENGTH, odb path \"%s\"", key_name);
                        }
                        if (string_length == 0) {
                           string_length = 32;
                           cm_msg(MERROR, "db_paste", "found string length of zero, set to 32, odb path \"%s\"", key_name);
                        }
                     }

                     if (string_length == -1) {
                        /* multi-line string */
                        if (strstr(buffer, "\n====#$@$#====\n") != NULL) {
                           string_length = (POINTER_T) strstr(buffer, "\n====#$@$#====\n") - (POINTER_T) buffer + 1;

                           if (string_length >= data_size) {
                              data_size += string_length + 100;
                              data = (char *) realloc(data, data_size);
                              if (data == NULL) {
                                 cm_msg(MERROR, "db_paste", "cannot allocate data buffer");
                                 return DB_NO_MEMORY;
                              }
                           }

                           memset(data, 0, data_size);
                           strncpy(data, buffer, string_length);
                           data[string_length - 1] = 0;
                           buffer = strstr(buffer, "\n====#$@$#====\n") + strlen("\n====#$@$#====\n");
                        } else
                           cm_msg(MERROR, "db_paste", "found multi-line string without termination sequence");
                     } else {
                        char* pc = data_str + 2;
                        while (*pc && *pc != ' ')
                           pc++;
                        while (*pc && *pc == ' ')
                           pc++;

                        /* limit string size */
                        *(pc + string_length - 1) = 0;

                        /* increase data buffer if necessary */
                        if (string_length * (i + 1) >= data_size) {
                           data_size += 1000;
                           data = (char *) realloc(data, data_size);
                           if (data == NULL) {
                              cm_msg(MERROR, "db_paste", "cannot allocate data buffer");
                              return DB_NO_MEMORY;
                           }
                        }

                        strlcpy(data + string_length * i, pc, string_length);
                     }
                  } else {
                     char* pc = data_str;

                     if (n_data > 1 && data_str[0] == '[') {
                        index = atoi(data_str+1);
                        pc = strchr(data_str, ']') + 1;
                        while (*pc && *pc == ' ')
                           pc++;
                     } else
                        index = 0;

                     /* increase data buffer if necessary */
                     if (rpc_tid_size(tid) * (index + 1) >= data_size) {
                        data_size += 1000;
                        data = (char *) realloc(data, data_size);
                        if (data == NULL) {
                           cm_msg(MERROR, "db_paste", "cannot allocate data buffer");
                           return DB_NO_MEMORY;
                        }
                     }

                     db_sscanf(pc, data, &size, index, tid);
                  }

                  if (i < n_data - 1) {
                     data_str[0] = 0;
                     if (!*buffer)
                        break;

                     pold = buffer;

                     for (j = 0; *buffer != '\n' && *buffer; j++)
                        data_str[j] = *buffer++;
                     data_str[j] = 0;
                     if (*buffer == '\n')
                        buffer++;

                     /* test if valid data */
                     if (tid != TID_STRING && tid != TID_LINK) {
                        if (data_str[0] == 0 || (strchr(data_str, '=')
                                                 && strchr(data_str, ':')))
                           buffer = pold;
                     }
                  }
               }

               /* skip system client entries */
               strlcpy(test_str, key_name, sizeof(test_str));
               test_str[15] = 0;

               if (!equal_ustring(test_str, "/System/Clients")) {
                  if (root_key.type != TID_KEY) {
                     /* root key is destination key */
                     hKey = hKeyRoot;
                  } else {
                     /* create key and set value */
                     if (key_name[0] == '/') {
                        status = db_find_link(hDB, 0, key_name, &hKey);
                        if (status == DB_NO_KEY) {
                           db_create_key(hDB, 0, key_name, tid);
                           status = db_find_link(hDB, 0, key_name, &hKey);
                        }
                     } else {
                        status = db_find_link(hDB, hKeyRoot, key_name, &hKey);
                        if (status == DB_NO_KEY) {
                           db_create_key(hDB, hKeyRoot, key_name, tid);
                           status = db_find_link(hDB, hKeyRoot, key_name, &hKey);
                        }
                     }
                  }

                  /* set key data if created sucessfully */
                  if (hKey) {
                     if (tid == TID_STRING || tid == TID_LINK)
                        db_set_link_data(hDB, hKey, data, string_length * n_data, n_data, tid);
                     else
                        db_set_link_data(hDB, hKey, data, rpc_tid_size(tid) * n_data, n_data, tid);
                  }
               }
            }
         }
      }
   } while (TRUE);

   free(data);
   return DB_SUCCESS;
}

/********************************************************************/
/*
  Only internally used by db_paste_xml
*/
int db_paste_node(HNDLE hDB, HNDLE hKeyRoot, PMXML_NODE node)
{
   char type[256], data[256], test_str[256];
   char *buf = NULL;
   int i, idx, status, size=0, tid, num_values;
   HNDLE hKey;
   PMXML_NODE child;

   if (strcmp(mxml_get_name(node), "odb") == 0) {
      for (i = 0; i < mxml_get_number_of_children(node); i++) {
         status = db_paste_node(hDB, hKeyRoot, mxml_subnode(node, i));
         if (status != DB_SUCCESS)
            return status;
      }
   } else if (strcmp(mxml_get_name(node), "dir") == 0) {
      status = db_find_link(hDB, hKeyRoot, mxml_get_attribute(node, "name"), &hKey);

      /* skip system client entries */
      strlcpy(test_str, mxml_get_attribute(node, "name"), sizeof(test_str));
      test_str[15] = 0;
      if (equal_ustring(test_str, "/System/Clients"))
         return DB_SUCCESS;

      if (status == DB_NO_KEY) {
         status = db_create_key(hDB, hKeyRoot, mxml_get_attribute(node, "name"), TID_KEY);
         if (status == DB_NO_ACCESS) {
            cm_msg(MINFO, "db_paste_node", "cannot load key \"%s\": write protected", mxml_get_attribute(node, "name"));
            return DB_SUCCESS;  /* key or tree is locked, just skip it */
         }

         if (status != DB_SUCCESS && status != DB_KEY_EXIST) {
            cm_msg(MERROR, "db_paste_node", "cannot create key \"%s\" in ODB, status = %d", mxml_get_attribute(node, "name"), status);
            return status;
         }
         status = db_find_link(hDB, hKeyRoot, mxml_get_attribute(node, "name"), &hKey);
         if (status != DB_SUCCESS) {
            cm_msg(MERROR, "db_paste_node", "cannot find key \"%s\" in ODB", mxml_get_attribute(node, "name"));
            return status;
         }
      }

      db_get_path(hDB, hKey, data, sizeof(data));
      if (strncmp(data, "/System/Clients", 15) != 0) {
         for (i = 0; i < mxml_get_number_of_children(node); i++) {
            status = db_paste_node(hDB, hKey, mxml_subnode(node, i));
            if (status != DB_SUCCESS)
               return status;
         }
      }
   } else if (strcmp(mxml_get_name(node), "key") == 0 || strcmp(mxml_get_name(node), "keyarray") == 0) {

      if (strcmp(mxml_get_name(node), "keyarray") == 0)
         num_values = atoi(mxml_get_attribute(node, "num_values"));
      else
         num_values = 0;

      if (mxml_get_attribute(node, "type") == NULL) {
         cm_msg(MERROR, "db_paste_node", "found key \"%s\" with no type in XML data", mxml_get_name(node));
         return DB_TYPE_MISMATCH;
      }

      strlcpy(type, mxml_get_attribute(node, "type"), sizeof(type));
      for (tid = 0; tid < TID_LAST; tid++)
         if (strcmp(rpc_tid_name(tid), type) == 0)
            break;
      if (tid == TID_LAST) {
         cm_msg(MERROR, "db_paste_node", "found unknown data type \"%s\" in XML data", type);
         return DB_TYPE_MISMATCH;
      }

      status = db_find_link(hDB, hKeyRoot, mxml_get_attribute(node, "name"), &hKey);
      if (status == DB_NO_KEY) {
         status = db_create_key(hDB, hKeyRoot, mxml_get_attribute(node, "name"), tid);
         if (status == DB_NO_ACCESS) {
            cm_msg(MINFO, "db_paste_node", "cannot load key \"%s\": write protected", mxml_get_attribute(node, "name"));
            return DB_SUCCESS;  /* key or tree is locked, just skip it */
         }

         if (status != DB_SUCCESS) {
            cm_msg(MERROR, "db_paste_node", "cannot create key \"%s\" in ODB, status = %d", mxml_get_attribute(node, "name"), status);
            return status;
         }
         status = db_find_link(hDB, hKeyRoot, mxml_get_attribute(node, "name"), &hKey);
         if (status != DB_SUCCESS) {
            cm_msg(MERROR, "db_paste_node", "cannot find key \"%s\" in ODB, status = %d", mxml_get_attribute(node, "name"), status);
            return status;
         }
      }

      if (tid == TID_STRING || tid == TID_LINK) {
         size = atoi(mxml_get_attribute(node, "size"));
         buf = (char *)malloc(size);
         assert(buf);
         buf[0] = 0;
      }

      if (num_values) {
         /* evaluate array */
         for (i = 0; i < mxml_get_number_of_children(node); i++) {
            child = mxml_subnode(node, i);
            if (mxml_get_attribute(child, "index"))
               idx = atoi(mxml_get_attribute(child, "index"));
            else
               idx = i;
            if (tid == TID_STRING || tid == TID_LINK) {
               if (mxml_get_value(child) == NULL) {
                  status = db_set_data_index(hDB, hKey, "", size, i, tid);
                  if (status == DB_NO_ACCESS) {
                     cm_msg(MINFO, "db_paste_node", "cannot load string or link \"%s\": write protected", mxml_get_attribute(node, "name"));
                     return DB_SUCCESS;  /* key or tree is locked, just skip it */
                  } else if (status != DB_SUCCESS) {
                     cm_msg(MERROR, "db_paste_node", "cannot load string or link \"%s\": db_set_data_index() status %d", mxml_get_attribute(node, "name"), status);
                     return status;
                  }
               } else {
                  strlcpy(buf, mxml_get_value(child), size);
                  status = db_set_data_index(hDB, hKey, buf, size, idx, tid);
                  if (status == DB_NO_ACCESS) {
                     cm_msg(MINFO, "db_paste_node", "cannot load array element \"%s\": write protected", mxml_get_attribute(node, "name"));
                     return DB_SUCCESS;  /* key or tree is locked, just skip it */
                  } else if (status != DB_SUCCESS) {
                     cm_msg(MERROR, "db_paste_node", "cannot load array element \"%s\": db_set_data_index() status %d", mxml_get_attribute(node, "name"), status);
                     return status;
                  }
               }
            } else {
               db_sscanf(mxml_get_value(child), data, &size, 0, tid);
               status = db_set_data_index(hDB, hKey, data, rpc_tid_size(tid), idx, tid);
               if (status == DB_NO_ACCESS) {
                  cm_msg(MINFO, "db_paste_node", "cannot load array element \"%s\": write protected", mxml_get_attribute(node, "name"));
                  return DB_SUCCESS;  /* key or tree is locked, just skip it */
               } else if (status != DB_SUCCESS) {
                  cm_msg(MERROR, "db_paste_node", "cannot load array element \"%s\": db_set_data_index() status %d", mxml_get_attribute(node, "name"), status);
                  return status;
               }
            }
         }

      } else {                  /* single value */
         if (tid == TID_STRING || tid == TID_LINK) {
            size = atoi(mxml_get_attribute(node, "size"));
            if (mxml_get_value(node) == NULL) {
               status = db_set_data(hDB, hKey, "", size, 1, tid);
               if (status == DB_NO_ACCESS) {
                  cm_msg(MINFO, "db_paste_node", "cannot load string or link \"%s\": write protected", mxml_get_attribute(node, "name"));
                  return DB_SUCCESS;  /* key or tree is locked, just skip it */
               } else if (status != DB_SUCCESS) {
                  cm_msg(MERROR, "db_paste_node", "cannot load string or link \"%s\": db_set_data() status %d", mxml_get_attribute(node, "name"), status);
                  return status;
               }
            } else {
               strlcpy(buf, mxml_get_value(node), size);
               status = db_set_data(hDB, hKey, buf, size, 1, tid);
               if (status == DB_NO_ACCESS) {
                  cm_msg(MINFO, "db_paste_node", "cannot load value \"%s\": write protected", mxml_get_attribute(node, "name"));
                  return DB_SUCCESS;  /* key or tree is locked, just skip it */
               } else if (status != DB_SUCCESS) {
                  cm_msg(MERROR, "db_paste_node", "cannot load value \"%s\": db_set_data() status %d", mxml_get_attribute(node, "name"), status);
                  return status;
               }
            }
         } else {
            db_sscanf(mxml_get_value(node), data, &size, 0, tid);
            status = db_set_data(hDB, hKey, data, rpc_tid_size(tid), 1, tid);
            if (status == DB_NO_ACCESS) {
               cm_msg(MINFO, "db_paste_node", "cannot load value \"%s\": write protected", mxml_get_attribute(node, "name"));
               return DB_SUCCESS;  /* key or tree is locked, just skip it */
            } else if (status != DB_SUCCESS) {
               cm_msg(MERROR, "db_paste_node", "cannot load value \"%s\": db_set_data() status %d", mxml_get_attribute(node, "name"), status);
               return status;
            }
         }
      }

      if (buf) {
         free(buf);
         buf = NULL;
      }
   }

   return DB_SUCCESS;
}

/********************************************************************/
/**
Paste an ODB subtree in XML format from a buffer
@param hDB          ODB handle obtained via cm_get_experiment_database().
@param hKeyRoot Handle for key where search starts, zero for root.
@param buffer NULL-terminated buffer
@return DB_SUCCESS, DB_INVALID_PARAM, DB_NO_MEMORY, DB_TYPE_MISMATCH
*/
INT db_paste_xml(HNDLE hDB, HNDLE hKeyRoot, const char *buffer)
{
   char error[256];
   INT status;
   PMXML_NODE tree, node;

   if (hKeyRoot == 0)
      db_find_key(hDB, hKeyRoot, "", &hKeyRoot);

   /* parse XML buffer */
   tree = mxml_parse_buffer(buffer, error, sizeof(error), NULL);
   if (tree == NULL) {
      puts(error);
      return DB_TYPE_MISMATCH;
   }

   node = mxml_find_node(tree, "odb");
   if (node == NULL) {
      puts("Cannot find element \"odb\" in XML data");
      return DB_TYPE_MISMATCH;
   }

   status = db_paste_node(hDB, hKeyRoot, node);

   mxml_free_tree(tree);

   return status;
}

/********************************************************************/
/**
Copy an ODB subtree in XML format to a buffer

@param hDB          ODB handle obtained via cm_get_experiment_database().
@param hKey Handle for key where search starts, zero for root.
@param buffer ASCII buffer which receives ODB contents.
@param buffer_size Size of buffer, returns remaining space in buffer.
@return DB_SUCCESS, DB_TRUNCATED, DB_NO_MEMORY
*/
INT db_copy_xml(HNDLE hDB, HNDLE hKey, char *buffer, INT * buffer_size)
{
#ifdef LOCAL_ROUTINES
   {
      INT len;
      char *p, str[256];
      MXML_WRITER *writer;

      /* open file */
      writer = mxml_open_buffer();
      if (writer == NULL) {
         cm_msg(MERROR, "db_copy_xml", "Cannot allocate buffer");
         return DB_NO_MEMORY;
      }

      db_get_path(hDB, hKey, str, sizeof(str));

      /* write XML header */
      mxml_start_element(writer, "odb");
      mxml_write_attribute(writer, "root", str);
      mxml_write_attribute(writer, "xmlns:xsi", "http://www.w3.org/2001/XMLSchema-instance");
      mxml_write_attribute(writer, "xsi:noNamespaceSchemaLocation", "http://midas.psi.ch/odb.xsd");

      db_save_xml_key(hDB, hKey, 0, writer);

      mxml_end_element(writer); // "odb"
      p = mxml_close_buffer(writer);

      strlcpy(buffer, p, *buffer_size);
      len = strlen(p);
      free(p);
      p = NULL;
      if (len > *buffer_size) {
         *buffer_size = 0;
         return DB_TRUNCATED;
      }

      *buffer_size -= len;
   }
#endif                          /* LOCAL_ROUTINES */

   return DB_SUCCESS;
}

/**dox***************************************************************/
#ifndef DOXYGEN_SHOULD_SKIP_THIS

/*------------------------------------------------------------------*/
void name2c(char *str)
/********************************************************************\

  Routine: name2c

  Purpose: Convert key name to C name. Internal use only.

\********************************************************************/
{
   if (*str >= '0' && *str <= '9')
      *str = '_';

   while (*str) {
      if (!(*str >= 'a' && *str <= 'z') && !(*str >= 'A' && *str <= 'Z') && !(*str >= '0' && *str <= '9'))
         *str = '_';
      *str = (char) tolower(*str);
      str++;
   }
}

/*------------------------------------------------------------------*/
static void db_save_tree_struct(HNDLE hDB, HNDLE hKey, int hfile, INT level)
/********************************************************************\

  Routine: db_save_tree_struct

  Purpose: Save database tree as a C structure. Gets called by
           db_save_struct(). Internal use only.

\********************************************************************/
{
   INT i, idx;
   KEY key;
   HNDLE hSubkey;
   int wr;

   /* first enumerate this level */
   for (idx = 0;; idx++) {
      char name[MAX_ODB_PATH];

      db_enum_link(hDB, hKey, idx, &hSubkey);
      if (!hSubkey)
         break;

      /* first get the name of the link, than the type of the link target */
      db_get_key(hDB, hSubkey, &key);
      strlcpy(name, key.name, sizeof(name));
      db_enum_key(hDB, hKey, idx, &hSubkey);

      db_get_key(hDB, hSubkey, &key);

      if (key.type != TID_KEY) {
         char line[MAX_ODB_PATH];
         char str[MAX_ODB_PATH];

         for (i = 0; i <= level; i++) {
            wr = write(hfile, "  ", 2);
            assert(wr == 2);
         }

         switch (key.type) {
         case TID_SBYTE:
         case TID_CHAR:
            strcpy(line, "char");
            break;
         case TID_SHORT:
            strcpy(line, "short");
            break;
         case TID_FLOAT:
            strcpy(line, "float");
            break;
         case TID_DOUBLE:
            strcpy(line, "double");
            break;
         case TID_BITFIELD:
            strcpy(line, "unsigned char");
            break;
         case TID_STRING:
            strcpy(line, "char");
            break;
         case TID_LINK:
            strcpy(line, "char");
            break;
         default:
            strcpy(line, rpc_tid_name(key.type));
            break;
         }

         strlcat(line, "                    ", sizeof(line));
         strlcpy(str, name, sizeof(str));
         name2c(str);

         if (key.num_values > 1)
            sprintf(str + strlen(str), "[%d]", key.num_values);
         if (key.type == TID_STRING || key.type == TID_LINK)
            sprintf(str + strlen(str), "[%d]", key.item_size);

         strlcpy(line + 10, str, sizeof(line) - 10);
         strlcat(line, ";\n", sizeof(line));

         wr = write(hfile, line, strlen(line));
         assert(wr > 0);
      } else {
         char line[10+MAX_ODB_PATH];
         char str[MAX_ODB_PATH];

         /* recurse subtree */
         for (i = 0; i <= level; i++) {
            wr = write(hfile, "  ", 2);
            assert(wr == 2);
         }

         sprintf(line, "struct {\n");
         wr = write(hfile, line, strlen(line));
         assert(wr > 0);
         db_save_tree_struct(hDB, hSubkey, hfile, level + 1);

         for (i = 0; i <= level; i++) {
            wr = write(hfile, "  ", 2);
            assert(wr == 2);
         }

         strcpy(str, name);
         name2c(str);

         sprintf(line, "} %s;\n", str);
         wr = write(hfile, line, strlen(line));
         assert(wr > 0);
      }
   }
}

/**dox***************************************************************/
#endif                          /* DOXYGEN_SHOULD_SKIP_THIS */

/********************************************************************/
/**
Save a branch of a database to an .ODB file

This function is used by the ODBEdit command save. For a
description of the ASCII format, see db_copy(). Data of the whole ODB can
be saved (hkey equal zero) or only a sub-tree.
@param hDB          ODB handle obtained via cm_get_experiment_database().
@param hKey Handle for key where search starts, zero for root.
@param filename Filename of .ODB file.
@param bRemote Flag for saving database on remote server.
@return DB_SUCCESS, DB_FILE_ERROR
*/
INT db_save(HNDLE hDB, HNDLE hKey, const char *filename, BOOL bRemote)
{
   if (rpc_is_remote() && bRemote)
      return rpc_call(RPC_DB_SAVE, hDB, hKey, filename, bRemote);

#ifdef LOCAL_ROUTINES
   {
      INT hfile, size, buffer_size, n, status;
      char *buffer, path[256];

      /* open file */
      hfile = open(filename, O_WRONLY | O_CREAT | O_TRUNC | O_TEXT, 0644);
      if (hfile == -1) {
         cm_msg(MERROR, "db_save", "Cannot open file \"%s\"", filename);
         return DB_FILE_ERROR;
      }

      db_get_path(hDB, hKey, path, sizeof(path));

      buffer_size = 10000;
      do {
         buffer = (char *) malloc(buffer_size);
         if (buffer == NULL) {
            cm_msg(MERROR, "db_save", "cannot allocate ODB dump buffer");
            break;
         }

         size = buffer_size;
         status = db_copy(hDB, hKey, buffer, &size, path);
         if (status != DB_TRUNCATED) {
            n = write(hfile, buffer, buffer_size - size);
            free(buffer);
            buffer = NULL;

            if (n != buffer_size - size) {
               cm_msg(MERROR, "db_save", "cannot save .ODB file");
               close(hfile);
               return DB_FILE_ERROR;
            }
            break;
         }

         /* increase buffer size if truncated */
         free(buffer);
         buffer = NULL;
         buffer_size *= 2;
      } while (1);

      close(hfile);

   }
#endif                          /* LOCAL_ROUTINES */

   return DB_SUCCESS;
}

/*------------------------------------------------------------------*/

void xml_encode(char *src, int size)
{
   int i;
   char *dst, *p;

   dst = (char *) malloc(size);
   if (dst == NULL)
      return;

   *dst = 0;
   for (i = 0; i < (int) strlen(src); i++) {
      switch (src[i]) {
      case '<':
         strlcat(dst, "&lt;", size);
         break;
      case '>':
         strlcat(dst, "&gt;", size);
         break;
      case '&':
         strlcat(dst, "&amp;", size);
         break;
      case '\"':
         strlcat(dst, "&quot;", size);
         break;
      case '\'':
         strlcat(dst, "&apos;", size);
         break;
      default:
         if ((int) strlen(dst) >= size) {
            free(dst);
            return;
         }
         p = dst + strlen(dst);
         *p = src[i];
         *(p + 1) = 0;
      }
   }

   strlcpy(src, dst, size);
}

/*------------------------------------------------------------------*/

INT db_save_xml_key(HNDLE hDB, HNDLE hKey, INT level, MXML_WRITER * writer)
{
   INT i, idx, size, status;
   char *data;
   HNDLE hSubkey;
   KEY key;

   status = db_get_link(hDB, hKey, &key);
   if (status != DB_SUCCESS)
      return status;

   if (key.type == TID_KEY) {

      /* save opening tag for subtree */

      if (level > 0) {
         mxml_start_element(writer, "dir");
         mxml_write_attribute(writer, "name", key.name);
      }

      for (idx = 0;; idx++) {
         db_enum_link(hDB, hKey, idx, &hSubkey);

         if (!hSubkey)
            break;

         /* save subtree */
         status = db_save_xml_key(hDB, hSubkey, level + 1, writer);
         if (status != DB_SUCCESS)
            return status;
      }

      /* save closing tag for subtree */
      if (level > 0)
         mxml_end_element(writer);

   } else {
      /* save key value */

      if (key.num_values > 1)
         mxml_start_element(writer, "keyarray");
      else
         mxml_start_element(writer, "key");
      mxml_write_attribute(writer, "name", key.name);
      mxml_write_attribute(writer, "type", rpc_tid_name(key.type));

      if (key.type == TID_STRING || key.type == TID_LINK) {
         char str[256];
         sprintf(str, "%d", key.item_size);
         mxml_write_attribute(writer, "size", str);
      }

      if (key.num_values > 1) {
         char str[256];
         sprintf(str, "%d", key.num_values);
         mxml_write_attribute(writer, "num_values", str);
      }

      size = key.total_size;
      data = (char *) malloc(size+1); // an extra byte to zero-terminate strings
      if (data == NULL) {
         cm_msg(MERROR, "db_save_xml_key", "cannot allocate data buffer");
         return DB_NO_MEMORY;
      }

      db_get_link_data(hDB, hKey, data, &size, key.type);

      if (key.num_values == 1) {
         if (key.type == TID_STRING) {
            data[size] = 0; // make sure strings are NUL-terminated
            mxml_write_value(writer, data);
         } else {
            char str[MAX_STRING_LENGTH];
            db_sprintf(str, data, key.item_size, 0, key.type);
            if (key.type == TID_STRING && strlen(data) >= MAX_STRING_LENGTH) {
               char path[MAX_ODB_PATH];
               db_get_path(hDB, hKey, path, sizeof(path));
               cm_msg(MERROR, "db_save_xml_key", "Long odb string probably truncated, odb path \"%s\", string length %d truncated to %d", path, (int)strlen(data), (int)strlen(str));
            }
            mxml_write_value(writer, str);
         }
         mxml_end_element(writer);

      } else {                  /* array of values */

         for (i = 0; i < key.num_values; i++) {

            mxml_start_element(writer, "value");

            {
               char str[256];
               sprintf(str, "%d", i);
               mxml_write_attribute(writer, "index", str);
            }

            if (key.type == TID_STRING) {
               char* p = data + i * key.item_size;
               p[key.item_size - 1] = 0; // make sure string is NUL-terminated
               //cm_msg(MINFO, "db_save_xml_key", "odb string array item_size %d, index %d length %d", key.item_size, i, (int)strlen(p));
               mxml_write_value(writer, p);
            } else {
               char str[MAX_STRING_LENGTH];
               db_sprintf(str, data, key.item_size, i, key.type);
               if (key.type == TID_STRING && strlen(str) >= MAX_STRING_LENGTH-1) {
                  char path[MAX_ODB_PATH];
                  db_get_path(hDB, hKey, path, sizeof(path));
                  cm_msg(MERROR, "db_save_xml_key", "Long odb string array probably truncated, odb path \"%s\"[%d]", path, i);
               }
               mxml_write_value(writer, str);
            }

            mxml_end_element(writer);
         }

         mxml_end_element(writer);      /* keyarray */
      }

      free(data);
      data = NULL;
   }

   return DB_SUCCESS;
}

/********************************************************************/
/**
Save a branch of a database to an .xml file

This function is used by the ODBEdit command save to write the contents
of the ODB into a XML file. Data of the whole ODB can
be saved (hkey equal zero) or only a sub-tree.
@param hDB          ODB handle obtained via cm_get_experiment_database().
@param hKey Handle for key where search starts, zero for root.
@param filename Filename of .XML file.
@return DB_SUCCESS, DB_FILE_ERROR
*/
INT db_save_xml(HNDLE hDB, HNDLE hKey, const char *filename)
{
#ifdef LOCAL_ROUTINES
   {
      INT status;
      char str[256];
      MXML_WRITER *writer;

      /* open file */
      writer = mxml_open_file(filename);
      if (writer == NULL) {
         cm_msg(MERROR, "db_save_xml", "Cannot open file \"%s\"", filename);
         return DB_FILE_ERROR;
      }

      db_get_path(hDB, hKey, str, sizeof(str));

      /* write XML header */
      mxml_start_element(writer, "odb");
      mxml_write_attribute(writer, "root", str);
      mxml_write_attribute(writer, "filename", filename);
      mxml_write_attribute(writer, "xmlns:xsi", "http://www.w3.org/2001/XMLSchema-instance");

      if (getenv("MIDASSYS"))
         strcpy(str, getenv("MIDASSYS"));
      else
         strcpy(str, "");
      strcat(str, DIR_SEPARATOR_STR);
      strcat(str, "odb.xsd");
      mxml_write_attribute(writer, "xsi:noNamespaceSchemaLocation", str);

      status = db_save_xml_key(hDB, hKey, 0, writer);

      mxml_end_element(writer); // "odb"
      mxml_close_file(writer);

      return status;
   }
#endif                          /* LOCAL_ROUTINES */

   return DB_SUCCESS;
}

/*------------------------------------------------------------------*/

static void json_write(char **buffer, int* buffer_size, int* buffer_end, int level, const char* s, int quoted)
{
   int len, remain, xlevel;

   len = strlen(s);
   remain = *buffer_size - *buffer_end;
   assert(remain >= 0);

   xlevel = 2*level;

   while (10 + xlevel + 3*len > remain) {
      // reallocate the buffer
      int new_buffer_size = 2*(*buffer_size);
      if (new_buffer_size < 4*1024)
         new_buffer_size = 4*1024;
      //printf("reallocate: len %d, size %d, remain %d, allocate %d\n", len, *buffer_size, remain, new_buffer_size);
      assert(new_buffer_size > *buffer_size);
      *buffer = (char *)realloc(*buffer, new_buffer_size);
      assert(*buffer);
      *buffer_size = new_buffer_size;
      remain = *buffer_size - *buffer_end;
      assert(remain >= 0);
   }

   if (xlevel) {
      int i;
      for (i=0; i<xlevel; i++)
         (*buffer)[(*buffer_end)++] = ' ';
   }

   if (!quoted) {
      memcpy(*buffer + *buffer_end, s, len);
      *buffer_end += len;
      (*buffer)[*buffer_end] = 0; // NUL-terminate the buffer
      return;
   }

   char *bufptr = *buffer;
   int bufend = *buffer_end;
   
   bufptr[bufend++] = '"';

   while (*s) {
      switch (*s) {
      case '\"':
         bufptr[bufend++] = '\\';
         bufptr[bufend++] = '\"';
         s++;
         break;
      case '\\':
         bufptr[bufend++] = '\\';
         bufptr[bufend++] = '\\';
         s++;
         break;
#if 0
      case '/':
         bufptr[bufend++] = '\\';
         bufptr[bufend++] = '/';
         s++;
         break;
#endif
      case '\b':
         bufptr[bufend++] = '\\';
         bufptr[bufend++] = 'b';
         s++;
         break;
      case '\f':
         bufptr[bufend++] = '\\';
         bufptr[bufend++] = 'f';
         s++;
         break;
      case '\n':
         bufptr[bufend++] = '\\';
         bufptr[bufend++] = 'n';
         s++;
         break;
      case '\r':
         bufptr[bufend++] = '\\';
         bufptr[bufend++] = 'r';
         s++;
         break;
      case '\t':
         bufptr[bufend++] = '\\';
         bufptr[bufend++] = 't';
         s++;
         break;
      default:
         bufptr[bufend++] = *s++;
      }
   }

   bufptr[bufend++] = '"';
   bufptr[bufend] = 0; // NUL-terminate the buffer

   *buffer_end = bufend;

   remain = *buffer_size - *buffer_end;
   assert(remain > 0);
}

static void json_write_data(char **buffer, int* buffer_size, int* buffer_end, int level, const KEY* key, const char* p)
{
   char str[256];
   switch (key->type) {
   case TID_BYTE:
      sprintf(str, "%u", *(unsigned char*)p);
      json_write(buffer, buffer_size, buffer_end, 0, str, 0);
      break;
   case TID_SBYTE:
      sprintf(str, "%d", *(char*)p);
      json_write(buffer, buffer_size, buffer_end, 0, str, 0);
      break;
   case TID_CHAR:
      sprintf(str, "%c", *(char*)p);
      json_write(buffer, buffer_size, buffer_end, 0, str, 1);
      break;
   case TID_WORD:
      sprintf(str, "\"0x%04x\"", *(WORD*)p);
      json_write(buffer, buffer_size, buffer_end, 0, str, 0);
      break;
   case TID_SHORT:
      sprintf(str, "%d", *(short*)p);
      json_write(buffer, buffer_size, buffer_end, 0, str, 0);
      break;
   case TID_DWORD:
      sprintf(str, "\"0x%08x\"", *(DWORD*)p);
      json_write(buffer, buffer_size, buffer_end, 0, str, 0);
      break;
   case TID_INT:
      sprintf(str, "%d", *(int*)p);
      json_write(buffer, buffer_size, buffer_end, 0, str, 0);
      break;
   case TID_BOOL:
      if (*(int*)p)
         json_write(buffer, buffer_size, buffer_end, 0, "true", 0);
      else
         json_write(buffer, buffer_size, buffer_end, 0, "false", 0);
      break;
   case TID_FLOAT: {
      float flt = (*(float*)p);
      if (isnan(flt))
         json_write(buffer, buffer_size, buffer_end, 0, "\"NaN\"", 0);
      else if (isinf(flt)) {
         if (flt > 0)
            json_write(buffer, buffer_size, buffer_end, 0, "\"Infinity\"", 0);
         else
            json_write(buffer, buffer_size, buffer_end, 0, "\"-Infinity\"", 0);
      } else if (flt == 0)
         json_write(buffer, buffer_size, buffer_end, 0, "0", 0);
      else if (flt == (int)flt) {
         sprintf(str, "%.0f", flt);
         json_write(buffer, buffer_size, buffer_end, 0, str, 0);
      } else {
         sprintf(str, "%.7e", flt);
         json_write(buffer, buffer_size, buffer_end, 0, str, 0);
      }
      break;
   }
   case TID_DOUBLE: {
      double dbl = (*(double*)p);
      if (isnan(dbl))
         json_write(buffer, buffer_size, buffer_end, 0, "\"NaN\"", 0);
      else if (isinf(dbl)) {
         if (dbl > 0)
            json_write(buffer, buffer_size, buffer_end, 0, "\"Infinity\"", 0);
         else
            json_write(buffer, buffer_size, buffer_end, 0, "\"-Infinity\"", 0);
      } else if (dbl == 0)
         json_write(buffer, buffer_size, buffer_end, 0, "0", 0);
      else if (dbl == (int)dbl) {
         sprintf(str, "%.0f", dbl);
         json_write(buffer, buffer_size, buffer_end, 0, str, 0);
      } else {
         sprintf(str, "%.16e", dbl);
         json_write(buffer, buffer_size, buffer_end, 0, str, 0);
      }
      break;
   }
   case TID_BITFIELD:
      json_write(buffer, buffer_size, buffer_end, 0, "(TID_BITFIELD value)", 1);
      break;
   case TID_STRING:
      // data is already NUL terminated // p[key.item_size-1] = 0;  // make sure string is NUL terminated!
      json_write(buffer, buffer_size, buffer_end, 0, p, 1);
      break;
   case TID_ARRAY:
      json_write(buffer, buffer_size, buffer_end, 0, "(TID_ARRAY value)", 1);
      break;
   case TID_STRUCT:
      json_write(buffer, buffer_size, buffer_end, 0, "(TID_STRUCT value)", 1);
      break;
   case TID_KEY:
      json_write(buffer, buffer_size, buffer_end, 0, "{ }", 0);
      break;
   case TID_LINK:
      // data is already NUL terminated // p[key.item_size-1] = 0;  // make sure string is NUL terminated!
      json_write(buffer, buffer_size, buffer_end, 0, p, 1);
      break;
   default:
      json_write(buffer, buffer_size, buffer_end, 0, "(TID_UNKNOWN value)", 1);
   }
}

static void json_write_key(HNDLE hDB, HNDLE hKey, const KEY* key, const char* link_path, char **buffer, int* buffer_size, int* buffer_end)
{
   char str[256]; // not used to store anything long, only numeric values like: "item_size: 100"

   json_write(buffer, buffer_size, buffer_end, 0, "{ ", 0);

   sprintf(str, "\"type\" : %d", key->type);
   json_write(buffer, buffer_size, buffer_end, 0, str, 0);

   if (link_path) {
      json_write(buffer, buffer_size, buffer_end, 0, ", ", 0);
      json_write(buffer, buffer_size, buffer_end, 0, "link", 1);
      json_write(buffer, buffer_size, buffer_end, 0, ": ", 0);
      json_write(buffer, buffer_size, buffer_end, 0, link_path, 1);
   }

   if (key->num_values > 1) {
      json_write(buffer, buffer_size, buffer_end, 0, ", ", 0);

      sprintf(str, "\"num_values\" : %d", key->num_values);
      json_write(buffer, buffer_size, buffer_end, 0, str, 0);
   }

   if (key->type == TID_STRING) {
      json_write(buffer, buffer_size, buffer_end, 0, ", ", 0);

      sprintf(str, "\"item_size\" : %d", key->item_size);
      json_write(buffer, buffer_size, buffer_end, 0, str, 0);
   }

   if (key->notify_count > 0) {
      json_write(buffer, buffer_size, buffer_end, 0, ", ", 0);

      sprintf(str, "\"notify_count\" : %d", key->notify_count);
      json_write(buffer, buffer_size, buffer_end, 0, str, 0);
   }

   json_write(buffer, buffer_size, buffer_end, 0, ", ", 0);

   sprintf(str, "\"access_mode\" : %d", key->access_mode);
   json_write(buffer, buffer_size, buffer_end, 0, str, 0);

   json_write(buffer, buffer_size, buffer_end, 0, ", ", 0);

   sprintf(str, "\"last_written\" : %d", key->last_written);
   json_write(buffer, buffer_size, buffer_end, 0, str, 0);

   json_write(buffer, buffer_size, buffer_end, 0, " ", 0);

   json_write(buffer, buffer_size, buffer_end, 0, "}", 0);
}

static int db_save_json_key_obsolete(HNDLE hDB, HNDLE hKey, INT level, char **buffer, int* buffer_size, int* buffer_end, int save_keys, int follow_links, int recurse)
{
   INT i, size, status;
   char *data;
   KEY key;
   KEY link_key;
   char link_path[MAX_ODB_PATH];
   int omit_top_level_braces = 0;

   //printf("db_save_json_key: key %d, level %d, save_keys %d, follow_links %d, recurse %d\n", hKey, level, save_keys, follow_links, recurse);

   if (level < 0) {
      level = 0;
      omit_top_level_braces = 1;
   }

   status = db_get_link(hDB, hKey, &key);

   if (status != DB_SUCCESS)
      return status;

   link_key = key;

   if (key.type == TID_LINK) {
      size = sizeof(link_path);
      status = db_get_data(hDB, hKey, link_path, &size, TID_LINK);

      if (status != DB_SUCCESS)
         return status;

      if (follow_links) {
         status = db_find_key(hDB, 0, link_path, &hKey);

         if (status != DB_SUCCESS)
            return status;

         status = db_get_key(hDB, hKey, &key);

         if (status != DB_SUCCESS)
            return status;
      }
   }

   //printf("key [%s] link [%s], type %d, link %d\n", key.name, link_key.name, key.type, link_key.type);

   if (key.type == TID_KEY && (recurse || level<=0)) {
      int idx = 0;
      int do_close_curly_bracket = 0;

      if (level == 0 && !omit_top_level_braces) {
         json_write(buffer, buffer_size, buffer_end, 0, "{\n", 0);
         do_close_curly_bracket = 1;
      }
      else if (level > 0) {
         json_write(buffer, buffer_size, buffer_end, level, link_key.name, 1);
         json_write(buffer, buffer_size, buffer_end, 0, " : {\n", 0);
         do_close_curly_bracket = 1;
      }

      if (level > 100) {
         char path[MAX_ODB_PATH];
         status = db_get_path(hDB, hKey, path, sizeof(path));
         if (status != DB_SUCCESS)
            strlcpy(path, "(path unknown)", sizeof(path));

         json_write(buffer, buffer_size, buffer_end, 0, "/error", 1);
         json_write(buffer, buffer_size, buffer_end, 0, " : ", 0);
         json_write(buffer, buffer_size, buffer_end, 0, "max nesting level exceed", 1);

         cm_msg(MERROR, "db_save_json_key", "max nesting level exceeded at \"%s\", check for symlink loops in this subtree", path);

      } else {
         HNDLE hSubkey;

         for (;; idx++) {
            db_enum_link(hDB, hKey, idx, &hSubkey);

            if (!hSubkey)
               break;

            if (idx != 0) {
               json_write(buffer, buffer_size, buffer_end, 0, ",\n", 0);
            }

            /* save subtree */
            status = db_save_json_key_obsolete(hDB, hSubkey, level + 1, buffer, buffer_size, buffer_end, save_keys, follow_links, recurse);
            if (status != DB_SUCCESS)
               return status;
         }
      }

      if (do_close_curly_bracket) {
         if (idx > 0)
            json_write(buffer, buffer_size, buffer_end, 0, "\n", 0);
         json_write(buffer, buffer_size, buffer_end, level, "}", 0);
      }

   } else {

      if (save_keys && level == 0) {
         json_write(buffer, buffer_size, buffer_end, 0, "{\n", 0);
      }

      /* save key value */

      if (save_keys == 1) {
         char str[NAME_LENGTH+15];
         sprintf(str, "%s/key", link_key.name);

         json_write(buffer, buffer_size, buffer_end, level, str, 1);
         json_write(buffer, buffer_size, buffer_end, 0, " : { ", 0);

         sprintf(str, "\"type\" : %d", key.type);
         json_write(buffer, buffer_size, buffer_end, 0, str, 0);

         if (link_key.type == TID_LINK && follow_links) {
            json_write(buffer, buffer_size, buffer_end, 0, ", ", 0);
            json_write(buffer, buffer_size, buffer_end, 0, "link", 1);
            json_write(buffer, buffer_size, buffer_end, 0, ": ", 0);
            json_write(buffer, buffer_size, buffer_end, 0, link_path, 1);
         }

         if (key.num_values > 1) {
            json_write(buffer, buffer_size, buffer_end, 0, ", ", 0);

            sprintf(str, "\"num_values\" : %d", key.num_values);
            json_write(buffer, buffer_size, buffer_end, 0, str, 0);
         }

         if (key.type == TID_STRING || key.type == TID_LINK) {
            json_write(buffer, buffer_size, buffer_end, 0, ", ", 0);

            sprintf(str, "\"item_size\" : %d", key.item_size);
            json_write(buffer, buffer_size, buffer_end, 0, str, 0);
         }

         if (key.notify_count > 0) {
            json_write(buffer, buffer_size, buffer_end, 0, ", ", 0);

            sprintf(str, "\"notify_count\" : %d", key.notify_count);
            json_write(buffer, buffer_size, buffer_end, 0, str, 0);
         }

         json_write(buffer, buffer_size, buffer_end, 0, ", ", 0);

         sprintf(str, "\"access_mode\" : %d", key.access_mode);
         json_write(buffer, buffer_size, buffer_end, 0, str, 0);

         json_write(buffer, buffer_size, buffer_end, 0, ", ", 0);

         sprintf(str, "\"last_written\" : %d", key.last_written);
         json_write(buffer, buffer_size, buffer_end, 0, str, 0);

         json_write(buffer, buffer_size, buffer_end, 0, " ", 0);

         json_write(buffer, buffer_size, buffer_end, 0, "}", 0);

         json_write(buffer, buffer_size, buffer_end, 0, ",\n", 0);
      }

      if (save_keys == 2) {
         char str[NAME_LENGTH+15];
         sprintf(str, "%s/last_written", link_key.name);

         json_write(buffer, buffer_size, buffer_end, level, str, 1);

         sprintf(str, " : %d", key.last_written);
         json_write(buffer, buffer_size, buffer_end, 0, str, 0);

         json_write(buffer, buffer_size, buffer_end, 0, ",\n", 0);
      }

      if (save_keys) {
         json_write(buffer, buffer_size, buffer_end, level, link_key.name, 1);
         json_write(buffer, buffer_size, buffer_end, 0, " : ", 0);
      }

      if (key.num_values > 1) {
         json_write(buffer, buffer_size, buffer_end, 0, "[ ", 0);
      }

      size = key.total_size;
      data = (char *) malloc(size);
      if (data == NULL) {
         cm_msg(MERROR, "db_save_json_key", "cannot allocate data buffer for %d bytes", size);
         return DB_NO_MEMORY;
      }

      if (key.type != TID_KEY) {
         if (follow_links)
            status = db_get_data(hDB, hKey, data, &size, key.type);
         else
            status = db_get_link_data(hDB, hKey, data, &size, key.type);

         if (status != DB_SUCCESS)
            return status;
      }

      for (i = 0; i < key.num_values; i++) {
         char str[256];
         char *p = data + key.item_size*i;

         if (i != 0)
            json_write(buffer, buffer_size, buffer_end, 0, ", ", 0);

         switch (key.type) {
         case TID_BYTE:
            sprintf(str, "%u", *(unsigned char*)p);
            json_write(buffer, buffer_size, buffer_end, 0, str, 0);
            break;
         case TID_SBYTE:
            sprintf(str, "%d", *(char*)p);
            json_write(buffer, buffer_size, buffer_end, 0, str, 0);
            break;
         case TID_CHAR:
            sprintf(str, "%c", *(char*)p);
            json_write(buffer, buffer_size, buffer_end, 0, str, 1);
            break;
         case TID_WORD:
            sprintf(str, "\"0x%04x\"", *(WORD*)p);
            json_write(buffer, buffer_size, buffer_end, 0, str, 0);
            break;
         case TID_SHORT:
            sprintf(str, "%d", *(short*)p);
            json_write(buffer, buffer_size, buffer_end, 0, str, 0);
            break;
         case TID_DWORD:
            sprintf(str, "\"0x%08x\"", *(DWORD*)p);
            json_write(buffer, buffer_size, buffer_end, 0, str, 0);
            break;
         case TID_INT:
            sprintf(str, "%d", *(int*)p);
            json_write(buffer, buffer_size, buffer_end, 0, str, 0);
            break;
         case TID_BOOL:
            if (*(int*)p)
               json_write(buffer, buffer_size, buffer_end, 0, "true", 0);
            else
               json_write(buffer, buffer_size, buffer_end, 0, "false", 0);
            break;
         case TID_FLOAT: {
            float flt = (*(float*)p);
            if (isnan(flt))
               json_write(buffer, buffer_size, buffer_end, 0, "\"NaN\"", 0);
            else if (isinf(flt)) {
               if (flt > 0)
                  json_write(buffer, buffer_size, buffer_end, 0, "\"Infinity\"", 0);
               else
                  json_write(buffer, buffer_size, buffer_end, 0, "\"-Infinity\"", 0);
            } else if (flt == 0)
               json_write(buffer, buffer_size, buffer_end, 0, "0", 0);
            else if (flt == (int)flt) {
               sprintf(str, "%.0f", flt);
               json_write(buffer, buffer_size, buffer_end, 0, str, 0);
            } else {
               sprintf(str, "%.7e", flt);
               json_write(buffer, buffer_size, buffer_end, 0, str, 0);
            }
            break;
         }
         case TID_DOUBLE: {
            double dbl = (*(double*)p);
            if (isnan(dbl))
               json_write(buffer, buffer_size, buffer_end, 0, "\"NaN\"", 0);
            else if (isinf(dbl)) {
               if (dbl > 0)
                  json_write(buffer, buffer_size, buffer_end, 0, "\"Infinity\"", 0);
               else
                  json_write(buffer, buffer_size, buffer_end, 0, "\"-Infinity\"", 0);
            } else if (dbl == 0)
               json_write(buffer, buffer_size, buffer_end, 0, "0", 0);
            else if (dbl == (int)dbl) {
               sprintf(str, "%.0f", dbl);
               json_write(buffer, buffer_size, buffer_end, 0, str, 0);
            } else {
               sprintf(str, "%.16e", dbl);
               json_write(buffer, buffer_size, buffer_end, 0, str, 0);
            }
            break;
         }
         case TID_BITFIELD:
            json_write(buffer, buffer_size, buffer_end, 0, "(TID_BITFIELD value)", 1);
            break;
         case TID_STRING:
            p[key.item_size-1] = 0;  // make sure string is NUL terminated!
            json_write(buffer, buffer_size, buffer_end, 0, p, 1);
            break;
         case TID_ARRAY:
            json_write(buffer, buffer_size, buffer_end, 0, "(TID_ARRAY value)", 1);
            break;
         case TID_STRUCT:
            json_write(buffer, buffer_size, buffer_end, 0, "(TID_STRUCT value)", 1);
            break;
         case TID_KEY:
            json_write(buffer, buffer_size, buffer_end, 0, "{ }", 0);
            break;
         case TID_LINK:
            p[key.item_size-1] = 0;  // make sure string is NUL terminated!
            json_write(buffer, buffer_size, buffer_end, 0, p, 1);
            break;
         default:
            json_write(buffer, buffer_size, buffer_end, 0, "(TID_UNKNOWN value)", 1);
         }

      }

      if (key.num_values > 1) {
         json_write(buffer, buffer_size, buffer_end, 0, " ]", 0);
      } else {
         json_write(buffer, buffer_size, buffer_end, 0, "", 0);
      }

      free(data);
      data = NULL;

      if (save_keys && level == 0) {
         json_write(buffer, buffer_size, buffer_end, 0, "\n}", 0);
      }
   }

   return DB_SUCCESS;
}

/********************************************************************/
/**
Copy an ODB array in JSON format to a buffer

@param hDB          ODB handle obtained via cm_get_experiment_database().
@param hKey Handle for key
@param buffer returns pointer to ASCII buffer with ODB contents
@param buffer_size returns size of ASCII buffer
@param buffer_end returns number of bytes contained in buffer
@return DB_SUCCESS, DB_NO_MEMORY
*/
INT db_copy_json_array(HNDLE hDB, HNDLE hKey, char **buffer, int* buffer_size, int* buffer_end)
{
   int size, asize;
   int status;
   char* data;
   int i;
   KEY key;

   status = db_get_key(hDB, hKey, &key);
   if (status != DB_SUCCESS)
      return status;

   assert(key.type != TID_KEY);

   if (key.num_values > 1) {
      json_write(buffer, buffer_size, buffer_end, 0, "[ ", 0);
   }

   size = key.total_size;

   asize = size;
   if (asize < 1024)
      asize = 1024;
   
   data = (char *) malloc(asize);
   if (data == NULL) {
      cm_msg(MERROR, "db_save_json_key_data", "cannot allocate data buffer for %d bytes", asize);
      return DB_NO_MEMORY;
   }

   data[0] = 0; // protect against TID_STRING that has key.total_size == 0.

   status = db_get_data(hDB, hKey, data, &size, key.type);
   if (status != DB_SUCCESS) {
      free(data);
      return status;
   }

   for (i = 0; i < key.num_values; i++) {
      char *p = data + key.item_size*i;

      if (i != 0)
         json_write(buffer, buffer_size, buffer_end, 0, ", ", 0);
      
      json_write_data(buffer, buffer_size, buffer_end, 0, &key, p);
   }
   
   if (key.num_values > 1) {
      json_write(buffer, buffer_size, buffer_end, 0, " ]", 0);
   }
   
   free(data);
   data = NULL;

   return DB_SUCCESS;
}

/********************************************************************/
/**
Copy an ODB array element in JSON format to a buffer

@param hDB          ODB handle obtained via cm_get_experiment_database().
@param hKey Handle for key
@param index Array index
@param buffer returns pointer to ASCII buffer with ODB contents
@param buffer_size returns size of ASCII buffer
@param buffer_end returns number of bytes contained in buffer
@return DB_SUCCESS, DB_NO_MEMORY
*/
INT db_copy_json_index(HNDLE hDB, HNDLE hKey, int index, char **buffer, int* buffer_size, int* buffer_end)
{
   int status;
   KEY key;

   status = db_get_key(hDB, hKey, &key);

   if (status != DB_SUCCESS)
      return status;

   int size = key.item_size;
   char* data = (char*)malloc(size + 1); // extra byte for string NUL termination
   assert(data != NULL);

   status = db_get_data_index(hDB, hKey, data, &size, index, key.type);

   if (status != DB_SUCCESS) {
      free(data);
      return status;
   }

   assert(size <= key.item_size);
   data[key.item_size] = 0; // make sure data is NUL terminated, in case of strings.

   json_write_data(buffer, buffer_size, buffer_end, 0, &key, data);

   free(data);

   return DB_SUCCESS;
}

#define JS_LEVEL_0        0
#define JS_LEVEL_1        1
#define JS_MUST_BE_SUBDIR 1
#define JSFLAG_SAVE_KEYS         (1<<1)
#define JSFLAG_FOLLOW_LINKS      (1<<2)
#define JSFLAG_RECURSE           (1<<3)
#define JSFLAG_LOWERCASE         (1<<4)
#define JSFLAG_OMIT_NAMES        (1<<5)
#define JSFLAG_OMIT_LAST_WRITTEN (1<<6)
#define JSFLAG_OMIT_OLD          (1<<7)

static int json_write_anything(HNDLE hDB, HNDLE hKey, char **buffer, int *buffer_size, int *buffer_end, int level, int must_be_subdir, int flags, time_t timestamp);

static int json_write_bare_subdir(HNDLE hDB, HNDLE hKey, char **buffer, int *buffer_size, int *buffer_end, int level, int flags, time_t timestamp)
{
   int status;
   int i;

   if (level > MAX_ODB_PATH/2) {
      // max nesting level is limited by max odb path, where each subdirectory takes
      // at least 2 bytes - 1 byte directory name and 1 byte for "/"
      cm_msg(MERROR, "json_write_bare_subdir", "Max ODB subdirectory nesting level exceeded %d", level);
      return DB_TRUNCATED;
   }

   for (i=0; ; i++) {
      HNDLE hLink, hLinkTarget;
      KEY link, link_target;
      char link_buf[MAX_ODB_PATH]; // link_path points to link_buf
      char* link_path = NULL;

      status = db_enum_link(hDB, hKey, i, &hLink);
      if (status != DB_SUCCESS && !hLink)
         break;

      status = db_get_link(hDB, hLink, &link);
      if (status != DB_SUCCESS)
         return status;

      hLinkTarget = hLink;

      if (link.type == TID_LINK) {
         int size = sizeof(link_buf);
         status = db_get_link_data(hDB, hLink, link_buf, &size, TID_LINK);
         if (status != DB_SUCCESS)
            return status;

         //printf("link size %d, len %d, data [%s]\n", size, (int)strlen(link_buf), link_buf);

         if ((size > 0) && (strlen(link_buf) > 0)) {
            link_path = link_buf;

            // resolve the link, unless it is a link to an array element
            if ((flags & JSFLAG_FOLLOW_LINKS) && strchr(link_path, '[') == NULL) {
               status = db_find_key(hDB, 0, link_path, &hLinkTarget);
               if (status != DB_SUCCESS) {
                  // dangling link to nowhere
                  hLinkTarget = hLink;
               }
            }
         }
      }

      status = db_get_key(hDB, hLinkTarget, &link_target);
      if (status != DB_SUCCESS)
         return status;

      if (flags & JSFLAG_OMIT_OLD) {
         if (link_target.last_written)
            if (link_target.last_written < timestamp)
               continue;
      }

      if (i != 0) {
         json_write(buffer, buffer_size, buffer_end, 0, ",\n", 0);
      } else {
         json_write(buffer, buffer_size, buffer_end, 0, "\n", 0);
      }

      char link_name[MAX_ODB_PATH];

      strlcpy(link_name, link.name, sizeof(link_name));

      if (flags & JSFLAG_LOWERCASE) {
         for (int i=0; (i<(int)sizeof(link.name)) && link.name[i]; i++)
            link_name[i] = tolower(link.name[i]);
      }

      if ((flags & JSFLAG_LOWERCASE) && !(flags & JSFLAG_OMIT_NAMES)) {
         char buf[MAX_ODB_PATH];
         strlcpy(buf, link_name, sizeof(buf));
         strlcat(buf, "/name", sizeof(buf));
         json_write(buffer, buffer_size, buffer_end, level, buf, 1);
         json_write(buffer, buffer_size, buffer_end, 0, " : " , 0);
         json_write(buffer, buffer_size, buffer_end, 0, link.name, 1);
         json_write(buffer, buffer_size, buffer_end, 0, ",\n", 0);
      }

      if (link.type != TID_KEY && (flags & JSFLAG_SAVE_KEYS)) {
         char buf[MAX_ODB_PATH];
         strlcpy(buf, link_name, sizeof(buf));
         strlcat(buf, "/key", sizeof(buf));
         json_write(buffer, buffer_size, buffer_end, level, buf, 1);
         json_write(buffer, buffer_size, buffer_end, 0, " : " , 0);
         json_write_key(hDB, hLink, &link_target, link_path, buffer, buffer_size, buffer_end);
         json_write(buffer, buffer_size, buffer_end, 0, ",\n", 0);
      } else if ((link_target.type != TID_KEY) && !(flags & JSFLAG_OMIT_LAST_WRITTEN)) {
         char buf[MAX_ODB_PATH];
         strlcpy(buf, link_name, sizeof(buf));
         strlcat(buf, "/last_written", sizeof(buf));
         json_write(buffer, buffer_size, buffer_end, level, buf, 1);
         json_write(buffer, buffer_size, buffer_end, 0, " : " , 0);
         sprintf(buf, "%d", link_target.last_written);
         json_write(buffer, buffer_size, buffer_end, 0, buf, 0);
         json_write(buffer, buffer_size, buffer_end, 0, ",\n", 0);
      }

      json_write(buffer, buffer_size, buffer_end, level, link_name, 1);
      json_write(buffer, buffer_size, buffer_end, 0, " : " , 0);

      if (link_target.type == TID_KEY && !(flags & JSFLAG_RECURSE)) {
         json_write(buffer, buffer_size, buffer_end, 0, "{ }" , 0);
      } else {
         status = json_write_anything(hDB, hLinkTarget, buffer, buffer_size, buffer_end, level, 0, flags, timestamp);
         if (status != DB_SUCCESS)
            return status;
      }
   }

   return DB_SUCCESS;
}

static int json_write_anything(HNDLE hDB, HNDLE hKey, char **buffer, int *buffer_size, int *buffer_end, int level, int must_be_subdir, int flags, time_t timestamp)
{
   int status;
   KEY key;

   status = db_get_key(hDB, hKey, &key);
   if (status != DB_SUCCESS)
      return status;

   if (key.type == TID_KEY) {

      json_write(buffer, buffer_size, buffer_end, 0, "{", 0);

      status = json_write_bare_subdir(hDB, hKey, buffer, buffer_size, buffer_end, level+1, flags, timestamp);
      if (status != DB_SUCCESS)
         return status;

      json_write(buffer, buffer_size, buffer_end, 0, "\n", 0);
      json_write(buffer, buffer_size, buffer_end, level, "}", 0);

   } else {
      if (must_be_subdir)
         return DB_TYPE_MISMATCH;

      status = db_copy_json_array(hDB, hKey, buffer, buffer_size, buffer_end);

      if (status != DB_SUCCESS)
         return status;
   }

   return DB_SUCCESS;
}

INT db_copy_json_ls(HNDLE hDB, HNDLE hKey, char **buffer, int* buffer_size, int* buffer_end)
{
   int status;
   status = db_lock_database(hDB);
   if (status != DB_SUCCESS) {
      return status;
   }
   status = json_write_anything(hDB, hKey, buffer, buffer_size, buffer_end, JS_LEVEL_0, JS_MUST_BE_SUBDIR, JSFLAG_SAVE_KEYS|JSFLAG_FOLLOW_LINKS, 0);
   db_unlock_database(hDB);
   return status;
}

INT db_copy_json_values(HNDLE hDB, HNDLE hKey, char **buffer, int* buffer_size, int* buffer_end, int omit_names, int omit_last_written, time_t omit_old_timestamp, int preserve_case)
{
   int status;
   int flags = JSFLAG_FOLLOW_LINKS|JSFLAG_RECURSE;
   if (omit_names)
      flags |= JSFLAG_OMIT_NAMES;
   if (omit_last_written)
      flags |= JSFLAG_OMIT_LAST_WRITTEN;
   if (omit_old_timestamp)
      flags |= JSFLAG_OMIT_OLD;
   if (!preserve_case)
      flags |= JSFLAG_LOWERCASE;
   status = db_lock_database(hDB);
   if (status != DB_SUCCESS) {
      return status;
   }
   status = json_write_anything(hDB, hKey, buffer, buffer_size, buffer_end, JS_LEVEL_0, 0, flags, omit_old_timestamp);
   db_unlock_database(hDB);
   return status;
}

INT db_copy_json_save(HNDLE hDB, HNDLE hKey, char **buffer, int* buffer_size, int* buffer_end)
{
   int status;
   status = db_lock_database(hDB);
   if (status != DB_SUCCESS) {
      return status;
   }
   status = json_write_anything(hDB, hKey, buffer, buffer_size, buffer_end, JS_LEVEL_0, JS_MUST_BE_SUBDIR, JSFLAG_SAVE_KEYS|JSFLAG_RECURSE, 0);
   db_unlock_database(hDB);
   return status;
}

/********************************************************************/
/**
Copy an ODB subtree in JSON format to a buffer

@param hDB          ODB handle obtained via cm_get_experiment_database().
@param hKey Handle for key where search starts, zero for root.
@param buffer returns pointer to ASCII buffer with ODB contents
@param buffer_size returns size of ASCII buffer
@param buffer_end returns number of bytes contained in buffer
@return DB_SUCCESS, DB_NO_MEMORY
*/
INT db_copy_json_obsolete(HNDLE hDB, HNDLE hKey, char **buffer, int* buffer_size, int* buffer_end, int save_keys, int follow_links, int recurse)
{
   db_save_json_key_obsolete(hDB, hKey, 0, buffer, buffer_size, buffer_end, save_keys, follow_links, recurse);
   json_write(buffer, buffer_size, buffer_end, 0, "\n", 0);
   return DB_SUCCESS;
}

/********************************************************************/
/**
Save a branch of a database to an .json file

This function is used by the ODBEdit command save to write the contents
of the ODB into a JSON file. Data of the whole ODB can
be saved (hkey equal zero) or only a sub-tree.
@param hDB          ODB handle obtained via cm_get_experiment_database().
@param hKey Handle for key where search starts, zero for root.
@param filename Filename of .json file.
@return DB_SUCCESS, DB_FILE_ERROR
*/
INT db_save_json(HNDLE hDB, HNDLE hKey, const char *filename)
{
#ifdef LOCAL_ROUTINES
   {
      INT status, buffer_size, buffer_end;
      char path[MAX_ODB_PATH];
      FILE *fp;
      char *buffer;

      /* open file */
      fp = fopen(filename, "w");
      if (fp == NULL) {
         cm_msg(MERROR, "db_save_json", "Cannot open file \"%s\", fopen() errno %d (%s)", filename, errno, strerror(errno));
         return DB_FILE_ERROR;
      }

      db_get_path(hDB, hKey, path, sizeof(path));

      buffer = NULL;
      buffer_size = 0;
      buffer_end = 0;

      json_write(&buffer, &buffer_size, &buffer_end, 0, "{\n", 0);

      json_write(&buffer, &buffer_size, &buffer_end, 1, "/MIDAS version", 1);
      json_write(&buffer, &buffer_size, &buffer_end, 0, " : ", 0);
      json_write(&buffer, &buffer_size, &buffer_end, 0, MIDAS_VERSION, 1);
      json_write(&buffer, &buffer_size, &buffer_end, 0, ",\n", 0);

      json_write(&buffer, &buffer_size, &buffer_end, 1, "/MIDAS git revision", 1);
      json_write(&buffer, &buffer_size, &buffer_end, 0, " : ", 0);
      json_write(&buffer, &buffer_size, &buffer_end, 0, GIT_REVISION, 1);
      json_write(&buffer, &buffer_size, &buffer_end, 0, ",\n", 0);

      json_write(&buffer, &buffer_size, &buffer_end, 1, "/filename", 1);
      json_write(&buffer, &buffer_size, &buffer_end, 0, " : ", 0);
      json_write(&buffer, &buffer_size, &buffer_end, 0, filename, 1);
      json_write(&buffer, &buffer_size, &buffer_end, 0, ",\n", 0);

      json_write(&buffer, &buffer_size, &buffer_end, 1, "/ODB path", 1);
      json_write(&buffer, &buffer_size, &buffer_end, 0, " : ", 0);
      json_write(&buffer, &buffer_size, &buffer_end, 0, path, 1);
      json_write(&buffer, &buffer_size, &buffer_end, 0, ",\n", 0);

      //status = db_save_json_key_obsolete(hDB, hKey, -1, &buffer, &buffer_size, &buffer_end, 1, 0, 1);
      status = json_write_bare_subdir(hDB, hKey, &buffer, &buffer_size, &buffer_end, JS_LEVEL_1, JSFLAG_SAVE_KEYS|JSFLAG_RECURSE, 0);

      json_write(&buffer, &buffer_size, &buffer_end, 0, "\n}\n", 0);

      if (status == DB_SUCCESS) {
         if (buffer) {
            size_t wr = fwrite(buffer, 1, buffer_end, fp);
            if (wr != (size_t)buffer_end) {
               cm_msg(MERROR, "db_save_json", "Cannot write to file \"%s\", fwrite() errno %d (%s)", filename, errno, strerror(errno));
               free(buffer);
               fclose(fp);
               return DB_FILE_ERROR;
            }
         }
      }

      if (buffer)
         free(buffer);

      fclose(fp);
   }
#endif                          /* LOCAL_ROUTINES */

   return DB_SUCCESS;
}

/********************************************************************/
/**
Save a branch of a database to a C structure .H file
@param hDB          ODB handle obtained via cm_get_experiment_database().
@param hKey Handle for key where search starts, zero for root.
@param file_name Filename of .ODB file.
@param struct_name Name of structure. If struct_name == NULL,
                   the name of the key is used.
@param append      If TRUE, append to end of existing file
@return DB_SUCCESS, DB_INVALID_HANDLE, DB_FILE_ERROR
*/
INT db_save_struct(HNDLE hDB, HNDLE hKey, const char *file_name, const char *struct_name, BOOL append)
{
   KEY key;
   char str[100], line[10+100];
   INT status, i, fh;
   int wr, size;

   /* open file */
   fh = open(file_name, O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC), 0644);

   if (fh == -1) {
      cm_msg(MERROR, "db_save_struct", "Cannot open file\"%s\"", file_name);
      return DB_FILE_ERROR;
   }

   status = db_get_key(hDB, hKey, &key);
   if (status != DB_SUCCESS) {
      cm_msg(MERROR, "db_save_struct", "cannot find key");
      return DB_INVALID_HANDLE;
   }

   sprintf(line, "typedef struct {\n");

   size = strlen(line);
   wr = write(fh, line, size);
   if (wr != size) {
      cm_msg(MERROR, "db_save_struct", "file \"%s\" write error: write(%d) returned %d, errno %d (%s)", file_name, size, wr, errno, strerror(errno));
      close(fh);
      return DB_FILE_ERROR;
   }

   db_save_tree_struct(hDB, hKey, fh, 0);

   if (struct_name && struct_name[0])
      strlcpy(str, struct_name, sizeof(str));
   else
      strlcpy(str, key.name, sizeof(str));

   name2c(str);
   for (i = 0; i < (int) strlen(str); i++)
      str[i] = (char) toupper(str[i]);

   sprintf(line, "} %s;\n\n", str);

   size = strlen(line);
   wr = write(fh, line, size);
   if (wr != size) {
      cm_msg(MERROR, "db_save_struct", "file \"%s\" write error: write(%d) returned %d, errno %d (%s)", file_name, size, wr, errno, strerror(errno));
      close(fh);
      return DB_FILE_ERROR;
   }

   close(fh);

   return DB_SUCCESS;
}

/**dox***************************************************************/
#ifndef DOXYGEN_SHOULD_SKIP_THIS

/*------------------------------------------------------------------*/
INT db_save_string(HNDLE hDB, HNDLE hKey, const char *file_name, const char *string_name, BOOL append)
/********************************************************************\

  Routine: db_save_string

  Purpose: Save a branch of a database as a string which can be used
           by db_create_record.

  Input:
    HNDLE hDB               Handle to the database
    HNDLE hKey              Handle of key to start, 0 for root
    int   fh                File handle to write to
    char  string_name       Name of string. If struct_name == NULL,
                            the name of the key is used.

  Output:
    none

  Function value:
    DB_SUCCESS              Successful completion
    DB_INVALID_HANDLE       Database handle is invalid

\********************************************************************/
{
   KEY key;
   char str[256], line[50+256];
   INT status, i, size, fh, buffer_size;
   char *buffer = NULL, *pc;
   int wr;


   /* open file */
   fh = open(file_name, O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC), 0644);

   if (fh == -1) {
      cm_msg(MERROR, "db_save_string", "Cannot open file\"%s\"", file_name);
      return DB_FILE_ERROR;
   }

   status = db_get_key(hDB, hKey, &key);
   if (status != DB_SUCCESS) {
      cm_msg(MERROR, "db_save_string", "cannot find key");
      return DB_INVALID_HANDLE;
   }

   if (string_name && string_name[0])
      strcpy(str, string_name);
   else
      strcpy(str, key.name);

   name2c(str);
   for (i = 0; i < (int) strlen(str); i++)
      str[i] = (char) toupper(str[i]);

   sprintf(line, "#define %s(_name) const char *_name[] = {\\\n", str);
   size = strlen(line);
   wr = write(fh, line, size);
   if (wr != size) {
      cm_msg(MERROR, "db_save", "file \"%s\" write error: write(%d) returned %d, errno %d (%s)", file_name, size, wr, errno, strerror(errno));
      close(fh);
      if (buffer)
         free(buffer);
      return DB_FILE_ERROR;
   }

   buffer_size = 10000;
   do {
      buffer = (char *) malloc(buffer_size);
      if (buffer == NULL) {
         cm_msg(MERROR, "db_save", "cannot allocate ODB dump buffer");
         break;
      }

      size = buffer_size;
      status = db_copy(hDB, hKey, buffer, &size, "");
      if (status != DB_TRUNCATED)
         break;

      /* increase buffer size if truncated */
      free(buffer);
      buffer = NULL;
      buffer_size *= 2;
   } while (1);


   pc = buffer;

   do {
      i = 0;
      line[i++] = '"';
      while (*pc != '\n' && *pc != 0) {
         if (*pc == '\"' || *pc == '\'')
            line[i++] = '\\';
         line[i++] = *pc++;
      }
      strcpy(&line[i], "\",\\\n");
      if (i > 0) {
         size = strlen(line);
         wr = write(fh, line, size);
         if (wr != size) {
            cm_msg(MERROR, "db_save", "file \"%s\" write error: write(%d) returned %d, errno %d (%s)", file_name, size, wr, errno, strerror(errno));
            close(fh);
            if (buffer)
               free(buffer);
            return DB_FILE_ERROR;
         }
      }

      if (*pc == '\n')
         pc++;

   } while (*pc);

   sprintf(line, "NULL }\n\n");
   size = strlen(line);
   wr = write(fh, line, size);
   if (wr != size) {
      cm_msg(MERROR, "db_save", "file \"%s\" write error: write(%d) returned %d, errno %d (%s)", file_name, size, wr, errno, strerror(errno));
      close(fh);
      if (buffer)
         free(buffer);
      return DB_FILE_ERROR;
   }

   close(fh);
   free(buffer);

   return DB_SUCCESS;
}

/**dox***************************************************************/
#endif                          /* DOXYGEN_SHOULD_SKIP_THIS */

/********************************************************************/
/**
Convert a database value to a string according to its type.

This function is a convenient way to convert a binary ODB value into a
string depending on its type if is not known at compile time. If it is known, the
normal sprintf() function can be used.
\code
...
  for (j=0 ; j<key.num_values ; j++)
  {
    db_sprintf(pbuf, pdata, key.item_size, j, key.type);
    strcat(pbuf, "\n");
  }
  ...
\endcode
@param string output ASCII string of data. must be at least MAX_STRING_LENGTH bytes long.
@param data Value data.
@param data_size Size of single data element.
@param idx Index for array data.
@param type Type of key, one of TID_xxx (see @ref Midas_Data_Types).
@return DB_SUCCESS
*/
INT db_sprintf(char *string, const void *data, INT data_size, INT idx, DWORD type)
{
   if (data_size == 0)
      sprintf(string, "<NULL>");
   else
      switch (type) {
      case TID_BYTE:
         sprintf(string, "%d", *(((BYTE *) data) + idx));
         break;
      case TID_SBYTE:
         sprintf(string, "%d", *(((char *) data) + idx));
         break;
      case TID_CHAR:
         sprintf(string, "%c", *(((char *) data) + idx));
         break;
      case TID_WORD:
         sprintf(string, "%u", *(((WORD *) data) + idx));
         break;
      case TID_SHORT:
         sprintf(string, "%d", *(((short *) data) + idx));
         break;
      case TID_DWORD:
         sprintf(string, "%u", *(((DWORD *) data) + idx));
         break;
      case TID_INT:
         sprintf(string, "%d", *(((INT *) data) + idx));
         break;
      case TID_BOOL:
         sprintf(string, "%c", *(((BOOL *) data) + idx) ? 'y' : 'n');
         break;
      case TID_FLOAT:
         if (ss_isnan(*(((float *) data) + idx)))
            sprintf(string, "NAN");
         else
            sprintf(string, "%.7g", *(((float *) data) + idx));
         break;
      case TID_DOUBLE:
         if (ss_isnan(*(((double *) data) + idx)))
            sprintf(string, "NAN");
         else
            sprintf(string, "%.16lg", *(((double *) data) + idx));
         break;
      case TID_BITFIELD:
         /* TBD */
         break;
      case TID_STRING:
      case TID_LINK:
         strlcpy(string, ((char *) data) + data_size * idx, MAX_STRING_LENGTH);
         break;
      default:
         sprintf(string, "<unknown>");
         break;
      }

   return DB_SUCCESS;
}

/********************************************************************/
/**
Same as db_sprintf, but with additional format parameter

@param string output ASCII string of data.
@param format Format specifier passed to sprintf()
@param data Value data.
@param data_size Size of single data element.
@param idx Index for array data.
@param type Type of key, one of TID_xxx (see @ref Midas_Data_Types).
@return DB_SUCCESS
*/

INT db_sprintff(char *string, const char *format, const void *data, INT data_size, INT idx, DWORD type)
{
   if (data_size == 0)
      sprintf(string, "<NULL>");
   else
      switch (type) {
      case TID_BYTE:
         sprintf(string, format, *(((BYTE *) data) + idx));
         break;
      case TID_SBYTE:
         sprintf(string, format, *(((char *) data) + idx));
         break;
      case TID_CHAR:
         sprintf(string, format, *(((char *) data) + idx));
         break;
      case TID_WORD:
         sprintf(string, format, *(((WORD *) data) + idx));
         break;
      case TID_SHORT:
         sprintf(string, format, *(((short *) data) + idx));
         break;
      case TID_DWORD:
         sprintf(string, format, *(((DWORD *) data) + idx));
         break;
      case TID_INT:
         sprintf(string, format, *(((INT *) data) + idx));
         break;
      case TID_BOOL:
         sprintf(string, format, *(((BOOL *) data) + idx) ? 'y' : 'n');
         break;
      case TID_FLOAT:
         if (ss_isnan(*(((float *) data) + idx)))
            sprintf(string, "NAN");
         else
            sprintf(string, format, *(((float *) data) + idx));
         break;
      case TID_DOUBLE:
         if (ss_isnan(*(((double *) data) + idx)))
            sprintf(string, "NAN");
         else
            sprintf(string, format, *(((double *) data) + idx));
         break;
      case TID_BITFIELD:
         /* TBD */
         break;
      case TID_STRING:
      case TID_LINK:
         strlcpy(string, ((char *) data) + data_size * idx, MAX_STRING_LENGTH);
         break;
      default:
         sprintf(string, "<unknown>");
         break;
      }

   return DB_SUCCESS;
}

/**dox***************************************************************/
#ifndef DOXYGEN_SHOULD_SKIP_THIS

/*------------------------------------------------------------------*/
INT db_sprintfh(char *string, const void *data, INT data_size, INT idx, DWORD type)
/********************************************************************\

  Routine: db_sprintfh

  Purpose: Convert a database value to a string according to its type
           in hex format

  Input:
    void  *data             Value data
    INT   idx               Index for array data
    INT   data_size         Size of single data element
    DWORD type              Valye type, one of TID_xxx

  Output:
    char  *string           ASCII string of data

  Function value:
    DB_SUCCESS              Successful completion

\********************************************************************/
{
   if (data_size == 0)
      sprintf(string, "<NULL>");
   else
      switch (type) {
      case TID_BYTE:
         sprintf(string, "0x%X", *(((BYTE *) data) + idx));
         break;
      case TID_SBYTE:
         sprintf(string, "0x%X", *(((char *) data) + idx));
         break;
      case TID_CHAR:
         sprintf(string, "%c", *(((char *) data) + idx));
         break;
      case TID_WORD:
         sprintf(string, "0x%X", *(((WORD *) data) + idx));
         break;
      case TID_SHORT:
         sprintf(string, "0x%hX", *(((short *) data) + idx));
         break;
      case TID_DWORD:
         sprintf(string, "0x%X", *(((DWORD *) data) + idx));
         break;
      case TID_INT:
         sprintf(string, "0x%X", *(((INT *) data) + idx));
         break;
      case TID_BOOL:
         sprintf(string, "%c", *(((BOOL *) data) + idx) ? 'y' : 'n');
         break;
      case TID_FLOAT:
         if (ss_isnan(*(((float *) data) + idx)))
            sprintf(string, "NAN");
         else
            sprintf(string, "%.7g", *(((float *) data) + idx));
         break;
      case TID_DOUBLE:
         if (ss_isnan(*(((double *) data) + idx)))
            sprintf(string, "NAN");
         else
            sprintf(string, "%.16lg", *(((double *) data) + idx));
         break;
      case TID_BITFIELD:
         /* TBD */
         break;
      case TID_STRING:
      case TID_LINK:
         sprintf(string, "%s", ((char *) data) + data_size * idx);
         break;
      default:
         sprintf(string, "<unknown>");
         break;
      }

   return DB_SUCCESS;
}

/*------------------------------------------------------------------*/
INT db_sscanf(const char *data_str, void *data, INT * data_size, INT i, DWORD tid)
/********************************************************************\

  Routine: db_sscanf

  Purpose: Convert a string to a database value according to its type

  Input:
    char  *data_str         ASCII string of data
    INT   i                 Index for array data
    DWORD tid               Value type, one of TID_xxx

  Output:
    void  *data             Value data
    INT   *data_size        Size of single data element

  Function value:
    DB_SUCCESS              Successful completion

\********************************************************************/
{
   DWORD value = 0;
   BOOL hex = FALSE;

   if (data_str == NULL)
      return 0;

   *data_size = rpc_tid_size(tid);
   if (strncmp(data_str, "0x", 2) == 0) {
      hex = TRUE;
      sscanf(data_str + 2, "%x", &value);
   }

   switch (tid) {
   case TID_BYTE:
   case TID_SBYTE:
      if (hex)
         *((char *) data + i) = (char) value;
      else
         *((char *) data + i) = (char) atoi(data_str);
      break;
   case TID_CHAR:
      *((char *) data + i) = data_str[0];
      break;
   case TID_WORD:
      if (hex)
         *((WORD *) data + i) = (WORD) value;
      else
         *((WORD *) data + i) = (WORD) atoi(data_str);
      break;
   case TID_SHORT:
      if (hex)
         *((short int *) data + i) = (short int) value;
      else
         *((short int *) data + i) = (short int) atoi(data_str);
      break;
   case TID_DWORD:
      if (!hex)
         sscanf(data_str, "%u", &value);

      *((DWORD *) data + i) = value;
      break;
   case TID_INT:
      if (hex)
         *((INT *) data + i) = value;
      else
         *((INT *) data + i) = atol(data_str);
      break;
   case TID_BOOL:
      if (data_str[0] == 'y' || data_str[0] == 'Y' ||
          data_str[0] == 't' || data_str[0] == 'T' || atoi(data_str) > 0)
         *((BOOL *) data + i) = 1;
      else
         *((BOOL *) data + i) = 0;
      break;
   case TID_FLOAT:
      if (data_str[0] == 'n' || data_str[0] == 'N')
         *((float *) data + i) = (float) ss_nan();
      else
         *((float *) data + i) = (float) atof(data_str);
      break;
   case TID_DOUBLE:
      if (data_str[0] == 'n' || data_str[0] == 'N')
         *((double *) data + i) = ss_nan();
      else
         *((double *) data + i) = atof(data_str);
      break;
   case TID_BITFIELD:
      /* TBD */
      break;
   case TID_STRING:
   case TID_LINK:
      strcpy((char *) data, data_str);
      *data_size = strlen(data_str) + 1;
      break;
   }

   return DB_SUCCESS;
}

/*------------------------------------------------------------------*/

#ifdef LOCAL_ROUTINES

static void db_recurse_record_tree(HNDLE hDB, HNDLE hKey, void **data,
                                   INT * total_size, INT base_align, INT * max_align, BOOL bSet, INT convert_flags)
/********************************************************************\

  Routine: db_recurse_record_tree

  Purpose: Recurse a database tree and calculate its size or copy
           data. Internal use only.

\********************************************************************/
{
   KEY *pold, link;
   HNDLE hKeyLink;
   INT size, align, corr, total_size_tmp;
   char link_path[MAX_ODB_PATH];

   /* get first subkey of hKey */
   DATABASE_HEADER *pheader = _database[hDB - 1].database_header;

   if (!db_validate_hkey(pheader, hKey)) {
      cm_msg(MERROR, "db_recurse_record_tree", "invalid hKey %d", hKey);
      return;
   }
   
   KEY *pkey = (KEY *) ((char *) pheader + hKey);

   if (!db_validate_pkey(pheader, pkey)) {
      cm_msg(MERROR, "db_recurse_record_tree", "invalid pkey at hKey %d", hKey);
      return;
   }
   
   KEYLIST *pkeylist = (KEYLIST *) ((char *) pheader + pkey->data);
   if (!pkeylist->first_key)
      return;
   // FIXME: validate pkeylist->first_key
   pkey = (KEY *) ((char *) pheader + pkeylist->first_key);

   /* first browse through this level */
   do {
      pold = NULL;
      
      if (pkey->type == TID_LINK) {
         strlcpy(link_path, (char *) pheader + pkey->data, sizeof(link_path));
         
         if (link_path[0] == '/')
            db_find_key1(hDB, 0, link_path, &hKeyLink);
         else
            db_find_key1(hDB, hKey, link_path, &hKeyLink);

         if (hKeyLink) {
            db_get_key(hDB, hKeyLink, &link);
            if (link.type == TID_KEY) {
            db_recurse_record_tree(hDB, hKeyLink,
                                   data, total_size, base_align, NULL, bSet, convert_flags);
            } else {
               pold = pkey;
               pkey = &link;
            }
         }
      }
      
      if (pkey->type != TID_KEY) {
         /* correct for alignment */
         align = 1;

         if (rpc_tid_size(pkey->type))
            align = rpc_tid_size(pkey->type) < base_align ? rpc_tid_size(pkey->type) : base_align;

         if (max_align && align > *max_align)
            *max_align = align;

         corr = VALIGN(*total_size, align) - *total_size;
         *total_size += corr;
         if (data)
            *data = (void *) ((char *) (*data) + corr);

         /* calculate data size */
         size = pkey->item_size * pkey->num_values;

         if (data) {
            if (bSet) {
               /* copy data if there is write access */
               if (pkey->access_mode & MODE_WRITE) {
                  memcpy((char *) pheader + pkey->data, *data, pkey->item_size * pkey->num_values);

                  /* convert data */
                  if (convert_flags) {
                     if (pkey->num_values > 1)
                        rpc_convert_data((char *) pheader + pkey->data,
                                         pkey->type, RPC_FIXARRAY, pkey->item_size * pkey->num_values, convert_flags);
                     else
                        rpc_convert_single((char *) pheader + pkey->data, pkey->type, 0, convert_flags);
                  }

                  /* update time */
                  pkey->last_written = ss_time();

                  /* notify clients which have key open */
                  db_notify_clients(hDB, (POINTER_T) pkey - (POINTER_T) pheader, -1, TRUE);
               }
            } else {
               /* copy key data if there is read access */
               if (pkey->access_mode & MODE_READ) {
                  memcpy(*data, (char *) pheader + pkey->data, pkey->item_size * pkey->num_values);

                  /* convert data */
                  if (convert_flags) {
                     if (pkey->num_values > 1)
                        rpc_convert_data(*data, pkey->type,
                                         RPC_FIXARRAY | RPC_OUTGOING,
                                         pkey->item_size * pkey->num_values, convert_flags);
                     else
                        rpc_convert_single(*data, pkey->type, RPC_OUTGOING, convert_flags);
                  }
               }
            }

            *data = (char *) (*data) + size;
         }

         *total_size += size;
      } else {
         /* align new substructure according to the maximum
            align value in this structure */
         align = 1;

         total_size_tmp = *total_size;
         db_recurse_record_tree(hDB, (POINTER_T) pkey - (POINTER_T) pheader,
                                NULL, &total_size_tmp, base_align, &align, bSet, convert_flags);

         if (max_align && align > *max_align)
            *max_align = align;

         corr = VALIGN(*total_size, align) - *total_size;
         *total_size += corr;
         if (data)
            *data = (void *) ((char *) (*data) + corr);

         /* now recurse subtree */
         db_recurse_record_tree(hDB, (POINTER_T) pkey - (POINTER_T) pheader,
                                data, total_size, base_align, NULL, bSet, convert_flags);

         corr = VALIGN(*total_size, align) - *total_size;
         *total_size += corr;
         if (data)
            *data = (void *) ((char *) (*data) + corr);
      }
         
      if (pold) {
         pkey = pold;
         pold = NULL;
      }

      if (!pkey->next_key)
         break;

      // FIXME: validate pkey->next_key
      pkey = (KEY *) ((char *) pheader + pkey->next_key);
   } while (TRUE);
}

#endif                          /* LOCAL_ROUTINES */

/**dox***************************************************************/
#endif                          /* DOXYGEN_SHOULD_SKIP_THIS */

/********************************************************************/
/**
Calculates the size of a record.
@param hDB          ODB handle obtained via cm_get_experiment_database().
@param hKey Handle for key where search starts, zero for root.
@param align Byte alignment calculated by the stub and
              passed to the rpc side to align data
              according to local machine. Must be zero
              when called from user level
@param buf_size Size of record structure
@return DB_SUCCESS, DB_INVALID_HANDLE, DB_TYPE_MISMATCH,
DB_STRUCT_SIZE_MISMATCH, DB_NO_KEY
*/
INT db_get_record_size(HNDLE hDB, HNDLE hKey, INT align, INT * buf_size)
{
   if (rpc_is_remote()) {
      align = ss_get_struct_align();
      return rpc_call(RPC_DB_GET_RECORD_SIZE, hDB, hKey, align, buf_size);
   }
#ifdef LOCAL_ROUTINES
   {
      KEY key;
      INT status, max_align;

      if (!align)
         align = ss_get_struct_align();

      /* check if key has subkeys */
      status = db_get_key(hDB, hKey, &key);
      if (status != DB_SUCCESS)
         return status;

      if (key.type != TID_KEY) {
         /* just a single key */
         *buf_size = key.item_size * key.num_values;
         return DB_SUCCESS;
      }

      db_lock_database(hDB);

      /* determine record size */
      *buf_size = max_align = 0;
      db_recurse_record_tree(hDB, hKey, NULL, buf_size, align, &max_align, 0, 0);

      /* correct for byte padding */
      *buf_size = VALIGN(*buf_size, max_align);

      db_unlock_database(hDB);
   }
#endif                          /* LOCAL_ROUTINES */

   return DB_SUCCESS;
}

/********************************************************************/
/**
Copy a set of keys to local memory.

An ODB sub-tree can be mapped to a C structure automatically via a
hot-link using the function db_open_record() or manually with this function.
Problems might occur if the ODB sub-tree contains values which don't match the
C structure. Although the structure size is checked against the sub-tree size, no
checking can be done if the type and order of the values in the structure are the
same than those in the ODB sub-tree. Therefore it is recommended to use the
function db_create_record() before db_get_record() is used which
ensures that both are equivalent.
\code
struct {
  INT level1;
  INT level2;
} trigger_settings;
char *trigger_settings_str =
"[Settings]\n\
level1 = INT : 0\n\
level2 = INT : 0";

main()
{
  HNDLE hDB, hkey;
  INT   size;
  ...
  cm_get_experiment_database(&hDB, NULL);
  db_create_record(hDB, 0, "/Equipment/Trigger", trigger_settings_str);
  db_find_key(hDB, 0, "/Equipment/Trigger/Settings", &hkey);
  size = sizeof(trigger_settings);
  db_get_record(hDB, hkey, &trigger_settings, &size, 0);
  ...
}
\endcode
@param hDB          ODB handle obtained via cm_get_experiment_database().
@param hKey         Handle for key where search starts, zero for root.
@param data         Pointer to the retrieved data.
@param buf_size     Size of data structure, must be obtained via sizeof(RECORD-NAME).
@param align        Byte alignment calculated by the stub and
                    passed to the rpc side to align data
                    according to local machine. Must be zero
                    when called from user level.
@return DB_SUCCESS, DB_INVALID_HANDLE, DB_STRUCT_SIZE_MISMATCH
*/
INT db_get_record(HNDLE hDB, HNDLE hKey, void *data, INT * buf_size, INT align)
{
   if (rpc_is_remote()) {
      align = ss_get_struct_align();
      return rpc_call(RPC_DB_GET_RECORD, hDB, hKey, data, buf_size, align);
   }
#ifdef LOCAL_ROUTINES
   {
      KEY key;
      INT convert_flags, status;
      INT total_size;
      void *pdata;
      char str[256];

      convert_flags = 0;

      if (!align)
         align = ss_get_struct_align();
      else {
         /* only convert data if called remotely, as indicated by align != 0 */
         if (rpc_is_mserver()) {
            convert_flags = rpc_get_server_option(RPC_CONVERT_FLAGS);
         }
      }

      /* check if key has subkeys */
      status = db_get_key(hDB, hKey, &key);
      if (status != DB_SUCCESS)
         return status;

      if (key.type != TID_KEY) {
         /* copy single key */
         if (key.item_size * key.num_values != *buf_size) {
            db_get_path(hDB, hKey, str, sizeof(str));
            cm_msg(MERROR, "db_get_record", "struct size mismatch for \"%s\" (expected size: %d, size in ODB: %d)", str, *buf_size, key.item_size * key.num_values);
            return DB_STRUCT_SIZE_MISMATCH;
         }

         db_get_data(hDB, hKey, data, buf_size, key.type);

         if (convert_flags) {
            if (key.num_values > 1)
               rpc_convert_data(data, key.type, RPC_OUTGOING | RPC_FIXARRAY, key.item_size * key.num_values, convert_flags);
            else
               rpc_convert_single(data, key.type, RPC_OUTGOING, convert_flags);
         }

         return DB_SUCCESS;
      }

      /* check record size */
      db_get_record_size(hDB, hKey, align, &total_size);
      if (total_size != *buf_size) {
         db_get_path(hDB, hKey, str, sizeof(str));
         cm_msg(MERROR, "db_get_record", "struct size mismatch for \"%s\" (expected size: %d, size in ODB: %d)", str, *buf_size, total_size);
         return DB_STRUCT_SIZE_MISMATCH;
      }

      /* get subkey data */
      pdata = data;
      total_size = 0;

      db_lock_database(hDB);
      db_recurse_record_tree(hDB, hKey, &pdata, &total_size, align, NULL, FALSE, convert_flags);
      db_unlock_database(hDB);

   }
#endif                          /* LOCAL_ROUTINES */

   return DB_SUCCESS;
}

/********************************************************************/
/**
Same as db_get_record() but if there is a record mismatch between ODB structure
and C record, it is automatically corrected by calling db_check_record()

@param hDB          ODB handle obtained via cm_get_experiment_database().
@param hKey         Handle for key where search starts, zero for root.
@param data         Pointer to the retrieved data.
@param buf_size     Size of data structure, must be obtained via sizeof(RECORD-NAME).
@param align        Byte alignment calculated by the stub and
                    passed to the rpc side to align data
                    according to local machine. Must be zero
                    when called from user level.
@param rec_str      ASCII representation of ODB record in the format
@return DB_SUCCESS, DB_INVALID_HANDLE, DB_STRUCT_SIZE_MISMATCH
*/
INT db_get_record1(HNDLE hDB, HNDLE hKey, void *data, INT * buf_size, INT align, const char *rec_str)
{
   int size = *buf_size;
   int odb_size = 0;
   int status;
   char path[MAX_ODB_PATH];

   /* check record size first */

   status = db_get_record_size(hDB, hKey, align, &odb_size);
   if (status != DB_SUCCESS)
      return status;

   /* if size mismatch, call repair function */

   if (odb_size != size) {
      db_get_path(hDB, hKey, path, sizeof(path));
      cm_msg(MINFO, "db_get_record1", "Fixing ODB \"%s\" struct size mismatch (expected %d, odb size %d)", path, size, odb_size);
      status = db_create_record(hDB, hKey, "", rec_str);
      if (status != DB_SUCCESS)
         return status;
   }

   /* run db_get_record(), if success, we are done */

   status = db_get_record(hDB, hKey, data, buf_size, align);
   if (status == DB_SUCCESS)
      return status;

   /* try repair with db_check_record() */

   status = db_check_record(hDB, hKey, "", rec_str, TRUE);
   if (status != DB_SUCCESS)
      return status;

   /* verify struct size again, because there can still be a mismatch if there
    * are extra odb entries at the end of the record as db_check_record()
    * seems to ignore all odb entries past the end of "rec_str". K.O.
    */

   status = db_get_record_size(hDB, hKey, align, &odb_size);
   if (status != DB_SUCCESS)
      return status;

   db_get_path(hDB, hKey, path, sizeof(path));

   if (odb_size != size) {
      cm_msg(MERROR, "db_get_record1", "after db_check_record() still struct size mismatch (expected %d, odb size %d) of \"%s\", calling db_create_record()", size, odb_size, path);
      status = db_create_record(hDB, hKey, "", rec_str);
      if (status != DB_SUCCESS)
         return status;
   }

   cm_msg(MERROR, "db_get_record1", "repaired struct size mismatch of \"%s\"", path);

   *buf_size = size;
   status = db_get_record(hDB, hKey, data, buf_size, align);

   return status;
}

static int db_parse_record(const char* rec_str, const char** out_rec_str, char* title, int title_size, char* key_name, int key_name_size, int* tid, int* n_data, int* string_length)
{
   title[0] = 0;
   key_name[0] = 0;
   *tid = 0;
   *n_data = 0;
   *string_length = 0;
   *out_rec_str = NULL;

   //
   // expected format of rec_str:
   //
   // title: "[.]",
   // numeric value: "example_int = INT : 3",
   // string value: "example_string = STRING : [20] /Runinfo/Run number",
   // array: "aaa = INT[10] : ...",
   // string array: "sarr = STRING[10] : [32] ",
   //

   //printf("parse_rec_str: [%s]\n", rec_str);

   while (*rec_str == '\n')
      rec_str++;
   
   /* check if it is a section title */
   if (rec_str[0] == '[') {
      rec_str++;

      title[0] = 0;
      
      /* extract title and append '/' */
      strlcpy(title, rec_str, title_size);
      char* p = strchr(title, ']');
      if (p)
         *p = 0;

      int len = strlen(title);
      if (len > 0) {
         if (title[len - 1] != '/')
            strlcat(title, "/", title_size);
      }

      // skip to the next end-of-line
      const char* pend = strchr(rec_str, '\n');
      if (pend)
         rec_str = pend;
      else
         rec_str = rec_str+strlen(rec_str);

      while (*rec_str == '\n')
         rec_str++;

      *out_rec_str = rec_str;
      return DB_SUCCESS;
   }

   if (rec_str[0] == ';') {
      // skip to the next end-of-line
      const char* pend = strchr(rec_str, '\n');
      if (pend)
         rec_str = pend;
      else
         rec_str = rec_str+strlen(rec_str);

      while (*rec_str == '\n')
         rec_str++;

      *out_rec_str = rec_str;
      return DB_SUCCESS;
   }

   const char* peq = strchr(rec_str, '=');
   if (!peq) {
      cm_msg(MERROR, "db_parse_record", "do not see \'=\'");
      return DB_INVALID_PARAM;
   }

   int key_name_len = peq - rec_str;

   // remove trailing equals sign and trailing spaces
   while (key_name_len > 1) {
      if (rec_str[key_name_len-1] == '=') {
         key_name_len--;
         continue;
      }
      if (rec_str[key_name_len-1] == ' ') {
         key_name_len--;
         continue;
      }
      break;
   }

   memcpy(key_name, rec_str, key_name_len);
   key_name[key_name_len] = 0;

   rec_str = peq + 1; // consume the "=" sign

   while (*rec_str == ' ') // consume spaces
      rec_str++;

   // extract type id
   char stid[256];
   int i;
   for (i=0; i<(int)sizeof(stid)-1; i++) {
      char s = *rec_str;
      if (s == 0)    break;
      if (s == ' ')  break;
      if (s == '\n') break;
      if (s == '[')  break;
      stid[i] = s;
      rec_str++;
   }
   stid[i] = 0;

   DWORD xtid = 0;
   for (xtid = 0; xtid < TID_LAST; xtid++) {
      if (strcmp(rpc_tid_name(xtid), stid) == 0) {
         *tid = xtid;
         break;
      }
   }

   //printf("tid [%s], tid %d\n", stid, *tid);

   if (xtid == TID_LAST) {
      cm_msg(MERROR, "db_parse_record", "do not see \':\'");
      return DB_INVALID_PARAM;
   }
      
   while (*rec_str == ' ') // consume spaces
      rec_str++;

   *n_data = 1;

   if (*rec_str == '[') {
      // decode array size
      rec_str++; // cosume the '['
      *n_data = atoi(rec_str);
      const char *pbr = strchr(rec_str, ']');
      if (!pbr) {
         cm_msg(MERROR, "db_parse_record", "do not see closing bracket \']\'");
         return DB_INVALID_PARAM;
      }
      rec_str = pbr + 1; // skip the closing bracket
   }
   
   while (*rec_str == ' ') // consume spaces
      rec_str++;

   const char* pcol = strchr(rec_str, ':');
   if (!pcol) {
      cm_msg(MERROR, "db_parse_record", "do not see \':\'");
      return DB_INVALID_PARAM;
   }

   rec_str = pcol + 1; // skip the ":"

   while (*rec_str == ' ') // consume spaces
      rec_str++;

   *string_length = 0;
   if (xtid == TID_LINK || xtid == TID_STRING) {
      // extract string length
      const char* pbr = strchr(rec_str, '[');
      if (pbr) {
         *string_length = atoi(pbr+1);
      }
   }

   // skip to the next end-of-line
   const char* pend = strchr(rec_str, '\n');
   if (pend)
      rec_str = pend;
   else
      rec_str = rec_str+strlen(rec_str);
   
   while (*rec_str == '\n')
      rec_str++;
   
   *out_rec_str = rec_str;
   return DB_SUCCESS;
}

static int db_get_record2_read_element(HNDLE hDB, HNDLE hKey, const char* key_name, int tid, int n_data, int string_length, char* buf_start, char** buf_ptr, int* buf_remain, BOOL correct)
{
   assert(tid > 0);
   assert(n_data > 0);
   int tsize = rpc_tid_size(tid);
   int offset = *buf_ptr - buf_start;
   int align = 0;
   if (tsize && (offset%tsize != 0)) {
      while (offset%tsize != 0) {
         align++;
         *(*buf_ptr) = 0xFF; // pad bytes for correct data alignement
         (*buf_ptr)++;
         (*buf_remain)--;
         offset++;
      }
   }
   printf("read element [%s] tid %d, n_data %d, string_length %d, tid_size %d, align %d, offset %d, buf_remain %d\n", key_name, tid, n_data, string_length, tsize, align, offset, *buf_remain);
   if (tsize > 0) {
      int xsize = tsize*n_data;
      if (xsize > *buf_remain) {
         cm_msg(MERROR, "db_get_record2", "buffer overrun at key \"%s\", size %d, buffer remaining %d", key_name, xsize, *buf_remain);
         return DB_INVALID_PARAM;
      }
      int ysize = xsize;
      int status = db_get_value(hDB, hKey, key_name, *buf_ptr, &ysize, tid, FALSE);
      //printf("status %d, xsize %d\n", status, xsize);
      if (status != DB_SUCCESS) {
         cm_msg(MERROR, "db_get_record2", "cannot read \"%s\", db_get_value() status %d", key_name, status);
         memset(*buf_ptr, 0, xsize);
         *buf_ptr += xsize;
         *buf_remain -= xsize;
         return status;
      }
      *buf_ptr += xsize;
      *buf_remain -= xsize;
   } else if (tid == TID_STRING) {
      int xstatus = 0;
      int i;
      for (i=0; i<n_data; i++) {
         int xsize = string_length;
         if (xsize > *buf_remain) {
            cm_msg(MERROR, "db_get_record2", "string buffer overrun at key \"%s\" index %d, size %d, buffer remaining %d", key_name, i, xsize, *buf_remain);
            return DB_INVALID_PARAM;
         }
         char xkey_name[MAX_ODB_PATH+100];
         sprintf(xkey_name, "%s[%d]", key_name, i);
         int status = db_get_value(hDB, hKey, xkey_name, *buf_ptr, &xsize, tid, FALSE);
         //printf("status %d, string length %d, xsize %d, actual len %d\n", status, string_length, xsize, (int)strlen(*buf_ptr));
         if (status == DB_TRUNCATED) {
            // make sure string is NUL terminated
            (*buf_ptr)[string_length-1] = 0;
            cm_msg(MERROR, "db_get_record2", "string key \"%s\" index %d, string value was truncated", key_name, i);
         } else if (status != DB_SUCCESS) {
            cm_msg(MERROR, "db_get_record2", "cannot read string \"%s\"[%d], db_get_value() status %d", key_name, i, status);
            memset(*buf_ptr, 0, string_length);
            xstatus = status;
         }
         *buf_ptr += string_length;
         *buf_remain -= string_length;
      }
      if (xstatus != 0) {
         return xstatus;
      }
   } else {
      cm_msg(MERROR, "db_get_record2", "cannot read key \"%s\" of unsupported type %d", key_name, tid);
      return DB_INVALID_PARAM;
   }
   return DB_SUCCESS;
}

/********************************************************************/
/**
Copy a set of keys to local memory.

An ODB sub-tree can be mapped to a C structure automatically via a
hot-link using the function db_open_record1() or manually with this function.
For correct operation, the description string *must* match the C data
structure. If the contents of ODB sub-tree does not exactly match
the description string, db_get_record2() will try to read as much as it can
and return DB_TRUNCATED to inform the user that there was a mismatch somewhere.
To ensure that the ODB sub-tree matches the desciption string, call db_create_record()
or db_check_record() before calling db_get_record2(). Unlike db_get_record()
and db_get_record1(), this function will not complain about data strucure mismatches.
It will ignore all extra entries in the ODB sub-tree and it will set to zero the C-structure
data fields that do not have corresponding ODB entries.
\code
struct {
  INT level1;
  INT level2;
} trigger_settings;
const char *trigger_settings_str =
"[Settings]\n\
level1 = INT : 0\n\
level2 = INT : 0";

main()
{
  HNDLE hDB, hkey;
  INT   size;
  ...
  cm_get_experiment_database(&hDB, NULL);
  db_create_record(hDB, 0, "/Equipment/Trigger", trigger_settings_str);
  db_find_key(hDB, 0, "/Equipment/Trigger/Settings", &hkey);
  size = sizeof(trigger_settings);
  db_get_record2(hDB, hkey, &trigger_settings, &size, 0, trigger_settings_str);
  ...
}
\endcode
@param hDB          ODB handle obtained via cm_get_experiment_database().
@param hKey         Handle for key where search starts, zero for root.
@param data         Pointer to the retrieved data.
@param buf_size     Size of data structure, must be obtained via sizeof(data).
@param align        Byte alignment calculated by the stub and
                    passed to the rpc side to align data
                    according to local machine. Must be zero
                    when called from user level.
@param rec_str      Description of the data structure, see db_create_record()
@param correct      Must be set to zero
@return DB_SUCCESS, DB_INVALID_HANDLE, DB_STRUCT_SIZE_MISMATCH
*/
INT db_get_record2(HNDLE hDB, HNDLE hKey, void *data, INT * xbuf_size, INT align, const char *rec_str, BOOL correct)
{
   int status = DB_SUCCESS;

   printf("db_get_record2!\n");

   assert(data != NULL);
   assert(xbuf_size != NULL);
   assert(*xbuf_size > 0);
   assert(correct == 0);

   int truncated = 0;
#if 1
   char* r1 = NULL;
   int rs = *xbuf_size;
   if (1) {
      r1 = (char*)malloc(rs);
      memset(data, 0xFF, *xbuf_size);
      memset(r1, 0xFF, rs);
      //status = db_get_record1(hDB, hKey, r1, &rs, 0, rec_str);
      status = db_get_record(hDB, hKey, r1, &rs, 0);
      printf("db_get_record status %d\n", status);
   }
#endif
      
   char* buf_start = (char*)data;
   int buf_size = *xbuf_size;

   char* buf_ptr = buf_start;
   int buf_remain = buf_size;

   while (rec_str && *rec_str != 0) {
      char title[256];
      char key_name[MAX_ODB_PATH];
      int tid = 0;
      int n_data = 0;
      int string_length = 0;
      const char* rec_str_next = NULL;
      
      status = db_parse_record(rec_str, &rec_str_next, title, sizeof(title), key_name, sizeof(key_name), &tid, &n_data, &string_length);

      //printf("parse [%s], status %d, title [%s], key_name [%s], tid %d, n_data %d, string_length %d, next [%s]\n", rec_str, status, title, key_name, tid, n_data, string_length, rec_str_next);

      rec_str = rec_str_next;

      if (status != DB_SUCCESS) {
         return status;
      }

      if (key_name[0] == 0) {
         // skip title strings, comments, etc
         continue;
      }
      
      status = db_get_record2_read_element(hDB, hKey, key_name, tid, n_data, string_length, buf_start, &buf_ptr, &buf_remain, correct);
      if (status == DB_INVALID_PARAM) {
         cm_msg(MERROR, "db_get_record2", "error: cannot continue reading odb record because of previous fatal error, status %d", status);
         return DB_INVALID_PARAM;
      } if (status != DB_SUCCESS) {
         truncated = 1;
      }

      rec_str = rec_str_next;
   }

   if (r1) {
      int ok = -1;
      int i;
      for (i=0; i<rs; i++) {
         if (r1[i] != buf_start[i]) {
            ok = i;
            break;
         }
      }
      if (ok>=0 || buf_remain>0) {
         printf("db_get_record2: miscompare at %d out of %d, buf_remain %d\n", ok, rs, buf_remain);
      } else {
         printf("db_get_record2: check ok\n");
      }
   }
   
   if (buf_remain > 0) {
      // FIXME: we finished processing the data definition string, but unused space remains in the buffer
      return DB_TRUNCATED;
   }

   if (truncated)
      return DB_TRUNCATED;
   else
      return DB_SUCCESS;
}

/********************************************************************/
/**
Copy a set of keys from local memory to the database.

An ODB sub-tree can be mapped to a C structure automatically via a
hot-link using the function db_open_record() or manually with this function.
Problems might occur if the ODB sub-tree contains values which don't match the
C structure. Although the structure size is checked against the sub-tree size, no
checking can be done if the type and order of the values in the structure are the
same than those in the ODB sub-tree. Therefore it is recommended to use the
function db_create_record() before using this function.
\code
...
  memset(&lazyst,0,size);
  if (db_find_key(hDB, pLch->hKey, "Statistics",&hKeyst) == DB_SUCCESS)
    status = db_set_record(hDB, hKeyst, &lazyst, size, 0);
  else
    cm_msg(MERROR,"task","record %s/statistics not found", pLch->name)
...
\endcode
@param hDB          ODB handle obtained via cm_get_experiment_database().
@param hKey Handle for key where search starts, zero for root.
@param data Pointer where data is stored.
@param buf_size Size of data structure, must be obtained via sizeof(RECORD-NAME).
@param align  Byte alignment calculated by the stub and
              passed to the rpc side to align data
              according to local machine. Must be zero
              when called from user level.
@return DB_SUCCESS, DB_INVALID_HANDLE, DB_TYPE_MISMATCH, DB_STRUCT_SIZE_MISMATCH
*/
INT db_set_record(HNDLE hDB, HNDLE hKey, void *data, INT buf_size, INT align)
{
   if (rpc_is_remote()) {
      align = ss_get_struct_align();
      return rpc_call(RPC_DB_SET_RECORD, hDB, hKey, data, buf_size, align);
   }
#ifdef LOCAL_ROUTINES
   {
      KEY key;
      INT convert_flags;
      INT total_size;
      void *pdata;

      convert_flags = 0;

      if (!align)
         align = ss_get_struct_align();
      else {
         /* only convert data if called remotely, as indicated by align != 0 */
         if (rpc_is_mserver()) {
            convert_flags = rpc_get_server_option(RPC_CONVERT_FLAGS);
         }
      }

      /* check if key has subkeys */
      db_get_key(hDB, hKey, &key);
      if (key.type != TID_KEY) {
         /* copy single key */
         if (key.item_size * key.num_values != buf_size) {
            cm_msg(MERROR, "db_set_record", "struct size mismatch for \"%s\"", key.name);
            return DB_STRUCT_SIZE_MISMATCH;
         }

         if (convert_flags) {
            if (key.num_values > 1)
               rpc_convert_data(data, key.type, RPC_FIXARRAY, key.item_size * key.num_values, convert_flags);
            else
               rpc_convert_single(data, key.type, 0, convert_flags);
         }

         db_set_data(hDB, hKey, data, key.total_size, key.num_values, key.type);
         return DB_SUCCESS;
      }

      /* check record size */
      db_get_record_size(hDB, hKey, align, &total_size);
      if (total_size != buf_size) {
         cm_msg(MERROR, "db_set_record", "struct size mismatch for \"%s\"", key.name);
         return DB_STRUCT_SIZE_MISMATCH;
      }

      /* set subkey data */
      pdata = data;
      total_size = 0;

      db_lock_database(hDB);
      db_allow_write_locked(&_database[hDB-1], "db_set_record");
      db_recurse_record_tree(hDB, hKey, &pdata, &total_size, align, NULL, TRUE, convert_flags);
      db_unlock_database(hDB);
   }
#endif                          /* LOCAL_ROUTINES */

   return DB_SUCCESS;
}

/**dox***************************************************************/
#ifndef DOXYGEN_SHOULD_SKIP_THIS

/*------------------------------------------------------------------*/
INT db_add_open_record(HNDLE hDB, HNDLE hKey, WORD access_mode)
/********************************************************************\

  Routine: db_add_open_record

  Purpose: Server part of db_open_record. Internal use only.

\********************************************************************/
{
   if (rpc_is_remote())
      return rpc_call(RPC_DB_ADD_OPEN_RECORD, hDB, hKey, access_mode);

#ifdef LOCAL_ROUTINES
   {
      DATABASE_HEADER *pheader;
      DATABASE_CLIENT *pclient;
      KEY *pkey;
      INT i;

      if (hDB > _database_entries || hDB <= 0) {
         cm_msg(MERROR, "db_add_open_record", "invalid database handle");
         return DB_INVALID_HANDLE;
      }

      /* lock database */
      db_lock_database(hDB);

      pheader = _database[hDB - 1].database_header;
      pclient = &pheader->client[_database[hDB - 1].client_index];

      /* check if key is already open */
      for (i = 0; i < pclient->max_index; i++)
         if (pclient->open_record[i].handle == hKey)
            break;

      if (i < pclient->max_index) {
         db_unlock_database(hDB);
         return DB_SUCCESS;
      }

      /* not found, search free entry */
      for (i = 0; i < pclient->max_index; i++)
         if (pclient->open_record[i].handle == 0)
            break;

      /* check if maximum number reached */
      if (i == MAX_OPEN_RECORDS) {
         db_unlock_database(hDB);
         return DB_NO_MEMORY;
      }

      db_allow_write_locked(&_database[hDB-1], "db_add_open_record");

      if (i == pclient->max_index)
         pclient->max_index++;

      pclient->open_record[i].handle = hKey;
      pclient->open_record[i].access_mode = access_mode;

      /* check if hKey argument is correct */
      if (!db_validate_hkey(pheader, hKey)) {
         db_unlock_database(hDB);
         return DB_INVALID_HANDLE;
      }

      pkey = (KEY *) ((char *) pheader + hKey);

      /* check if pkey is correct */
      if (!db_validate_pkey(pheader, pkey)) {
         db_unlock_database(hDB);
         return DB_INVALID_HANDLE;
      }

      /* increment notify_count */
      pkey->notify_count++;

      pclient->num_open_records++;

      /* set exclusive bit if requested */
      if (access_mode & MODE_WRITE)
         db_set_mode(hDB, hKey, (WORD) (pkey->access_mode | MODE_EXCLUSIVE), 2);

      db_unlock_database(hDB);
   }
#endif                          /* LOCAL_ROUTINES */

   return DB_SUCCESS;
}

/*------------------------------------------------------------------*/

INT db_remove_open_record(HNDLE hDB, HNDLE hKey, BOOL lock)
/********************************************************************\

  Routine: db_remove_open_record

  Purpose: Gets called by db_close_record. Internal use only.

\********************************************************************/
{
   if (rpc_is_remote())
      return rpc_call(RPC_DB_REMOVE_OPEN_RECORD, hDB, hKey, lock);

#ifdef LOCAL_ROUTINES
   {
      DATABASE_HEADER *pheader;
      DATABASE_CLIENT *pclient;
      KEY *pkey;
      INT i, idx;

      if (hDB > _database_entries || hDB <= 0) {
         cm_msg(MERROR, "db_remove_open_record", "invalid database handle");
         return DB_INVALID_HANDLE;
      }

      if (lock)
         db_lock_database(hDB);

      pheader = _database[hDB - 1].database_header;
      pclient = &pheader->client[_database[hDB - 1].client_index];

      /* search key */
      for (idx = 0; idx < pclient->max_index; idx++)
         if (pclient->open_record[idx].handle == hKey)
            break;

      if (idx == pclient->max_index) {
         if (lock)
            db_unlock_database(hDB);
         return DB_INVALID_HANDLE;
      }

      /* check if hKey argument is correct */
      if (!db_validate_hkey(pheader, hKey)) {
         if (lock)
            db_unlock_database(hDB);
         return DB_INVALID_HANDLE;
      }

      /* decrement notify_count */
      pkey = (KEY *) ((char *) pheader + hKey);

      db_allow_write_locked(&_database[hDB-1], "db_remove_open_record");

      if (pkey->notify_count > 0)
         pkey->notify_count--;

      pclient->num_open_records--;

      /* remove exclusive flag */
      if (pclient->open_record[idx].access_mode & MODE_WRITE)
         db_set_mode(hDB, hKey, (WORD) (pkey->access_mode & ~MODE_EXCLUSIVE), 2);

      memset(&pclient->open_record[idx], 0, sizeof(OPEN_RECORD));

      /* calculate new max_index entry */
      for (i = pclient->max_index - 1; i >= 0; i--)
         if (pclient->open_record[i].handle != 0)
            break;
      pclient->max_index = i + 1;

      if (lock)
         db_unlock_database(hDB);
   }
#endif                          /* LOCAL_ROUTINES */

   return DB_SUCCESS;
}

/*------------------------------------------------------------------*/

#ifdef LOCAL_ROUTINES

INT db_notify_clients(HNDLE hDB, HNDLE hKeyMod, int index, BOOL bWalk)
/********************************************************************\

  Routine: db_notify_clients

  Purpose: Gets called by db_set_xxx functions. Internal use only.

\********************************************************************/
{
   DATABASE_HEADER *pheader;
   DATABASE_CLIENT *pclient;
   HNDLE hKey;
   KEY *pkey;
   KEYLIST *pkeylist;
   INT i, j;
   char str[80];

   if (hDB > _database_entries || hDB <= 0) {
      cm_msg(MERROR, "db_notify_clients", "invalid database handle");
      return DB_INVALID_HANDLE;
   }

   pheader = _database[hDB - 1].database_header;
   hKey = hKeyMod;

   /* check if hKey argument is correct */
   if (!db_validate_hkey(pheader, hKey)) {
      return DB_INVALID_HANDLE;
   }
   
   /* check if key or parent has notify_flag set */
   pkey = (KEY *) ((char *) pheader + hKey);

   do {

      /* check which client has record open */
      if (pkey->notify_count)
         for (i = 0; i < pheader->max_client_index; i++) {
            pclient = &pheader->client[i];
            for (j = 0; j < pclient->max_index; j++)
               if (pclient->open_record[j].handle == hKey) {
                  /* send notification to remote process */
                  sprintf(str, "O %d %d %d %d", hDB, hKey, hKeyMod, index);
                  ss_resume(pclient->port, str);
               }
         }

      if (pkey->parent_keylist == 0 || !bWalk)
         return DB_SUCCESS;

      pkeylist = (KEYLIST *) ((char *) pheader + pkey->parent_keylist);
      // FIXME: validate pkeylist->parent
      pkey = (KEY *) ((char *) pheader + pkeylist->parent);
      hKey = (POINTER_T) pkey - (POINTER_T) pheader;
   } while (TRUE);

}

#endif                          /* LOCAL_ROUTINES */
/*------------------------------------------------------------------*/

INT db_notify_clients_array(HNDLE hDB, HNDLE hKeys[], INT size)
/********************************************************************\
 
 Routine: db_notify_clients_array
 
 Purpose: This function is typically called after a set of calls
          to db_set_data1 which omits hot-link notification to 
          programs. After several ODB values are modified in a set,
          this function has to be called to trigger the hot-links
          of the whole set.
 
 \********************************************************************/
{
   if (rpc_is_remote())
      return rpc_call(RPC_DB_NOTIFY_CLIENTS_ARRAY, hDB, hKeys, size);
   
#ifdef LOCAL_ROUTINES
   {
      db_lock_database(hDB);
      int count = size/sizeof(INT);
      for (int i=0 ; i<count; i++) {
         db_notify_clients(hDB, hKeys[i], -1, TRUE);
      }
      db_unlock_database(hDB);
   }
#endif
   return DB_SUCCESS;
}

/*------------------------------------------------------------------*/
static void merge_records(HNDLE hDB, HNDLE hKey, KEY * pkey, INT level, void *info)
{
   char full_name[MAX_ODB_PATH];
   INT status, size;
   HNDLE hKeyInit;
   KEY initkey, key;

   /* avoid compiler warnings */
   status = level;

   /* compose name of init key */
   db_get_path(hDB, hKey, full_name, sizeof(full_name));
   *strchr(full_name, 'O') = 'I';

   /* if key in init record found, copy original data to init data */
   status = db_find_key(hDB, 0, full_name, &hKeyInit);
   if (status == DB_SUCCESS) {
      status = db_get_key(hDB, hKeyInit, &initkey);
      if (status != DB_SUCCESS) {
         cm_msg(MERROR, "merge_records", "merge_record error at \'%s\', db_get_key() status %d", full_name, status);
         return;
      }
      status = db_get_key(hDB, hKey, &key);
      if (status != DB_SUCCESS) {
         cm_msg(MERROR, "merge_records", "merge_record error at \'%s\', second db_get_key() status %d", full_name, status);
         return;
      }

      if (initkey.type != TID_KEY && initkey.type == key.type) {
         char* allocbuffer = NULL;
         char  stackbuffer[10000];
         char* buffer = stackbuffer;
         size = sizeof(stackbuffer);
         while (1) {
            /* copy data from original key to new key */
            status = db_get_data(hDB, hKey, buffer, &size, initkey.type);
            if (status == DB_SUCCESS) {
               status = db_set_data(hDB, hKeyInit, buffer, initkey.total_size, initkey.num_values, initkey.type);
               if (status != DB_SUCCESS) {
                  cm_msg(MERROR, "merge_records", "merge_record error at \'%s\', db_set_data() status %d", full_name, status);
                  return;
               }
               break;
            }
            if (status == DB_TRUNCATED) {
               size *= 2;
               allocbuffer = (char *)realloc(allocbuffer, size);
               assert(allocbuffer != NULL);
               buffer = allocbuffer;
               continue;
            }
            cm_msg(MERROR, "merge_records", "aborting on unexpected failure of db_get_data(%s), status %d", full_name, status);
            abort();
         }
         if (allocbuffer)
            free(allocbuffer);
      }
   } else if (status == DB_NO_KEY) {
      /* do nothing */
   } else if (status == DB_INVALID_LINK) {
      status = db_find_link(hDB, 0, full_name, &hKeyInit);
      if (status == DB_SUCCESS) {
         size = sizeof(full_name);
         status = db_get_data(hDB, hKeyInit, full_name, &size, TID_LINK);
      }
      cm_msg(MERROR, "merge_records", "Invalid link \"%s\"", full_name);
   } else {
      cm_msg(MERROR, "merge_records", "aborting on unexpected failure of db_find_key(%s), status %d", full_name, status);
      abort();
   }
}

static int _global_open_count; // FIXME: this is not thread-safe

static void check_open_keys(HNDLE hDB, HNDLE hKey, KEY * pkey, INT level, void *info)
{
   if (pkey->notify_count)
      _global_open_count++;
}

/**dox***************************************************************/
#endif                          /* DOXYGEN_SHOULD_SKIP_THIS */

/********************************************************************/
/**
Create a record. If a part of the record exists alreay,
merge it with the init_str (use values from the init_str only when they are
not in the existing record).

This functions creates a ODB sub-tree according to an ASCII
representation of that tree. See db_copy() for a description. It can be used to
create a sub-tree which exactly matches a C structure. The sub-tree can then
later mapped to the C structure ("hot-link") via the function db_open_record().

If a sub-tree exists already which exactly matches the ASCII representation, it is
not modified. If part of the tree exists, it is merged with the ASCII representation
where the ODB values have priority, only values not present in the ODB are
created with the default values of the ASCII representation. It is therefore
recommended that before creating an ODB hot-link the function
db_create_record() is called to insure that the ODB tree and the C structure
contain exactly the same values in the same order.

Following example creates a record under /Equipment/Trigger/Settings,
opens a hot-link between that record and a local C structure
trigger_settings and registers a callback function trigger_update()
which gets called each time the record is changed.
\code
struct {
  INT level1;
  INT level2;
} trigger_settings;
char *trigger_settings_str =
"[Settings]\n\
level1 = INT : 0\n\
level2 = INT : 0";
void trigger_update(INT hDB, INT hkey, void *info)
{
  printf("New levels: %d %d\n",
    trigger_settings.level1,
    trigger_settings.level2);
}
main()
{
  HNDLE hDB, hkey;
  char[128] info;
  ...
  cm_get_experiment_database(&hDB, NULL);
  db_create_record(hDB, 0, "/Equipment/Trigger", trigger_settings_str);
  db_find_key(hDB, 0,"/Equipment/Trigger/Settings", &hkey);
  db_open_record(hDB, hkey, &trigger_settings,
    sizeof(trigger_settings), MODE_READ, trigger_update, info);
  ...
}
\endcode
@param hDB          ODB handle obtained via cm_get_experiment_database().
@param hKey         Handle for key where search starts, zero for root.
@param orig_key_name     Name of key to search, can contain directories.
@param init_str     Initialization string in the format of the db_copy/db_save functions.
@return DB_SUCCESS, DB_INVALID_HANDLE, DB_FULL, DB_NO_ACCESS, DB_OPEN_RECORD
*/
INT db_create_record(HNDLE hDB, HNDLE hKey, const char *orig_key_name, const char *init_str)
{
   char str[256], key_name[256], *buffer;
   INT status, size, i, buffer_size;
   HNDLE hKeyTmp, hKeyTmpO, hKeyOrig, hSubkey;

   if (rpc_is_remote())
      return rpc_call(RPC_DB_CREATE_RECORD, hDB, hKey, orig_key_name, init_str);

   /* make this function atomic */
   db_lock_database(hDB);

   /* strip trailing '/' */
   strlcpy(key_name, orig_key_name, sizeof(key_name));
   if (strlen(key_name) > 1 && key_name[strlen(key_name) - 1] == '/')
      key_name[strlen(key_name) - 1] = 0;

   /* merge temporay record and original record */
   status = db_find_key(hDB, hKey, key_name, &hKeyOrig);
   if (status == DB_SUCCESS) {
      assert(hKeyOrig != 0);
#ifdef CHECK_OPEN_RECORD
      /* check if key or subkey is opened */
      _global_open_count = 0; // FIXME: this is not thread safe
      db_scan_tree_link(hDB, hKeyOrig, 0, check_open_keys, NULL);
      if (_global_open_count) {
         db_unlock_database(hDB);
         return DB_OPEN_RECORD;
      }
#endif
      /* create temporary records */
      sprintf(str, "/System/Tmp/%sI", ss_tid_to_string(ss_gettid()).c_str());
      //printf("db_create_record str [%s]\n", str);
      db_find_key(hDB, 0, str, &hKeyTmp);
      if (hKeyTmp)
         db_delete_key(hDB, hKeyTmp, FALSE);
      db_create_key(hDB, 0, str, TID_KEY);
      status = db_find_key(hDB, 0, str, &hKeyTmp);
      if (status != DB_SUCCESS) {
         db_unlock_database(hDB);
         return status;
      }

      sprintf(str, "/System/Tmp/%sO", ss_tid_to_string(ss_gettid()).c_str());
      //printf("db_create_record str [%s]\n", str);
      db_find_key(hDB, 0, str, &hKeyTmpO);
      if (hKeyTmpO)
         db_delete_key(hDB, hKeyTmpO, FALSE);
      db_create_key(hDB, 0, str, TID_KEY);
      status = db_find_key(hDB, 0, str, &hKeyTmpO);
      if (status != DB_SUCCESS) {
         db_unlock_database(hDB);
         return status;
      }

      status = db_paste(hDB, hKeyTmp, init_str);
      if (status != DB_SUCCESS) {
         db_unlock_database(hDB);
         return status;
      }

      buffer_size = 10000;
      buffer = (char *) malloc(buffer_size);
      do {
         size = buffer_size;
         status = db_copy(hDB, hKeyOrig, buffer, &size, "");
         if (status == DB_TRUNCATED) {
            buffer_size += 10000;
            buffer = (char *) realloc(buffer, buffer_size);
            continue;
         }
         if (status != DB_SUCCESS) {
            db_unlock_database(hDB);
            return status;
         }

      } while (status != DB_SUCCESS);

      status = db_paste(hDB, hKeyTmpO, buffer);
      if (status != DB_SUCCESS) {
         free(buffer);
         db_unlock_database(hDB);
         return status;
      }

      /* merge temporay record and original record */
      db_scan_tree_link(hDB, hKeyTmpO, 0, merge_records, NULL);

      /* delete original record */
      for (i = 0;; i++) {
         db_enum_link(hDB, hKeyOrig, 0, &hSubkey);
         if (!hSubkey)
            break;

         status = db_delete_key(hDB, hSubkey, FALSE);
         if (status != DB_SUCCESS) {
            free(buffer);
            db_unlock_database(hDB);
            return status;
         }
      }

      /* copy merged record to original record */
      do {
         size = buffer_size;
         status = db_copy(hDB, hKeyTmp, buffer, &size, "");
         if (status == DB_TRUNCATED) {
            buffer_size += 10000;
            buffer = (char *) realloc(buffer, buffer_size);
            continue;
         }
         if (status != DB_SUCCESS) {
            free(buffer);
            db_unlock_database(hDB);
            return status;
         }

      } while (status != DB_SUCCESS);

      status = db_paste(hDB, hKeyOrig, buffer);
      if (status != DB_SUCCESS) {
         free(buffer);
         db_unlock_database(hDB);
         return status;
      }

      /* delete temporary records */
      db_delete_key(hDB, hKeyTmp, FALSE);
      db_delete_key(hDB, hKeyTmpO, FALSE);

      free(buffer);
      buffer = NULL;
   } else if (status == DB_NO_KEY) {
      /* create fresh record */
      db_create_key(hDB, hKey, key_name, TID_KEY);
      status = db_find_key(hDB, hKey, key_name, &hKeyTmp);
      if (status != DB_SUCCESS) {
         db_unlock_database(hDB);
         return status;
      }

      status = db_paste(hDB, hKeyTmp, init_str);
      if (status != DB_SUCCESS) {
         db_unlock_database(hDB);
         return status;
      }
   } else {
      cm_msg(MERROR, "db_create_record", "aborting on unexpected failure of db_find_key(%s), status %d", key_name, status);
      abort();
   }

   db_unlock_database(hDB);

   return DB_SUCCESS;
}

/********************************************************************/
/**
This function ensures that a certain ODB subtree matches
a given C structure, by comparing the init_str with the
current ODB structure. If the record does not exist at all,
it is created with the default values in init_str. If it
does exist but does not match the variables in init_str,
the function returns an error if correct=FALSE or calls
db_create_record() if correct=TRUE.
@param hDB          ODB handle obtained via cm_get_experiment_database().
@param hKey     Handle for key where search starts, zero for root.
@param keyname  Name of key to search, can contain directories.
@param rec_str  ASCII representation of ODB record in the format
@param correct  If TRUE, correct ODB record if necessary
@return DB_SUCCESS, DB_INVALID_HANDLE, DB_NO_KEY, DB_STRUCT_MISMATCH
*/
INT db_check_record(HNDLE hDB, HNDLE hKey, const char *keyname, const char *rec_str, BOOL correct)
{
   char line[MAX_STRING_LENGTH];
   char title[MAX_STRING_LENGTH];
   char key_name[MAX_STRING_LENGTH];
   char info_str[MAX_STRING_LENGTH + 50];
   char *pc;
   const char *pold, *rec_str_orig;
   DWORD tid;
   INT i, j, n_data, string_length, status;
   HNDLE hKeyRoot, hKeyTest;
   KEY key;
   int bad_string_length;

   if (rpc_is_remote())
      return rpc_call(RPC_DB_CHECK_RECORD, hDB, hKey, keyname, rec_str, correct);

   /* check if record exists */
   status = db_find_key(hDB, hKey, keyname, &hKeyRoot);

   /* create record if not */
   if (status == DB_NO_KEY) {
      if (correct)
         return db_create_record(hDB, hKey, keyname, rec_str);
      return DB_NO_KEY;
   }

   assert(hKeyRoot);

   title[0] = 0;
   rec_str_orig = rec_str;

   db_get_key(hDB, hKeyRoot, &key);
   if (key.type == TID_KEY)
      /* get next subkey which is not of type TID_KEY */
      db_get_next_link(hDB, hKeyRoot, &hKeyTest);
   else
      /* test root key itself */
      hKeyTest = hKeyRoot;

   if (hKeyTest == 0 && *rec_str != 0) {
      if (correct)
         return db_create_record(hDB, hKey, keyname, rec_str_orig);

      return DB_STRUCT_MISMATCH;
   }

   do {
      if (*rec_str == 0)
         break;

      for (i = 0; *rec_str != '\n' && *rec_str && i < MAX_STRING_LENGTH; i++)
         line[i] = *rec_str++;

      if (i == MAX_STRING_LENGTH) {
         cm_msg(MERROR, "db_check_record", "line too long");
         return DB_TRUNCATED;
      }

      line[i] = 0;
      if (*rec_str == '\n')
         rec_str++;

      /* check if it is a section title */
      if (line[0] == '[') {
         /* extract title and append '/' */
         strcpy(title, line + 1);
         if (strchr(title, ']'))
            *strchr(title, ']') = 0;
         if (title[0] && title[strlen(title) - 1] != '/')
            strcat(title, "/");
      } else {
         /* valid data line if it includes '=' and no ';' */
         if (strchr(line, '=') && line[0] != ';') {
            /* copy type info and data */
            pc = strchr(line, '=') + 1;
            while (*pc == ' ')
               pc++;
            strcpy(info_str, pc);

            /* extract key name */
            *strchr(line, '=') = 0;

            pc = &line[strlen(line) - 1];
            while (*pc == ' ')
               *pc-- = 0;

            strlcpy(key_name, line, sizeof(key_name));

            /* evaluate type info */
            strcpy(line, info_str);
            if (strchr(line, ' '))
               *strchr(line, ' ') = 0;

            n_data = 1;
            if (strchr(line, '[')) {
               n_data = atol(strchr(line, '[') + 1);
               *strchr(line, '[') = 0;
            }

            for (tid = 0; tid < TID_LAST; tid++)
               if (strcmp(rpc_tid_name(tid), line) == 0)
                  break;

            string_length = 0;

            if (tid == TID_LAST)
               cm_msg(MERROR, "db_check_record", "found unknown data type \"%s\" in ODB file", line);
            else {
               /* skip type info */
               pc = info_str;
               while (*pc != ' ' && *pc)
                  pc++;
               while ((*pc == ' ' || *pc == ':') && *pc)
                  pc++;

               if (n_data > 1) {
                  info_str[0] = 0;
                  if (!*rec_str)
                     break;

                  for (j = 0; *rec_str != '\n' && *rec_str; j++)
                     info_str[j] = *rec_str++;
                  info_str[j] = 0;
                  if (*rec_str == '\n')
                     rec_str++;
               }

               for (i = 0; i < n_data; i++) {
                  /* strip trailing \n */
                  pc = &info_str[strlen(info_str) - 1];
                  while (*pc == '\n' || *pc == '\r')
                     *pc-- = 0;

                  if (tid == TID_STRING || tid == TID_LINK) {
                     if (!string_length) {
                        if (info_str[1] == '=')
                           string_length = -1;
                        else {
                           pc = strchr(info_str, '[');
                           if (pc != NULL)
                              string_length = atoi(pc + 1);
                           else
                              string_length = -1;
                        }
                        if (string_length > MAX_STRING_LENGTH) {
                           string_length = MAX_STRING_LENGTH;
                           cm_msg(MERROR, "db_check_record", "found string exceeding MAX_STRING_LENGTH");
                        }
                     }

                     if (string_length == -1) {
                        /* multi-line string */
                        if (strstr(rec_str, "\n====#$@$#====\n") != NULL) {
                           string_length = (POINTER_T) strstr(rec_str, "\n====#$@$#====\n") - (POINTER_T) rec_str + 1;

                           rec_str = strstr(rec_str, "\n====#$@$#====\n") + strlen("\n====#$@$#====\n");
                        } else
                           cm_msg(MERROR, "db_check_record", "found multi-line string without termination sequence");
                     } else {
                        if (strchr(info_str, ']'))
                           pc = strchr(info_str, ']') + 1;
                        else
                           pc = info_str + 2;
                        while (*pc && *pc != ' ')
                           pc++;
                        while (*pc && *pc == ' ')
                           pc++;

                        /* limit string size */
                        *(pc + string_length - 1) = 0;
                     }
                  } else {
                     pc = info_str;

                     if (n_data > 1 && info_str[0] == '[') {
                        pc = strchr(info_str, ']') + 1;
                        while (*pc && *pc == ' ')
                           pc++;
                     }
                  }

                  if (i < n_data - 1) {
                     info_str[0] = 0;
                     if (!*rec_str)
                        break;

                     pold = rec_str;

                     for (j = 0; *rec_str != '\n' && *rec_str; j++)
                        info_str[j] = *rec_str++;
                     info_str[j] = 0;
                     if (*rec_str == '\n')
                        rec_str++;

                     /* test if valid data */
                     if (tid != TID_STRING && tid != TID_LINK) {
                        if (info_str[0] == 0 || (strchr(info_str, '=')
                                                 && strchr(info_str, ':')))
                           rec_str = pold;
                     }
                  }
               }

               /* if rec_str contains key, but not ODB, return mismatch */
               if (!hKeyTest) {
                  if (correct)
                     return db_create_record(hDB, hKey, keyname, rec_str_orig);

                  return DB_STRUCT_MISMATCH;
               }

               status = db_get_key(hDB, hKeyTest, &key);
               assert(status == DB_SUCCESS);

               bad_string_length = 0;

               if (key.type == TID_STRING) {
                  //printf("key name [%s], tid %d/%d, num_values %d/%d, string length %d/%d\n", key_name, key.type, tid, key.num_values, n_data, string_length, key.item_size);
                  if (string_length > 0 && string_length != key.item_size) {
                     bad_string_length = 1;
                  }
               }

               /* check rec_str vs. ODB key */
               if (!equal_ustring(key.name, key_name) || key.type != tid || key.num_values != n_data || bad_string_length) {
                  //printf("miscompare key name [%s], tid %d/%d, num_values %d/%d, string length %d/%d\n", key_name, key.type, tid, key.num_values, n_data, key.item_size, string_length);
                  if (correct)
                     return db_create_record(hDB, hKey, keyname, rec_str_orig);

                  return DB_STRUCT_MISMATCH;
               }

               /* get next key in ODB */
               db_get_next_link(hDB, hKeyTest, &hKeyTest);
            }
         }
      }
   } while (TRUE);

   return DB_SUCCESS;
}

/********************************************************************/
/**
Open a record. Create a local copy and maintain an automatic update.

This function opens a hot-link between an ODB sub-tree and a local
structure. The sub-tree is copied to the structure automatically every time it is
modified by someone else. Additionally, a callback function can be declared
which is called after the structure has been updated. The callback function
receives the database handle and the key handle as parameters.

Problems might occur if the ODB sub-tree contains values which don't match the
C structure. Although the structure size is checked against the sub-tree size, no
checking can be done if the type and order of the values in the structure are the
same than those in the ODB sub-tree. Therefore it is recommended to use the
function db_create_record() before db_open_record() is used which
ensures that both are equivalent.

The access mode might either be MODE_READ or MODE_WRITE. In read mode,
the ODB sub-tree is automatically copied to the local structure when modified by
other clients. In write mode, the local structure is copied to the ODB sub-tree if it
has been modified locally. This update has to be manually scheduled by calling
db_send_changed_records() periodically in the main loop. The system
keeps a copy of the local structure to determine if its contents has been changed.

If MODE_ALLOC is or'ed with the access mode, the memory for the structure is
allocated internally. The structure pointer must contain a pointer to a pointer to
the structure. The internal memory is released when db_close_record() is
called.
- To open a record in write mode.
\code
struct {
  INT level1;
  INT level2;
} trigger_settings;
char *trigger_settings_str =
"[Settings]\n\
level1 = INT : 0\n\
level2 = INT : 0";
main()
{
  HNDLE hDB, hkey, i=0;
  ...
  cm_get_experiment_database(&hDB, NULL);
  db_create_record(hDB, 0, "/Equipment/Trigger", trigger_settings_str);
  db_find_key(hDB, 0,"/Equipment/Trigger/Settings", &hkey);
  db_open_record(hDB, hkey, &trigger_settings, sizeof(trigger_settings)
                  , MODE_WRITE, NULL);
  do
  {
    trigger_settings.level1 = i++;
    db_send_changed_records()
    status = cm_yield(1000);
  } while (status != RPC_SHUTDOWN && status != SS_ABORT);
  ...
}
\endcode
@param hDB          ODB handle obtained via cm_get_experiment_database().
@param hKey         Handle for key where search starts, zero for root.
@param ptr          If access_mode includes MODE_ALLOC:
                    Address of pointer which points to the record data after
                    the call if access_mode includes not MODE_ALLOC:
                    Address of record if ptr==NULL, only the dispatcher is called.
@param rec_size     Record size in bytes
@param access_mode Mode for opening record, either MODE_READ or
                    MODE_WRITE. May be or'ed with MODE_ALLOC to
                    let db_open_record allocate the memory for the record.
@param (*dispatcher)   Function which gets called when record is updated.The
                    argument list composed of: HNDLE hDB, HNDLE hKey, void *info
@param info Additional info passed to the dispatcher.
@return DB_SUCCESS, DB_INVALID_HANDLE, DB_NO_MEMORY, DB_NO_ACCESS, DB_STRUCT_SIZE_MISMATCH
*/
INT db_open_record(HNDLE hDB, HNDLE hKey, void *ptr, INT rec_size,
                   WORD access_mode, void (*dispatcher) (INT, INT, void *), void *info)
{
   INT idx, status, size;
   KEY key;
   void *data;
   char str[256];

   /* allocate new space for the local record list */
   if (_record_list_entries == 0) {
      _record_list = (RECORD_LIST *) malloc(sizeof(RECORD_LIST));
      memset(_record_list, 0, sizeof(RECORD_LIST));
      if (_record_list == NULL) {
         cm_msg(MERROR, "db_open_record", "not enough memory");
         return DB_NO_MEMORY;
      }

      _record_list_entries = 1;
      idx = 0;
   } else {
      /* check for a deleted entry */
      for (idx = 0; idx < _record_list_entries; idx++)
         if (!_record_list[idx].handle)
            break;

      /* if not found, create new one */
      if (idx == _record_list_entries) {
         _record_list = (RECORD_LIST *) realloc(_record_list, sizeof(RECORD_LIST) * (_record_list_entries + 1));
         if (_record_list == NULL) {
            cm_msg(MERROR, "db_open_record", "not enough memory");
            return DB_NO_MEMORY;
         }

         memset(&_record_list[_record_list_entries], 0, sizeof(RECORD_LIST));

         _record_list_entries++;
      }
   }

   db_get_key(hDB, hKey, &key);
 
   /* check record size */
   status = db_get_record_size(hDB, hKey, 0, &size);
   if (status != DB_SUCCESS) {
      _record_list_entries--;
      cm_msg(MERROR, "db_open_record", "cannot get record size, db_get_record_size() status %d", status);
      return DB_NO_MEMORY;
   }

   if (size != rec_size && ptr != NULL) {
      _record_list_entries--;
      db_get_path(hDB, hKey, str, sizeof(str));
      cm_msg(MERROR, "db_open_record", "struct size mismatch for \"%s\" (expected size: %d, size in ODB: %d)", str, rec_size, size);
      return DB_STRUCT_SIZE_MISMATCH;
   }

   /* check for read access */
   if (((key.access_mode & MODE_EXCLUSIVE) && (access_mode & MODE_WRITE))
       || (!(key.access_mode & MODE_WRITE) && (access_mode & MODE_WRITE))
       || (!(key.access_mode & MODE_READ) && (access_mode & MODE_READ))) {
      _record_list_entries--;
      return DB_NO_ACCESS;
   }

   if (access_mode & MODE_ALLOC) {
      data = malloc(size);

      if (data == NULL) {
         _record_list_entries--;
         cm_msg(MERROR, "db_open_record", "not enough memory, malloc(%d) returned NULL", size);
         return DB_NO_MEMORY;
      }

      memset(data, 0, size);

      *((void **) ptr) = data;
   } else {
      data = ptr;
   }

   /* copy record to local memory */
   if (access_mode & MODE_READ && data != NULL) {
      status = db_get_record(hDB, hKey, data, &size, 0);
      if (status != DB_SUCCESS) {
         _record_list_entries--;
         cm_msg(MERROR, "db_open_record", "cannot get record, db_get_record() status %d", status);
         return DB_NO_MEMORY;
      }
   }

   /* copy local record to ODB */
   if (access_mode & MODE_WRITE) {
      /* only write to ODB if not in MODE_ALLOC */
      if ((access_mode & MODE_ALLOC) == 0) {
         status = db_set_record(hDB, hKey, data, size, 0);
         if (status != DB_SUCCESS) {
            _record_list_entries--;
            cm_msg(MERROR, "db_open_record", "cannot set record, db_set_record() status %d", status);
            return DB_NO_MEMORY;
         }
      }

      /* init a local copy of the record */
      _record_list[idx].copy = malloc(size);
      if (_record_list[idx].copy == NULL) {
         cm_msg(MERROR, "db_open_record", "not enough memory");
         return DB_NO_MEMORY;
      }

      memcpy(_record_list[idx].copy, data, size);
   }

   /* initialize record list */
   _record_list[idx].handle = hKey;
   _record_list[idx].hDB = hDB;
   _record_list[idx].access_mode = access_mode;
   _record_list[idx].data = data;
   _record_list[idx].buf_size = size;
   _record_list[idx].dispatcher = dispatcher;
   _record_list[idx].info = info;

   /* add record entry in database structure */
   return db_add_open_record(hDB, hKey, (WORD) (access_mode & ~MODE_ALLOC));
}

/********************************************************************/
/**
Open a record. Create a local copy and maintain an automatic update.

This function is same as db_open_record(), except that it calls
db_check_record(), db_get_record1() and db_create_record()
to ensure that the ODB structure matches

Parameters are the same as for db_open_record():

@param hDB          ODB handle obtained via cm_get_experiment_database().
@param hKey         Handle for key where search starts, zero for root.
@param ptr          If access_mode includes MODE_ALLOC:
                    Address of pointer which points to the record data after
                    the call if access_mode includes not MODE_ALLOC:
                    Address of record if ptr==NULL, only the dispatcher is called.
@param rec_size     Record size in bytes
@param access_mode Mode for opening record, either MODE_READ or
                    MODE_WRITE. May be or'ed with MODE_ALLOC to
                    let db_open_record allocate the memory for the record.
@param (*dispatcher)   Function which gets called when record is updated.The
                    argument list composed of: HNDLE hDB, HNDLE hKey, void *info
@param info Additional info passed to the dispatcher.
@param rec_str  ASCII representation of ODB record in the format
@return DB_SUCCESS, DB_INVALID_HANDLE, DB_NO_MEMORY, DB_NO_ACCESS, DB_STRUCT_SIZE_MISMATCH
*/
INT db_open_record1(HNDLE hDB, HNDLE hKey, void *ptr, INT rec_size,
                    WORD access_mode, void (*dispatcher) (INT, INT, void *), void *info,
                     const char *rec_str)
{
   if (rec_str) {
      int status;
      if (rec_size) {
         char* pbuf;
         int size = rec_size;
         pbuf = (char*)malloc(size);
         assert(pbuf != NULL);
         status = db_get_record1(hDB, hKey, pbuf, &size, 0, rec_str);
         free(pbuf);
         if (status != DB_SUCCESS)
            return status;
      }

      status = db_check_record(hDB, hKey, "", rec_str, TRUE);
      if (status != DB_SUCCESS)
         return status;
   }

   return db_open_record(hDB, hKey, ptr, rec_size, access_mode, dispatcher, info);
}

/********************************************************************/
/**
Close a record previously opend with db_open_record.
@param hDB          ODB handle obtained via cm_get_experiment_database().
@param hKey Handle for key where search starts, zero for root.
@return DB_SUCCESS, DB_INVALID_HANDLE
*/
INT db_close_record(HNDLE hDB, HNDLE hKey)
{
#ifdef LOCAL_ROUTINES
   {
      INT i;

      for (i = 0; i < _record_list_entries; i++)
         if (_record_list[i].handle == hKey && _record_list[i].hDB == hDB)
            break;

      if (i == _record_list_entries)
         return DB_INVALID_HANDLE;

      /* remove record entry from database structure */
      db_remove_open_record(hDB, hKey, TRUE);

      /* free local memory */
      if (_record_list[i].access_mode & MODE_ALLOC) {
         free(_record_list[i].data);
         _record_list[i].data = NULL;
      }

      if (_record_list[i].access_mode & MODE_WRITE) {
         free(_record_list[i].copy);
         _record_list[i].copy = NULL;
      }

      memset(&_record_list[i], 0, sizeof(RECORD_LIST));
   }
#endif                          /* LOCAL_ROUTINES */

   return DB_SUCCESS;
}

/********************************************************************/
/**
Release local memory for open records.
This routines is called by db_close_all_databases() and
cm_disconnect_experiment()
@return DB_SUCCESS, DB_INVALID_HANDLE
*/
INT db_close_all_records()
{
   INT i;

   for (i = 0; i < _record_list_entries; i++) {
      if (_record_list[i].handle) {
         if (_record_list[i].access_mode & MODE_WRITE) {
            free(_record_list[i].copy);
            _record_list[i].copy = NULL;
         }

         if (_record_list[i].access_mode & MODE_ALLOC) {
            free(_record_list[i].data);
            _record_list[i].data = NULL;
         }

         memset(&_record_list[i], 0, sizeof(RECORD_LIST));
      }
   }

   if (_record_list_entries > 0) {
      _record_list_entries = 0;
      free(_record_list);
      _record_list = NULL;
   }

   return DB_SUCCESS;
}

/********************************************************************/
/**
db_open_record() and db_watch() event handler

@param hDB          ODB handle obtained via cm_get_experiment_database().
@param hKey         Handle for key which changed.
@param index        Index for array keys.
@return DB_SUCCESS, DB_INVALID_HANDLE
*/
INT db_update_record_local(INT hDB, INT hKeyRoot, INT hKey, int index)
{
   INT i, size, status;

   status = DB_INVALID_HANDLE;

   /* check all record entries for matching key */
   for (i = 0; i < _record_list_entries; i++)
      if (_record_list[i].handle == hKeyRoot) {
         status = DB_SUCCESS;

         /* get updated data if record not opened in write mode */
         if ((_record_list[i].access_mode & MODE_WRITE) == 0) {
            size = _record_list[i].buf_size;
            if (_record_list[i].data != NULL) {
               status = db_get_record(hDB, hKeyRoot, _record_list[i].data, &size, 0); // db_open_record() update
               //printf("db_open_record update status %d, size %d %d\n", status, _record_list[i].buf_size, size);
            }
            
            /* call dispatcher if requested */
            if (_record_list[i].dispatcher)
               _record_list[i].dispatcher(hDB, hKeyRoot, _record_list[i].info);
         }
      }

   /* check all watch entries for matching key */
   for (i = 0; i < _watch_list_entries; i++)
      if (_watch_list[i].handle == hKeyRoot) {
         status = DB_SUCCESS;
         
         /* call dispatcher if requested */
         if (_watch_list[i].dispatcher)
            _watch_list[i].dispatcher(hDB, hKey, index, _watch_list[i].info);
      }

   return status;
}

/********************************************************************/
/**
Relay db_open_record() and db_watch() notification to the remote client.
@param hDB          ODB handle obtained via cm_get_experiment_database().
@param hKey         Handle for key which changed.
@param index        Index for array keys.
@param s            client socket.
@return DB_SUCCESS, DB_INVALID_HANDLE
*/
INT db_update_record_mserver(INT hDB, INT hKeyRoot, INT hKey, int index, int client_socket)
{
   char buffer[32];

   int convert_flags = rpc_get_server_option(RPC_CONVERT_FLAGS);
   
   NET_COMMAND* nc = (NET_COMMAND *) buffer;
   
   nc->header.routine_id = MSG_ODB;
   nc->header.param_size = 4 * sizeof(INT);
   *((INT *) nc->param) = hDB;
   *((INT *) nc->param + 1) = hKeyRoot;
   *((INT *) nc->param + 2) = hKey;
   *((INT *) nc->param + 3) = index;
   
   if (convert_flags) {
      rpc_convert_single(&nc->header.routine_id, TID_DWORD, RPC_OUTGOING, convert_flags);
      rpc_convert_single(&nc->header.param_size, TID_DWORD, RPC_OUTGOING, convert_flags);
      rpc_convert_single(&nc->param[0], TID_DWORD, RPC_OUTGOING, convert_flags);
      rpc_convert_single(&nc->param[4], TID_DWORD, RPC_OUTGOING, convert_flags);
      rpc_convert_single(&nc->param[8], TID_DWORD, RPC_OUTGOING, convert_flags);
      rpc_convert_single(&nc->param[12], TID_DWORD, RPC_OUTGOING, convert_flags);
   }
   
   /* send the update notification to the client */
   send_tcp(client_socket, buffer, sizeof(NET_COMMAND_HEADER) + 4 * sizeof(INT), 0);
   
   return DB_SUCCESS;
}

/********************************************************************/
/**
Send all records to the ODB which were changed locally since the last
call to this function.

This function is valid if used in conjunction with db_open_record()
under the condition the record is open as MODE_WRITE access code.

- Full example dbchange.c which can be compiled as follow
\code
gcc -DOS_LINUX -I/midas/include -o dbchange dbchange.c
/midas/linux/lib/libmidas.a -lutil}

\begin{verbatim}
//------- dbchange.c
#include "midas.h"
#include "msystem.h"
\endcode

\code
//-------- BOF dbchange.c
typedef struct {
    INT    my_number;
    float   my_rate;
} MY_STATISTICS;

MY_STATISTICS myrec;

#define MY_STATISTICS(_name) char *_name[] = {\
"My Number = INT : 0",\
"My Rate = FLOAT : 0",\
"",\
NULL }

HNDLE hDB, hKey;

// Main
int main(unsigned int argc,char **argv)
{
  char      host_name[HOST_NAME_LENGTH];
  char      expt_name[HOST_NAME_LENGTH];
  INT       lastnumber, status, msg;
  BOOL      debug=FALSE;
  char      i, ch;
  DWORD     update_time, mainlast_time;
  MY_STATISTICS (my_stat);

  // set default
  host_name[0] = 0;
  expt_name[0] = 0;

  // get default
  cm_get_environment(host_name, sizeof(host_name), expt_name, sizeof(expt_name));

  // get parameters
  for (i=1 ; i<argc ; i++)
  {
    if (argv[i][0] == '-' && argv[i][1] == 'd')
      debug = TRUE;
    else if (argv[i][0] == '-')
    {
      if (i+1 >= argc || argv[i+1][0] == '-')
        goto usage;
      if (strncmp(argv[i],"-e",2) == 0)
        strcpy(expt_name, argv[++i]);
      else if (strncmp(argv[i],"-h",2)==0)
        strcpy(host_name, argv[++i]);
    }
    else
    {
   usage:
      printf("usage: dbchange [-h <Hostname>] [-e <Experiment>]\n");
      return 0;
    }
  }

  // connect to experiment
  status = cm_connect_experiment(host_name, expt_name, "dbchange", 0);
  if (status != CM_SUCCESS)
    return 1;

  // Connect to DB
  cm_get_experiment_database(&hDB, &hKey);

  // Create a default structure in ODB
  db_create_record(hDB, 0, "My statistics", strcomb(my_stat));

  // Retrieve key for that strucutre in ODB
  if (db_find_key(hDB, 0, "My statistics", &hKey) != DB_SUCCESS)
  {
    cm_msg(MERROR, "mychange", "cannot find My statistics");
    goto error;
  }

  // Hot link this structure in Write mode
  status = db_open_record(hDB, hKey, &myrec, sizeof(MY_STATISTICS), MODE_WRITE, NULL, NULL);
  if (status != DB_SUCCESS)
  {
    cm_msg(MERROR, "mychange", "cannot open My statistics record");
    goto error;
  }

  // initialize ss_getchar()
  ss_getchar(0);

  // Main loop
  do
  {
    // Update local structure
    if ((ss_millitime() - update_time) > 100)
    {
      myrec.my_number += 1;
      if (myrec.my_number - lastnumber) {
        myrec.my_rate = 1000.f * (float) (myrec.my_number - lastnumber)
          / (float) (ss_millitime() - update_time);
      }
      update_time = ss_millitime();
      lastnumber = myrec.my_number;
    }

    // Publish local structure to ODB (db_send_changed_record)
    if ((ss_millitime() - mainlast_time) > 5000)
    {
      db_send_changed_records();                   // <------- Call
      mainlast_time = ss_millitime();
    }

    // Check for keyboard interaction
    ch = 0;
    while (ss_kbhit())
    {
      ch = ss_getchar(0);
      if (ch == -1)
        ch = getchar();
      if ((char) ch == '!')
        break;
    }
    msg = cm_yield(20);
  } while (msg != RPC_SHUTDOWN && msg != SS_ABORT && ch != '!');

 error:
  cm_disconnect_experiment();
  return 1;
}
//-------- EOF dbchange.c
\endcode
@return DB_SUCCESS
*/
INT db_send_changed_records()
{
   INT i;

   for (i = 0; i < _record_list_entries; i++)
      if (_record_list[i].access_mode & MODE_WRITE) {
         if (memcmp(_record_list[i].copy, _record_list[i].data, _record_list[i].buf_size) != 0) {
            if (rpc_is_remote()) {
               int align = ss_get_struct_align();
               rpc_call(RPC_DB_SET_RECORD|RPC_NO_REPLY, _record_list[i].hDB, _record_list[i].handle, _record_list[i].data, _record_list[i].buf_size, align);
            } else {
               db_set_record(_record_list[i].hDB, _record_list[i].handle, _record_list[i].data, _record_list[i].buf_size, 0);
            }
            memcpy(_record_list[i].copy, _record_list[i].data, _record_list[i].buf_size);
         }
      }

   return DB_SUCCESS;
}

/*------------------------------------------------------------------*/

/********************************************************************/
/**
 Watch an ODB subtree. The callback function gets called whenever a
 key in the watched subtree changes. The callback function
 receives the database handle and the key handle as parameters.
 
 @param hDB          ODB handle obtained via cm_get_experiment_database().
 @param hKey         Handle for key at top of subtree to watch, zero for root.
 @param (*dispatcher)   Function which gets called when record is updated.The
 argument list composed of: HNDLE hDB, HNDLE hKey
 @return DB_SUCCESS, DB_INVALID_HANDLE, DB_NO_MEMORY, DB_NO_ACCESS, DB_STRUCT_SIZE_MISMATCH
 */
INT db_watch(HNDLE hDB, HNDLE hKey, void (*dispatcher) (INT, INT, INT, void*), void* info)
{
   INT idx, status;
   KEY key;
   char str[256];
   
   /* check for valid key */
   assert(hKey);
   
   /* allocate new space for the local record list */
   if (_watch_list_entries == 0) {
      _watch_list = (WATCH_LIST *) malloc(sizeof(WATCH_LIST));
      memset(_watch_list, 0, sizeof(WATCH_LIST));
      if (_watch_list == NULL) {
         cm_msg(MERROR, "db_watch", "not enough memory");
         return DB_NO_MEMORY;
      }
      
      _watch_list_entries = 1;
      idx = 0;
   } else {
      /* check for a deleted entry */
      for (idx = 0; idx < _watch_list_entries; idx++)
         if (!_watch_list[idx].handle)
            break;
      
      /* if not found, create new one */
      if (idx == _watch_list_entries) {
         _watch_list = (WATCH_LIST *) realloc(_watch_list, sizeof(WATCH_LIST) * (_watch_list_entries + 1));
         if (_watch_list == NULL) {
            cm_msg(MERROR, "db_watch", "not enough memory");
            return DB_NO_MEMORY;
         }
         
         memset(&_watch_list[_watch_list_entries], 0, sizeof(WATCH_LIST));
         
         _watch_list_entries++;
      }
   }
   
   /* check key */
   status = db_get_key(hDB, hKey, &key);
   if (status != DB_SUCCESS) {
      _watch_list_entries--;
      db_get_path(hDB, hKey, str, sizeof(str));
      cm_msg(MERROR, "db_watch", "cannot get key %s", str);
      return DB_NO_MEMORY;
   }
   
   /* check for read access */
   if (!(key.access_mode & MODE_READ)) {
      _watch_list_entries--;
      db_get_path(hDB, hKey, str, sizeof(str));
      cm_msg(MERROR, "db_watch", "cannot get key %s", str);
      return DB_NO_ACCESS;
   }
   
   /* initialize record list */
   _watch_list[idx].handle = hKey;
   _watch_list[idx].hDB = hDB;
   _watch_list[idx].dispatcher = dispatcher;
   _watch_list[idx].info = info;
   
   /* add record entry in database structure */
   return db_add_open_record(hDB, hKey, MODE_WATCH);
}


/********************************************************************/
/**
 Remove watch callback from a key previously watched with db_watch.
 @param hDB          ODB handle obtained via cm_get_experiment_database().
 @param hKey         Handle for key, zero for root.
 @return DB_SUCCESS, DB_INVALID_HANDLE
 */
INT db_unwatch(HNDLE hDB, HNDLE hKey)
{
#ifdef LOCAL_ROUTINES
   {
   INT i;
   
   for (i = 0; i < _watch_list_entries; i++)
      if (_watch_list[i].handle == hKey && _watch_list[i].hDB == hDB)
         break;
   
   if (i == _watch_list_entries)
      return DB_INVALID_HANDLE;
   
   /* remove record entry from database structure */
   db_remove_open_record(hDB, hKey, TRUE);
   
   memset(&_watch_list[i], 0, sizeof(WATCH_LIST));
   }
#endif                          /* LOCAL_ROUTINES */
   
   return DB_SUCCESS;
}

/********************************************************************/
/**
 Closes all watched variables.
 This routines is called by db_close_all_databases() and
 cm_disconnect_experiment()
 @return DB_SUCCESS, DB_INVALID_HANDLE
 */
INT db_unwatch_all()
{
   INT i;
   
   for (i = _watch_list_entries-1; i >= 0 ; i--)
      db_unwatch(_watch_list[i].hDB, _watch_list[i].handle);
   
   return DB_SUCCESS;
}

/*------------------------------------------------------------------*/

/* C++ wrapper for db_get_value */

INT EXPRT db_get_value_string(HNDLE hdb, HNDLE hKeyRoot, const char *key_name, int index, std::string* s, BOOL create, int create_string_length)
{
   int status;
   int hkey;

   //printf("db_get_value_string: key_name [%s], index %d, string [%s], create %d, create_string_length %d\n", key_name, index, s->c_str(), create, create_string_length);

   if (index > 0 && create) {
      cm_msg(MERROR, "db_get_value_string", "cannot resize odb string arrays, please use db_resize_string() instead");
      return DB_OUT_OF_RANGE;
   }

   status = db_find_key(hdb, hKeyRoot, key_name, &hkey);
   if (status == DB_SUCCESS) {
      KEY key;
      status = db_get_key(hdb, hkey, &key);
      if (status != DB_SUCCESS)
         return status;
      if (index < 0 || index >= key.num_values)
         return DB_OUT_OF_RANGE;
      int size = key.item_size;
      if (size == 0) {
         if (s)
            *s = "";
         //printf("db_get_value_string: return empty string, item_size %d\n", key.item_size);
         return DB_SUCCESS;
      }
      char* buf = (char*)malloc(size);
      assert(buf != NULL);
      status = db_get_data_index(hdb, hkey, buf, &size, index, TID_STRING);
      if (status != DB_SUCCESS) {
         free(buf);
         return status;
      }
      if (s)
         *s = buf;
      free(buf);
      //printf("db_get_value_string: return [%s], len %d, item_size %d, size %d\n", s->c_str(), s->length(), key.item_size, size);
      return DB_SUCCESS;
   } else if (!create) {
      // does not exist and not asked to create it
      return status;
   } else {
      //printf("db_get_value_string: creating [%s]\n", key_name);
      status = db_create_key(hdb, hKeyRoot, key_name, TID_STRING);
      if (status != DB_SUCCESS)
         return status;
      status = db_find_key(hdb, hKeyRoot, key_name, &hkey);
      if (status != DB_SUCCESS)
         return status;
      assert(s != NULL);
      if (create_string_length == 0) {
         int size = s->length() + 1; // 1 byte for final \0
         status = db_set_data_index(hdb, hkey, s->c_str(), size, index, TID_STRING);
      } else {
         char* buf = (char*)malloc(create_string_length);
         assert(buf);
         strlcpy(buf, s->c_str(), create_string_length);
         status = db_set_data_index(hdb, hkey, buf, create_string_length, index, TID_STRING);
         free(buf);
      }
      if (status != DB_SUCCESS)
         return status;
      //printf("db_get_value_string: created with value [%s]\n", s->c_str());
      return DB_SUCCESS;
   }
   // NOT REACHED
}

/* C++ wrapper for db_set_value */

INT EXPRT db_set_value_string(HNDLE hDB, HNDLE hKeyRoot, const char *key_name, const std::string* s)
{
   assert(s != NULL);
   int size = s->length() + 1; // 1 byte for final \0
   //printf("db_set_value_string: key_name [%s], string [%s], size %d\n", key_name, s->c_str(), size);
   return db_set_value(hDB, hKeyRoot, key_name, s->c_str(), size, 1, TID_STRING);
}

/********************************************************************/
/**
Change size of string arrays.

This function can change the number of elements and the string element length of an array of strings.
@param hDB  ODB handle obtained via cm_get_experiment_database().
@param hKey Handle for key where search starts, zero for root.
@param key_name Odb key name, if NULL, will resize ODB entry pointed to by hKey
@param num_values New number of array elements, if 0, remains unchanged
@param max_string_length New max string length for array elements, if 0, remains unchanged
@return DB_SUCCESS, or error from db_find_key, db_create_key, db_get_data(), db_set_data()
*/
INT EXPRT db_resize_string(HNDLE hdb, HNDLE hKeyRoot, const char *key_name, int num_values, int max_string_length)
{
   int status;
   int hkey;

   //printf("db_resize_string: key_name [%s], num_values %d, max_string_length %d\n", key_name, num_values, max_string_length);

   int old_num_values = 0;
   int old_item_size = 0;
   int old_size = 0;
   char* old_data = NULL;

   if (key_name) {
      status = db_find_key(hdb, hKeyRoot, key_name, &hkey);
   } else {
      hkey = hKeyRoot;
      status = DB_SUCCESS;
   }
   if (status == DB_SUCCESS) {
      KEY key;
      status = db_get_key(hdb, hkey, &key);
      if (status != DB_SUCCESS)
         return status;
      old_num_values = key.num_values;
      old_item_size = key.item_size;
      old_size = old_num_values * old_item_size;
      old_data = (char*)malloc(old_size);
      assert(old_data != NULL);
      int size = old_size;
      status = db_get_data(hdb, hkey, old_data, &size, TID_STRING);
      if (status != DB_SUCCESS) {
         free(old_data);
         return status;
      }
      assert(size == old_size);
   } else {
      status = db_create_key(hdb, hKeyRoot, key_name, TID_STRING);
      if (status != DB_SUCCESS)
         return status;
      status = db_find_key(hdb, hKeyRoot, key_name, &hkey);
      if (status != DB_SUCCESS)
         return status;
   }

   //printf("old_num_values %d, old_item_size %d, old_size %d\n", old_num_values, old_item_size, old_size);

   int item_size = max_string_length;

   if (item_size < 1)
      item_size = old_item_size;

   if (num_values < 1)
      num_values = old_num_values;

   int new_size = num_values * item_size;
   char* new_data = (char*)malloc(new_size);
   assert(new_data);

   int num = old_num_values;
   if (num > num_values)
      num = num_values;

   //printf("new num_values %d, item_size %d, new_size %d, to copy %d values\n", num_values, item_size, new_size, num);

   memset(new_data, 0, new_size);

   for (int i=0; i<num; i++) {
      const char* old_ptr = old_data + i*old_item_size;
      char* new_ptr = new_data + i*item_size;
      strlcpy(new_ptr, old_ptr, item_size);
   }

   status = db_set_data(hdb, hkey, new_data, new_size, num_values, TID_STRING);

   if (old_data)
      free(old_data);
   if (new_data)
      free(new_data);
   
   return status;
}

/** @} *//* end of odbfunctionc */

/* emacs
 * Local Variables:
 * tab-width: 8
 * c-basic-offset: 3
 * indent-tabs-mode: nil
 * End:
 */
