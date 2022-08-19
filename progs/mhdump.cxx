//
// mhdump: midas history explorer
//
// Author: Konstantin Olchanski, 20-NOV-2007
//
// Compile: 
//   g++ -o mhdump.o -c mhdump.cxx -O2 -g -Wall -Wuninitialized
//   g++ -o mhdump -g -O2 mhdump.o
//
// $Id$
//

#undef NDEBUG // midas required assert() to be always enabled

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <errno.h>

#include <string>
#include <map>
#include <vector>

////////////////////////////////////////
// Definitions extracted from midas.h //
////////////////////////////////////////

#define DWORD uint32_t

#define NAME_LENGTH            32            /**< length of names, mult.of 8! */

typedef struct {
   DWORD record_type;           /* RT_DATA or RT_DEF */
   DWORD event_id;
   DWORD time;
   DWORD def_offset;            /* place of definition */
   DWORD data_size;             /* data following this header in bytes */
} HIST_RECORD;

typedef struct {
   char name[NAME_LENGTH];             /**< - */
   DWORD type;                         /**< - */
   DWORD n_data;                       /**< - */
} TAG;

////////////////////////////////////////
// Definitions extracted from midas.c //
////////////////////////////////////////

/********************************************************************/
/* data type sizes */
int tid_size[] = {
   0,                           /* tid == 0 not defined                               */
   1,                           /* TID_BYTE      unsigned byte         0       255    */
   1,                           /* TID_SBYTE     signed byte         -128      127    */
   1,                           /* TID_CHAR      single character      0       255    */
   2,                           /* TID_WORD      two bytes             0      65535   */
   2,                           /* TID_SHORT     signed word        -32768    32767   */
   4,                           /* TID_DWORD     four bytes            0      2^32-1  */
   4,                           /* TID_INT       signed dword        -2^31    2^31-1  */
   4,                           /* TID_BOOL      four bytes bool       0        1     */
   4,                           /* TID_FLOAT     4 Byte float format                  */
   8,                           /* TID_DOUBLE    8 Byte float format                  */
   1,                           /* TID_BITFIELD  8 Bits Bitfield    00000000 11111111 */
   0,                           /* TID_STRING    zero terminated string               */
   0,                           /* TID_ARRAY     variable length array of unkown type */
   0,                           /* TID_STRUCT    C structure                          */
   0,                           /* TID_KEY       key in online database               */
   0,                           /* TID_LINK      link in online database              */
   8,                           /* TID_INT64     8 bytes int          -2^63   2^63-1  */
   8                            /* TID_UINT64    8 bytes unsigned int  0      2^64-1  */
};

/* data type names */
const char *tid_name[] = {
   "NULL",
   "BYTE",
   "SBYTE",
   "CHAR",
   "UINT16",
   "INT16",
   "UINT32",
   "INT32",
   "BOOL",
   "FLOAT",
   "DOUBLE",
   "BITFIELD",
   "STRING",
   "ARRAY",
   "STRUCT",
   "KEY",
   "LINK",
   "INT64",
   "UINT64"
};

////////////////////////////////////////

struct Tag
{
   int event_id;
   std::string name;
   int offset;
   int arraySize;
   int typeSize;
   int typeCode;
};

struct Event
{
   bool printAllTags;
   int size;
   std::map<std::string,Tag*> tags;
   std::vector<std::string> tagNames;
   std::vector<int> tagIndexes;
};

std::map<int,Event*> gTags;

bool doPrintTags  = true;
bool doPrintNames = true;
bool doPrintData  = true;
bool doAll        = false;

int readHstFile(FILE*f)
{
   bool doRead = true;

   assert(f!=NULL);

   while (1)
      {
         HIST_RECORD rec;

         if (doRead)
            {
               int rd = fread(&rec, sizeof(rec), 1, f);
               if (!rd)
                  break;
            }

         doRead = true;
#if 0
         printf("HIST_RECORD:\n");
         printf("  Record type: 0x%x\n", rec.record_type);
         printf("  Event ID: %d\n", rec.event_id);
         printf("  Time: %d\n", rec.time);
         printf("  Offset: 0x%x\n", rec.def_offset);
         printf("  Size: 0x%x\n", rec.data_size);
#endif

         switch (rec.record_type)
            {
            default:
               printf("Unexpected record type: 0x%08x, trying to recover by skipping bad data.\n", rec.record_type);
               while (1)
                  {
                     int c = fgetc(f);
                     //printf("read 0x%02x\n", c);
                     if (c==EOF)
                        return 0;
                     if (c!=0x48)
                        continue;

                     c = fgetc(f);
                     if (c==EOF)
                        return 0;
                     if (c!=0x53)
                        continue;

                     c = fgetc(f);
                     if (c==EOF)
                        return 0;
                     if (c!=0x44)
                        continue;

                     printf("Maybe recovered - see what looks like valid history record header.\n");

                     ((char*)(&rec))[0] = 0x48;
                     ((char*)(&rec))[1] = 0x53;
                     ((char*)(&rec))[2] = 0x44;

                     int rd = fread(((char*)(&rec))+3, sizeof(rec)-3, 1, f);
                     if (!rd)
                        return 0;

                     doRead = false;
                     break;
                  }
               break;

            case 0x46445348: // RT_DEF:
               {
                  char event_name[NAME_LENGTH];
                  int rd = fread(event_name, 1, NAME_LENGTH, f);
                  assert(rd == NAME_LENGTH);
	    
                  int size = rec.data_size;
                  int ntags = size/sizeof(TAG);

                  if (doPrintTags)
                     printf("Event %d, \"%s\", size %d, %d tags.\n", rec.event_id, event_name, size, ntags);

                  assert(size > 0);
                  assert(size < 1*1024*1024);
                  assert(size == ntags*(int)sizeof(TAG));
	    
                  TAG *tags = new TAG[ntags];
                  rd = fread(tags, 1, size, f);
                  assert(rd == size);

                  Event* e = gTags[rec.event_id];
                  if (!e)
                     {
                        gTags[rec.event_id] = new Event;
                        e = gTags[rec.event_id];
                        e->printAllTags = false;
                        if (doAll)
                           e->printAllTags = true;
                     }
                  else
                     {
                        if (e->printAllTags) {
                           e->tagNames.clear();
                           e->tagIndexes.clear();
                        }
                     }

                  e->size = 0;

                  int offset = 0;

                  for (int itag=0; itag<ntags; itag++)
                     {
                        int tsize = tid_size[tags[itag].type];

                        if (tsize == 0)
                           tsize = 1;

                        int size = tags[itag].n_data * tsize;

                        if (tags[itag].type == 12) { // TID_STRING
                           //if (size == 1)
                           //size = 32; // kludge old broken history files
                           fprintf(stderr, "Error: Event %d, \"%s\", has a tag \"%s\" of type TID_STRING, which is forbidden and cannot be decoded, all data after this tag will be gibberish\n", rec.event_id, event_name, tags[itag].name);
                           size = 0;
                        }

                        if (offset%tsize != 0)
                           offset += tsize-offset%tsize;

                        assert(offset%tsize == 0);

                        Tag* t = new Tag;
                        t->event_id  = rec.event_id;
                        t->name      = tags[itag].name;
                        t->offset    = offset;
                        t->arraySize = tags[itag].n_data;
                        t->typeSize  = tid_size[tags[itag].type];
                        t->typeCode  = tags[itag].type;

                        e->tags[t->name] = t;

                        if (e->printAllTags)
                           {
                              e->tagNames.push_back(t->name);
                              e->tagIndexes.push_back(-1);
                           }

                        if (doPrintTags)
                           printf("  Tag %d: \"%s\"[%d], type \"%s\" (%d), type size %d, offset %d+%d\n", itag, tags[itag].name, tags[itag].n_data, tid_name[tags[itag].type], tags[itag].type, tid_size[tags[itag].type], offset, size);

                        offset += size;
                     }

                  e->size = offset;

                  //if (doPrintTags)
                  //printf("  Event size %d\n", e->size);

                  delete[] tags;
                  break;
               }

            case 0x41445348: // RT_DATA:
               {
                  int size = rec.data_size;

                  if (0)
                     printf("Data record, size %d.\n", size);

                  if (size <= 1 || size > 1*1024*1024)
                     {
                        fprintf(stderr, "Error: Invalid data record: event %d, size %d is invalid\n", rec.event_id, rec.data_size);
                        continue;
                     }
	    
                  char *buf = new char[size];
                  int rd = fread(buf, 1, size, f);
                  assert(rd == size);

                  time_t t = (time_t)rec.time;

                  Event* e = gTags[rec.event_id];
                  if (e && doPrintData)
                     {
                        if (size != e->size)
                           {
                              fprintf(stderr, "Error: event %d, size mismatch should be %d, got %d bytes\n", rec.event_id, e->size, size);
                           }

                        //printf("event %d, time %s", rec.event_id, ctime(&t));

                        int n  = e->tagNames.size();

                        if (n>0)
                           printf("%d %d ", rec.event_id, rec.time);

                        for (int i=0; i<n; i++)
                           {
                              Tag*t = e->tags[e->tagNames[i]];
                              int index = e->tagIndexes[i];

                              //printf(" dump %s[%d] (%p)\n", e->tagNames[i].c_str(), e->tagIndexes[i], t);

                              if (t)
                                 {
                                    int offset = t->offset;
                                    void* ptr = (void*)(buf+offset);

                                    if (doPrintNames)
                                       {
                                          if (index < 0)
                                             printf(" %s=", t->name.c_str());
                                          else
                                             printf(" %s[%d]=", t->name.c_str(), index);
                                       }

                                    for (int j=0; j<t->arraySize; j++)
                                       {
                                          if (index >= 0)
                                             j = index;

                                          switch (t->typeCode)
                                             {
                                             default:
                                                printf("unknownType%d ",t->typeCode);
                                                break;
                                             case 1: /* BYTE */
                                                printf("%u ",((uint8_t*)ptr)[j]);
                                                break;
                                             case 2: /* SBYTE */
                                                printf("%d ",((int8_t*)ptr)[j]);
                                                break;
                                             case 3: /* CHAR */
                                                printf("\'%c\' ",((char*)ptr)[j]);
                                                break;
                                             case 4: /* WORD */
                                                printf("%u ",((uint16_t*)ptr)[j]);
                                                break;
                                             case 5: /* SHORT */
                                                printf("%d ",((int16_t*)ptr)[j]);
                                                break;
                                             case 6: /* DWORD */
                                                printf("%u ",((uint32_t*)ptr)[j]);
                                                break;
                                             case 7: /* INT */
                                                printf("%d ",((int32_t*)ptr)[j]);
                                                break;
                                             case 8: /* BOOL */
                                                printf("%u ",((uint32_t*)ptr)[j]);
                                                break;
                                             case 9: /* FLOAT */
                                                printf("%.8g ",((float*)ptr)[j]);
                                                break;
                                             case 10: /* DOUBLE */
                                                printf("%.16g ",((double*)ptr)[j]);
                                                break;
                                             //case 12: /* STRING */
                                                //printf("%s ",&((char*)ptr)[j]);
                                                //break;
                                             }

                                          if (index >= 0)
                                             break;
                                       }
                                 }
                           }

                        if (n>0)
                           printf(" %s", ctime(&t));
                     }

                  delete[] buf;
                  break;
               }
            }
      }

   return 0;
}

int readHst(const char* name)
{
   FILE* f = fopen(name,"r");
   if (!f)
      {
         fprintf(stderr,"Error: Cannot open \'%s\', errno %d (%s)\n", name, errno, strerror(errno));
         exit(1);
      }

   readHstFile(f);
   fclose(f);
   return 0;
}

std::string readString(FILE* f)
{
   char buf[1024];
   memset(buf, 0, sizeof(buf));
   const char* s = fgets(buf, sizeof(buf)-1, f);
   if (!s)
      return "";
   // NUL-teminated by memset(0)
   size_t len = strlen(buf);
   if (len > 0) {
      // remove trailing newline
      if (buf[len-1] == '\n')
         buf[len-1] = 0;
   }
   return buf;
}

std::string tagValue(const char* tag, const std::string& s)
{
   size_t len = strlen(tag);
   if (strncmp(s.c_str(), tag, len) == 0) {
      const char* sptr = s.c_str() + len;
      while (sptr[0] == ' ')
         sptr++;
      return sptr;
   }
   return "";
}

int readMhfFileV2(const char* filename, FILE*f)
{
   std::string event_name = tagValue("event_name:", readString(f));
   std::string time       = tagValue("time:", readString(f));
   printf("event name: [%s], time [%s]\n", event_name.c_str(), time.c_str());
   std::string s;
   while (1) {
      s = readString(f);
      if (strncmp(s.c_str(), "tag:", 4) != 0)
         break;
      printf("tag: %s\n", s.c_str());
   }
   std::string s_record_size = tagValue("record_size:", s);
   std::string s_data_offset = tagValue("data_offset:", readString(f));
   size_t record_size = atoi(s_record_size.c_str());
   size_t data_offset = atoi(s_data_offset.c_str());
   //record_size += 4; // 4 bytes of timestamp
   printf("record size: %zu, data offset: %zu\n", record_size, data_offset);
   int status = fseek(f, data_offset, SEEK_SET);
   if (status != 0) {
      fprintf(stderr, "%s: cannot seek to %zu, fseek() returned %d, errno %d (%s)\n", filename, data_offset, status, errno, strerror(errno));
      return -1;
   }
   char buf[record_size];
   int count = 0;
   uint32_t last_time = atoi(time.c_str());
   while (!feof(f)) {
      size_t rd = fread(buf, 1, record_size, f);
      //printf("read %zu\n", rd);
      if (rd == 0) {
         // EOF
         break;
      } else if (rd != record_size) {
         // short read
         fprintf(stderr, "%s: short read at the end of file, last data record is truncated from %zu to %zu bytes\n", filename, record_size, rd);
         break;
      }
      const uint32_t t = *(uint32_t*)&buf[0];
      printf("record %d, time %lu, incr %lu\n", count, (long unsigned int)t, (long unsigned int)(t-last_time));
      count++;
      if (t == last_time) {
         printf("duplicate time %lu -> %lu\n", (long unsigned int)last_time, (long unsigned int)t);
      } else if (t < last_time) {
         printf("non-monotonic time %lu -> %lu\n", (long unsigned int)last_time, (long unsigned int)t);
      }
      last_time = t;
   }
   fprintf(stderr, "%s: read %d records\n", filename, count);
   return 0;
}

int readMhfFile(const char* filename, FILE*f)
{
   std::string version = readString(f);
   if (version == "version: 2.0") {
      return readMhfFileV2(filename, f);
   }

   fprintf(stderr, "%s: unexpected file version: %s\n", filename, version.c_str());
   return -1;
}

int readMhf(const char* name)
{
   FILE* f = fopen(name,"r");
   if (!f) {
      fprintf(stderr,"Error: Cannot open \'%s\', errno %d (%s)\n", name, errno, strerror(errno));
      exit(1);
   }

   readMhfFile(name, f);
   fclose(f);
   return 0;
}

void help()
{
   fprintf(stderr,"Usage: mhdump [-h] [-L] [-n] [-t] [-E event_id] [-T tag_name] file1.hst file2.hst ...\n");
   fprintf(stderr,"Usage: mhdump [-L] [-n] [-t] [-T tag_name] mhf_file1.dat mhf_file2.dat ...\n");
   fprintf(stderr,"\n");
   fprintf(stderr,"Switches:\n");
   fprintf(stderr,"  -h --- print this help message\n");
   fprintf(stderr,"  -L --- list tag definitions only\n");
   fprintf(stderr,"  -t --- omit tag definitions\n");
   fprintf(stderr,"  -n --- omit variable names\n");
   fprintf(stderr,"\n");
   fprintf(stderr,"Examples:\n");
   fprintf(stderr,"  To list all existing tags: mhdump -L file1.hst file2.hst ...\n");
   fprintf(stderr,"  To show data for all events, all tags: mhdump file1.hst file2.hst ...\n");
   fprintf(stderr,"  To show all data for event 0: mhdump -E 0 file1.hst file2.hst ...\n");
   fprintf(stderr,"  To show data for event 0, tag \"State\": mhdump -n -E 0 -T State file1.hst file2.hst ...\n");
   fprintf(stderr,"  To show data for event 3, tag \"MCRT\", array index 5: mhdump -n -E 3 -T MCRT[5] file1.hst file2.hst ...\n");
   exit(1);
}

int main(int argc,char*argv[])
{
   int event_id = -1;

   if (argc <= 1)
      help(); // DOES NOT RETURN

   for (int iarg=1; iarg<argc; iarg++)
      if (strcmp(argv[iarg], "-h")==0)
         {
            help(); // DOES NOT RETURN
         }
      else if (strcmp(argv[iarg], "-E")==0)
         {
            iarg++;
            event_id = atoi(argv[iarg]);
            if (!gTags[event_id])
               gTags[event_id] = new Event;
            gTags[event_id]->printAllTags = true;
         }
      else if (strcmp(argv[iarg], "-T")==0)
         {
            iarg++;
            char *s = strchr(argv[iarg],'[');
            int index = -1;
            if (s)
               {
                  index = atoi(s+1);
                  *s = 0;
               }

            std::string name = argv[iarg];

            Event* e = gTags[event_id];

            if ((event_id<0) || !e)
               {
                  fprintf(stderr,"Error: expected \"-E event_id\" before \"-T ...\"\n");
                  exit(1);
               }

            e->printAllTags = false;
            e->tagNames.push_back(name);
            e->tagIndexes.push_back(index);
         }
      else if (strcmp(argv[iarg], "-t")==0)
         doPrintTags = false;
      else if (strcmp(argv[iarg], "-L")==0)
         {
            doPrintTags = true;
            doPrintData = false;
         }
      else if (strcmp(argv[iarg], "-A")==0)
         doAll = true;
      else if (strcmp(argv[iarg], "-n")==0)
         doPrintNames = false;
      else if (strncmp(argv[iarg], "mhf_", 4) == 0 || strstr(argv[iarg], "/mhf_")) {
         // read mhf_xxx.dat files
         if (gTags.size() == 0)
            doAll = true;
         readMhf(argv[iarg]);
      } else {
         // read xxx.hst files
         if (gTags.size() == 0)
            doAll = true;
         readHst(argv[iarg]);
      }

   // make leak sanitizer happy, delete everything we allocated.
   for (auto& ei: gTags) {
      if (ei.second) {
         for (auto& ti: ei.second->tags) {
            if (ti.second) {
               delete ti.second;
            }
         }
         ei.second->tags.clear();
         delete ei.second;
      }
   }
   gTags.clear();

   return 0;
}

/* emacs
 * Local Variables:
 * tab-width: 8
 * c-basic-offset: 3
 * indent-tabs-mode: nil
 * End:
 */
