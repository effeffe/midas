/********************************************************************\

  Name:         history_schema.cxx
  Created by:   Konstantin Olchanski

  Contents:     Schema based MIDAS history. Available drivers:
                FileHistory: storage of data in binary files (replacement for the traditional MIDAS history)
                MysqlHistory: storage of data in MySQL database (replacement for the ODBC based SQL history)
                SqliteHistory: storage of data in SQLITE3 database (not suitable for production use)

\********************************************************************/

#undef NDEBUG // midas required assert() to be always enabled

#include "midas.h"
#include "msystem.h"

#include <math.h>

#include <vector>
#include <list>
#include <string>
#include <map>
#include <algorithm>

#ifndef HAVE_STRLCPY
#include "strlcpy.h"
#endif

// make mysql/my_global.h happy - it redefines closesocket()
#undef closesocket

//
// benchmarks
//
// /usr/bin/time ./linux/bin/mh2sql . /ladd/iris_data2/alpha/alphacpc09-elog-history/history/121019.hst
// -rw-r--r-- 1 alpha users 161028048 Oct 19  2012 /ladd/iris_data2/alpha/alphacpc09-elog-history/history/121019.hst
// flush   10000, sync=OFF    -> 51.51user 1.51system 0:53.76elapsed 98%CPU
// flush 1000000, sync=NORMAL -> 51.83user 2.09system 1:08.37elapsed 78%CPU (flush never activated)
// flush  100000, sync=NORMAL -> 51.38user 1.94system 1:06.94elapsed 79%CPU
// flush   10000, sync=NORMAL -> 51.37user 2.03system 1:31.63elapsed 58%CPU
// flush    1000, sync=NORMAL -> 52.16user 2.70system 4:38.58elapsed 19%CPU

////////////////////////////////////////
//         MIDAS includes             //
////////////////////////////////////////

#include "midas.h"
#include "history.h"

////////////////////////////////////////
//           helper stuff             //
////////////////////////////////////////

#define STRLCPY(dst, src) strlcpy((dst), (src), sizeof(dst))
#define FREE(x) { if (x) free(x); (x) = NULL; }

static std::string TimeToString(time_t t)
{
   const char* sign = "";

   if (t == 0)
      return "0";

   time_t tt = t;

   if (t < 0) {
      sign = "-";
      tt = -t;
   }

   assert(tt > 0);

   std::string v;
   while (tt) {
      char c = '0' + tt%10;
      tt /= 10;
      v = c + v;
   }

   v = sign + v;

   //printf("time %.0f -> %s\n", (double)t, v.c_str());

   return v;
}

static std::string SmallIntToString(int i)
{
   //int ii = i;

   if (i == 0)
      return "0";

   assert(i > 0);

   std::string v;
   while (i) {
      char c = '0' + i%10;
      i /= 10;
      v = c + v;
   }

   //printf("SmallIntToString: %d -> %s\n", ii, v.c_str());

   return v;
}

static bool MatchEventName(const char* event_name, const char* var_event_name)
{
   // new-style event name: "equipment_name/variable_name:tag_name"
   // old-style event name: "equipment_name:tag_name" ("variable_name" is missing)
   bool newStyleEventName = (strchr(var_event_name, '/')!=NULL);

   //printf("looking for event_name [%s], try table [%s] event name [%s], new style [%d]\n", var_event_name, table_name, event_name, newStyleEventName);

   if (strcasecmp(event_name, var_event_name) == 0) {
      return true;
   } else if (newStyleEventName) {
      return false;
   } else { // for old style names, need more parsing
      bool match = false;

      const char* s = event_name;
      for (int j=0; s[j]; j++) {

         if ((var_event_name[j]==0) && (s[j]=='/')) {
            match = true;
            break;
         }

         if ((var_event_name[j]==0) && (s[j]=='_')) {
            match = true;
            break;
         }

         if (var_event_name[j]==0) {
            match = false;
            break;
         }

         if (tolower(var_event_name[j]) != tolower(s[j])) { // does not work for UTF-8 Unicode
            match = false;
            break;
         }
      }

      return match;
   }
}

static bool MatchTagName(const char* tag_name, int n_data, const char* var_tag_name, const int var_tag_index)
{
   char alt_tag_name[1024]; // maybe this is an array without "Names"?
   sprintf(alt_tag_name, "%s[%d]", var_tag_name, var_tag_index);

   //printf("  looking for tag [%s] alt [%s], try column name [%s]\n", var_tag_name, alt_tag_name, tag_name);

   if (strcasecmp(tag_name, var_tag_name) == 0)
      if (var_tag_index >= 0 && var_tag_index < n_data)
         return true;

   if (strcasecmp(tag_name, alt_tag_name) == 0)
      return true;

   return false;
}

static void PrintTags(int ntags, const TAG tags[])
{
   for (int i=0; i<ntags; i++)
      printf("tag %d: %s %s[%d]\n", i, rpc_tid_name(tags[i].type), tags[i].name, tags[i].n_data);
}

// convert MIDAS event name to something acceptable as an SQL identifier - table name, column name, etc

static std::string MidasNameToSqlName(const char* s)
{
   std::string out;

   for (int i=0; s[i]!=0; i++) {
      char c = s[i];
      if (isalpha(c) || isdigit(c))
         out += tolower(c); // does not work for UTF-8 Unicode
      else
         out += '_';
   }

   return out;
}

// convert MIDAS event name to something acceptable as a file name

static std::string MidasNameToFileName(const char* s)
{
   std::string out;

   for (int i=0; s[i]!=0; i++) {
      char c = s[i];
      if (isalpha(c) || isdigit(c))
         out += tolower(c); // does not work for UTF-8 Unicode
      else
         out += '_';
   }

   return out;
}

// compare event names

static int event_name_cmp(const std::string& e1, const char* e2)
{
   return strcasecmp(e1.c_str(), e2);
}

// compare variable names

static int var_name_cmp(const std::string& v1, const char* v2)
{
   return strcasecmp(v1.c_str(), v2);
}

////////////////////////////////////////
//         SQL data types             //
////////////////////////////////////////

#ifdef HAVE_SQLITE
static const char *sql_type_sqlite[TID_LAST] = {
   "xxxINVALIDxxxNULL", // TID_NULL
   "INTEGER",           // TID_UINT8
   "INTEGER",           // TID_INT8
   "TEXT",              // TID_CHAR
   "INTEGER",           // TID_UINT16
   "INTEGER",           // TID_INT16
   "INTEGER",           // TID_UINT32
   "INTEGER",           // TID_INT32
   "INTEGER",           // TID_BOOL
   "REAL",              // TID_FLOAT
   "REAL",              // TID_DOUBLE
   "INTEGER",           // TID_BITFIELD
   "TEXT",              // TID_STRING
   "xxxINVALIDxxxARRAY",
   "xxxINVALIDxxxSTRUCT",
   "xxxINVALIDxxxKEY",
   "xxxINVALIDxxxLINK"
};
#endif

#ifdef HAVE_PGSQL
static const char *sql_type_pgsql[TID_LAST] = {
   "xxxINVALIDxxxNULL", // TID_NULL
   "SMALLINT",  // MYSQL "TINYINT SIGNED", // TID_BYTE
   "SMALLINT",  // MYSQL "TINYINT UNSIGNED",  // TID_SBYTE
   "CHAR(1)",   // TID_CHAR
   "SMALLINT",  // MYSQL "SMALLINT UNSIGNED ", // TID_WORD
   "SMALLINT",  // MYSQL "SMALLINT SIGNED ", // TID_SHORT
   "INTEGER",   // MYSQL "INT UNSIGNED ", // TID_DWORD
   "INTEGER",   // MYSQL "INT SIGNED ", // TID_INT
   "BOOL",      // TID_BOOL
   "FLOAT(53)", // MYSQL "DOUBLE" TID_FLOAT
   "FLOAT(53)", // MYSQL "DOUBLE" TID_DOUBLE
   "SMALLINT",  // MYSQL "TINYINT UNSIGNED", // TID_BITFIELD
   "VARCHAR",   // TID_STRING
   "xxxINVALIDxxxARRAY",
   "xxxINVALIDxxxSTRUCT",
   "xxxINVALIDxxxKEY",
   "xxxINVALIDxxxLINK"
};
#endif

#ifdef HAVE_MYSQL
static const char *sql_type_mysql[TID_LAST] = {
   "xxxINVALIDxxxNULL", // TID_NULL
   "tinyint unsigned",  // TID_BYTE
   "tinyint",           // TID_SBYTE
   "char",              // TID_CHAR
   "smallint unsigned", // TID_WORD
   "smallint",          // TID_SHORT
   "integer unsigned",  // TID_DWORD
   "integer",           // TID_INT
   "tinyint",           // TID_BOOL
   "float",             // TID_FLOAT
   "double",            // TID_DOUBLE
   "tinyint unsigned",  // TID_BITFIELD
   "VARCHAR",           // TID_STRING
   "xxxINVALIDxxxARRAY",
   "xxxINVALIDxxxSTRUCT",
   "xxxINVALIDxxxKEY",
   "xxxINVALIDxxxLINK"
};
#endif

void DoctorSqlColumnType(std::string* col_type, const char* index_type)
{
   if (*col_type == index_type)
      return;

   if (*col_type == "int(10) unsigned" && strcmp(index_type, "integer unsigned")==0) {
      *col_type = index_type;
      return;
   }

   if (*col_type == "int(11)" && strcmp(index_type, "integer")==0) {
      *col_type = index_type;
      return;
   }

   if (*col_type == "integer" && strcmp(index_type, "int(11)")==0) {
      *col_type = index_type;
      return;
   }

   // MYSQL 8.0.23

   if (*col_type == "int" && strcmp(index_type, "integer")==0) {
      *col_type = index_type;
      return;
   }

   if (*col_type == "int unsigned" && strcmp(index_type, "integer unsigned")==0) {
      *col_type = index_type;
      return;
   }

   cm_msg(MERROR, "SqlHistory", "Cannot use this SQL database, incompatible column names: created column type [%s] is reported with column type [%s]", index_type, col_type->c_str());
   cm_msg_flush_buffer();
   abort();
}

#if 0
static int sql2midasType_mysql(const char* name)
{
   for (int tid=0; tid<TID_LAST; tid++)
      if (strcasecmp(name, sql_type_mysql[tid])==0)
         return tid;
   // FIXME!
   printf("sql2midasType: Cannot convert SQL data type \'%s\' to a MIDAS data type!\n", name);
   return 0;
}
#endif

#if 0
static int sql2midasType_sqlite(const char* name)
{
   if (strcmp(name, "INTEGER") == 0)
      return TID_INT;
   if (strcmp(name, "REAL") == 0)
      return TID_DOUBLE;
   if (strcmp(name, "TEXT") == 0)
      return TID_STRING;
   // FIXME!
   printf("sql2midasType: Cannot convert SQL data type \'%s\' to a MIDAS data type!\n", name);
   return 0;
}
#endif

////////////////////////////////////////
//        Schema base classes         //
////////////////////////////////////////

struct HsSchemaEntry {
   std::string tag_name; // tag name from MIDAS
   std::string tag_type; // tag type from MIDAS
   std::string name;     // entry name, same as tag_name except when read from SQL history when it could be the SQL column name
   int type    = 0; // MIDAS data type TID_xxx
   int n_data  = 0; // MIDAS array size
   int n_bytes = 0; // n_data * size of MIDAS data type (only used by HsFileSchema?)
   bool inactive = false; // inactive SQL column

   HsSchemaEntry() // ctor
   {
      type = 0;
      n_data = 0;
      n_bytes = 0;
   }
};

struct HsSchema
{
   // event schema definitions
   std::string event_name;
   time_t time_from = 0;
   time_t time_to = 0;
   std::vector<HsSchemaEntry> variables;
   std::vector<int> offsets;
   int  n_bytes = 0;

   // run time data used by hs_write_event()
   int count_write_undersize = 0;
   int count_write_oversize = 0;
   int write_max_size = 0;
   int write_min_size = 0;

   // schema disabled by write error
   bool disabled = true;

   HsSchema() // ctor
   {
      time_from = 0;
      time_to = 0;
      n_bytes = 0;

      count_write_undersize = 0;
      count_write_oversize = 0;
      write_max_size = 0;
      write_min_size = 0;
   }

   virtual void print(bool print_tags = true) const;
   virtual ~HsSchema(); // dtor
   virtual int flush_buffers() = 0;
   virtual int close() = 0;
   virtual int write_event(const time_t t, const char* data, const int data_size) = 0;
   virtual int match_event_var(const char* event_name, const char* var_name, const int var_index);
   virtual int read_last_written(const time_t timestamp,
                                 const int debug,
                                 time_t* last_written) = 0;
   virtual int read_data(const time_t start_time,
                         const time_t end_time,
                         const int num_var, const std::vector<int>& var_schema_index, const int var_index[],
                         const int debug,
                         std::vector<time_t>& last_time,
                         MidasHistoryBufferInterface* buffer[]) = 0;
};

class HsSchemaVector
{
protected:
   std::vector<HsSchema*> data;

public:
   ~HsSchemaVector() { // dtor
      clear();
   }

   HsSchema* operator[](int index) const {
      return data[index];
   }

   unsigned size() const {
      return data.size();
   }

   void add(HsSchema* s);

   void clear() {
      for (unsigned i=0; i<data.size(); i++)
         if (data[i]) {
            delete data[i];
            data[i] = NULL;
         }
      data.clear();
   }

   void print(bool print_tags = true) const {
      for (unsigned i=0; i<data.size(); i++)
         data[i]->print(print_tags);
   }

   HsSchema* find_event(const char* event_name, const time_t timestamp, int debug = 0);
};

////////////////////////////////////////////
//        Base class functions            //
////////////////////////////////////////////

HsSchema::~HsSchema() // dtor
{
   // only report if undersize/oversize happens more than once -
   // the first occurence is already reported by hs_write_event()
   if (count_write_undersize > 1) {
      cm_msg(MERROR, "hs_write_event", "Event \'%s\' data size mismatch count: %d, expected %d bytes, hs_write_event() called with as few as %d bytes", event_name.c_str(), count_write_undersize, n_bytes, write_min_size);
   }

   if (count_write_oversize > 1) {
      cm_msg(MERROR, "hs_write_event", "Event \'%s\' data size mismatch count: %d, expected %d bytes, hs_write_event() called with as much as %d bytes", event_name.c_str(), count_write_oversize, n_bytes, write_max_size);
   }
};

void HsSchemaVector::add(HsSchema* s)
{
   // schema list "data" is sorted by decreasing "time_from", newest schema first

   //printf("add: %s..%s %s\n", TimeToString(s->time_from).c_str(), TimeToString(s->time_to).c_str(), s->event_name.c_str());

   bool added = false;

   for (auto it = data.begin(); it != data.end(); it++) {
      if (event_name_cmp((*it)->event_name, s->event_name.c_str())==0) {
         if (s->time_from == (*it)->time_from) {
            // duplicate schema, keep the last one added (for file schema it is the newer file)
            s->time_to = (*it)->time_to;
            delete (*it);
            (*it) = s;
            return;
         }
      }

      if (s->time_from > (*it)->time_from) {
         data.insert(it, s);
         added = true;
         break;
      }
   }

   if (!added) {
      data.push_back(s);
   }

   //time_t oldest_time_from = data.back()->time_from;

   time_t time_to = 0;

   for (auto it = data.begin(); it != data.end(); it++) {
      if (event_name_cmp((*it)->event_name, s->event_name.c_str())==0) {
         (*it)->time_to = time_to;
         time_to = (*it)->time_from;

         //printf("vvv: %s..%s %s\n", TimeToString((*it)->time_from-oldest_time_from).c_str(), TimeToString((*it)->time_to-oldest_time_from).c_str(), (*it)->event_name.c_str());
      }
   }
}

HsSchema* HsSchemaVector::find_event(const char* event_name, time_t t, int debug)
{
   HsSchema* ss = NULL;

   if (debug) {
      printf("find_event: All schema for event %s: (total %d)\n", event_name, (int)data.size());
      int found = 0;
      for (unsigned i=0; i<data.size(); i++) {
         HsSchema* s = data[i];
         printf("find_event: schema %d name [%s]\n", i, s->event_name.c_str());
         if (event_name_cmp(s->event_name, event_name)!=0)
            continue;
         s->print();
         found++;
      }
      printf("find_event: Found %d schemas for event %s\n", found, event_name);

      //if (found == 0)
      //   abort();
   }

   for (unsigned i=0; i<data.size(); i++) {
      HsSchema* s = data[i];

      // wrong event
      if (event_name_cmp(s->event_name, event_name)!=0)
         continue;

      // schema is from after the time we are looking for
      if (s->time_from > t)
         continue;

      if (!ss)
         ss = s;

      // remember the newest schema
      if (s->time_from > ss->time_from)
         ss = s;
   }

   // try to find
   for (unsigned i=0; i<data.size(); i++) {
      HsSchema* s = data[i];

      // wrong event
      if (event_name_cmp(s->event_name, event_name)!=0)
         continue;

      // schema is from after the time we are looking for
      if (s->time_from > t)
         continue;

      if (!ss)
         ss = s;

      // remember the newest schema
      if (s->time_from > ss->time_from)
         ss = s;
   }

   if (debug) {
      if (ss) {
         printf("find_event: for time %s, returning:\n", TimeToString(t).c_str());
         ss->print();
      } else {
         printf("find_event: for time %s, nothing found:\n", TimeToString(t).c_str());
      }
   }

   return ss;
}

////////////////////////////////////////////
//        Sql interface class             //
////////////////////////////////////////////

class SqlBase
{
public:
   int  fDebug;
   bool fIsConnected;
   bool fTransactionPerTable;

   SqlBase() {  // ctor
      fDebug = 0;
      fIsConnected = false;
      fTransactionPerTable = true;
   };

   virtual ~SqlBase() { // dtor
      // confirm that the destructor of the concrete class
      // disconnected the database
      assert(!fIsConnected);
      fDebug = 0;
      fIsConnected = false;
   }

   virtual int Connect(const char* path) = 0;
   virtual int Disconnect() = 0;
   virtual bool IsConnected() = 0;

   virtual int ListTables(std::vector<std::string> *plist) = 0;
   virtual int ListColumns(const char* table_name, std::vector<std::string> *plist) = 0;

   // sql commands
   virtual int Exec(const char* table_name, const char* sql) = 0;
   virtual int ExecDisconnected(const char* table_name, const char* sql) = 0;

   // queries
   virtual int Prepare(const char* table_name, const char* sql) = 0;
   virtual int Step() = 0;
   virtual const char* GetText(int column) = 0;
   virtual time_t      GetTime(int column) = 0;
   virtual double      GetDouble(int column) = 0;
   virtual int Finalize() = 0;

   // transactions
   virtual int OpenTransaction(const char* table_name) = 0;
   virtual int CommitTransaction(const char* table_name) = 0;
   virtual int RollbackTransaction(const char* table_name) = 0;

   // data types
   virtual const char* ColumnType(int midas_tid) = 0;
   virtual bool TypesCompatible(int midas_tid, const char* sql_type) = 0;

   // string quoting
   virtual std::string QuoteString(const char* s) = 0; // quote text string
   virtual std::string QuoteId(const char* s) = 0; // quote identifier, such as table or column name
};

////////////////////////////////////////////
//        Schema concrete classes         //
////////////////////////////////////////////

struct HsSqlSchema : public HsSchema {
   SqlBase* sql;
   std::vector<std::string> disconnected_buffer;
   std::string table_name;
   std::vector<std::string> column_names;
   std::vector<std::string> column_types;

   HsSqlSchema() // ctor
   {
      sql = 0;
      table_transaction_count = 0;
   }

   ~HsSqlSchema() // dtor
   {
      assert(get_transaction_count() == 0);
   }

   void print(bool print_tags = true) const;
   int get_transaction_count();
   void reset_transaction_count();
   void increment_transaction_count();
   int close_transaction();
   int flush_buffers() { return close_transaction(); }
   int close() { return close_transaction(); }
   int write_event(const time_t t, const char* data, const int data_size);
   int match_event_var(const char* event_name, const char* var_name, const int var_index);
   int read_last_written(const time_t timestamp,
                         const int debug,
                         time_t* last_written);
   int read_data(const time_t start_time,
                 const time_t end_time,
                 const int num_var, const std::vector<int>& var_schema_index, const int var_index[],
                 const int debug,
                 std::vector<time_t>& last_time,
                 MidasHistoryBufferInterface* buffer[]);

private:
   // Sqlite uses a transaction per table; MySQL uses a single transaction for all tables.
   // But to support future "single transaction" DBs more easily (e.g. if user wants to
   // log to both Postgres and MySQL in future), we keep track of the transaction count
   // per SQL engine.
   int table_transaction_count;
   static std::map<SqlBase*, int> global_transaction_count;
};

std::map<SqlBase*, int> HsSqlSchema::global_transaction_count;

struct HsFileSchema : public HsSchema {
   std::string file_name;
   int record_size;
   int data_offset;
   int last_size;
   int writer_fd;

   HsFileSchema() // ctor
   {
      record_size = 0;
      data_offset = 0;
      last_size = 0;
      writer_fd = -1;
   }

   ~HsFileSchema() // dtor
   {
      close();
      record_size = 0;
      data_offset = 0;
      last_size = 0;
      writer_fd = -1;
   }

   void print(bool print_tags = true) const;
   int flush_buffers() { return HS_SUCCESS; };
   int close();
   int write_event(const time_t t, const char* data, const int data_size);
   int read_last_written(const time_t timestamp,
                         const int debug,
                         time_t* last_written);
   int read_data(const time_t start_time,
                 const time_t end_time,
                 const int num_var, const std::vector<int>& var_schema_index, const int var_index[],
                 const int debug,
                 std::vector<time_t>& last_time,
                 MidasHistoryBufferInterface* buffer[]);
};

////////////////////////////////////////////
//        Print functions                 //
////////////////////////////////////////////

void HsSchema::print(bool print_tags) const
{
   unsigned nv = this->variables.size();
   printf("event [%s], time %s..%s, %d variables, %d bytes\n", this->event_name.c_str(), TimeToString(this->time_from).c_str(), TimeToString(this->time_to).c_str(), nv, n_bytes);
   if (print_tags)
      for (unsigned j=0; j<nv; j++)
         printf("  %d: name [%s], type [%s] tid %d, n_data %d, n_bytes %d, offset %d\n", j, this->variables[j].name.c_str(), rpc_tid_name(this->variables[j].type), this->variables[j].type, this->variables[j].n_data, this->variables[j].n_bytes, this->offsets[j]);
};

void HsSqlSchema::print(bool print_tags) const
{
   unsigned nv = this->variables.size();
   printf("event [%s], sql_table [%s], time %s..%s, %d variables, %d bytes\n", this->event_name.c_str(), this->table_name.c_str(), TimeToString(this->time_from).c_str(), TimeToString(this->time_to).c_str(), nv, n_bytes);
   if (print_tags) {
      for (unsigned j=0; j<nv; j++) {
         printf("  %d: name [%s], type [%s] tid %d, n_data %d, n_bytes %d", j, this->variables[j].name.c_str(), rpc_tid_name(this->variables[j].type), this->variables[j].type, this->variables[j].n_data, this->variables[j].n_bytes);
         printf(", sql_column [%s], sql_type [%s], offset %d", this->column_names[j].c_str(), this->column_types[j].c_str(), this->offsets[j]);
         printf(", inactive %d", this->variables[j].inactive);
         printf("\n");
      }
   }
}

void HsFileSchema::print(bool print_tags) const
{
   unsigned nv = this->variables.size();
   printf("event [%s], file_name [%s], time %s..%s, %d variables, %d bytes, dat_offset %d, record_size %d\n", this->event_name.c_str(), this->file_name.c_str(), TimeToString(this->time_from).c_str(), TimeToString(this->time_to).c_str(), nv, n_bytes, data_offset, record_size);
   if (print_tags) {
      for (unsigned j=0; j<nv; j++)
         printf("  %d: name [%s], type [%s] tid %d, n_data %d, n_bytes %d, offset %d\n", j, this->variables[j].name.c_str(), rpc_tid_name(this->variables[j].type), this->variables[j].type, this->variables[j].n_data, this->variables[j].n_bytes, this->offsets[j]);
   }
}

////////////////////////////////////////////
//        File functions                  //
////////////////////////////////////////////

#ifdef HAVE_MYSQL

////////////////////////////////////////
//     MYSQL/MariaDB database access  //
////////////////////////////////////////

//#warning !!!HAVE_MYSQL!!!

//#include <my_global.h> // my_global.h removed MySQL 8.0, MariaDB 10.2. K.O.
#include <mysql.h>

class Mysql: public SqlBase
{
public:
   std::string fConnectString;
   MYSQL* fMysql;

   // query results
   MYSQL_RES* fResult;
   MYSQL_ROW  fRow;
   int fNumFields;

   // disconnected operation
   unsigned fMaxDisconnected;
   std::list<std::string> fDisconnectedBuffer;
   time_t fNextReconnect;
   int fNextReconnectDelaySec;
   int fDisconnectedLost;

   Mysql(); // ctor
   ~Mysql(); // dtor

   int Connect(const char* path);
   int Disconnect();
   bool IsConnected();

   int ConnectTable(const char* table_name);

   int ListTables(std::vector<std::string> *plist);
   int ListColumns(const char* table_name, std::vector<std::string> *plist);

   int Exec(const char* table_name, const char* sql);
   int ExecDisconnected(const char* table_name, const char* sql);

   int Prepare(const char* table_name, const char* sql);
   int Step();
   const char* GetText(int column);
   time_t      GetTime(int column);
   double      GetDouble(int column);
   int Finalize();

   int OpenTransaction(const char* table_name);
   int CommitTransaction(const char* table_name);
   int RollbackTransaction(const char* table_name);

   const char* ColumnType(int midas_tid);
   bool TypesCompatible(int midas_tid, const char* sql_type);

   std::string QuoteId(const char* s);
   std::string QuoteString(const char* s);
};

Mysql::Mysql() // ctor
{
   fMysql = NULL;
   fResult = NULL;
   fRow = NULL;
   fNumFields = 0;
   fMaxDisconnected = 1000;
   fNextReconnect = 0;
   fNextReconnectDelaySec = 0;
   fDisconnectedLost = 0;
   fTransactionPerTable = false;
}

Mysql::~Mysql() // dtor
{
   Disconnect();
   fMysql = NULL;
   fResult = NULL;
   fRow = NULL;
   fNumFields = 0;
   if (fDisconnectedBuffer.size() > 0) {
      cm_msg(MINFO, "Mysql::~Mysql", "Lost %d history entries accumulated while disconnected from the database", (int)fDisconnectedBuffer.size());
      cm_msg_flush_buffer();
   }
}

static char* skip_spaces(char* s)
{
   while (*s) {
      if (!isspace(*s))
         break;
      s++;
   }
   return s;
}

int Mysql::Connect(const char* connect_string)
{
   if (fIsConnected)
      Disconnect();

   fConnectString = connect_string;

   if (fDebug) {
      cm_msg(MINFO, "Mysql::Connect", "Connecting to Mysql database specified by \'%s\'", connect_string);
      cm_msg_flush_buffer();
   }

   std::string host_name;
   std::string user_name;
   std::string user_password;
   std::string db_name;
   int tcp_port = 0;
   std::string unix_socket;
   std::string buffer;

   FILE* fp = fopen(connect_string, "r");
   if (!fp) {
      cm_msg(MERROR, "Mysql::Connect", "Cannot read MYSQL connection parameters from \'%s\', fopen() error %d (%s)", connect_string, errno, strerror(errno));
      return DB_FILE_ERROR;
   }

   while (1) {
      char buf[256];
      char* s = fgets(buf, sizeof(buf), fp);
      if (!s)
         break; // EOF

      char*ss;
      // kill trailing \n and \r
      ss = strchr(s, '\n');
      if (ss) *ss = 0;
      ss = strchr(s, '\r');
      if (ss) *ss = 0;

      //printf("line [%s]\n", s);

      if (strncasecmp(s, "server=", 7)==0)
         host_name = skip_spaces(s + 7);
      if (strncasecmp(s, "port=", 5)==0)
         tcp_port = atoi(skip_spaces(s + 5));
      if (strncasecmp(s, "database=", 9)==0)
         db_name = skip_spaces(s + 9);
      if (strncasecmp(s, "socket=", 7)==0)
         unix_socket = skip_spaces(s + 7);
      if (strncasecmp(s, "user=", 5)==0)
         user_name = skip_spaces(s + 5);
      if (strncasecmp(s, "password=", 9)==0)
         user_password = skip_spaces(s + 9);
      if (strncasecmp(s, "buffer=", 7)==0)
         buffer = skip_spaces(s + 7);
   }

   fclose(fp);

   int buffer_int = atoi(buffer.c_str());

   if (buffer_int > 0 && buffer_int < 1000000)
      fMaxDisconnected = buffer_int;

   if (fDebug)
      printf("Mysql::Connect: connecting to server [%s] port %d, unix socket [%s], database [%s], user [%s], password [%s], buffer [%d]\n", host_name.c_str(), tcp_port, unix_socket.c_str(), db_name.c_str(), user_name.c_str(), user_password.c_str(), fMaxDisconnected);

   if (!fMysql) {
      fMysql = mysql_init(NULL);
      if (!fMysql) {
         return DB_FILE_ERROR;
      }
   }

   int client_flag = 0|CLIENT_IGNORE_SIGPIPE;

   if (mysql_real_connect(fMysql, host_name.c_str(), user_name.c_str(), user_password.c_str(), db_name.c_str(), tcp_port, unix_socket.c_str(), client_flag) == NULL) {
      cm_msg(MERROR, "Mysql::Connect", "mysql_real_connect() to host [%s], port %d, unix socket [%s], database [%s], user [%s], password [%s]: error %d (%s)", host_name.c_str(), tcp_port, unix_socket.c_str(), db_name.c_str(), user_name.c_str(), "xxx", mysql_errno(fMysql), mysql_error(fMysql));
      Disconnect();
      return DB_FILE_ERROR;
   }

   int status;

   // FIXME:
   //my_bool reconnect = 0;
   //mysql_options(&mysql, MYSQL_OPT_RECONNECT, &reconnect);

   status = Exec("(notable)", "SET SESSION sql_mode='ANSI'");
   if (status != DB_SUCCESS) {
      cm_msg(MERROR, "Mysql::Connect", "Cannot set ANSI mode, nothing will work");
      Disconnect();
      return DB_FILE_ERROR;
   }

   if (fDebug) {
      cm_msg(MINFO, "Mysql::Connect", "Connected to a MySQL database on host [%s], port %d, unix socket [%s], database [%s], user [%s], password [%s], buffer %d", host_name.c_str(), tcp_port, unix_socket.c_str(), db_name.c_str(), user_name.c_str(), "xxx", fMaxDisconnected);
      cm_msg_flush_buffer();
   }

   fIsConnected = true;

   int count = 0;
   while (fDisconnectedBuffer.size() > 0) {
      status = Exec("(flush)", fDisconnectedBuffer.front().c_str());
      if (status != DB_SUCCESS) {
         return status;
      }
      fDisconnectedBuffer.pop_front();
      count++;
   }

   if (count > 0) {
      cm_msg(MINFO, "Mysql::Connect", "Saved %d, lost %d history events accumulated while disconnected from the database", count, fDisconnectedLost);
      cm_msg_flush_buffer();
   }

   assert(fDisconnectedBuffer.size() == 0);
   fDisconnectedLost = 0;

   return DB_SUCCESS;
}

int Mysql::Disconnect()
{
   if (fRow) {
      // FIXME: mysql_free_result(fResult);
   }

   if (fResult)
      mysql_free_result(fResult);

   if (fMysql)
      mysql_close(fMysql);

   fMysql = NULL;
   fResult = NULL;
   fRow = NULL;

   fIsConnected = false;
   return DB_SUCCESS;
}

bool Mysql::IsConnected()
{
   return fIsConnected;
}

int Mysql::OpenTransaction(const char* table_name)
{
   return Exec(table_name, "START TRANSACTION");
   return DB_SUCCESS;
}

int Mysql::CommitTransaction(const char* table_name)
{
   Exec(table_name, "COMMIT");
   return DB_SUCCESS;
}

int Mysql::RollbackTransaction(const char* table_name)
{
   Exec(table_name, "ROLLBACK");
   return DB_SUCCESS;
}

int Mysql::ListTables(std::vector<std::string> *plist)
{
   if (!fIsConnected)
      return DB_FILE_ERROR;

   if (fDebug)
      printf("Mysql::ListTables!\n");

   int status;

   fResult = mysql_list_tables(fMysql, NULL);

   if (fResult == NULL) {
      cm_msg(MERROR, "Mysql::ListTables", "mysql_list_tables() error %d (%s)", mysql_errno(fMysql), mysql_error(fMysql));
      return DB_FILE_ERROR;
   }

   fNumFields = mysql_num_fields(fResult);

   while (1) {
      status = Step();
      if (status != DB_SUCCESS)
         break;
      std::string tn = GetText(0);
      plist->push_back(tn);
   };

   status = Finalize();

   return DB_SUCCESS;
}

int Mysql::ListColumns(const char* table_name, std::vector<std::string> *plist)
{
   if (!fIsConnected)
      return DB_FILE_ERROR;

   if (fDebug)
      printf("Mysql::ListColumns for table \'%s\'\n", table_name);

   int status;

   std::string cmd;
   cmd += "SHOW COLUMNS FROM ";
   cmd += QuoteId(table_name);
   cmd += ";";

   status = Prepare(table_name, cmd.c_str());
   if (status != DB_SUCCESS)
      return status;

   fNumFields = mysql_num_fields(fResult);

   while (1) {
      status = Step();
      if (status != DB_SUCCESS)
         break;
      std::string cn = GetText(0);
      std::string ct = GetText(1);
      plist->push_back(cn);
      plist->push_back(ct);
      //printf("cn [%s]\n", cn.c_str());
      //for (int i=0; i<fNumFields; i++)
      //printf(" field[%d]: [%s]\n", i, GetText(i));
   };

   status = Finalize();

   return DB_SUCCESS;
}

int Mysql::Exec(const char* table_name, const char* sql)
{
   if (fDebug)
      printf("Mysql::Exec(%s, %s)\n", table_name, sql);

   // FIXME: match Sqlite::Exec() return values:
   // return values:
   // DB_SUCCESS
   // DB_FILE_ERROR: not connected
   // DB_KEY_EXIST: "table already exists"

   if (!fMysql)
      return DB_FILE_ERROR;

   assert(fMysql);
   assert(fResult == NULL); // there should be no unfinalized queries
   assert(fRow == NULL);

   if (mysql_query(fMysql, sql)) {
      if (mysql_errno(fMysql) == 1050) { // "Table already exists"
         return DB_KEY_EXIST;
      }         
      if (mysql_errno(fMysql) == 1146) { // "Table does not exist"
         return DB_FILE_ERROR;
      }         
      cm_msg(MERROR, "Mysql::Exec", "mysql_query(%s) error %d (%s)", sql, mysql_errno(fMysql), mysql_error(fMysql));
      if (mysql_errno(fMysql) == 1060) // "Duplicate column name"
         return DB_KEY_EXIST;
      if (mysql_errno(fMysql) == 2006) { // "MySQL server has gone away"
         Disconnect();
         return ExecDisconnected(table_name, sql);
      }
      return DB_FILE_ERROR;
   }

   return DB_SUCCESS;
}

int Mysql::ExecDisconnected(const char* table_name, const char* sql)
{
   if (fDebug)
      printf("Mysql::ExecDisconnected(%s, %s)\n", table_name, sql);

   if (fDisconnectedBuffer.size() < fMaxDisconnected) {
      fDisconnectedBuffer.push_back(sql);
      if (fDisconnectedBuffer.size() >= fMaxDisconnected) {
         cm_msg(MERROR, "Mysql::ExecDisconnected", "Error: Disconnected database buffer overflow, size %d, subsequent events are lost", (int)fDisconnectedBuffer.size());
      }
   } else {
      fDisconnectedLost++;
   }

   time_t now = time(NULL);

   if (fNextReconnect == 0 || now >= fNextReconnect) {
      int status = Connect(fConnectString.c_str());
      if (status == DB_SUCCESS) {
         fNextReconnect = 0;
         fNextReconnectDelaySec = 0;
      } else {
         if (fNextReconnectDelaySec == 0) {
            fNextReconnectDelaySec = 5;
         } else if (fNextReconnectDelaySec < 10*60) {
            fNextReconnectDelaySec *= 2;
         }
         if (fDebug) {
            cm_msg(MINFO, "Mysql::ExecDisconnected", "Next reconnect attempt in %d sec, history events buffered %d, lost %d", fNextReconnectDelaySec, (int)fDisconnectedBuffer.size(), fDisconnectedLost);
            cm_msg_flush_buffer();
         }
         fNextReconnect = now + fNextReconnectDelaySec;
      }
   }
   
   return DB_SUCCESS;
}

int Mysql::Prepare(const char* table_name, const char* sql)
{
   if (fDebug)
      printf("Mysql::Prepare(%s, %s)\n", table_name, sql);

   if (!fMysql)
      return DB_FILE_ERROR;

   assert(fMysql);
   assert(fResult == NULL); // there should be no unfinalized queries
   assert(fRow == NULL);

   //   if (mysql_query(fMysql, sql)) {
   //   cm_msg(MERROR, "Mysql::Prepare", "mysql_query(%s) error %d (%s)", sql, mysql_errno(fMysql), mysql_error(fMysql));
   //   return DB_FILE_ERROR;
   //}

   // Check if the connection to MySQL timed out; fix from B. Smith
   int status = mysql_query(fMysql, sql);
   if (status) {
      if (mysql_errno(fMysql) == 2006 || mysql_errno(fMysql) == 2013) {
         // "MySQL server has gone away" or "Lost connection to MySQL server during query"
         status = Connect(fConnectString.c_str());
         if (status == DB_SUCCESS) {
            // Retry after reconnecting
            status = mysql_query(fMysql, sql);
         } else {
            cm_msg(MERROR, "Mysql::Prepare", "mysql_query(%s) - MySQL server has gone away, and couldn't reconnect - %d", sql, status);
            return DB_FILE_ERROR;
         }
      }
      if (status) {
         cm_msg(MERROR, "Mysql::Prepare", "mysql_query(%s) error %d (%s)", sql, mysql_errno(fMysql), mysql_error(fMysql));
         return DB_FILE_ERROR;
      }
      cm_msg(MINFO, "Mysql::Prepare", "Reconnected to MySQL after long inactivity.");
   }

   fResult = mysql_store_result(fMysql);
   //fResult = mysql_use_result(fMysql); // cannot use this because it blocks writing into table

   if (!fResult) {
      cm_msg(MERROR, "Mysql::Prepare", "mysql_store_result(%s) returned NULL, error %d (%s)", sql, mysql_errno(fMysql), mysql_error(fMysql));
      return DB_FILE_ERROR;
   }

   fNumFields = mysql_num_fields(fResult);

   //printf("num fields %d\n", fNumFields);

   return DB_SUCCESS;
}

int Mysql::Step()
{
   if (/* DISABLES CODE */ (0) && fDebug)
      printf("Mysql::Step()\n");

   assert(fMysql);
   assert(fResult);

   fRow = mysql_fetch_row(fResult);

   if (fRow)
      return DB_SUCCESS;

   if (mysql_errno(fMysql) == 0)
      return DB_NO_MORE_SUBKEYS;

   cm_msg(MERROR, "Mysql::Step", "mysql_fetch_row() error %d (%s)", mysql_errno(fMysql), mysql_error(fMysql));

   return DB_FILE_ERROR;
}

const char* Mysql::GetText(int column)
{
   assert(fMysql);
   assert(fResult);
   assert(fRow);
   assert(fNumFields > 0);
   assert(column >= 0);
   assert(column < fNumFields);
   if (fRow[column] == NULL)
      return "";
   return fRow[column];
}

double Mysql::GetDouble(int column)
{
   return atof(GetText(column));
}

time_t Mysql::GetTime(int column)
{
   return strtoul(GetText(column), NULL, 0);
}

int Mysql::Finalize()
{
   assert(fMysql);
   assert(fResult);

   mysql_free_result(fResult);
   fResult = NULL;
   fRow = NULL;
   fNumFields = 0;

   return DB_SUCCESS;
}

const char* Mysql::ColumnType(int midas_tid)
{
   assert(midas_tid>=0);
   assert(midas_tid<TID_LAST);
   return sql_type_mysql[midas_tid];
}

bool Mysql::TypesCompatible(int midas_tid, const char* sql_type)
{
   if (/* DISABLES CODE */ (0))
      printf("compare types midas \'%s\'=\'%s\' and sql \'%s\'\n", rpc_tid_name(midas_tid), ColumnType(midas_tid), sql_type);

   //if (sql2midasType_mysql(sql_type) == midas_tid)
   //   return true;

   if (strcasecmp(ColumnType(midas_tid), sql_type) == 0)
      return true;

   // permit writing FLOAT into DOUBLE
   if (midas_tid==TID_FLOAT && strcmp(sql_type, "double")==0)
      return true;

   // T2K quirk!
   // permit writing BYTE into signed tinyint
   if (midas_tid==TID_BYTE && strcmp(sql_type, "tinyint")==0)
      return true;

   // T2K quirk!
   // permit writing WORD into signed tinyint
   if (midas_tid==TID_WORD && strcmp(sql_type, "tinyint")==0)
      return true;

   // mysql quirk!
   //if (midas_tid==TID_DWORD && strcmp(sql_type, "int(10) unsigned")==0)
   //   return true;

   if (/* DISABLES CODE */ (0))
      printf("type mismatch!\n");

   return false;
}

std::string Mysql::QuoteId(const char* s)
{
   std::string q;
   q += "`";
   q += s;
   q += "`";
   return q;
}

std::string Mysql::QuoteString(const char* s)
{
   std::string q;
   q += "\'";
   q += s;
#if 0
   while (int c = *s++) {
      if (c == '\'') {
         q += "\\'";
      } if (c == '"') {
         q += "\\\"";
      } else if (isprint(c)) {
         q += c;
      } else {
         char buf[256];
         sprintf(buf, "\\\\x%02x", c&0xFF);
         q += buf;
      }
   }
#endif
   q += "\'";
   return q;
}

#endif // HAVE_MYSQL

#ifdef HAVE_SQLITE

////////////////////////////////////////
//        SQLITE database access      //
////////////////////////////////////////

#include <sqlite3.h>

typedef std::map<std::string, sqlite3*> DbMap;

class Sqlite: public SqlBase
{
public:
   std::string fPath;

   DbMap fMap;

   // temporary storage of query data
   sqlite3* fTempDB;
   sqlite3_stmt* fTempStmt;

   Sqlite(); // ctor
   ~Sqlite(); // dtor

   int Connect(const char* path);
   int Disconnect();
   bool IsConnected();

   int ConnectTable(const char* table_name);
   sqlite3* GetTable(const char* table_name);

   int ListTables(std::vector<std::string> *plist);
   int ListColumns(const char* table_name, std::vector<std::string> *plist);

   int Exec(const char* table_name, const char* sql);
   int ExecDisconnected(const char* table_name, const char* sql);

   int Prepare(const char* table_name, const char* sql);
   int Step();
   const char* GetText(int column);
   time_t      GetTime(int column);
   double      GetDouble(int column);
   int Finalize();

   int OpenTransaction(const char* table_name);
   int CommitTransaction(const char* table_name);
   int RollbackTransaction(const char* table_name);

   const char* ColumnType(int midas_tid);
   bool TypesCompatible(int midas_tid, const char* sql_type);

   std::string QuoteId(const char* s);
   std::string QuoteString(const char* s);
};

std::string Sqlite::QuoteId(const char* s)
{
   std::string q;
   q += "\"";
   q += s;
   q += "\"";
   return q;
}

std::string Sqlite::QuoteString(const char* s)
{
   std::string q;
   q += "\'";
   q += s;
   q += "\'";
   return q;
}

const char* Sqlite::ColumnType(int midas_tid)
{
   assert(midas_tid>=0);
   assert(midas_tid<TID_LAST);
   return sql_type_sqlite[midas_tid];
}

bool Sqlite::TypesCompatible(int midas_tid, const char* sql_type)
{
   if (0)
      printf("compare types midas \'%s\'=\'%s\' and sql \'%s\'\n", rpc_tid_name(midas_tid), ColumnType(midas_tid), sql_type);

   //if (sql2midasType_sqlite(sql_type) == midas_tid)
   //   return true;

   if (strcasecmp(ColumnType(midas_tid), sql_type) == 0)
      return true;

   // permit writing FLOAT into DOUBLE
   if (midas_tid==TID_FLOAT && strcasecmp(sql_type, "double")==0)
      return true;

   return false;
}

const char* Sqlite::GetText(int column)
{
   return (const char*)sqlite3_column_text(fTempStmt, column);
}

time_t Sqlite::GetTime(int column)
{
   return sqlite3_column_int64(fTempStmt, column);
}

double Sqlite::GetDouble(int column)
{
   return sqlite3_column_double(fTempStmt, column);
}

Sqlite::Sqlite() // ctor
{
   fIsConnected = false;
   fTempDB = NULL;
   fTempStmt = NULL;
   fDebug = 0;
}

Sqlite::~Sqlite() // dtor
{
   Disconnect();
}

const char* xsqlite3_errstr(sqlite3* db, int errcode)
{
   //return sqlite3_errstr(errcode);
   return sqlite3_errmsg(db);
}

int Sqlite::ConnectTable(const char* table_name)
{
   std::string fname = fPath + "mh_" + table_name + ".sqlite3";

   sqlite3* db = NULL;

   int status = sqlite3_open(fname.c_str(), &db);

   if (status != SQLITE_OK) {
      cm_msg(MERROR, "Sqlite::Connect", "Table %s: sqlite3_open(%s) error %d (%s)", table_name, fname.c_str(), status, xsqlite3_errstr(db, status));
      sqlite3_close(db);
      db = NULL;
      return DB_FILE_ERROR;
   }

#if SQLITE_VERSION_NUMBER >= 3006020
   status = sqlite3_extended_result_codes(db, 1);
   if (status != SQLITE_OK) {
      cm_msg(MERROR, "Sqlite::Connect", "Table %s: sqlite3_extended_result_codes(1) error %d (%s)", table_name, status, xsqlite3_errstr(db, status));
   }
#else
#warning Missing sqlite3_extended_result_codes()!
#endif

   fMap[table_name] = db;

   Exec(table_name, "PRAGMA journal_mode=persist;");
   Exec(table_name, "PRAGMA synchronous=normal;");
   //Exec(table_name, "PRAGMA synchronous=off;");
   Exec(table_name, "PRAGMA journal_size_limit=-1;");

   if (0) {
      Exec(table_name, "PRAGMA legacy_file_format;");
      Exec(table_name, "PRAGMA synchronous;");
      Exec(table_name, "PRAGMA journal_mode;");
      Exec(table_name, "PRAGMA journal_size_limit;");
   }

#ifdef SQLITE_LIMIT_COLUMN
   if (0) {
      int max_columns = sqlite3_limit(db, SQLITE_LIMIT_COLUMN, -1);
      printf("Sqlite::Connect: SQLITE_LIMIT_COLUMN=%d\n", max_columns);
   }
#endif

   if (fDebug)
      cm_msg(MINFO, "Sqlite::Connect", "Table %s: connected to Sqlite file \'%s\'", table_name, fname.c_str());

   return DB_SUCCESS;
}

sqlite3* Sqlite::GetTable(const char* table_name)
{
   sqlite3* db = fMap[table_name];

   if (db)
      return db;

   int status = ConnectTable(table_name);
   if (status != DB_SUCCESS)
      return NULL;

   return fMap[table_name];
}

int Sqlite::Connect(const char* path)
{
   if (fIsConnected)
      Disconnect();

   fPath = path;

   // add trailing '/'
   if (fPath.length() > 0) {
      if (fPath[fPath.length()-1] != DIR_SEPARATOR)
         fPath += DIR_SEPARATOR_STR;
   }

   if (fDebug)
      cm_msg(MINFO, "Sqlite::Connect", "Connected to Sqlite database in \'%s\'", fPath.c_str());

   fIsConnected = true;

   return DB_SUCCESS;
}

int Sqlite::Disconnect()
{
   if (!fIsConnected)
      return DB_SUCCESS;

   for (DbMap::iterator iter = fMap.begin(); iter != fMap.end(); ++iter) {
      const char* table_name = iter->first.c_str();
      sqlite3* db = iter->second;
      int status = sqlite3_close(db);
      if (status != SQLITE_OK) {
         cm_msg(MERROR, "Sqlite::Disconnect", "sqlite3_close(%s) error %d (%s)", table_name, status, xsqlite3_errstr(db, status));
      }
   }

   fMap.clear();

   fIsConnected = false;

   return DB_SUCCESS;
}

bool Sqlite::IsConnected()
{
   return fIsConnected;
}

int Sqlite::OpenTransaction(const char* table_name)
{
   int status = Exec(table_name, "BEGIN TRANSACTION");
   return status;
}

int Sqlite::CommitTransaction(const char* table_name)
{
   int status = Exec(table_name, "COMMIT TRANSACTION");
   return status;
}

int Sqlite::RollbackTransaction(const char* table_name)
{
   int status = Exec(table_name, "ROLLBACK TRANSACTION");
   return status;
}

int Sqlite::Prepare(const char* table_name, const char* sql)
{
   sqlite3* db = GetTable(table_name);
   if (!db)
      return DB_FILE_ERROR;

   if (fDebug)
      printf("Sqlite::Prepare(%s, %s)\n", table_name, sql);

   assert(fTempDB==NULL);
   fTempDB = db;

#if SQLITE_VERSION_NUMBER >= 3006020
   int status = sqlite3_prepare_v2(db, sql, strlen(sql), &fTempStmt, NULL);
#else
#warning Missing sqlite3_prepare_v2()!
   int status = sqlite3_prepare(db, sql, strlen(sql), &fTempStmt, NULL);
#endif

   if (status == SQLITE_OK)
      return DB_SUCCESS;

   std::string sqlstring = sql;
   cm_msg(MERROR, "Sqlite::Prepare", "Table %s: sqlite3_prepare_v2(%s...) error %d (%s)", table_name, sqlstring.substr(0,60).c_str(), status, xsqlite3_errstr(db, status));

   fTempDB = NULL;

   return DB_FILE_ERROR;
}

int Sqlite::Step()
{
   if (0 && fDebug)
      printf("Sqlite::Step()\n");

   assert(fTempDB);
   assert(fTempStmt);

   int status = sqlite3_step(fTempStmt);

   if (status == SQLITE_DONE)
      return DB_NO_MORE_SUBKEYS;

   if (status == SQLITE_ROW)
      return DB_SUCCESS;

   cm_msg(MERROR, "Sqlite::Step", "sqlite3_step() error %d (%s)", status, xsqlite3_errstr(fTempDB, status));

   return DB_FILE_ERROR;
}

int Sqlite::Finalize()
{
   if (0 && fDebug)
      printf("Sqlite::Finalize()\n");

   assert(fTempDB);
   assert(fTempStmt);

   int status = sqlite3_finalize(fTempStmt);

   if (status != SQLITE_OK) {
      cm_msg(MERROR, "Sqlite::Finalize", "sqlite3_finalize() error %d (%s)", status, xsqlite3_errstr(fTempDB, status));

      fTempDB = NULL;
      fTempStmt = NULL; // FIXME: maybe a memory leak?
      return DB_FILE_ERROR;
   }

   fTempDB = NULL;
   fTempStmt = NULL;

   return DB_SUCCESS;
}

int Sqlite::ListTables(std::vector<std::string> *plist)
{
   if (!fIsConnected)
      return DB_FILE_ERROR;

   if (fDebug)
      printf("Sqlite::ListTables at path [%s]\n", fPath.c_str());

   int status;

   const char* cmd = "SELECT name FROM sqlite_master WHERE type='table' ORDER BY name;";

   DIR *dir = opendir(fPath.c_str());
   if (!dir) {
      cm_msg(MERROR, "Sqlite::ListTables", "Cannot opendir(%s), errno %d (%s)", fPath.c_str(), errno, strerror(errno));
      return HS_FILE_ERROR;
   }

   while (1) {
      const struct dirent* de = readdir(dir);
      if (!de)
         break;

      const char* dn = de->d_name;

      //if (dn[0]!='m' || dn[1]!='h')
      //continue;

      const char* s;

      s = strstr(dn, "mh_");
      if (!s || s!=dn)
         continue;

      s = strstr(dn, ".sqlite3");
      if (!s || s[8]!=0)
         continue;

      char table_name[256];
      STRLCPY(table_name, dn+3);
      // FIXME: skip names like "xxx.sqlite3~" and "xxx.sqlite3-deleted"
      char* ss = strstr(table_name, ".sqlite3");
      if (!ss)
         continue;
      *ss = 0;

      //printf("dn [%s] tn [%s]\n", dn, table_name);

      status = Prepare(table_name, cmd);
      if (status != DB_SUCCESS)
         continue;

      while (1) {
         status = Step();
         if (status != DB_SUCCESS)
            break;

         const char* tn = GetText(0);
         //printf("table [%s]\n", tn);
         plist->push_back(tn);
      }

      status = Finalize();
   }

   closedir(dir);
   dir = NULL;

   return DB_SUCCESS;
}

int Sqlite::ListColumns(const char* table, std::vector<std::string> *plist)
{
   if (!fIsConnected)
      return DB_FILE_ERROR;

   if (fDebug)
      printf("Sqlite::ListColumns for table \'%s\'\n", table);

   std::string cmd;
   cmd = "PRAGMA table_info(";
   cmd += table;
   cmd += ");";

   int status;

   status = Prepare(table, cmd.c_str());
   if (status != DB_SUCCESS)
      return status;

   while (1) {
      status = Step();
      if (status != DB_SUCCESS)
         break;

      const char* colname = GetText(1);
      const char* coltype = GetText(2);
      //printf("column [%s] [%s]\n", colname, coltype);
      plist->push_back(colname); // column name
      plist->push_back(coltype); // column type
   }

   status = Finalize();

   return DB_SUCCESS;
}

static int callback_debug = 0;

static int callback(void *NotUsed, int argc, char **argv, char **azColName){
   if (callback_debug) {
      printf("history_sqlite::callback---->\n");
      for (int i=0; i<argc; i++){
         printf("history_sqlite::callback[%d] %s = %s\n", i, azColName[i], argv[i] ? argv[i] : "NULL");
      }
   }
   return 0;
}

int Sqlite::Exec(const char* table_name, const char* sql)
{
   // return values:
   // DB_SUCCESS
   // DB_FILE_ERROR: not connected
   // DB_KEY_EXIST: "table already exists"

   if (!fIsConnected)
      return DB_FILE_ERROR;

   sqlite3* db = GetTable(table_name);
   if (!db)
      return DB_FILE_ERROR;

   if (fDebug)
      printf("Sqlite::Exec(%s, %s)\n", table_name, sql);

   int status;

   callback_debug = fDebug;
   char* errmsg = NULL;

   status = sqlite3_exec(db, sql, callback, 0, &errmsg);
   if (status != SQLITE_OK) {
      if (status == SQLITE_ERROR && strstr(errmsg, "duplicate column name"))
         return DB_KEY_EXIST;
      if (status == SQLITE_ERROR && strstr(errmsg, "already exists"))
         return DB_KEY_EXIST;
      std::string sqlstring = sql;
      cm_msg(MERROR, "Sqlite::Exec", "Table %s: sqlite3_exec(%s...) error %d (%s)", table_name, sqlstring.substr(0,60).c_str(), status, errmsg);
      sqlite3_free(errmsg);
      return DB_FILE_ERROR;
   }

   return DB_SUCCESS;
}

int Sqlite::ExecDisconnected(const char* table_name, const char* sql)
{
   cm_msg(MERROR, "Sqlite::Exec", "sqlite driver does not support disconnected operations");
   return DB_FILE_ERROR;
}

#endif // HAVE_SQLITE

////////////////////////////////////
//    Methods of HsFileSchema     //
////////////////////////////////////

int HsFileSchema::write_event(const time_t t, const char* data, const int data_size)
{
   HsFileSchema* s = this;

   int status;

   if (s->writer_fd < 0) {
      s->writer_fd = open(s->file_name.c_str(), O_RDWR);
      if (s->writer_fd < 0) {
         cm_msg(MERROR, "FileHistory::write_event", "Cannot write to \'%s\', open() errno %d (%s)", s->file_name.c_str(), errno, strerror(errno));
         return HS_FILE_ERROR;
      }

      int file_size = lseek(s->writer_fd, 0, SEEK_END);

      int nrec = (file_size - s->data_offset)/s->record_size;
      if (nrec < 0)
         nrec = 0;
      int data_end = s->data_offset + nrec*s->record_size;

      //printf("file_size %d, nrec %d, data_end %d\n", file_size, nrec, data_end);

      if (data_end != file_size) {
         if (nrec > 0)
            cm_msg(MERROR, "FileHistory::write_event", "File \'%s\' may be truncated, data offset %d, record size %d, file size: %d, should be %d, truncating the file", s->file_name.c_str(), s->data_offset, s->record_size, file_size, data_end);

         status = lseek(s->writer_fd, data_end, SEEK_SET);
         if (status < 0) {
            cm_msg(MERROR, "FileHistory::write_event", "Cannot seek \'%s\' to offset %d, lseek() errno %d (%s)", s->file_name.c_str(), data_end, errno, strerror(errno));
            return HS_FILE_ERROR;
         }
         status = ftruncate(s->writer_fd, data_end);
         if (status < 0) {
            cm_msg(MERROR, "FileHistory::write_event", "Cannot truncate \'%s\' to size %d, ftruncate() errno %d (%s)", s->file_name.c_str(), data_end, errno, strerror(errno));
            return HS_FILE_ERROR;
         }
      }
   }

   int expected_size = s->record_size - 4;

   // sanity check: record_size and n_bytes are computed from the byte counts in the file header
   assert(expected_size == s->n_bytes);

   if (s->last_size == 0)
      s->last_size = expected_size;

   if (data_size != s->last_size) {
      cm_msg(MERROR, "FileHistory::write_event", "Event \'%s\' data size mismatch, expected %d bytes, got %d bytes, previously %d bytes", s->event_name.c_str(), expected_size, data_size, s->last_size);
      //printf("schema:\n");
      //s->print();

      if (data_size < expected_size)
         return HS_FILE_ERROR;

      // truncate for now
      // data_size = expected_size;
      s->last_size = data_size;
   }

   status = write(s->writer_fd, &t, 4);
   if (status != 4) {
      cm_msg(MERROR, "FileHistory::write_event", "Cannot write to \'%s\', write(timestamp) errno %d (%s)", s->file_name.c_str(), errno, strerror(errno));
      return HS_FILE_ERROR;
   }

   status = write(s->writer_fd, data, expected_size);
   if (status != expected_size) {
      cm_msg(MERROR, "FileHistory::write_event", "Cannot write to \'%s\', write(%d) errno %d (%s)", s->file_name.c_str(), data_size, errno, strerror(errno));
      return HS_FILE_ERROR;
   }

   return HS_SUCCESS;
}

int HsFileSchema::close()
{
   if (writer_fd >= 0) {
      ::close(writer_fd);
      writer_fd = -1;
   }
   return HS_SUCCESS;
}

static int ReadRecord(const char* file_name, int fd, int offset, int recsize, int irec, char* rec)
{
   int status;
   int fpos = offset + irec*recsize;

   status = ::lseek(fd, fpos, SEEK_SET);
   if (status == -1) {
      cm_msg(MERROR, "FileHistory::ReadRecord", "Cannot read \'%s\', lseek(%d) errno %d (%s)", file_name, fpos, errno, strerror(errno));
      return -1;
   }

   status = ::read(fd, rec, recsize);
   if (status == 0) {
      cm_msg(MERROR, "FileHistory::ReadRecord", "Cannot read \'%s\', unexpected end of file on read()", file_name);
      return -1;
   }
   if (status != recsize) {
      cm_msg(MERROR, "FileHistory::ReadRecord", "Cannot read \'%s\', read() errno %d (%s)", file_name, errno, strerror(errno));
      return -1;
   }
   return HS_SUCCESS;
}

static int FindTime(const char* file_name, int fd, int offset, int recsize, int nrec, time_t timestamp, int* i1p, time_t* t1p, int* i2p, time_t* t2p, time_t* tstart, time_t* tend, int debug)
{
   //
   // purpose: find location time timestamp inside given file.
   // uses binary search
   // returns:
   // tstart, tend - time of first and last data in a file
   // i1p,t1p - data just before timestamp, used as "last_written"
   // i2p,t2p - data at timestamp or after timestamp, used as starting point to read data from file
   // assertions:
   // tstart <= t1p < t2p <= tend
   // i1p+1==i2p
   // t1p < timestamp <= t2p
   //
   // special cases:
   // 1) timestamp <= tstart - all data is in the future, return i1p==-1, t1p==-1, i2p==0, t2p==tstart
   // 2) tend < timestamp - all the data is in the past, return i1p = nrec-1, t1p = tend, i2p = nrec, t2p = 0;
   // 3) nrec == 1 only one record in this file and it is older than the timestamp (tstart == tend < timestamp)
   //

   int status;
   char* buf = new char[recsize];

   assert(nrec > 0);

   int rec1 = 0;
   int rec2 = nrec-1;

   status = ReadRecord(file_name, fd, offset, recsize, rec1, buf);
   if (status != HS_SUCCESS) {
      delete[] buf;
      return HS_FILE_ERROR;
   }

   time_t t1 = *(DWORD*)buf;

   *tstart = t1;

   // timestamp is older than any data in this file
   if (timestamp <= t1) {
      *i1p    = -1;
      *t1p    =  0;
      *i2p    = 0;
      *t2p    = t1;
      *tend   = 0;
      delete[] buf;
      return HS_SUCCESS;
   }

   assert(t1 < timestamp);

   if (nrec == 1) {
      *i1p    = 0;
      *t1p    = t1;
      *i2p    = nrec; // == 1
      *t2p    = 0;
      *tend   = t1;
      delete[] buf;
      return HS_SUCCESS;
   }

   status = ReadRecord(file_name, fd, offset, recsize, rec2, buf);
   if (status != HS_SUCCESS) {
      delete[] buf;
      return HS_FILE_ERROR;
   }

   time_t t2 = *(DWORD*)buf;

   *tend = t2;

   // all the data is in the past
   if (t2 < timestamp) {
      *i1p = rec2;
      *t1p = t2;
      *i2p = nrec;
      *t2p = 0;
      delete[] buf;
      return HS_SUCCESS;
   }

   assert(t1 < timestamp);
   assert(timestamp <= t2);

   if (debug)
      printf("FindTime: rec %d..(x)..%d, time %s..(%s)..%s\n", rec1, rec2, TimeToString(t1).c_str(), TimeToString(timestamp).c_str(), TimeToString(t2).c_str());

   // implement binary search

   do {
      int rec = (rec1+rec2)/2;

      assert(rec >= 0);
      assert(rec < nrec);

      status = ReadRecord(file_name, fd, offset, recsize, rec, buf);
      if (status != HS_SUCCESS) {
         delete[] buf;
         return HS_FILE_ERROR;
      }

      time_t t = *(DWORD*)buf;

      if (timestamp <= t) {
         if (debug)
            printf("FindTime: rec %d..(x)..%d..%d, time %s..(%s)..%s..%s\n", rec1, rec, rec2, TimeToString(t1).c_str(), TimeToString(timestamp).c_str(), TimeToString(t).c_str(), TimeToString(t2).c_str());

         rec2 = rec;
         t2 = t;
      } else {
         if (debug)
            printf("FindTime: rec %d..%d..(x)..%d, time %s..%s..(%s)..%s\n", rec1, rec, rec2, TimeToString(t1).c_str(), TimeToString(t).c_str(), TimeToString(timestamp).c_str(), TimeToString(t2).c_str());

         rec1 = rec;
         t1 = t;
      }
   } while (rec2 - rec1 > 1);

   assert(rec1+1 == rec2);
   assert(t1 < timestamp);
   assert(timestamp <= t2);

   if (debug)
      printf("FindTime: rec %d..(x)..%d, time %s..(%s)..%s, this is the result.\n", rec1, rec2, TimeToString(t1).c_str(), TimeToString(timestamp).c_str(), TimeToString(t2).c_str());

   *i1p = rec1;
   *t1p = t1;

   *i2p = rec2;
   *t2p = t2;

   delete[] buf;
   return HS_SUCCESS;
}

int HsFileSchema::read_last_written(const time_t timestamp,
                                    const int debug,
                                    time_t* last_written)
{
   int status;
   HsFileSchema* s = this;

   if (debug)
      printf("FileHistory::read_last_written: file %s, schema time %s..%s, timestamp %s\n", s->file_name.c_str(), TimeToString(s->time_from).c_str(), TimeToString(s->time_to).c_str(), TimeToString(timestamp).c_str());

   int fd = open(s->file_name.c_str(), O_RDONLY);
   if (fd < 0) {
      cm_msg(MERROR, "FileHistory::read_last_written", "Cannot read \'%s\', open() errno %d (%s)", s->file_name.c_str(), errno, strerror(errno));
      return HS_FILE_ERROR;
   }

   int file_size = ::lseek(fd, 0, SEEK_END);

   int nrec = (file_size - s->data_offset)/s->record_size;
   if (nrec < 0)
      nrec = 0;

   if (nrec < 1) {
      ::close(fd);
      if (last_written)
         *last_written = 0;
      return HS_SUCCESS;
   }

   time_t lw = 0;

   // read last record to check if desired time is inside or outside of the file

   if (1) {
      char* buf = new char[s->record_size];

      status = ReadRecord(s->file_name.c_str(), fd, s->data_offset, s->record_size, nrec - 1, buf);
      if (status != HS_SUCCESS) {
         delete[] buf;
         ::close(fd);
         return HS_FILE_ERROR;
      }

      lw = *(DWORD*)buf;

      delete[] buf;
   }

   if (lw >= timestamp) {
      int irec = 0;
      time_t trec = 0;
      int iunused = 0;
      time_t tunused = 0;
      time_t tstart = 0; // not used
      time_t tend   = 0; // not used

      status = FindTime(s->file_name.c_str(), fd, s->data_offset, s->record_size, nrec, timestamp, &irec, &trec, &iunused, &tunused, &tstart, &tend, 0*debug);
      if (status != HS_SUCCESS) {
         ::close(fd);
         return HS_FILE_ERROR;
      }

      assert(trec < timestamp);

      lw = trec;
   }

   if (last_written)
      *last_written = lw;

   if (debug)
      printf("FileHistory::read_last_written: file %s, schema time %s..%s, timestamp %s, last_written %s\n", s->file_name.c_str(), TimeToString(s->time_from).c_str(), TimeToString(s->time_to).c_str(), TimeToString(timestamp).c_str(), TimeToString(lw).c_str());

   assert(lw < timestamp);

   ::close(fd);

   return HS_SUCCESS;
}

int HsFileSchema::read_data(const time_t start_time,
                            const time_t end_time,
                            const int num_var, const std::vector<int>& var_schema_index, const int var_index[],
                            const int debug,
                            std::vector<time_t>& last_time,
                            MidasHistoryBufferInterface* buffer[])
{
   HsFileSchema* s = this;

   if (debug)
      printf("FileHistory::read_data: file %s, schema time %s..%s, read time %s..%s, %d vars\n", s->file_name.c_str(), TimeToString(s->time_from).c_str(), TimeToString(s->time_to).c_str(), TimeToString(start_time).c_str(), TimeToString(end_time).c_str(), num_var);

   //if (1) {
   //   printf("Last time: ");
   //   for (int i=0; i<num_var; i++) {
   //      printf(" %s", TimeToString(last_time[i]).c_str());
   //   }
   //   printf("\n");
   //}

   if (debug) {
      printf("FileHistory::read_data: file %s map", s->file_name.c_str());
      for (size_t i=0; i<var_schema_index.size(); i++) {
         printf(" %2d", var_schema_index[i]);
      }
      printf("\n");
   }

   int fd = ::open(s->file_name.c_str(), O_RDONLY);
   if (fd < 0) {
      cm_msg(MERROR, "FileHistory::read_data", "Cannot read \'%s\', open() errno %d (%s)", s->file_name.c_str(), errno, strerror(errno));
      return HS_FILE_ERROR;
   }

   off_t file_size = ::lseek(fd, 0, SEEK_END);

   if (file_size == (off_t)-1) {
      cm_msg(MERROR, "FileHistory::read_data", "Cannot read \'%s\', fseek(SEEK_END) errno %d (%s)", s->file_name.c_str(), errno, strerror(errno));
      ::close(fd);
      return HS_FILE_ERROR;
   }

   int nrec = (file_size - s->data_offset)/s->record_size;
   if (nrec < 0)
      nrec = 0;

   if (nrec < 1) {
      ::close(fd);
      return HS_SUCCESS;
   }

   int iunused = 0;
   time_t tunused = 0;
   int irec = 0;
   time_t trec   = 0;
   time_t tstart = 0;
   time_t tend   = 0;

   int status = FindTime(s->file_name.c_str(), fd, s->data_offset, s->record_size, nrec, start_time, &iunused, &tunused, &irec, &trec, &tstart, &tend, 0*debug);

   if (status != HS_SUCCESS) {
      ::close(fd);
      return HS_FILE_ERROR;
   }

   if (debug) {
      printf("FindTime %d, nrec %d, (%d, %s) (%d, %s), tstart %s, tend %s, want %s\n", status, nrec, iunused, TimeToString(tunused).c_str(), irec, TimeToString(trec).c_str(), TimeToString(tstart).c_str(), TimeToString(tend).c_str(), TimeToString(start_time).c_str());
   }

   if (irec < 0 || irec >= nrec) {
      // all data in this file is older than start_time

      ::close(fd);

      if (debug)
         printf("FileHistory::read: file %s, schema time %s..%s, read time %s..%s, file time %s..%s, data in this file is too old\n", s->file_name.c_str(), TimeToString(s->time_from).c_str(), TimeToString(s->time_to).c_str(), TimeToString(start_time).c_str(), TimeToString(end_time).c_str(), TimeToString(tstart).c_str(), TimeToString(tend).c_str());

      return HS_SUCCESS;
   }

   if (tstart < s->time_from) {
      // data starts before time declared in schema

      ::close(fd);

      cm_msg(MERROR, "FileHistory::read_data", "Bad history file \'%s\': timestamp of first data %s is before schema start time %s", s->file_name.c_str(), TimeToString(tstart).c_str(), TimeToString(s->time_from).c_str());

      return HS_FILE_ERROR;
   }

   if (tend && s->time_to && tend > s->time_to) {
      // data ends after time declared in schema (overlaps with next file)

      ::close(fd);

      cm_msg(MERROR, "FileHistory::read_data", "Bad history file \'%s\': timestamp of last data %s is after schema end time %s", s->file_name.c_str(), TimeToString(tend).c_str(), TimeToString(s->time_to).c_str());

      return HS_FILE_ERROR;
   }

   for (int i=0; i<num_var; i++) {
      int si = var_schema_index[i];
      if (si < 0)
         continue;
         
      if (trec < last_time[i]) { // protect against duplicate and non-monotonous data
         ::close(fd);

         cm_msg(MERROR, "FileHistory::read_data", "Internal history error at file \'%s\': variable %d data timestamp %s is before last timestamp %s", s->file_name.c_str(), i, TimeToString(trec).c_str(), TimeToString(last_time[i]).c_str());
         
         return HS_FILE_ERROR;
      }
   }

   int count = 0;

   off_t fpos = s->data_offset + irec*s->record_size;
   
   off_t xpos = ::lseek(fd, fpos, SEEK_SET);
   if (xpos == (off_t)-1) {
      cm_msg(MERROR, "FileHistory::read_data", "Cannot read \'%s\', lseek(%zu) errno %d (%s)", s->file_name.c_str(), (size_t)fpos, errno, strerror(errno));
      ::close(fd);
      return HS_FILE_ERROR;
   }
   
   char* buf = new char[s->record_size];

   int prec = irec;
   int nread = 0;

   while (1) {
      status = ::read(fd, buf, s->record_size);
      if (status == 0) // EOF
         break;
      if (status != s->record_size) {
         cm_msg(MERROR, "FileHistory::read_data", "Cannot read \'%s\', read() errno %d (%s)", s->file_name.c_str(), errno, strerror(errno));
         break;
      }

      nread++;
      prec++;

      bool past_end_of_last_file = (s->time_to == 0) && (prec > nrec);

      time_t t = *(DWORD*)buf;
      
      if (debug > 1)
         printf("FileHistory::read: file %s, schema time %s..%s, read time %s..%s, row time %s\n", s->file_name.c_str(), TimeToString(s->time_from).c_str(), TimeToString(s->time_to).c_str(), TimeToString(start_time).c_str(), TimeToString(end_time).c_str(), TimeToString(t).c_str());
      
      if (t < trec) {
         delete[] buf;
         ::close(fd);
         cm_msg(MERROR, "FileHistory::read_data", "Bad history file \'%s\': record %d timestamp %s is before start time %s", s->file_name.c_str(), irec + count, TimeToString(t).c_str(), TimeToString(trec).c_str());
         return HS_FILE_ERROR;
      }
      
      if (tend && (t > tend) && !past_end_of_last_file) {
         delete[] buf;
         ::close(fd);
         cm_msg(MERROR, "FileHistory::read_data", "Bad history file \'%s\': record %d timestamp %s is after last timestamp %s", s->file_name.c_str(), irec + count, TimeToString(t).c_str(), TimeToString(tend).c_str());
         return HS_FILE_ERROR;
      }
      
      if (t > end_time)
         break;
      
      char* data = buf + 4;
      
      for (int i=0; i<num_var; i++) {
         int si = var_schema_index[i];
         if (si < 0)
            continue;
         
         if (t < last_time[i]) { // protect against duplicate and non-monotonous data
            delete[] buf;
            ::close(fd);

            cm_msg(MERROR, "FileHistory::read_data", "Bad history file \'%s\': record %d timestamp %s is before timestamp %s of variable %d", s->file_name.c_str(), irec + count, TimeToString(t).c_str(), TimeToString(last_time[i]).c_str(), i);

            return HS_FILE_ERROR;
         }
         
         double v = 0;
         void* ptr = data + s->offsets[si];
         
         int ii = var_index[i];
         assert(ii >= 0);
         assert(ii < s->variables[si].n_data);
         
         switch (s->variables[si].type) {
         default:
            // unknown data type
            v = 0;
            break;
         case TID_BYTE:
            v = ((unsigned char*)ptr)[ii];
            break;
         case TID_SBYTE:
            v = ((signed char *)ptr)[ii];
            break;
         case TID_CHAR:
            v = ((char*)ptr)[ii];
            break;
         case TID_WORD:
            v = ((unsigned short *)ptr)[ii];
            break;
         case TID_SHORT:
            v = ((signed short *)ptr)[ii];
            break;
         case TID_DWORD:
            v = ((unsigned int *)ptr)[ii];
            break;
         case TID_INT:
            v = ((int *)ptr)[ii];
            break;
         case TID_BOOL:
            v = ((unsigned int *)ptr)[ii];
            break;
         case TID_FLOAT:
            v = ((float*)ptr)[ii];
            break;
         case TID_DOUBLE:
            v = ((double*)ptr)[ii];
            break;
         }
         
         buffer[i]->Add(t, v);
         last_time[i] = t;
      }
      count++;
   }

   delete[] buf;

   ::close(fd);

   if (debug) {
      printf("FileHistory::read_data: file %s map", s->file_name.c_str());
      for (size_t i=0; i<var_schema_index.size(); i++) {
         printf(" %2d", var_schema_index[i]);
      }
      printf(" read %d rows\n", count);
   }

   if (debug)
      printf("FileHistory::read: file %s, schema time %s..%s, read time %s..%s, %d vars, read %d rows\n", s->file_name.c_str(), TimeToString(s->time_from).c_str(), TimeToString(s->time_to).c_str(), TimeToString(start_time).c_str(), TimeToString(end_time).c_str(), num_var, count);

   return HS_SUCCESS;
}

////////////////////////////////////////////////////////
//    Implementation of the MidasHistoryInterface     //
////////////////////////////////////////////////////////

class SchemaHistoryBase: public MidasHistoryInterface
{
protected:
   int fDebug;
   std::string fConnectString;

   // writer data
   HsSchemaVector fWriterCurrentSchema;
   std::vector<HsSchema*> fEvents;

   // reader data
   HsSchemaVector fSchema;

public:
   SchemaHistoryBase()
   {
      fDebug = 0;
   }

   virtual ~SchemaHistoryBase()
   {
      for (unsigned i=0; i<fEvents.size(); i++)
         if (fEvents[i]) {
            delete fEvents[i];
            fEvents[i] = NULL;
         }
      fEvents.clear();
   }

   virtual int hs_set_debug(int debug)
   {
      int old = fDebug;
      fDebug = debug;
      return old;
   }

   virtual int hs_connect(const char* connect_string) = 0;
   virtual int hs_disconnect() = 0;

protected:
   ////////////////////////////////////////////////////////
   //             Schema functions                       //
   ////////////////////////////////////////////////////////

   // load existing schema
   virtual int read_schema(HsSchemaVector* sv, const char* event_name, const time_t timestamp) = 0; // event_name: =NULL means read only event names, =event_name means load tag names for all matching events; timestamp: =0 means read all schema all the way to the beginning of time, =time means read schema in effect at this time and all newer schema

   // return a new copy of a schema for writing into history. If schema for this event does not exist, it will be created
   virtual HsSchema* new_event(const char* event_name, time_t timestamp, int ntags, const TAG tags[]) = 0;

public:
   ////////////////////////////////////////////////////////
   //             Functions used by mlogger              //
   ////////////////////////////////////////////////////////

   int hs_define_event(const char* event_name, time_t timestamp, int ntags, const TAG tags[]);
   int hs_write_event(const char* event_name, time_t timestamp, int buffer_size, const char* buffer);
   int hs_flush_buffers();

   ////////////////////////////////////////////////////////
   //             Functions used by mhttpd               //
   ////////////////////////////////////////////////////////

   int hs_clear_cache();
   int hs_get_events(time_t t, std::vector<std::string> *pevents);
   int hs_get_tags(const char* event_name, time_t t, std::vector<TAG> *ptags);
   int hs_get_last_written(time_t timestamp, int num_var, const char* const event_name[], const char* const var_name[], const int var_index[], time_t last_written[]);
   int hs_read_buffer(time_t start_time, time_t end_time,
                      int num_var, const char* const event_name[], const char* const var_name[], const int var_index[],
                      MidasHistoryBufferInterface* buffer[],
                      int hs_status[]);

   /*------------------------------------------------------------------*/

   class ReadBuffer: public MidasHistoryBufferInterface
   {
   public:
      time_t fFirstTime;
      time_t fLastTime;
      time_t fInterval;

      int fNumAdded;

      int      fNumAlloc;
      int     *fNumEntries;
      time_t **fTimeBuffer;
      double **fDataBuffer;

      time_t   fPrevTime;

      ReadBuffer(time_t first_time, time_t last_time, time_t interval) // ctor
      {
         fNumAdded = 0;

         fFirstTime = first_time;
         fLastTime = last_time;
         fInterval = interval;

         fNumAlloc = 0;
         fNumEntries = NULL;
         fTimeBuffer = NULL;
         fDataBuffer = NULL;

         fPrevTime = 0;
      }

      ~ReadBuffer() // dtor
      {
      }

      void Realloc(int wantalloc)
      {
         if (wantalloc < fNumAlloc - 10)
            return;

         int newalloc = fNumAlloc*2;

         if (newalloc <= 1000)
            newalloc = wantalloc + 1000;

         //printf("wantalloc %d, fNumEntries %d, fNumAlloc %d, newalloc %d\n", wantalloc, *fNumEntries, fNumAlloc, newalloc);

         *fTimeBuffer = (time_t*)realloc(*fTimeBuffer, sizeof(time_t)*newalloc);
         assert(*fTimeBuffer);

         *fDataBuffer = (double*)realloc(*fDataBuffer, sizeof(double)*newalloc);
         assert(*fDataBuffer);

         fNumAlloc = newalloc;
      }

      void Add(time_t t, double v)
      {
         if (t < fFirstTime)
            return;
         if (t > fLastTime)
            return;

         fNumAdded++;

         if ((fPrevTime==0) || (t >= fPrevTime + fInterval)) {
            int pos = *fNumEntries;

            Realloc(pos + 1);

            (*fTimeBuffer)[pos] = t;
            (*fDataBuffer)[pos] = v;

            (*fNumEntries) = pos + 1;

            fPrevTime = t;
         }
      }

      void Finish()
      {

      }
   };

   /*------------------------------------------------------------------*/

   int hs_read(time_t start_time, time_t end_time, time_t interval,
               int num_var,
               const char* const event_name[], const char* const var_name[], const int var_index[],
               int num_entries[],
               time_t* time_buffer[], double* data_buffer[],
               int st[]);
   /*------------------------------------------------------------------*/

   class BinnedBuffer: public MidasHistoryBufferInterface
   {
   public:
      int fNumBins;
      time_t fFirstTime;
      time_t fLastTime;

      int fNumEntries;
      double *fSum0;
      double *fSum1;
      double *fSum2;

      int    *fCount;
      double *fMean;
      double *fRms;
      double *fMin;
      double *fMax;

      time_t *fLastTimePtr;
      double *fLastValuePtr;

      BinnedBuffer(time_t first_time, time_t last_time, int num_bins) // ctor
      {
         fNumEntries = 0;

         fNumBins = num_bins;
         fFirstTime = first_time;
         fLastTime = last_time;

         fSum0 = new double[num_bins];
         fSum1 = new double[num_bins];
         fSum2 = new double[num_bins];

         for (int i=0; i<num_bins; i++) {
            fSum0[i] = 0;
            fSum1[i] = 0;
            fSum2[i] = 0;
         }

         fCount = NULL;
         fMean = NULL;
         fRms = NULL;
         fMin = NULL;
         fMax = NULL;
         fLastTimePtr = NULL;
         fLastValuePtr = NULL;
      }

      ~BinnedBuffer() // dtor
      {
         delete fSum0;
         delete fSum1;
         delete fSum2;
      }

      void Add(time_t t, double v)
      {
         if (t < fFirstTime)
            return;
         if (t > fLastTime)
            return;

         fNumEntries++;

         double a = (double)(t - fFirstTime);
         double b = (double)(fLastTime - fFirstTime);
         double fbin = fNumBins*a/b;

         int ibin = (int)fbin;

         if (ibin < 0)
            ibin = 0;
         else if (ibin >= fNumBins)
            ibin = fNumBins-1;

         if (fSum0[ibin] == 0) {
            if (fMin)
               fMin[ibin] = v;
            if (fMax)
               fMax[ibin] = v;
            if (fLastTimePtr)
               *fLastTimePtr = t;
            if (fLastValuePtr)
               *fLastValuePtr = v;
         }

         fSum0[ibin] += 1.0;
         fSum1[ibin] += v;
         fSum2[ibin] += v*v;

         if (fMin)
            if (v < fMin[ibin])
               fMin[ibin] = v;

         if (fMax)
            if (v > fMax[ibin])
               fMax[ibin] = v;

         if (fLastTimePtr)
            if (t > *fLastTimePtr) {
               *fLastTimePtr = t;
               if (fLastValuePtr)
                  *fLastValuePtr = v;
            }
      }

      void Finish()
      {
         for (int i=0; i<fNumBins; i++) {
            double num = fSum0[i];
            double mean = 0;
            double variance = 0;
            if (num > 0) {
               mean = fSum1[i]/num;
               variance = fSum2[i]/num-mean*mean;
            }
            double rms = 0;
            if (variance > 0)
               rms = sqrt(variance);

            if (fCount)
               fCount[i] = (int)num;

            if (fMean)
               fMean[i] = mean;

            if (fRms)
               fRms[i] = rms;

            if (num == 0) {
               if (fMin)
                  fMin[i] = 0;
               if (fMax)
                  fMax[i] = 0;
            }
         }
      }
   };

   int hs_read_binned(time_t start_time, time_t end_time, int num_bins,
                      int num_var, const char* const event_name[], const char* const var_name[], const int var_index[],
                      int num_entries[],
                      int* count_bins[], double* mean_bins[], double* rms_bins[], double* min_bins[], double* max_bins[],
                      time_t last_time[], double last_value[],
                      int st[]);
};

int SchemaHistoryBase::hs_define_event(const char* event_name, time_t timestamp, int ntags, const TAG tags[])
{
   if (fDebug) {
      printf("hs_define_event: event name [%s] with %d tags\n", event_name, ntags);
      if (fDebug > 1)
         PrintTags(ntags, tags);
   }

   // delete all events with the same name
   for (unsigned int i=0; i<fEvents.size(); i++)
      if (fEvents[i])
         if (event_name_cmp(fEvents[i]->event_name, event_name)==0) {
            if (fDebug)
               printf("deleting exising event %s\n", event_name);
            fEvents[i]->close();
            delete fEvents[i];
            fEvents[i] = NULL;
         }

   // check for wrong types etc
   for (int i=0; i<ntags; i++) {
      if (strlen(tags[i].name) < 1) {
         cm_msg(MERROR, "hs_define_event", "Error: History event \'%s\' has empty name at index %d", event_name, i);
         return HS_FILE_ERROR;
      }
      if (tags[i].type <= 0 || tags[i].type >= TID_LAST) {
         cm_msg(MERROR, "hs_define_event", "Error: History event \'%s\' tag \'%s\' at index %d has invalid type %d",
                event_name, tags[i].name, i, tags[i].type);
         return HS_FILE_ERROR;
      }
      if (tags[i].type == TID_STRING) {
         cm_msg(MERROR, "hs_define_event",
                "Error: History event \'%s\' tag \'%s\' at index %d has forbidden type TID_STRING", event_name,
                tags[i].name, i);
         return HS_FILE_ERROR;
      }
      if (tags[i].n_data <= 0) {
         cm_msg(MERROR, "hs_define_event", "Error: History event \'%s\' tag \'%s\' at index %d has invalid n_data %d",
                event_name, tags[i].name, i, tags[i].n_data);
         return HS_FILE_ERROR;
      }
   }

   // check for duplicate names. Done by sorting, since this takes only O(N*log*N))
   std::vector<std::string> names;
   for (int i=0; i<ntags; i++) {
      std::string str(tags[i].name);
      std::transform(str.begin(), str.end(), str.begin(), ::toupper);
      names.push_back(str);
   }
   std::sort(names.begin(), names.end());
   for (int i=0; i<ntags-1; i++) {
      if (names[i] == names[i + 1]) {
         cm_msg(MERROR, "hs_define_event",
                "Error: History event \'%s\' has duplicate tag name \'%s\'", event_name,
                names[i].c_str());
         return HS_FILE_ERROR;
      }
   }

   HsSchema* s = new_event(event_name, timestamp, ntags, tags);
   if (!s)
      return HS_FILE_ERROR;

   s->disabled = false;

   // keep only active variables
   std::vector<HsSchemaEntry> active_vars;

   for (auto& var : s->variables) {
      if (!var.inactive) {
         active_vars.push_back(var);
      }
   }

   s->variables = active_vars;

   // find empty slot in events list
   for (unsigned int i=0; i<fEvents.size(); i++)
      if (!fEvents[i]) {
         fEvents[i] = s;
         s = NULL;
         break;
      }

   // if no empty slots, add at the end
   if (s)
      fEvents.push_back(s);

   return HS_SUCCESS;
}

int SchemaHistoryBase::hs_write_event(const char* event_name, time_t timestamp, int buffer_size, const char* buffer)
{
   if (fDebug)
      printf("hs_write_event: write event \'%s\', time %d, size %d\n", event_name, (int)timestamp, buffer_size);

   HsSchema *s = NULL;

   // find this event
   for (size_t i=0; i<fEvents.size(); i++)
      if (fEvents[i] && (event_name_cmp(fEvents[i]->event_name, event_name)==0)) {
         s = fEvents[i];
         break;
      }

   // not found
   if (!s)
      return HS_UNDEFINED_EVENT;

   // deactivated because of error?
   if (s->disabled)
      return HS_FILE_ERROR;

   if (s->n_bytes == 0) { // compute expected data size
      // NB: history data does not have any padding!
      for (unsigned i=0; i<s->variables.size(); i++) {
         s->n_bytes += s->variables[i].n_bytes;
      }
   }

   int status;

   if (buffer_size > s->n_bytes) { // too many bytes!
      if (s->count_write_oversize == 0) {
         // only report first occurence
         // count of all occurences is reported by HsSchema destructor
         cm_msg(MERROR, "hs_write_event", "Event \'%s\' data size mismatch: expected %d bytes, got %d bytes", s->event_name.c_str(), s->n_bytes, buffer_size);
      }
      s->count_write_oversize++;
      if (buffer_size > s->write_max_size)
         s->write_max_size = buffer_size;
      status = s->write_event(timestamp, buffer, s->n_bytes);
   } else if (buffer_size < s->n_bytes) { // too few bytes
      if (s->count_write_undersize == 0) {
         // only report first occurence
         // count of all occurences is reported by HsSchema destructor
         cm_msg(MERROR, "hs_write_event", "Event \'%s\' data size mismatch: expected %d bytes, got %d bytes", s->event_name.c_str(), s->n_bytes, buffer_size);
      }
      s->count_write_undersize++;
      if (s->write_min_size == 0)
         s->write_min_size = buffer_size;
      else if (buffer_size < s->write_min_size)
         s->write_min_size = buffer_size;
      char* tmp = (char*)malloc(s->n_bytes);
      memcpy(tmp, buffer, buffer_size);
      memset(tmp + buffer_size, 0, s->n_bytes - buffer_size);
      status = s->write_event(timestamp, tmp, s->n_bytes);
      free(tmp);
   } else {
      assert(buffer_size == s->n_bytes); // obviously
      status = s->write_event(timestamp, buffer, buffer_size);
   }

   // if could not write event, deactivate it
   if (status != HS_SUCCESS) {
      s->disabled = true;
      cm_msg(MERROR, "hs_write_event", "Event \'%s\' disabled after write error %d", event_name, status);
      return HS_FILE_ERROR;
   }

   return HS_SUCCESS;
}

int SchemaHistoryBase::hs_flush_buffers()
{
   int status = HS_SUCCESS;

   if (fDebug)
      printf("hs_flush_buffers!\n");

   for (unsigned int i=0; i<fEvents.size(); i++)
      if (fEvents[i]) {
         int xstatus = fEvents[i]->flush_buffers();
         if (xstatus != HS_SUCCESS)
            status = xstatus;
      }

   return status;
}

////////////////////////////////////////////////////////
//             Functions used by mhttpd               //
////////////////////////////////////////////////////////

int SchemaHistoryBase::hs_clear_cache()
{
   if (fDebug)
      printf("SchemaHistoryBase::hs_clear_cache!\n");

   fWriterCurrentSchema.clear();
   fSchema.clear();

   return HS_SUCCESS;
}

int SchemaHistoryBase::hs_get_events(time_t t, std::vector<std::string> *pevents)
{
   if (fDebug)
      printf("hs_get_events, time %s\n", TimeToString(t).c_str());

   int status = read_schema(&fSchema, NULL, t);
   if (status != HS_SUCCESS)
      return status;

   if (fDebug) {
      printf("hs_get_events: available schema:\n");
      fSchema.print(false);
   }

   assert(pevents);

   for (unsigned i=0; i<fSchema.size(); i++) {
      HsSchema* s = fSchema[i];
      if (t && s->time_to && s->time_to < t)
         continue;
      bool dupe = false;
      for (unsigned j=0; j<pevents->size(); j++)
         if (event_name_cmp((*pevents)[j], s->event_name.c_str())==0) {
            dupe = true;
            break;
         }

      if (!dupe)
         pevents->push_back(s->event_name);
   }

   std::sort(pevents->begin(), pevents->end());

   if (fDebug) {
      printf("hs_get_events: returning %d events\n", (int)pevents->size());
      for (unsigned i=0; i<pevents->size(); i++) {
         printf("  %d: [%s]\n", i, (*pevents)[i].c_str());
      }
   }

   return HS_SUCCESS;
}

int SchemaHistoryBase::hs_get_tags(const char* event_name, time_t t, std::vector<TAG> *ptags)
{
   if (fDebug)
      printf("hs_get_tags: event [%s], time %s\n", event_name, TimeToString(t).c_str());

   assert(ptags);

   int status = read_schema(&fSchema, event_name, t);
   if (status != HS_SUCCESS)
      return status;

   bool found_event = false;
   for (unsigned i=0; i<fSchema.size(); i++) {
      HsSchema* s = fSchema[i];
      if (t && s->time_to && s->time_to < t)
         continue;

      if (event_name_cmp(s->event_name, event_name) != 0)
         continue;

      found_event = true;

      for (unsigned i=0; i<s->variables.size(); i++) {
         const char* tagname = s->variables[i].name.c_str();
         //printf("event_name [%s], table_name [%s], column name [%s], tag name [%s]\n", event_name, tn.c_str(), cn.c_str(), tagname);

         bool dupe = false;

         for (unsigned k=0; k<ptags->size(); k++)
            if (strcasecmp((*ptags)[k].name, tagname) == 0) {
               dupe = true;
               break;
            }

         if (!dupe) {
            TAG t;
            STRLCPY(t.name, tagname);
            t.type = s->variables[i].type;
            t.n_data = s->variables[i].n_data;

            ptags->push_back(t);
         }
      }
   }

   if (!found_event)
      return HS_UNDEFINED_EVENT;

   if (fDebug) {
      printf("hs_get_tags: event [%s], returning %d tags\n", event_name, (int)ptags->size());
      for (unsigned i=0; i<ptags->size(); i++) {
         printf("  tag[%d]: %s[%d] type %d\n", i, (*ptags)[i].name, (*ptags)[i].n_data, (*ptags)[i].type);
      }
   }

   return HS_SUCCESS;
}

int SchemaHistoryBase::hs_get_last_written(time_t timestamp, int num_var, const char* const event_name[], const char* const var_name[], const int var_index[], time_t last_written[])
{
   if (fDebug) {
      printf("hs_get_last_written: timestamp %s, num_var %d\n", TimeToString(timestamp).c_str(), num_var);
   }

   for (int j=0; j<num_var; j++) {
      last_written[j] = 0;
   }

   for (int i=0; i<num_var; i++) {
      int status = read_schema(&fSchema, event_name[i], 0);
      if (status != HS_SUCCESS)
         return status;
   }

   //fSchema.print(false);

   for (int i=0; i<num_var; i++) {
      for (unsigned ss=0; ss<fSchema.size(); ss++) {
         HsSchema* s = fSchema[ss];
         // schema is too new
         if (s->time_from && s->time_from >= timestamp)
            continue;
         // this schema is newer than last_written and may contain newer data?
         if (s->time_from && s->time_from < last_written[i])
            continue;
         // schema for the variables we want?
         int sindex = s->match_event_var(event_name[i], var_name[i], var_index[i]);
         if (sindex < 0)
            continue;

         time_t lw = 0;

         int status = s->read_last_written(timestamp, fDebug, &lw);

         if (status == HS_SUCCESS && lw != 0) {
            for (int j=0; j<num_var; j++) {
               int sj = s->match_event_var(event_name[j], var_name[j], var_index[j]);
               if (sj < 0)
                  continue;

               if (lw > last_written[j])
                  last_written[j] = lw;
            }
         }
      }
   }

   if (fDebug) {
      printf("hs_get_last_written: timestamp time %s, num_var %d, result:\n", TimeToString(timestamp).c_str(), num_var);
      for (int i=0; i<num_var; i++) {
         printf("  event [%s] tag [%s] index [%d] last_written %s\n", event_name[i], var_name[i], var_index[i], TimeToString(last_written[i]).c_str());
      }
   }

   return HS_SUCCESS;
}

int SchemaHistoryBase::hs_read_buffer(time_t start_time, time_t end_time,
                                      int num_var, const char* const event_name[], const char* const var_name[], const int var_index[],
                                      MidasHistoryBufferInterface* buffer[],
                                      int hs_status[])
{
   if (fDebug)
      printf("hs_read_buffer: %d variables, start time %s, end time %s\n", num_var, TimeToString(start_time).c_str(), TimeToString(end_time).c_str());

   for (int i=0; i<num_var; i++) {
      int status = read_schema(&fSchema, event_name[i], start_time);
      if (status != HS_SUCCESS)
         return status;
   }

#if 0
   if (fDebug)
      fSchema.print(false);
#endif
   
   for (int i=0; i<num_var; i++) {
      hs_status[i] = HS_UNDEFINED_VAR;
   }

   //for (unsigned ss=0; ss<fSchema.size(); ss++) {
   //   HsSchema* s = fSchema[ss];
   //   HsFileSchema* fs = dynamic_cast<HsFileSchema*>(s);
   //   assert(fs != NULL);
   //   printf("schema %d from %s to %s, filename %s\n", ss, TimeToString(fs->time_from).c_str(),  TimeToString(fs->time_to).c_str(), fs->file_name.c_str());
   //}

   // check that schema are sorted by time

#if 0
   // check that schema list is sorted by time, descending time_from, newest schema first
   for (unsigned ss=0; ss<fSchema.size(); ss++) {
      if (fDebug) {
         //printf("Check schema %zu/%zu: prev from %s, this from %s to %s, compare %d %d %d\n", ss, fSchema.size(),
         //       TimeToString(fSchema[ss-1]->time_from).c_str(),
         //       TimeToString(fSchema[ss]->time_from).c_str(),
         //       TimeToString(fSchema[ss]->time_to).c_str(),
         //       fSchema[ss-1]->time_from >= fSchema[ss]->time_to,
         //       fSchema[ss-1]->time_from > fSchema[ss]->time_from,
         //       (fSchema[ss-1]->time_from >= fSchema[ss]->time_to) && (fSchema[ss-1]->time_from > fSchema[ss]->time_from));
         printf("Schema %zu/%zu: from %s to %s, name %s\n", ss, fSchema.size(),
                TimeToString(fSchema[ss]->time_from).c_str(),
                TimeToString(fSchema[ss]->time_to).c_str(),
                fSchema[ss]->event_name.c_str());
      }

      if (ss > 0) {
         //if ((fSchema[ss-1]->time_from >= fSchema[ss]->time_to) && (fSchema[ss-1]->time_from > fSchema[ss]->time_from)) {
         if ((fSchema[ss-1]->time_from >= fSchema[ss]->time_from)) {
            // good
         } else {
            cm_msg(MERROR, "SchemaHistoryBase::hs_read_buffer", "History internal error, schema is not ordered by time. Please report this error to the midas forum.");
            return HS_FILE_ERROR;
         }
      }
   }
#endif

   std::vector<HsSchema*> slist;
   std::vector<std::vector<int>> smap;

   for (unsigned ss=0; ss<fSchema.size(); ss++) {
      HsSchema* s = fSchema[ss];
      // schema is too new?
      if (s->time_from && s->time_from > end_time)
         continue;
      // schema is too old
      if (s->time_to && s->time_to < start_time)
         continue;

      std::vector<int> sm;

      for (int i=0; i<num_var; i++) {
         // schema for the variables we want?
         int sindex = s->match_event_var(event_name[i], var_name[i], var_index[i]);
         if (sindex < 0)
            continue;

         if (sm.empty()) {
            for (int i=0; i<num_var; i++) {
               sm.push_back(-1);
            }
         }

         sm[i] = sindex;
      }

      if (!sm.empty()) {
         slist.push_back(s);
         smap.push_back(sm);
      }
   }

   if (0||fDebug) {
      printf("Found %d matching schema:\n", (int)slist.size());

      for (size_t i=0; i<slist.size(); i++) {
         HsSchema* s = slist[i];
         s->print();
         for (int k=0; k<num_var; k++)
            printf("  tag %s[%d] sindex %d\n", var_name[k], var_index[k], smap[i][k]);
      }
   }

   //for (size_t ss=0; ss<slist.size(); ss++) {
   //   HsSchema* s = slist[ss];
   //   HsFileSchema* fs = dynamic_cast<HsFileSchema*>(s);
   //   assert(fs != NULL);
   //   printf("schema %zu from %s to %s, filename %s", ss, TimeToString(fs->time_from).c_str(),  TimeToString(fs->time_to).c_str(), fs->file_name.c_str());
   //   printf(" smap ");
   //   for (int k=0; k<num_var; k++)
   //      printf(" %2d", smap[ss][k]);
   //   printf("\n");
   //}

   for (size_t ss=1; ss<slist.size(); ss++) {
      if (fDebug) {
         printf("Check schema %zu/%zu: prev from %s, this from %s to %s, compare %d\n", ss, slist.size(),
                TimeToString(slist[ss-1]->time_from).c_str(),
                TimeToString(slist[ss]->time_from).c_str(),
                TimeToString(slist[ss]->time_to).c_str(),
                slist[ss-1]->time_from >= slist[ss]->time_from);
      }
      if (slist[ss-1]->time_from >= slist[ss]->time_from) {
         // good
      } else {
         cm_msg(MERROR, "SchemaHistoryBase::hs_read_buffer", "History internal error, selected schema is not ordered by time. Please report this error to the midas forum.");
         return HS_FILE_ERROR;
      }
   }

   std::vector<time_t> last_time;

   for (int i=0; i<num_var; i++) {
      last_time.push_back(start_time);
   }

   for (int i=slist.size()-1; i>=0; i--) {
      HsSchema* s = slist[i];

      int status = s->read_data(start_time, end_time, num_var, smap[i], var_index, fDebug, last_time, buffer);

      if (status == HS_SUCCESS) {
         for (int j=0; j<num_var; j++) {
            if (smap[i][j] >= 0)
               hs_status[j] = HS_SUCCESS;
         }
      }
   }

   return HS_SUCCESS;
}

int SchemaHistoryBase::hs_read(time_t start_time, time_t end_time, time_t interval,
                               int num_var,
                               const char* const event_name[], const char* const var_name[], const int var_index[],
                               int num_entries[],
                               time_t* time_buffer[], double* data_buffer[],
                               int st[])
{
   int status;

   ReadBuffer** buffer = new ReadBuffer*[num_var];
   MidasHistoryBufferInterface** bi = new MidasHistoryBufferInterface*[num_var];

   for (int i=0; i<num_var; i++) {
      buffer[i] = new ReadBuffer(start_time, end_time, interval);
      bi[i] = buffer[i];

      // make sure outputs are initialized to something sane
      if (num_entries)
         num_entries[i] = 0;
      if (time_buffer)
         time_buffer[i] = NULL;
      if (data_buffer)
         data_buffer[i] = NULL;
      if (st)
         st[i] = 0;

      if (num_entries)
         buffer[i]->fNumEntries = &num_entries[i];
      if (time_buffer)
         buffer[i]->fTimeBuffer = &time_buffer[i];
      if (data_buffer)
         buffer[i]->fDataBuffer = &data_buffer[i];
   }

   status = hs_read_buffer(start_time, end_time,
                           num_var, event_name, var_name, var_index,
                           bi, st);

   for (int i=0; i<num_var; i++) {
      buffer[i]->Finish();
      delete buffer[i];
   }

   delete[] buffer;
   delete[] bi;

   return status;
}

int SchemaHistoryBase::hs_read_binned(time_t start_time, time_t end_time, int num_bins,
                                      int num_var, const char* const event_name[], const char* const var_name[], const int var_index[],
                                      int num_entries[],
                                      int* count_bins[], double* mean_bins[], double* rms_bins[], double* min_bins[], double* max_bins[],
                                      time_t last_time[], double last_value[],
                                      int st[])
{
   int status;

   BinnedBuffer** buffer = new BinnedBuffer*[num_var];
   MidasHistoryBufferInterface** xbuffer = new MidasHistoryBufferInterface*[num_var];

   for (int i=0; i<num_var; i++) {
      buffer[i] = new BinnedBuffer(start_time, end_time, num_bins);
      xbuffer[i] = buffer[i];

      if (count_bins)
         buffer[i]->fCount = count_bins[i];
      if (mean_bins)
         buffer[i]->fMean = mean_bins[i];
      if (rms_bins)
         buffer[i]->fRms = rms_bins[i];
      if (min_bins)
         buffer[i]->fMin = min_bins[i];
      if (max_bins)
         buffer[i]->fMax = max_bins[i];
      if (last_time)
         buffer[i]->fLastTimePtr = &last_time[i];
      if (last_value)
         buffer[i]->fLastValuePtr = &last_value[i];
   }

   status = hs_read_buffer(start_time, end_time,
                           num_var, event_name, var_name, var_index,
                           xbuffer,
                           st);

   for (int i=0; i<num_var; i++) {
      buffer[i]->Finish();
      if (num_entries)
         num_entries[i] = buffer[i]->fNumEntries;
      delete buffer[i];
   }

   delete[] buffer;
   delete[] xbuffer;

   return status;
}

////////////////////////////////////////////////////////
//                    SQL schema                      //
////////////////////////////////////////////////////////

int HsSqlSchema::close_transaction()
{
   if (!sql->IsConnected()) {
      return HS_SUCCESS;
   }
   
   int status = HS_SUCCESS;
   if (get_transaction_count() > 0) {
      status = sql->CommitTransaction(table_name.c_str());
      reset_transaction_count();
   }
   return status;
}

int HsSchema::match_event_var(const char* event_name, const char* var_name, const int var_index)
{
   if (!MatchEventName(this->event_name.c_str(), event_name))
      return -1;

   for (unsigned j=0; j<this->variables.size(); j++) {
      if (MatchTagName(this->variables[j].name.c_str(), this->variables[j].n_data, var_name, var_index)) {
         // Second clause in if() is case where MatchTagName used the "alternate tag name".
         // E.g. our variable name is "IM05[3]" (n_data=1), but we're looking for var_name="IM05" and var_index=3.
         if (var_index < this->variables[j].n_data || (this->variables[j].n_data == 1 && this->variables[j].name.find("[") != std::string::npos)) {
            return j;
         }
      }
   }

   return -1;
}

int HsSqlSchema::match_event_var(const char* event_name, const char* var_name, const int var_index)
{
   if (event_name_cmp(this->table_name, event_name)==0) {
      for (unsigned j=0; j<this->variables.size(); j++) {
         if (var_name_cmp(this->column_names[j], var_name)==0)
            return j;
      }
   }

   return HsSchema::match_event_var(event_name, var_name, var_index);
}

static HsSqlSchema* NewSqlSchema(HsSchemaVector* sv, const char* table_name, time_t t)
{
   time_t tt = 0;
   int j=-1;
   int jjx=-1; // remember oldest schema
   time_t ttx = 0;
   for (unsigned i=0; i<sv->size(); i++) {
      HsSqlSchema* s = (HsSqlSchema*)(*sv)[i];
      if (s->table_name != table_name)
         continue;

      if (s->time_from == t)
         return s;

      // remember the last schema before time t
      if (s->time_from < t) {
         if (s->time_from > tt) {
            tt = s->time_from;
            j = i;
         }
      }

      if (jjx < 0) {
         jjx = i;
         ttx = s->time_from;
      }

      if (s->time_from < ttx) {
         jjx = i;
         ttx = s->time_from;
      }

      //printf("table_name [%s], t=%s, i=%d, j=%d %s, tt=%s, dt is %d\n", table_name, TimeToString(t).c_str(), i, j, TimeToString(s->time_from).c_str(), TimeToString(tt).c_str(), (int)(s->time_from-t));
   }

   //printf("NewSqlSchema: will copy schema j=%d, tt=%d at time %d\n", j, tt, t);

   //printf("cloned schema at time %s: ", TimeToString(t).c_str());
   //(*sv)[j]->print(false);

   //printf("schema before:\n");
   //sv->print(false);

   if (j >= 0) {
      HsSqlSchema* s = new HsSqlSchema;
      *s = *(HsSqlSchema*)(*sv)[j]; // make a copy
      s->time_from = t;
      sv->add(s);

      //printf("schema after:\n");
      //sv->print(false);

      return s;
   }

   if (jjx >= 0) {
      cm_msg(MERROR, "NewSqlSchema", "Error: Unexpected ordering of schema for table \'%s\', good luck!", table_name);

      HsSqlSchema* s = new HsSqlSchema;
      *s = *(HsSqlSchema*)(*sv)[jjx]; // make a copy
      s->time_from = t;
      s->time_to = ttx;
      sv->add(s);

      //printf("schema after:\n");
      //sv->print(false);

      return s;
   }

   cm_msg(MERROR, "NewSqlSchema", "Error: Cannot clone schema for table \'%s\', good luck!", table_name);
   return NULL;
}

int HsSqlSchema::write_event(const time_t t, const char* data, const int data_size)
{
   HsSqlSchema* s = this;

   std::string tags;
   std::string values;

   for (unsigned i=0; i<s->variables.size(); i++) {
      if (s->variables[i].inactive)
         continue;

      int type   = s->variables[i].type;
      int n_data = s->variables[i].n_data;
      int offset = s->offsets[i];
      const char* column_name = s->column_names[i].c_str();

      if (offset < 0)
         continue;

      assert(n_data == 1);
      assert(strlen(column_name) > 0);
      assert(offset < data_size);

      void* ptr = (void*)(data+offset);

      tags += ", ";
      tags += sql->QuoteId(column_name);

      values += ", ";

      char buf[1024];
      int j=0;

      switch (type) {
      default:
         sprintf(buf, "unknownType%d", type);
         break;
      case TID_BYTE:
         sprintf(buf, "%u",((unsigned char *)ptr)[j]);
         break;
      case TID_SBYTE:
         sprintf(buf, "%d",((signed char*)ptr)[j]);
         break;
      case TID_CHAR:
         // FIXME: quotes
         sprintf(buf, "\'%c\'",((char*)ptr)[j]);
         break;
      case TID_WORD:
         sprintf(buf, "%u",((unsigned short *)ptr)[j]);
         break;
      case TID_SHORT:
         sprintf(buf, "%d",((short *)ptr)[j]);
         break;
      case TID_DWORD:
         sprintf(buf, "%u",((unsigned int *)ptr)[j]);
         break;
      case TID_INT:
         sprintf(buf, "%d",((int *)ptr)[j]);
         break;
      case TID_BOOL:
         sprintf(buf, "%u",((unsigned int *)ptr)[j]);
         break;
      case TID_FLOAT:
         // FIXME: quotes
         sprintf(buf, "\'%.8g\'",((float*)ptr)[j]);
         break;
      case TID_DOUBLE:
         // FIXME: quotes
         sprintf(buf, "\'%.16g\'",((double*)ptr)[j]);
         break;
      }

      values += buf;
   }

   // 2001-02-16 20:38:40.1
   struct tm tms;
   localtime_r(&t, &tms); // somebody must call tzset() before this.
   char buf[1024];
   strftime(buf, sizeof(buf)-1, "%Y-%m-%d %H:%M:%S.0", &tms);

   std::string cmd;
   cmd = "INSERT INTO ";
   cmd += sql->QuoteId(s->table_name.c_str());
   cmd += " (_t_time, _i_time";
   cmd += tags;
   cmd += ") VALUES (";
   cmd += sql->QuoteString(buf);
   cmd += ", ";
   cmd += sql->QuoteString(TimeToString(t).c_str());
   cmd += "";
   cmd += values;
   cmd += ");";

   if (sql->IsConnected()) {
      if (s->get_transaction_count() == 0)
         sql->OpenTransaction(s->table_name.c_str());

      s->increment_transaction_count();

      int status = sql->Exec(s->table_name.c_str(), cmd.c_str());

      // mh2sql who does not call hs_flush_buffers()
      // so we should flush the transaction by hand
      // some SQL engines have limited transaction buffers... K.O.
      if (s->get_transaction_count() > 100000) {
         //printf("flush table %s\n", table_name);
         sql->CommitTransaction(s->table_name.c_str());
         s->reset_transaction_count();
      }
      
      if (status != DB_SUCCESS) {
         return status;
      }
   } else {
      int status = sql->ExecDisconnected(s->table_name.c_str(), cmd.c_str());
      if (status != DB_SUCCESS) {
         return status;
      }
   }

   return HS_SUCCESS;
}

int HsSqlSchema::read_last_written(const time_t timestamp,
                                   const int debug,
                                   time_t* last_written)
{
   if (debug)
      printf("SqlHistory::read_last_written: table [%s], timestamp %s\n", table_name.c_str(), TimeToString(timestamp).c_str());

   std::string cmd;
   cmd += "SELECT _i_time FROM ";
   cmd += sql->QuoteId(table_name.c_str());
   cmd += " WHERE _i_time < ";
   cmd += TimeToString(timestamp);
   cmd += " ORDER BY _i_time DESC LIMIT 2;";

   int status = sql->Prepare(table_name.c_str(), cmd.c_str());

   if (status != DB_SUCCESS)
      return status;

   time_t lw = 0;

   /* Loop through the rows in the result-set */

   for (int k=0; ; k++) {
      status = sql->Step();
      if (status != DB_SUCCESS)
         break;

      time_t t = sql->GetTime(0);

      if (t >= timestamp)
         continue;

      if (t > lw)
         lw = t;
   }

   sql->Finalize();

   *last_written = lw;

   if (debug)
      printf("SqlHistory::read_last_written: table [%s], timestamp %s, last_written %s\n", table_name.c_str(), TimeToString(timestamp).c_str(), TimeToString(lw).c_str());

   return HS_SUCCESS;
}

int HsSqlSchema::read_data(const time_t start_time,
                           const time_t end_time,
                           const int num_var, const std::vector<int>& var_schema_index, const int var_index[],
                           const int debug,
                           std::vector<time_t>& last_time,
                           MidasHistoryBufferInterface* buffer[])
{
   bool bad_last_time = false;

   if (debug)
      printf("SqlHistory::read_data: table [%s], start %s, end %s\n", table_name.c_str(), TimeToString(start_time).c_str(), TimeToString(end_time).c_str());

   std::string collist;

   for (int i=0; i<num_var; i++) {
      int j = var_schema_index[i];
      if (j < 0)
         continue;
      if (collist.length() > 0)
         collist += ", ";
      collist += sql->QuoteId(column_names[j].c_str());
   }

   std::string cmd;
   cmd += "SELECT _i_time, ";
   cmd += collist;
   cmd += " FROM ";
   cmd += sql->QuoteId(table_name.c_str());
   cmd += " WHERE _i_time>=";
   cmd += TimeToString(start_time);
   cmd += " and _i_time<=";
   cmd += TimeToString(end_time);
   cmd += " ORDER BY _i_time;";

   int status = sql->Prepare(table_name.c_str(), cmd.c_str());

   if (status != DB_SUCCESS)
      return HS_FILE_ERROR;

   /* Loop through the rows in the result-set */

   int count = 0;

   while (1) {
      status = sql->Step();
      if (status != DB_SUCCESS)
         break;

      count++;

      time_t t = sql->GetTime(0);

      if (t < start_time || t > end_time)
         continue;

      int k = 0;

      for (int i=0; i<num_var; i++) {
         int j = var_schema_index[i];
         if (j < 0)
            continue;

         if (t < last_time[i]) { // protect against duplicate and non-monotonous data
            bad_last_time = true;
         } else {
            double v = sql->GetDouble(1+k);

            //printf("Column %d, index %d, Row %d, time %d, value %f\n", k, colindex[k], count, t, v);

            buffer[i]->Add(t, v);
            last_time[i] = t;
         }

         k++;
      }
   }

   sql->Finalize();

   if (bad_last_time) {
      cm_msg(MERROR, "SqlHistory::read_data", "Detected duplicate or non-monotonous data in table \"%s\" for start time %s and end time %s", table_name.c_str(), TimeToString(start_time).c_str(), TimeToString(end_time).c_str());
   }

   if (debug)
      printf("SqlHistory::read_data: table [%s], start %s, end %s, read %d rows\n", table_name.c_str(), TimeToString(start_time).c_str(), TimeToString(end_time).c_str(), count);

   return HS_SUCCESS;
}

int HsSqlSchema::get_transaction_count() {
   if (!sql || sql->fTransactionPerTable) {
      return table_transaction_count;
   } else {
      return global_transaction_count[sql];
   }
}

void HsSqlSchema::reset_transaction_count() {
   if (!sql || sql->fTransactionPerTable) {
      table_transaction_count = 0;
   } else {
      global_transaction_count[sql] = 0;
   }
}

void HsSqlSchema::increment_transaction_count() {
   if (!sql || sql->fTransactionPerTable) {
      table_transaction_count++;
   } else {
      global_transaction_count[sql]++;
   }
}

////////////////////////////////////////////////////////
//             SQL history functions                  //
////////////////////////////////////////////////////////

static int StartSqlTransaction(SqlBase* sql, const char* table_name, bool* have_transaction)
{
   if (*have_transaction)
      return HS_SUCCESS;

   int status = sql->OpenTransaction(table_name);
   if (status != DB_SUCCESS)
      return HS_FILE_ERROR;

   *have_transaction = true;
   return HS_SUCCESS;
}

static int CreateSqlTable(SqlBase* sql, const char* table_name, bool* have_transaction)
{
   int status;

   status = StartSqlTransaction(sql, table_name, have_transaction);
   if (status != DB_SUCCESS)
      return HS_FILE_ERROR;

   std::string cmd;

   cmd = "CREATE TABLE ";
   cmd += sql->QuoteId(table_name);
   cmd += " (_t_time TIMESTAMP NOT NULL, _i_time INTEGER NOT NULL);";

   status = sql->Exec(table_name, cmd.c_str());


   if (status == DB_KEY_EXIST) {
      cm_msg(MINFO, "CreateSqlTable", "Adding SQL table \"%s\", but it already exists", table_name);
      cm_msg_flush_buffer();
      return status;
   }

   if (status != DB_SUCCESS) {
      cm_msg(MINFO, "CreateSqlTable", "Adding SQL table \"%s\", error status %d", table_name, status);
      cm_msg_flush_buffer();
      return HS_FILE_ERROR;
   }

   cm_msg(MINFO, "CreateSqlTable", "Adding SQL table \"%s\"", table_name);
   cm_msg_flush_buffer();

   std::string i_index_name;
   i_index_name = table_name;
   i_index_name += "_i_time_index";

   std::string t_index_name;
   t_index_name = table_name;
   t_index_name += "_t_time_index";

   cmd = "CREATE INDEX ";
   cmd += sql->QuoteId(i_index_name.c_str());
   cmd += " ON ";
   cmd += sql->QuoteId(table_name);
   cmd += " (_i_time ASC);";

   status = sql->Exec(table_name, cmd.c_str());
   if (status != DB_SUCCESS)
      return HS_FILE_ERROR;

   cmd = "CREATE INDEX ";
   cmd += sql->QuoteId(t_index_name.c_str());
   cmd += " ON ";
   cmd += sql->QuoteId(table_name);
   cmd += " (_t_time);";

   status = sql->Exec(table_name, cmd.c_str());
   if (status != DB_SUCCESS)
      return HS_FILE_ERROR;

   return status;
}

static int CreateSqlColumn(SqlBase* sql, const char* table_name, const char* column_name, const char* column_type, bool* have_transaction, int debug)
{
   if (debug)
      printf("CreateSqlColumn: table [%s], column [%s], type [%s]\n", table_name, column_name, column_type);

   int status = StartSqlTransaction(sql, table_name, have_transaction);
   if (status != HS_SUCCESS)
      return status;

   std::string cmd;
   cmd = "ALTER TABLE ";
   cmd += sql->QuoteId(table_name);
   cmd += " ADD COLUMN ";
   cmd += sql->QuoteId(column_name);
   cmd += " ";
   cmd += column_type;
   cmd += ";";

   status = sql->Exec(table_name, cmd.c_str());

   cm_msg(MINFO, "CreateSqlColumn", "Adding column \"%s\" to SQL table \"%s\", status %d", column_name, table_name, status);
   cm_msg_flush_buffer();

   return status;
}

////////////////////////////////////////////////////////
//             SQL history base classes               //
////////////////////////////////////////////////////////

class SqlHistoryBase: public SchemaHistoryBase
{
public:
   SqlBase *fSql;

   SqlHistoryBase() // ctor
   {
      fSql = NULL;
      hs_clear_cache();
   }

   virtual ~SqlHistoryBase() // dtor
   {
      hs_disconnect();
      if (fSql)
         delete fSql;
      fSql = NULL;
   }

   int hs_set_debug(int debug)
   {
      if (fSql)
         fSql->fDebug = debug;
      return SchemaHistoryBase::hs_set_debug(debug);
   }

   int hs_connect(const char* connect_string);
   int hs_disconnect();
   HsSchema* new_event(const char* event_name, time_t timestamp, int ntags, const TAG tags[]);
   int read_schema(HsSchemaVector* sv, const char* event_name, const time_t timestamp);

protected:
   virtual int read_table_and_event_names(HsSchemaVector *sv) = 0;
   virtual int read_column_names(HsSchemaVector *sv, const char* table_name, const char* event_name) = 0;
   virtual int create_table(HsSchemaVector* sv, const char* event_name, time_t timestamp) = 0;
   virtual int update_column(const char* event_name, const char* table_name, const char* column_name, const char* column_type, const char* tag_name, const char* tag_type, const time_t timestamp, bool active, bool* have_transaction) = 0;

   int update_schema(HsSqlSchema* s, const time_t timestamp, const int ntags, const TAG tags[], bool write_enable);
   int update_schema1(HsSqlSchema* s, const time_t timestamp, const int ntags, const TAG tags[], bool write_enable, bool* have_transaction);
};

int SqlHistoryBase::hs_connect(const char* connect_string)
{
   if (fDebug)
      printf("hs_connect [%s]!\n", connect_string);

   assert(fSql);

   if (fSql->IsConnected())
      if (strcmp(fConnectString.c_str(), connect_string) == 0)
         return HS_SUCCESS;

   hs_disconnect();

   if (!connect_string || strlen(connect_string) < 1) {
      // FIXME: should use "logger dir" or some such default, that code should be in hs_get_history(), not here
      connect_string = ".";
   }

   fConnectString = connect_string;

   if (fDebug)
      printf("hs_connect: connecting to SQL database \'%s\'\n", fConnectString.c_str());

   int status = fSql->Connect(fConnectString.c_str());
   if (status != DB_SUCCESS)
      return status;

   return HS_SUCCESS;
}

int SqlHistoryBase::hs_disconnect()
{
   if (fDebug)
      printf("hs_disconnect!\n");

   hs_flush_buffers();

   fSql->Disconnect();

   hs_clear_cache();

   return HS_SUCCESS;
}

HsSchema* SqlHistoryBase::new_event(const char* event_name, time_t timestamp, int ntags, const TAG tags[])
{
   if (fDebug)
      printf("SqlHistory::new_event: event [%s], timestamp %s, ntags %d\n", event_name, TimeToString(timestamp).c_str(), ntags);

   int status;

   if (fWriterCurrentSchema.size() == 0) {
      status = read_table_and_event_names(&fWriterCurrentSchema);
      if (status != HS_SUCCESS)
         return NULL;
   }

   HsSqlSchema* s = (HsSqlSchema*)fWriterCurrentSchema.find_event(event_name, timestamp);

   // schema does not exist, the SQL tables probably do not exist yet

   if (!s) {
      status = create_table(&fWriterCurrentSchema, event_name, timestamp);
      if (status != HS_SUCCESS)
         return NULL;

      s = (HsSqlSchema*)fWriterCurrentSchema.find_event(event_name, timestamp);

      if (!s) {
         cm_msg(MERROR, "SqlHistory::new_event", "Error: Cannot create schema for event \'%s\', see previous messages", event_name);
         fWriterCurrentSchema.find_event(event_name, timestamp, 1);
         return NULL;
      }
   }

   assert(s != NULL);

   status = read_column_names(&fWriterCurrentSchema, s->table_name.c_str(), s->event_name.c_str());
   if (status != HS_SUCCESS)
      return NULL;

   s = (HsSqlSchema*)fWriterCurrentSchema.find_event(event_name, timestamp);

   if (!s) {
      cm_msg(MERROR, "SqlHistory::new_event", "Error: Cannot update schema database for event \'%s\', see previous messages", event_name);
      return NULL;
   }

   if (0||fDebug) {
      printf("SqlHistory::new_event: schema for [%s] is %p\n", event_name, s);
      if (s)
         s->print();
   }

   status = update_schema(s, timestamp, ntags, tags, true);
   if (status != HS_SUCCESS) {
      cm_msg(MERROR, "SqlHistory::new_event", "Error: Cannot create schema for event \'%s\', see previous messages", event_name);
      return NULL;
   }

   status = read_column_names(&fWriterCurrentSchema, s->table_name.c_str(), s->event_name.c_str());
   if (status != HS_SUCCESS)
      return NULL;

   s = (HsSqlSchema*)fWriterCurrentSchema.find_event(event_name, timestamp);

   if (!s) {
      cm_msg(MERROR, "SqlHistory::new_event", "Error: Cannot update schema database for event \'%s\', see previous messages", event_name);
      return NULL;
   }

   if (0||fDebug) {
      printf("SqlHistory::new_event: schema for [%s] is %p\n", event_name, s);
      if (s)
         s->print();
   }

   // last call to UpdateMysqlSchema with "false" will check that new schema matches the new tags

   status = update_schema(s, timestamp, ntags, tags, false);
   if (status != HS_SUCCESS) {
      cm_msg(MERROR, "SqlHistory::new_event", "Error: Cannot create schema for event \'%s\', see previous messages", event_name);
      //fDebug = 1;
      //update_schema(s, timestamp, ntags, tags, false);
      //abort();
      return NULL;
   }

   HsSqlSchema* e = new HsSqlSchema();

   *e = *s; // make a copy of the schema

   return e;
}

int SqlHistoryBase::read_schema(HsSchemaVector* sv, const char* event_name, const time_t timestamp)
{
   if (fDebug)
      printf("SqlHistory::read_schema: loading schema for event [%s] at time %s\n", event_name, TimeToString(timestamp).c_str());

   int status;

   if (fSchema.size() == 0) {
      status = read_table_and_event_names(sv);
      if (status != HS_SUCCESS)
         return status;
   }

   //sv->print(false);

   if (event_name == NULL)
      return HS_SUCCESS;

   for (unsigned i=0; i<sv->size(); i++) {
      HsSqlSchema* h = (HsSqlSchema*)(*sv)[i];
      // skip schema with already read column names
      if (h->variables.size() > 0)
         continue;
      // skip schema with different name
      if (!MatchEventName(h->event_name.c_str(), event_name))
         continue;

      unsigned nn = sv->size();

      status = read_column_names(sv, h->table_name.c_str(), h->event_name.c_str());

      // if new schema was added, loop all over again
      if (sv->size() != nn)
         i=0;
   }

   //sv->print(false);

   return HS_SUCCESS;
}

int SqlHistoryBase::update_schema(HsSqlSchema* s, const time_t timestamp, const int ntags, const TAG tags[], bool write_enable)
{
   int status;
   bool have_transaction = false;

   status = update_schema1(s, timestamp, ntags, tags, write_enable, &have_transaction);

   if (have_transaction) {
      int xstatus;

      if (status == HS_SUCCESS)
         xstatus = fSql->CommitTransaction(s->table_name.c_str());
      else
         xstatus = fSql->RollbackTransaction(s->table_name.c_str());

      if (xstatus != DB_SUCCESS) {
         return HS_FILE_ERROR;
      }
      have_transaction = false;
   }

   return status;
}

int SqlHistoryBase::update_schema1(HsSqlSchema* s, const time_t timestamp, const int ntags, const TAG tags[], bool write_enable, bool* have_transaction)
{
   int status;

   if (fDebug)
      printf("update_schema1\n");

   // check that compare schema with tags[]

   bool schema_ok = true;

   int offset = 0;
   for (int i=0; i<ntags; i++) {
      for (unsigned int j=0; j<tags[i].n_data; j++) {
         int         tagtype = tags[i].type;
         std::string tagname = tags[i].name;
         std::string maybe_colname = MidasNameToSqlName(tags[i].name);

         if (tags[i].n_data > 1) {
            char s[256];
            sprintf(s, "[%d]", j);
            tagname += s;

            sprintf(s, "_%d", j);
            maybe_colname += s;
         }

         int count = 0;

         for (unsigned j=0; j<s->variables.size(); j++) {
            // NB: inactive columns will be reactivated or recreated by the if(count==0) branch. K.O.
            if (s->variables[j].inactive)
               continue;
            if (tagname == s->variables[j].name) {
               if (s->sql->TypesCompatible(tagtype, s->column_types[j].c_str())) {
                  if (count == 0) {
                     s->offsets[j] = offset;
                     offset += rpc_tid_size(tagtype);
                  }
                  count++;
                  if (count > 1) {
                     cm_msg(MERROR, "SqlHistory::update_schema", "Duplicate SQL column \'%s\' type \'%s\' in table \"%s\" with MIDAS type \'%s\' history event \"%s\" tag \"%s\"", s->column_names[j].c_str(), s->column_types[j].c_str(), s->table_name.c_str(), rpc_tid_name(tagtype), s->event_name.c_str(), tagname.c_str());
                     cm_msg_flush_buffer();
                  }
               } else {
                  // column with incompatible type, mark it as unused
                  schema_ok = false;
                  if (fDebug)
                     printf("Incompatible column!\n");
                  if (write_enable) {
                     cm_msg(MINFO, "SqlHistory::update_schema", "Deactivating SQL column \'%s\' type \'%s\' in table \"%s\" as incompatible with MIDAS type \'%s\' history event \"%s\" tag \"%s\"", s->column_names[j].c_str(), s->column_types[j].c_str(), s->table_name.c_str(), rpc_tid_name(tagtype), s->event_name.c_str(), tagname.c_str());
                     cm_msg_flush_buffer();

                     status = update_column(s->event_name.c_str(), s->table_name.c_str(), s->column_names[j].c_str(), s->column_types[j].c_str(), s->variables[j].tag_name.c_str(), s->variables[i].tag_type.c_str(), timestamp, false, have_transaction);
                     if (status != HS_SUCCESS)
                        return status;
                  }
               }
            }
         }

         if (count == 0) {
            // tag does not have a corresponding column
            schema_ok = false;
            if (fDebug)
               printf("No column for tag %s!\n", tagname.c_str());

            bool found_column = false;
            
            if (write_enable) {
               for (unsigned j=0; j<s->variables.size(); j++) {
                  if (tagname == s->variables[j].tag_name) {
                     bool typeok = s->sql->TypesCompatible(tagtype, s->column_types[j].c_str());
                     if (typeok) {
                        cm_msg(MINFO, "SqlHistory::update_schema", "Reactivating SQL column \'%s\' type \'%s\' in table \"%s\" for history event \"%s\" tag \"%s\"", s->column_names[j].c_str(), s->column_types[j].c_str(), s->table_name.c_str(), s->event_name.c_str(), tagname.c_str());
                        cm_msg_flush_buffer();

                        status = update_column(s->event_name.c_str(), s->table_name.c_str(), s->column_names[j].c_str(), s->column_types[j].c_str(), s->variables[j].tag_name.c_str(), s->variables[j].tag_type.c_str(), timestamp, true, have_transaction);
                        if (status != HS_SUCCESS)
                           return status;

                        if (count == 0) {
                           s->offsets[j] = offset;
                           offset += rpc_tid_size(tagtype);
                        }
                        count++;
                        found_column = true;
                        if (count > 1) {
                           cm_msg(MERROR, "SqlHistory::update_schema", "Duplicate SQL column \'%s\' type \'%s\' in table \"%s\" for history event \"%s\" tag \"%s\"", s->column_names[j].c_str(), s->column_types[j].c_str(), s->table_name.c_str(), s->event_name.c_str(), tagname.c_str());
                           cm_msg_flush_buffer();
                        }
                     }
                  }
               }
            }

            // create column
            if (!found_column && write_enable) {
               std::string col_name = maybe_colname;
               const char* col_type = s->sql->ColumnType(tagtype);

               bool dupe = false;
               for (unsigned kk=0; kk<s->column_names.size(); kk++)
                  if (s->column_names[kk] == col_name) {
                     dupe = true;
                     break;
                  }

               time_t now = time(NULL);
               
               bool retry = false;
               for (int t=0; t<20; t++) {

                  // if duplicate column name, change it, try again
                  if (dupe || retry) {
                     col_name = maybe_colname;
                     col_name += "_";
                     col_name += TimeToString(now);
                     if (t > 0) {
                        char s[256];
                        sprintf(s, "_%d", t);
                        col_name += s;
                     }
                  }

                  if (fDebug)
                     printf("SqlHistory::update_schema: table [%s], add column [%s] type [%s] for tag [%s]\n", s->table_name.c_str(), col_name.c_str(), col_type, tagname.c_str());

                  status = CreateSqlColumn(fSql, s->table_name.c_str(), col_name.c_str(), col_type, have_transaction, fDebug);

                  if (status == DB_KEY_EXIST) {
                     if (fDebug)
                        printf("SqlHistory::update_schema: table [%s], add column [%s] type [%s] for tag [%s] failed: duplicate column name\n", s->table_name.c_str(), col_name.c_str(), col_type, tagname.c_str());
                     retry = true;
                     continue;
                  }

                  if (status != HS_SUCCESS)
                     return status;

                  break;
               }

               if (status != HS_SUCCESS)
                  return status;

               status = update_column(s->event_name.c_str(), s->table_name.c_str(), col_name.c_str(), col_type, tagname.c_str(), rpc_tid_name(tagtype), timestamp, true, have_transaction);
               if (status != HS_SUCCESS)
                  return status;
            }
         }

         if (count > 1) {
            // schema has duplicate tags
            schema_ok = false;
            cm_msg(MERROR, "SqlHistory::update_schema", "Duplicate tags or SQL columns for history event \"%s\" tag \"%s\"", s->event_name.c_str(), tagname.c_str());
            cm_msg_flush_buffer();
         }
      }
   }

   // mark as unused all columns not listed in tags

   for (unsigned k=0; k<s->column_names.size(); k++)
      if (s->variables[k].name.length() > 0) {
         bool found = false;

         for (int i=0; i<ntags; i++) {
            for (unsigned int j=0; j<tags[i].n_data; j++) {
               std::string tagname = tags[i].name;

               if (tags[i].n_data > 1) {
                  char s[256];
                  sprintf(s, "[%d]", j);
                  tagname += s;
               }

               if (s->variables[k].name == tagname) {
                  found = true;
                  break;
               }
            }

            if (found)
               break;
         }

         if (!found) {
            // column not found in tags list
            schema_ok = false;
            if (fDebug)
               printf("Event [%s] Column [%s] tag [%s] not listed in tags list!\n", s->event_name.c_str(), s->column_names[k].c_str(), s->variables[k].name.c_str());
            if (write_enable) {
               cm_msg(MINFO, "SqlHistory::update_schema", "Deactivating SQL column \'%s\' type \'%s\' in table \"%s\" for history event \"%s\" not used for any tags", s->column_names[k].c_str(), s->column_types[k].c_str(), s->table_name.c_str(), s->event_name.c_str());
               cm_msg_flush_buffer();

               status = update_column(s->event_name.c_str(), s->table_name.c_str(), s->column_names[k].c_str(), s->column_types[k].c_str(), s->variables[k].tag_name.c_str(), s->variables[k].tag_type.c_str(), timestamp, false, have_transaction);
               if (status != HS_SUCCESS)
                  return status;
            }
         }
      }

   if (!write_enable)
      if (!schema_ok) {
         if (fDebug)
            printf("Return error!\n");
         return HS_FILE_ERROR;
      }

   return HS_SUCCESS;
}

////////////////////////////////////////////////////////
//             SQLITE functions                       //
////////////////////////////////////////////////////////

static int ReadSqliteTableNames(SqlBase* sql, HsSchemaVector *sv, const char* table_name, int debug)
{
   if (debug)
      printf("ReadSqliteTableNames: table [%s]\n", table_name);

   int status;
   std::string cmd;

   // FIXME: quotes
   cmd = "SELECT event_name, _i_time FROM \'_event_name_";
   cmd += table_name;
   cmd += "\' WHERE table_name='";
   cmd += table_name;
   cmd += "';";

   status = sql->Prepare(table_name, cmd.c_str());

   if (status != DB_SUCCESS)
      return status;

   while (1) {
      status = sql->Step();

      if (status != DB_SUCCESS)
         break;

      std::string xevent_name  = sql->GetText(0);
      time_t      xevent_time  = sql->GetTime(1);

      //printf("read event name [%s] time %s\n", xevent_name.c_str(), TimeToString(xevent_time).c_str());

      HsSqlSchema* s = new HsSqlSchema;
      s->sql = sql;
      s->event_name = xevent_name;
      s->time_from = xevent_time;
      s->time_to = 0;
      s->table_name = table_name;
      sv->add(s);
   }

   status = sql->Finalize();

   return HS_SUCCESS;
}

static int ReadSqliteTableSchema(SqlBase* sql, HsSchemaVector *sv, const char* table_name, int debug)
{
   if (debug)
      printf("ReadSqliteTableSchema: table [%s]\n", table_name);

   if (1) {
      // seed schema with table names
      HsSqlSchema* s = new HsSqlSchema;
      s->sql = sql;
      s->event_name = table_name;
      s->time_from = 0;
      s->time_to = 0;
      s->table_name = table_name;
      sv->add(s);
   }

   return ReadSqliteTableNames(sql, sv, table_name, debug);
}

////////////////////////////////////////////////////////
//             SQLITE history classes                 //
////////////////////////////////////////////////////////

class SqliteHistory: public SqlHistoryBase
{
public:
   SqliteHistory() { // ctor
#ifdef HAVE_SQLITE
      fSql = new Sqlite();
#endif
   }

   int read_table_and_event_names(HsSchemaVector *sv);
   int read_column_names(HsSchemaVector *sv, const char* table_name, const char* event_name);
   int create_table(HsSchemaVector* sv, const char* event_name, time_t timestamp);
   int update_column(const char* event_name, const char* table_name, const char* column_name, const char* column_type, const char* tag_name, const char* tag_type, const time_t timestamp, bool active, bool* have_transaction);
};

int SqliteHistory::read_table_and_event_names(HsSchemaVector *sv)
{
   int status;

   if (fDebug)
      printf("SqliteHistory::read_table_and_event_names!\n");

   // loop over all tables

   std::vector<std::string> tables;
   status = fSql->ListTables(&tables);
   if (status != DB_SUCCESS)
      return status;

   for (unsigned i=0; i<tables.size(); i++) {
      const char* table_name = tables[i].c_str();

      const char* s;
      s = strstr(table_name, "_event_name_");
      if (s == table_name)
         continue;
      s = strstr(table_name, "_column_names_");
      if (s == table_name)
         continue;

      status = ReadSqliteTableSchema(fSql, sv, table_name, fDebug);
   }

   return HS_SUCCESS;
}

int SqliteHistory::read_column_names(HsSchemaVector *sv, const char* table_name, const char* event_name)
{
   if (fDebug)
      printf("SqliteHistory::read_column_names: table [%s], event [%s]\n", table_name, event_name);

   // for all schema for table_name, prepopulate is with column names

   std::vector<std::string> columns;
   fSql->ListColumns(table_name, &columns);

   // first, populate column names

   for (unsigned i=0; i<sv->size(); i++) {
      HsSqlSchema* s = (HsSqlSchema*)(*sv)[i];

      if (s->table_name != table_name)
         continue;

      // schema should be empty at this point
      //assert(s->variables.size() == 0);

      for (unsigned j=0; j<columns.size(); j+=2) {
         const char* cn = columns[j+0].c_str();
         const char* ct = columns[j+1].c_str();

         if (strcmp(cn, "_t_time") == 0)
            continue;
         if (strcmp(cn, "_i_time") == 0)
            continue;

         bool found = false;

         for (unsigned k=0; k<s->column_names.size(); k++) {
            if (s->column_names[k] == cn) {
               found = true;
               break;
            }
         }

         //printf("column [%s] sql type [%s]\n", cn.c_str(), ct);

         if (!found) {
            HsSchemaEntry se;
            se.name = cn;
            se.type = 0;
            se.n_data = 1;
            se.n_bytes = 0;
            s->variables.push_back(se);
            s->column_names.push_back(cn);
            s->column_types.push_back(ct);
            s->offsets.push_back(-1);
         }
      }
   }

   // then read column name information

   std::string tn;
   tn += "_column_names_";
   tn += table_name;
   
   std::string cmd;
   cmd = "SELECT column_name, tag_name, tag_type, _i_time FROM ";
   cmd += fSql->QuoteId(tn.c_str());
   cmd += " WHERE table_name=";
   cmd += fSql->QuoteString(table_name);
   cmd += " ORDER BY _i_time ASC;";

   int status = fSql->Prepare(table_name, cmd.c_str());

   if (status != DB_SUCCESS) {
      return status;
   }

   while (1) {
      status = fSql->Step();

      if (status != DB_SUCCESS)
         break;

      // NOTE: SQL "SELECT ORDER BY _i_time ASC" returns data sorted by time
      // in this code we use the data from the last data row
      // so if multiple rows are present, the latest one is used

      std::string col_name  = fSql->GetText(0);
      std::string tag_name  = fSql->GetText(1);
      std::string tag_type  = fSql->GetText(2);
      time_t   schema_time  = fSql->GetTime(3);

      //printf("read table [%s] column [%s] tag name [%s] time %s\n", table_name, col_name.c_str(), tag_name.c_str(), TimeToString(xxx_time).c_str());

      // make sure a schema exists at this time point
      NewSqlSchema(sv, table_name, schema_time);

      // add this information to all schema

      for (unsigned i=0; i<sv->size(); i++) {
         HsSqlSchema* s = (HsSqlSchema*)(*sv)[i];
         if (s->table_name != table_name)
            continue;
         if (s->time_from < schema_time)
            continue;

         //printf("add column to schema %d\n", s->time_from);

         for (unsigned j=0; j<s->column_names.size(); j++) {
            if (col_name != s->column_names[j])
               continue;
            s->variables[j].name = tag_name;
            s->variables[j].type = rpc_name_tid(tag_type.c_str());
            s->variables[j].n_data = 1;
            s->variables[j].n_bytes = rpc_tid_size(s->variables[j].type);
         }
      }
   }

   status = fSql->Finalize();

   return HS_SUCCESS;
}

int SqliteHistory::create_table(HsSchemaVector* sv, const char* event_name, time_t timestamp)
{
   if (fDebug)
      printf("SqliteHistory::create_table: event [%s], timestamp %s\n", event_name, TimeToString(timestamp).c_str());

   int status;
   bool have_transaction = false;
   std::string table_name = MidasNameToSqlName(event_name);

   // FIXME: what about duplicate table names?
   status = CreateSqlTable(fSql, table_name.c_str(), &have_transaction);

   //if (status == DB_KEY_EXIST) {
   //   return ReadSqliteTableSchema(fSql, sv, table_name.c_str(), fDebug);
   //}

   if (status != HS_SUCCESS) {
      // FIXME: ???
      // FIXME: at least close or revert the transaction
      return status;
   }

   std::string cmd;

   std::string en;
   en += "_event_name_";
   en += table_name;

   cmd = "CREATE TABLE ";
   cmd += fSql->QuoteId(en.c_str());
   cmd += " (table_name TEXT NOT NULL, event_name TEXT NOT NULL, _i_time INTEGER NOT NULL);";

   status = fSql->Exec(table_name.c_str(), cmd.c_str());

   cmd = "INSERT INTO ";
   cmd += fSql->QuoteId(en.c_str());
   cmd += " (table_name, event_name, _i_time) VALUES (";
   cmd += fSql->QuoteString(table_name.c_str());
   cmd += ", ";
   cmd += fSql->QuoteString(event_name);
   cmd += ", ";
   cmd += fSql->QuoteString(TimeToString(timestamp).c_str());
   cmd += ");";

   status = fSql->Exec(table_name.c_str(), cmd.c_str());

   std::string cn;
   cn += "_column_names_";
   cn += table_name;

   cmd = "CREATE TABLE ";
   cmd += fSql->QuoteId(cn.c_str());
   cmd += " (table_name TEXT NOT NULL, column_name TEXT NOT NULL, tag_name TEXT NOT NULL, tag_type TEXT NOT NULL, column_type TEXT NOT NULL, _i_time INTEGER NOT NULL);";

   status = fSql->Exec(table_name.c_str(), cmd.c_str());

   status = fSql->CommitTransaction(table_name.c_str());
   if (status != DB_SUCCESS) {
      return HS_FILE_ERROR;
   }

   return ReadSqliteTableSchema(fSql, sv, table_name.c_str(), fDebug);
}

int SqliteHistory::update_column(const char* event_name, const char* table_name, const char* column_name, const char* column_type, const char* tag_name, const char* tag_type, const time_t timestamp, bool active, bool* have_transaction)
{
   if (fDebug)
      printf("SqliteHistory::update_column: event [%s], table [%s], column [%s], new name [%s], timestamp %s\n", event_name, table_name, column_name, tag_name, TimeToString(timestamp).c_str());

   int status = StartSqlTransaction(fSql, table_name, have_transaction);
   if (status != HS_SUCCESS)
      return status;

   // FIXME: quotes
   std::string cmd;
   cmd = "INSERT INTO \'_column_names_";
   cmd += table_name;
   cmd += "\' (table_name, column_name, tag_name, tag_type, column_type, _i_time) VALUES (\'";
   cmd += table_name;
   cmd += "\', \'";
   cmd += column_name;
   cmd += "\', \'";
   cmd += tag_name;
   cmd += "\', \'";
   cmd += tag_type;
   cmd += "\', \'";
   cmd += column_type;
   cmd += "\', \'";
   cmd += TimeToString(timestamp);
   cmd += "\');";
   status = fSql->Exec(table_name, cmd.c_str());

   return status;
}

////////////////////////////////////////////////////////
//              Mysql history classes                 //
////////////////////////////////////////////////////////

class MysqlHistory: public SqlHistoryBase
{
public:
   MysqlHistory() { // ctor
#ifdef HAVE_MYSQL
      fSql = new Mysql();
#endif
   }

   int read_table_and_event_names(HsSchemaVector *sv);
   int read_column_names(HsSchemaVector *sv, const char* table_name, const char* event_name);
   int create_table(HsSchemaVector* sv, const char* event_name, time_t timestamp);
   int update_column(const char* event_name, const char* table_name, const char* column_name, const char* column_type, const char* tag_name, const char* tag_type, const time_t timestamp, bool active, bool* have_transaction);
};

static int ReadMysqlTableNames(SqlBase* sql, HsSchemaVector *sv, const char* table_name, int debug, const char* must_have_event_name, const char* must_have_table_name)
{
   if (debug)
      printf("ReadMysqlTableNames: table [%s], must have event [%s] table [%s]\n", table_name, must_have_event_name, must_have_table_name);

   int status;
   std::string cmd;

   if (table_name) {
      cmd = "SELECT event_name, table_name, itimestamp FROM _history_index WHERE table_name='";
      cmd += table_name;
      cmd += "';";
   } else {
      cmd = "SELECT event_name, table_name, itimestamp FROM _history_index WHERE table_name!='';";
      table_name = "_history_index";
   }

   status = sql->Prepare(table_name, cmd.c_str());

   if (status != DB_SUCCESS)
      return status;

   bool found_must_have_table = false;
   int count = 0;

   while (1) {
      status = sql->Step();

      if (status != DB_SUCCESS)
         break;

      const char* xevent_name  = sql->GetText(0);
      const char* xtable_name  = sql->GetText(1);
      time_t      xevent_time  = sql->GetTime(2);

      if (debug == 999) {
         printf("entry %d event name [%s] table name [%s] time %s\n", count, xevent_name, xtable_name, TimeToString(xevent_time).c_str());
      }

      if (must_have_table_name && (strcmp(xtable_name, must_have_table_name) == 0)) {
         assert(must_have_event_name != NULL);
         if (event_name_cmp(xevent_name, must_have_event_name) == 0) {
            found_must_have_table = true;
            //printf("Found table [%s]: event name [%s] table name [%s] time %s\n", must_have_table_name, xevent_name, xtable_name, TimeToString(xevent_time).c_str());
         } else {
            //printf("Found correct table [%s] with wrong event name [%s] expected [%s] time %s\n", must_have_table_name, xevent_name, must_have_event_name, TimeToString(xevent_time).c_str());
         }
      }
      
      HsSqlSchema* s = new HsSqlSchema;
      s->sql = sql;
      s->event_name = xevent_name;
      s->time_from = xevent_time;
      s->time_to = 0;
      s->table_name = xtable_name;
      sv->add(s);
      count++;
   }

   status = sql->Finalize();

   if (must_have_table_name && !found_must_have_table) {
      cm_msg(MERROR, "ReadMysqlTableNames", "Error: Table [%s] for event [%s] missing from the history index\n", must_have_table_name, must_have_event_name);
      if (debug == 999)
         return HS_FILE_ERROR;
      // NB: recursion is broken by setting debug to 999.
      ReadMysqlTableNames(sql, sv, table_name, 999, must_have_event_name, must_have_table_name);
      cm_msg(MERROR, "ReadMysqlTableNames", "Error: Cannot continue, nothing will work after this error\n");
      cm_msg_flush_buffer();
      abort();
      return HS_FILE_ERROR;
   }

   if (0) {
      // print accumulated schema
      printf("ReadMysqlTableNames: table_name [%s] event_name [%s] table_name [%s]\n", table_name, must_have_event_name, must_have_table_name);
      sv->print(false);
   }

   return HS_SUCCESS;
}

int MysqlHistory::read_column_names(HsSchemaVector *sv, const char* table_name, const char* event_name)
{
   if (fDebug)
      printf("MysqlHistory::read_column_names: table [%s], event [%s]\n", table_name, event_name);

   // for all schema for table_name, prepopulate is with column names

   std::vector<std::string> columns;
   fSql->ListColumns(table_name, &columns);

   // first, populate column names

   for (unsigned i=0; i<sv->size(); i++) {
      HsSqlSchema* s = (HsSqlSchema*)(*sv)[i];

      if (s->table_name != table_name)
         continue;

      // schema should be empty at this point
      //assert(s->variables.size() == 0);

      for (unsigned j=0; j<columns.size(); j+=2) {
         const char* cn = columns[j+0].c_str();
         const char* ct = columns[j+1].c_str();

         if (strcmp(cn, "_t_time") == 0)
            continue;
         if (strcmp(cn, "_i_time") == 0)
            continue;

         bool found = false;

         for (unsigned k=0; k<s->column_names.size(); k++) {
            if (s->column_names[k] == cn) {
               found = true;
               break;
            }
         }

         //printf("column [%s] sql type [%s]\n", cn.c_str(), ct);

         if (!found) {
            HsSchemaEntry se;
            se.tag_name = cn;
            se.tag_type = "";
            se.name = cn;
            se.type = 0;
            se.n_data = 1;
            se.n_bytes = 0;
            s->variables.push_back(se);
            s->column_names.push_back(cn);
            s->column_types.push_back(ct);
            s->offsets.push_back(-1);
         }
      }
   }

   // then read column name information

   std::string cmd;
   cmd = "SELECT column_name, column_type, tag_name, tag_type, itimestamp, active FROM _history_index WHERE event_name=";
   cmd += fSql->QuoteString(event_name);
   cmd += ";";

   int status = fSql->Prepare(table_name, cmd.c_str());

   if (status != DB_SUCCESS) {
      return status;
   }

   while (1) {
      status = fSql->Step();

      if (status != DB_SUCCESS)
         break;

      const char* col_name  = fSql->GetText(0);
      const char* col_type  = fSql->GetText(1);
      const char* tag_name  = fSql->GetText(2);
      const char* tag_type  = fSql->GetText(3);
      time_t   schema_time  = fSql->GetTime(4);
      const char* active    = fSql->GetText(5);
      int iactive = atoi(active);

      //printf("read table [%s] column [%s] type [%s] tag name [%s] type [%s] time %s active [%s] %d\n", table_name, col_name, col_type, tag_name, tag_type, TimeToString(schema_time).c_str(), active, iactive);

      if (!col_name)
         continue;
      if (!tag_name)
         continue;
      if (strlen(col_name) < 1)
         continue;

      // make sure a schema exists at this time point
      NewSqlSchema(sv, table_name, schema_time);

      // add this information to all schema

      for (unsigned i=0; i<sv->size(); i++) {
         HsSqlSchema* s = (HsSqlSchema*)(*sv)[i];
         if (s->table_name != table_name)
            continue;
         if (s->time_from < schema_time)
            continue;

         int tid = rpc_name_tid(tag_type);
         int tid_size = rpc_tid_size(tid);

         for (unsigned j=0; j<s->column_names.size(); j++) {
            if (col_name != s->column_names[j])
               continue;

            s->variables[j].tag_name = tag_name;
            s->variables[j].tag_type = tag_type;
            if (!iactive) {
               s->variables[j].name = "";
               s->variables[j].inactive = true;
            } else {
               s->variables[j].name = tag_name;
               s->variables[j].inactive = false;
            }
            s->variables[j].type = tid;
            s->variables[j].n_data = 1;
            s->variables[j].n_bytes = tid_size;

            // doctor column names in case MySQL returns different type
            // from the type used to create the column, but the types
            // are actually the same. K.O.
            DoctorSqlColumnType(&s->column_types[j], col_type);
         }
      }
   }

   status = fSql->Finalize();

   return HS_SUCCESS;
}

#if 0
static int ReadMysqlTableSchema(SqlBase* sql, HsSchemaVector *sv, const char* table_name, int debug)
{
   if (debug)
      printf("ReadMysqlTableSchema: table [%s]\n", table_name);

   if (1) {
      // seed schema with table names
      HsSqlSchema* s = new HsSqlSchema;
      s->sql = sql;
      s->event_name = table_name;
      s->time_from = 0;
      s->time_to = 0;
      s->table_name = table_name;
      sv->add(s);
   }

   return ReadMysqlTableNames(sql, sv, table_name, debug, NULL, NULL);
}
#endif

int MysqlHistory::read_table_and_event_names(HsSchemaVector *sv)
{
   int status;

   if (fDebug)
      printf("MysqlHistory::read_table_and_event_names!\n");

   // loop over all tables

   std::vector<std::string> tables;
   status = fSql->ListTables(&tables);
   if (status != DB_SUCCESS)
      return status;

   for (unsigned i=0; i<tables.size(); i++) {
      const char* table_name = tables[i].c_str();

      const char* s;
      s = strstr(table_name, "_history_index");
      if (s == table_name)
         continue;

      if (1) {
         // seed schema with table names
         HsSqlSchema* s = new HsSqlSchema;
         s->sql = fSql;
         s->event_name = table_name;
         s->time_from = 0;
         s->time_to = 0;
         s->table_name = table_name;
         sv->add(s);
      }
   }

   if (0) {
      // print accumulated schema
      printf("read_table_and_event_names:\n");
      sv->print(false);
   }

   status = ReadMysqlTableNames(fSql, sv, NULL, fDebug, NULL, NULL);

   return HS_SUCCESS;
}

int MysqlHistory::create_table(HsSchemaVector* sv, const char* event_name, time_t timestamp)
{
   if (fDebug)
      printf("MysqlHistory::create_table: event [%s], timestamp %s\n", event_name, TimeToString(timestamp).c_str());

   int status;
   std::string table_name = MidasNameToSqlName(event_name);

   // MySQL table name length limit is 64 bytes
   if (table_name.length() > 40) {
      table_name.resize(40);
      table_name += "_T";
   }

   time_t now = time(NULL);

   int max_attempts = 10;
   for (int i=0; i<max_attempts; i++) {
      status = fSql->OpenTransaction(table_name.c_str());
      if (status != DB_SUCCESS) {
         return HS_FILE_ERROR;
      }

      bool have_transaction = true;

      std::string xtable_name = table_name;

      if (i>0) {
         xtable_name += "_";
         xtable_name += TimeToString(now);
         if (i>1) {
            xtable_name += "_";
            char buf[256];
            sprintf(buf, "%d", i);
            xtable_name += buf;
         }
      }
      
      status = CreateSqlTable(fSql, xtable_name.c_str(), &have_transaction);

      //printf("event [%s] create table [%s] status %d\n", event_name, xtable_name.c_str(), status);

      if (status == DB_KEY_EXIST) {
         // already exists, try with different name!
         fSql->RollbackTransaction(table_name.c_str());
         continue;
      }

      if (status != HS_SUCCESS) {
         // MYSQL cannot roll back "create table", if we cannot create SQL tables, nothing will work. Give up now.
         cm_msg(MERROR, "MysqlHistory::create_table", "Could not create table [%s] for event [%s], timestamp %s, please fix the SQL database configuration and try again", table_name.c_str(), event_name, TimeToString(timestamp).c_str());
         abort();

         // fatal error, give up!
         fSql->RollbackTransaction(table_name.c_str());
         break;
      }

      for (int j=0; j<2; j++) {
         std::string cmd;
         cmd += "INSERT INTO _history_index (event_name, table_name, itimestamp, active) VALUES (";
         cmd += fSql->QuoteString(event_name);
         cmd += ", ";
         cmd += fSql->QuoteString(xtable_name.c_str());
         cmd += ", ";
         char buf[256];
         sprintf(buf, "%.0f", (double)timestamp);
         cmd += fSql->QuoteString(buf);
         cmd += ", ";
         cmd += fSql->QuoteString("1");
         cmd += ");";
         
         int status = fSql->Exec(table_name.c_str(), cmd.c_str());
         if (status == DB_SUCCESS)
            break;
         
         status = CreateSqlTable(fSql,  "_history_index", &have_transaction);
         status = CreateSqlColumn(fSql, "_history_index", "event_name",  "varchar(256) character set binary not null", &have_transaction, fDebug);
         status = CreateSqlColumn(fSql, "_history_index", "table_name",  "varchar(256)",     &have_transaction, fDebug);
         status = CreateSqlColumn(fSql, "_history_index", "tag_name",    "varchar(256) character set binary",     &have_transaction, fDebug);
         status = CreateSqlColumn(fSql, "_history_index", "tag_type",    "varchar(256)",     &have_transaction, fDebug);
         status = CreateSqlColumn(fSql, "_history_index", "column_name", "varchar(256)",     &have_transaction, fDebug);
         status = CreateSqlColumn(fSql, "_history_index", "column_type", "varchar(256)",     &have_transaction, fDebug);
         status = CreateSqlColumn(fSql, "_history_index", "itimestamp",  "integer not null", &have_transaction, fDebug);
         status = CreateSqlColumn(fSql, "_history_index", "active",      "boolean",          &have_transaction, fDebug);
      }

      status = fSql->CommitTransaction(table_name.c_str());

      if (status != DB_SUCCESS) {
         return HS_FILE_ERROR;
      }
      
      return ReadMysqlTableNames(fSql, sv, xtable_name.c_str(), fDebug, event_name, xtable_name.c_str());
   }

   cm_msg(MERROR, "MysqlHistory::create_table", "Could not create table [%s] for event [%s], timestamp %s, after %d attempts", table_name.c_str(), event_name, TimeToString(timestamp).c_str(), max_attempts);

   return HS_FILE_ERROR;
}

int MysqlHistory::update_column(const char* event_name, const char* table_name, const char* column_name, const char* column_type, const char* tag_name, const char* tag_type, const time_t timestamp, bool active, bool* have_transaction)
{
   if (fDebug)
      printf("MysqlHistory::update_column: event [%s], table [%s], column [%s], type [%s] new name [%s], timestamp %s\n", event_name, table_name, column_name, column_type, tag_name, TimeToString(timestamp).c_str());

   std::string cmd;
   cmd += "INSERT INTO _history_index (event_name, table_name, tag_name, tag_type, column_name, column_type, itimestamp, active) VALUES (";
   cmd += fSql->QuoteString(event_name);
   cmd += ", ";
   cmd += fSql->QuoteString(table_name);
   cmd += ", ";
   cmd += fSql->QuoteString(tag_name);
   cmd += ", ";
   cmd += fSql->QuoteString(tag_type);
   cmd += ", ";
   cmd += fSql->QuoteString(column_name);
   cmd += ", ";
   cmd += fSql->QuoteString(column_type);
   cmd += ", ";
   char buf[256];
   sprintf(buf, "%.0f", (double)timestamp);
   cmd += fSql->QuoteString(buf);
   cmd += ", ";
   if (active)
      cmd += fSql->QuoteString("1");
   else
      cmd += fSql->QuoteString("0");
   cmd += ");";
         
   int status = fSql->Exec(table_name, cmd.c_str());
   if (status != DB_SUCCESS)
      return HS_FILE_ERROR;

   return HS_SUCCESS;
}

////////////////////////////////////////////////////////
//               File history class                   //
////////////////////////////////////////////////////////

const time_t kDay = 24*60*60;
const time_t kMonth = 30*kDay;

const double KiB = 1024;
const double MiB = KiB*KiB;
//const double GiB = KiB*MiB;

class FileHistory: public SchemaHistoryBase
{
protected:
   std::string fPath;
   time_t fPathLastMtime;
   std::vector<std::string> fSortedFiles;
   std::vector<bool>        fSortedRead;
   time_t fConfMaxFileAge;
   double fConfMaxFileSize;

public:
   FileHistory() // ctor
   {
      fConfMaxFileAge = 1*kMonth;
      fConfMaxFileSize = 100*MiB;

      fPathLastMtime = 0;
   }

   int hs_connect(const char* connect_string);
   int hs_disconnect();
   int hs_clear_cache();
   int read_schema(HsSchemaVector* sv, const char* event_name, const time_t timestamp);
   HsSchema* new_event(const char* event_name, time_t timestamp, int ntags, const TAG tags[]);

protected:
   int create_file(const char* event_name, time_t timestamp, int ntags, const TAG tags[], std::string* filenamep);
   HsFileSchema* read_file_schema(const char* filename);
   int read_file_list(bool *pchanged);
};

int FileHistory::hs_connect(const char* connect_string)
{
   if (fDebug)
      printf("hs_connect [%s]!\n", connect_string);

   hs_disconnect();

   fConnectString = connect_string;
   fPath = connect_string;

   // add trailing '/'
   if (fPath.length() > 0) {
      if (fPath[fPath.length()-1] != DIR_SEPARATOR)
         fPath += DIR_SEPARATOR_STR;
   }

   return HS_SUCCESS;
}

int FileHistory::hs_clear_cache()
{
   if (fDebug)
      printf("FileHistory::hs_clear_cache!\n");
   fPathLastMtime = 0;
   return SchemaHistoryBase::hs_clear_cache();
}

int FileHistory::hs_disconnect()
{
   if (fDebug)
      printf("FileHistory::hs_disconnect!\n");

   hs_flush_buffers();
   hs_clear_cache();

   return HS_SUCCESS;
}

int FileHistory::read_file_list(bool* pchanged)
{
   int status;
   double start_time = ss_time_sec();

   if (pchanged)
      *pchanged = false;

   struct stat stat_buf;
   status = stat(fPath.c_str(), &stat_buf);
   if (status != 0) {
      cm_msg(MERROR, "FileHistory::read_file_list", "Cannot stat(%s), errno %d (%s)", fPath.c_str(), errno, strerror(errno));
      return HS_FILE_ERROR;
   }

   //printf("dir [%s], mtime: %d %d last: %d %d, mtime %s", fPath.c_str(), stat_buf.st_mtimespec.tv_sec, stat_buf.st_mtimespec.tv_nsec, last_mtimespec.tv_sec, last_mtimespec.tv_nsec, ctime(&stat_buf.st_mtimespec.tv_sec));

   if (stat_buf.st_mtime == fPathLastMtime) {
      if (fDebug)
         printf("FileHistory::read_file_list: history directory \"%s\" mtime %d did not change\n", fPath.c_str(), int(stat_buf.st_mtime));
      return HS_SUCCESS;
   }

   fPathLastMtime = stat_buf.st_mtime;

   if (fDebug)
      printf("FileHistory::read_file_list: reading list of history files in \"%s\"\n", fPath.c_str());

   std::vector<std::string> flist;

   ss_file_find(fPath.c_str(), "mhf_*.dat", &flist);

   double ls_time = ss_time_sec();
   double ls_elapsed = ls_time - start_time;
   if (ls_elapsed > 5.000) {
      cm_msg(MINFO, "FileHistory::read_file_list", "\"ls -l\" of \"%s\" took %.1f sec", fPath.c_str(), ls_elapsed);
      cm_msg_flush_buffer();
   }

   // note: reverse iterator is used to sort filenames by time, newest first
   std::sort(flist.rbegin(), flist.rend());

#if 0
   {
      printf("file names sorted by time:\n");
      for (unsigned i=0; i<flist.size(); i++) {
         printf("%d: %s\n", i, flist[i].c_str());
      }
   }
#endif

   std::vector<bool> fread;
   fread.resize(flist.size()); // fill with "false"

   // loop over the old list of files,
   // for files we already read, loop over new file
   // list and mark the same file as read. K.O.
   for (size_t j=0; j<fSortedFiles.size(); j++) {
      if (fSortedRead[j]) {
         for (size_t i=0; i<flist.size(); i++) {
            if (flist[i] == fSortedFiles[j]) {
               fread[i] = true;
               break;
            }
         }
      }
   }

   fSortedFiles = flist;
   fSortedRead  = fread;

   if (pchanged)
      *pchanged = true;

   return HS_SUCCESS;
}

int FileHistory::read_schema(HsSchemaVector* sv, const char* event_name, const time_t timestamp)
{
   if (fDebug)
      printf("FileHistory::read_schema: event [%s] at time %s\n", event_name, TimeToString(timestamp).c_str());

   BOOL old_call_watchdog = FALSE;
   DWORD old_timeout = 0;
   cm_get_watchdog_params(&old_call_watchdog, &old_timeout);
   cm_set_watchdog_params(old_call_watchdog, 0);

   bool changed = false;

   int status = read_file_list(&changed);

   if (status != HS_SUCCESS) {
      cm_set_watchdog_params(old_call_watchdog, old_timeout);
      return status;
   }

   if (!changed) {
      if ((*sv).find_event(event_name, timestamp)) {
         if (fDebug)
            printf("FileHistory::read_schema: event [%s] at time %s, no new history files, already have this schema\n", event_name, TimeToString(timestamp).c_str());
         cm_set_watchdog_params(old_call_watchdog, old_timeout);
         return HS_SUCCESS;
      }
   }

   double start_time = ss_time_sec();

   int count_read = 0;

   for (unsigned i=0; i<fSortedFiles.size(); i++) {
      std::string file_name = fPath + fSortedFiles[i];
      if (fSortedRead[i])
         continue;
      //bool dupe = false;
      //for (unsigned ss=0; ss<sv->size(); ss++) {
      //   HsFileSchema* ssp = (HsFileSchema*)(*sv)[ss];
      //   if (file_name == ssp->file_name) {
      //      dupe = true;
      //      break;
      //   }
      //}
      //if (dupe)
      //   continue;
      fSortedRead[i] = true;
      HsFileSchema* s = read_file_schema(file_name.c_str());
      if (!s)
         continue;
      sv->add(s);
      count_read++;

      if (event_name) {
         if (s->event_name == event_name) {
            //printf("file %s event_name %s time %s, age %f\n", file_name.c_str(), s->event_name.c_str(), TimeToString(s->time_from).c_str(), double(timestamp - s->time_from));
            if (s->time_from <= timestamp) {
               // this file is older than the time requested,
               // subsequent files will be even older,
               // we can stop reading here.
               break;
            }
         }
      }
   }

   double end_time = ss_time_sec();
   double read_elapsed = end_time - start_time;
   if (read_elapsed > 5.000) {
      cm_msg(MINFO, "FileHistory::read_schema", "Loading schema for event \"%s\" timestamp %s, reading %d history files took %.1f sec", event_name, TimeToString(timestamp).c_str(), count_read, read_elapsed);
      cm_msg_flush_buffer();
   }

   cm_set_watchdog_params(old_call_watchdog, old_timeout);

   return HS_SUCCESS;
}

HsSchema* FileHistory::new_event(const char* event_name, time_t timestamp, int ntags, const TAG tags[])
{
   if (fDebug)
      printf("FileHistory::new_event: event [%s], timestamp %s, ntags %d\n", event_name, TimeToString(timestamp).c_str(), ntags);

   int status;

   HsFileSchema* s = (HsFileSchema*)fWriterCurrentSchema.find_event(event_name, timestamp);

   if (!s) {
      //printf("hs_define_event: no schema for event %s\n", event_name);
      status = read_schema(&fWriterCurrentSchema, event_name, timestamp);
      if (status != HS_SUCCESS)
         return NULL;
      s = (HsFileSchema*)fWriterCurrentSchema.find_event(event_name, timestamp);
   } else {
      //printf("hs_define_event: already have schema for event %s\n", s->event_name.c_str());
   }

   bool xdebug = false;

   if (s) { // is existing schema the same as new schema?
      bool same = true;

      if (same)
         if (s->event_name != event_name) {
            if (xdebug)
               printf("AAA: [%s] [%s]!\n", s->event_name.c_str(), event_name);
            same = false;
         }

      if (same)
         if (s->variables.size() != (unsigned)ntags) {
            if (xdebug)
               printf("BBB: event [%s]: ntags: %d -> %d!\n", event_name, (int)s->variables.size(), ntags);
            same = false;
         }

      if (same)
         for (unsigned i=0; i<s->variables.size(); i++) {
            if (s->variables[i].name != tags[i].name) {
               if (xdebug)
                  printf("CCC: event [%s] index %d: name [%s] -> [%s]!\n", event_name, i, s->variables[i].name.c_str(), tags[i].name);
               same = false;
            }
            if (s->variables[i].type != (int)tags[i].type) {
               if (xdebug)
                  printf("DDD: event [%s] index %d: type %d -> %d!\n", event_name, i, s->variables[i].type, tags[i].type);
               same = false;
            }
            if (s->variables[i].n_data != (int)tags[i].n_data) {
               if (xdebug)
                  printf("EEE: event [%s] index %d: n_data %d -> %d!\n", event_name, i, s->variables[i].n_data, tags[i].n_data);
               same = false;
            }
            if (!same)
               break;
         }

      if (!same) {
         if (xdebug) {
            printf("*** Schema for event %s has changed!\n", event_name);

            printf("*** Old schema for event [%s] time %s:\n", event_name, TimeToString(timestamp).c_str());
            s->print();
            printf("*** New tags:\n");
            PrintTags(ntags, tags);
         }

         if (fDebug)
            printf("FileHistory::new_event: event [%s], timestamp %s, ntags %d: schema mismatch, starting a new file.\n", event_name, TimeToString(timestamp).c_str(), ntags);

         s = NULL;
      }
   }

   if (s) {
      // maybe this schema is too old - rotate files every so often
      time_t age = timestamp - s->time_from;
      //printf("*** age %s (%.1f months), timestamp %s, time_from %s, file %s\n", TimeToString(age).c_str(), (double)age/(double)kMonth, TimeToString(timestamp).c_str(), TimeToString(s->time_from).c_str(), s->file_name.c_str());
      if (age > fConfMaxFileAge) {
         if (fDebug)
            printf("FileHistory::new_event: event [%s], timestamp %s, ntags %d: schema is too old, age %.1f months, starting a new file.\n", event_name, TimeToString(timestamp).c_str(), ntags, (double)age/(double)kMonth);

         // force creation of a new file
         s = NULL;
      }
   }

   if (s) {
      // maybe this file is too big - rotate files to limit maximum size
      double size = ss_file_size(s->file_name.c_str());
      //printf("*** size %.0f, file %s\n", size, s->file_name.c_str());
      if (size > fConfMaxFileSize) {
         if (fDebug)
            printf("FileHistory::new_event: event [%s], timestamp %s, ntags %d: file too big, size %.1f MiBytes, starting a new file.\n", event_name, TimeToString(timestamp).c_str(), ntags, size/MiB);

         // force creation of a new file
         s = NULL;
      }
   }

   if (!s) {
      std::string filename;

      status = create_file(event_name, timestamp, ntags, tags, &filename);
      if (status != HS_SUCCESS)
         return NULL;

      HsFileSchema* ss = read_file_schema(filename.c_str());
      if (!ss) {
         cm_msg(MERROR, "FileHistory::new_event", "Error: Cannot create schema for event \'%s\', see previous messages", event_name);
         return NULL;
      }

      fWriterCurrentSchema.add(ss);

      s = (HsFileSchema*)fWriterCurrentSchema.find_event(event_name, timestamp);

      if (!s) {
         cm_msg(MERROR, "FileHistory::new_event", "Error: Cannot create schema for event \'%s\', see previous messages", event_name);
         return NULL;
      }

      if (xdebug) {
         printf("*** New schema for event [%s] time %s:\n", event_name, TimeToString(timestamp).c_str());
         s->print();
      }
   }

   assert(s != NULL);

#if 0
   {
      printf("schema for [%s] is %p\n", event_name, s);
      if (s)
         s->print();
   }
#endif

   HsFileSchema* e = new HsFileSchema();

   *e = *s; // make a copy of the schema

   return e;
}

int FileHistory::create_file(const char* event_name, time_t timestamp, int ntags, const TAG tags[], std::string* filenamep)
{
   if (fDebug)
      printf("FileHistory::create_file: event [%s]\n", event_name);

   // NB: file names are constructed in such a way
   // that when sorted lexicographically ("ls -1 | sort")
   // they *also* become sorted by time

   struct tm tm;
   localtime_r(&timestamp, &tm); // somebody must call tzset() before this.

   char buf[256];
   strftime(buf, sizeof(buf), "%Y%m%d", &tm);

   std::string filename;
   filename += fPath;
   filename += "mhf_";
   filename += TimeToString(timestamp);
   filename += "_";
   filename += buf;
   filename += "_";
   filename += MidasNameToFileName(event_name);

   std::string try_filename = filename + ".dat";

   FILE *fp = NULL;
   for (int itry=0; itry<10; itry++) {
      if (itry > 0) {
         char s[256];
         sprintf(s, "_%d", rand());
         try_filename = filename + s + ".dat";
      }

      fp = fopen(try_filename.c_str(), "r");
      if (fp != NULL) {
         // this file already exists, try with a different name
         fclose(fp);
         continue;
      }

      fp = fopen(try_filename.c_str(), "w");
      if (fp == NULL) {
         // somehow cannot create this file, try again
         cm_msg(MERROR, "FileHistory::create_file", "Error: Cannot create file \'%s\' for event \'%s\', fopen() errno %d (%s)", try_filename.c_str(), event_name, errno, strerror(errno));
         continue;
      }

      // file opened
      break;
   }

   if (fp == NULL) {
      // somehow cannot create any file, whine!
      cm_msg(MERROR, "FileHistory::create_file", "Error: Cannot create file \'%s\' for event \'%s\'", filename.c_str(), event_name);
      return HS_FILE_ERROR;
   }

   std::string ss;

   ss += "version: 2.0\n";
   ss += "event_name: ";
   ss    += event_name;
   ss    += "\n";
   ss += "time: ";
   ss    += TimeToString(timestamp);
   ss    += "\n";

   int recsize = 0;

   ss += "tag: /DWORD 1 4 /timestamp\n";
   recsize += 4;

   bool padded = false;
   int offset = 0;

   bool xdebug = false; // (strcmp(event_name, "u_Beam") == 0);

   for (int i=0; i<ntags; i++) {
      int tsize = rpc_tid_size(tags[i].type);
      int n_bytes = tags[i].n_data*tsize;
      int xalign = (offset % tsize);

      if (xdebug)
         printf("tag %d, tsize %d, n_bytes %d, xalign %d\n", i, tsize, n_bytes, xalign);

#if 0
      // looks like history data does not do alignement and padding
      if (xalign != 0) {
         padded = true;
         int pad_bytes = tsize - xalign;
         assert(pad_bytes > 0);

         ss += "tag: ";
         ss    += "XPAD";
         ss    += " ";
         ss    += SmallIntToString(1);
         ss    += " ";
         ss    += SmallIntToString(pad_bytes);
         ss    += " ";
         ss    += "pad_";
         ss    += SmallIntToString(i);
         ss    += "\n";

         offset += pad_bytes;
         recsize += pad_bytes;

         assert((offset % tsize) == 0);
         fprintf(stderr, "FIXME: need to debug padding!\n");
         //abort();
      }
#endif
      
      ss += "tag: ";
      ss    += rpc_tid_name(tags[i].type);
      ss    += " ";
      ss    += SmallIntToString(tags[i].n_data);
      ss    += " ";
      ss    += SmallIntToString(n_bytes);
      ss    += " ";
      ss    += tags[i].name;
      ss    += "\n";

      recsize += n_bytes;
      offset += n_bytes;
   }

   ss += "record_size: ";
   ss    += SmallIntToString(recsize);
   ss    += "\n";

   // reserve space for "data_offset: ..."
   int sslength = ss.length() + 127;

   int block = 1024;
   int nb = (sslength + block - 1)/block;
   int data_offset = block * nb;

   ss += "data_offset: ";
   ss    += SmallIntToString(data_offset);
   ss    += "\n";

   fprintf(fp, "%s", ss.c_str());

   fclose(fp);

   if (1 && padded) {
      printf("Schema in file %s has padding:\n", try_filename.c_str());
      printf("%s", ss.c_str());
   }

   if (filenamep)
      *filenamep = try_filename;

   return HS_SUCCESS;
}

HsFileSchema* FileHistory::read_file_schema(const char* filename)
{
   if (fDebug)
      printf("FileHistory::read_file_schema: file %s\n", filename);

   FILE* fp = fopen(filename, "r");
   if (!fp) {
      cm_msg(MERROR, "FileHistory::read_file_schema", "Cannot read \'%s\', fopen() errno %d (%s)", filename, errno, strerror(errno));
      return NULL;
   }

   HsFileSchema* s = NULL;

   // File format looks like this:
   // version: 2.0
   // event_name: u_Beam
   // time: 1023174012
   // tag: /DWORD 1 4 /timestamp
   // tag: FLOAT 1 4 B1
   // ...
   // tag: FLOAT 1 4 Ref Heater
   // record_size: 84
   // data_offset: 1024

   int rd_recsize = 0;
   int offset = 0;

   while (1) {
      char buf[1024];
      char* b = fgets(buf, sizeof(buf), fp);

      //printf("read: %s\n", b);

      if (!b) {
         break; // end of file
      }

      char*bb;

      bb = strchr(b, '\n');
      if (bb)
         *bb = 0;

      bb = strchr(b, '\r');
      if (bb)
         *bb = 0;

      bb = strstr(b, "version: 2.0");
      if (bb == b) {
         s = new HsFileSchema();
         assert(s);

         s->file_name = filename;
         continue;
      }

      if (!s) {
         // malformed history file
         break;
      }

      bb = strstr(b, "event_name: ");
      if (bb == b) {
         s->event_name = bb + 12;
         continue;
      }

      bb = strstr(b, "time: ");
      if (bb == b) {
         s->time_from = strtoul(bb + 6, NULL, 10);
         continue;
      }

      // tag format is like this:
      //
      // tag: FLOAT 1 4 Ref Heater
      //
      // "FLOAT" is the MIDAS type, "/DWORD" is special tag for the timestamp
      // "1" is the number of array elements
      // "4" is the total tag size in bytes (n_data*tid_size)
      // "Ref Heater" is the tag name

      bb = strstr(b, "tag: ");
      if (bb == b) {
         bb += 5; // now points to the tag MIDAS type
         const char* midas_type = bb;
         char* bbb = strchr(bb, ' ');
         if (bbb) {
            *bbb = 0;
            HsSchemaEntry t;
            if (midas_type[0] == '/') {
               t.type = 0;
            } else {
               t.type = rpc_name_tid(midas_type);
               if (t.type == 0) {
                  cm_msg(MERROR, "FileHistory::read_file_schema", "Unknown MIDAS data type \'%s\' in history file \'%s\'", midas_type, filename);
                  if (s)
                     delete s;
                  s = NULL;
                  break;
               }
            }
            bbb++;
            while (*bbb == ' ')
               bbb++;
            if (*bbb) {
               t.n_data = strtoul(bbb, &bbb, 10);
               while (*bbb == ' ')
                  bbb++;
               if (*bbb) {
                  t.n_bytes = strtoul(bbb, &bbb, 10);
                  while (*bbb == ' ')
                     bbb++;
                  t.name = bbb;
               }
            }

            if (midas_type[0] != '/') {
               s->variables.push_back(t);
               s->offsets.push_back(offset);
               offset += t.n_bytes;
            }

            rd_recsize += t.n_bytes;
         }
         continue;
      }

      bb = strstr(b, "record_size: ");
      if (bb == b) {
         s->record_size = atoi(bb + 12);
         continue;
      }

      bb = strstr(b, "data_offset: ");
      if (bb == b) {
         s->data_offset = atoi(bb + 12);
         // data offset is the last entry in the file
         break;
      }
   }

   fclose(fp);

   if (!s) {
      cm_msg(MERROR, "FileHistory::read_file_schema", "Malformed history schema in \'%s\', maybe it is not a history file", filename);
      return NULL;
   }

   if (rd_recsize != s->record_size) {
      cm_msg(MERROR, "FileHistory::read_file_schema", "Record size mismatch in history schema from \'%s\', file says %d while total of all tags is %d", filename, s->record_size, rd_recsize);
      if (s)
         delete s;
      return NULL;
   }

   if (!s) {
      cm_msg(MERROR, "FileHistory::read_file_schema", "Could not read history schema from \'%s\', maybe it is not a history file", filename);
      if (s)
         delete s;
      return NULL;
   }

   if (fDebug > 1)
      s->print();

   return s;
}

////////////////////////////////////////////////////////
//               Factory constructors                 //
////////////////////////////////////////////////////////

MidasHistoryInterface* MakeMidasHistorySqlite()
{
#ifdef HAVE_SQLITE
   return new SqliteHistory();
#else
   cm_msg(MERROR, "MakeMidasHistorySqlite", "Error: Cannot initialize SQLITE history - this MIDAS was built without SQLITE support - HAVE_SQLITE is not defined");
   return NULL;
#endif
}

MidasHistoryInterface* MakeMidasHistoryMysql()
{
#ifdef HAVE_MYSQL
   return new MysqlHistory();
#else
   cm_msg(MERROR, "MakeMidasHistoryMysql", "Error: Cannot initialize MySQL history - this MIDAS was built without MySQL support - HAVE_MYSQL is not defined");
   return NULL;
#endif
}

MidasHistoryInterface* MakeMidasHistoryFile()
{
   return new FileHistory();
}

/* emacs
 * Local Variables:
 * tab-width: 8
 * c-basic-offset: 3
 * indent-tabs-mode: nil
 * End:
 */
