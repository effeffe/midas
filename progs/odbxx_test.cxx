/********************************************************************\

  Name:         odbxx.cxx
  Created by:   Stefan Ritt

  Contents:     Object oriented interface to ODB

\********************************************************************/

#include <stdio.h>
#include <assert.h>
#include <string>
#include <iostream>
#include <stdexcept>
#include <string.h>

/*
#include <json.hpp>
using json = nlohmann::json;
*/

#include "midas.h"

/*------------------------------------------------------------------*/

namespace midas {

   class odb;

   class u_odb {
   private:
      // union to hold data
      union {
         uint8_t      m_uint8;
         int8_t       m_int8;
         uint16_t     m_uint16;
         int16_t      m_int16;
         uint32_t     m_uint32;
         int32_t      m_int32;
         bool         m_bool;
         float        m_float;
         double       m_double;
         std::string* m_string;
         odb*         m_odb;
      };

      odb* m_parent_odb;

   public:
      u_odb() : m_string{} {};
      u_odb(odb* o) : m_string{} {};

      // Destructor
      ~u_odb();

      odb *get_odb() { return m_odb; }

      void set_parent(odb *o) {
         m_parent_odb = o;
      }

      // Overload the Assignment Operators
      template <typename T>
      T operator=(T);

      void set(uint8_t v) {
         m_uint8 = v;
      }
      void set(int8_t v) {
         m_uint8 = v;
      }
      void set(uint16_t v) {
         m_uint16 = v;
      }
      void set(int16_t v) {
         m_uint16 = v;
      }
      void set(uint32_t v) {
         m_uint32 = v;
      }
      void set(int32_t v) {
         m_uint32 = v;
      }
      void set(bool v) {
         m_bool = v;
      }
      void set(float v) {
         m_float = v;
      }
      void set(double v) {
         m_double = v;
      }
      void set(const char *v) {
         m_string = new std::string(v);
      }
      void set(const std::string &v) {
         m_string = new std::string(v);
      }
      void set(odb *v) {
         m_odb = v;
      }

      // overload the conversion operator for std::string
      operator std::string() {
         std::string s;
         get(s);
         return s;
      }

      // overload all standard conversion operators
      template <typename T>
      operator T() {
         return get<T>(); // forward to get<T>()
      }

      // get function for basic type
      template <typename T>
      T get();

      std::string get() {
         std::string s;
         get(s);
         return s;
      }

      // get function for strings
      void get(std::string &s);

      // overload stream out operator
      friend std::ostream &operator<<(std::ostream &output, u_odb &o) {
         std::string s = o;
         output << s;
         return output;
      };

   };

   //-----------------------------------------------

   class odb {
   private:

      // handle to ODB, same for all instances
      static HNDLE m_hDB;
      static bool  m_debug;

      // various parameters
      bool         m_preserve_string_size;

      // type of this object, one of TID_xxx
      int          m_tid;
      // vector containing data for this object
      u_odb*       m_data;

      std::string  m_name;
      int          m_num_values;
      int          m_last_index;
      HNDLE        m_hKey;

   public:
      // Default constructor
      odb() :
         m_preserve_string_size(true),
         m_tid{0},
         m_data{nullptr},
         m_name{},
         m_num_values{},
         m_last_index{-1},
         m_hKey{}
         {}

      // Destructor
      ~odb() {
         delete[] m_data;
      }

      // Constructor with ODB path
      explicit odb(std::string path, int tid = 0, int n_values = 1) : odb() {
         if (m_hDB == 0)
            cm_get_experiment_database(&m_hDB, nullptr);
         if (path.find_last_of('/') == std::string::npos)
            m_name = path;
         else
            m_name = path.substr(path.find_last_of('/')+1);
         get_hkey(path, tid, n_values);
         init_mdata();
      }

      // Constuctor with ODB key
      explicit odb(HNDLE hkey) : odb() {
         get_hkey(hkey);
         init_mdata();
      }

      // Setters and Getters
      static void set_debug(bool flag) { m_debug = flag; }
      static bool get_debug() { return m_debug; }

      bool is_preserve_string_size() const { return m_preserve_string_size; }
      void set_preserve_string_size(bool f) { m_preserve_string_size = f; }
      int get_tid() { return m_tid; }
      std::string get_name() { return m_name; }

      std::string get_full_path() {
         char str[256];
         db_get_path(m_hDB, m_hKey, str, sizeof(str));
         return str;
      }

      // Overload the Assignment Operators
      template <typename T>
      const T &operator=(const T &v) {
         m_data[0] = v;
         return v;
      }

      template <typename T>
      const std::vector<T> &operator=(const std::vector<T> &v) {
         if (v.size() != m_num_values)
            throw std::runtime_error("ODB key \"" + get_full_path() +
            "\["+std::to_string(m_num_values)+"] is assigned a vector of size " +
            std::to_string(v.size()));

         for (int i=0 ; i<m_num_values ; i++)
            m_data[i].set(v[i]);
         m_last_index = -1; // force update of all values to ODB
         send_data_to_odb();
         return v;
      }

      // overload conversion operator for std::string
      operator std::string() {
         std::string s;
         if (m_tid == TID_KEY)
            print(s, 0);
         else
            get(s); // forward to get(std::string)
         return s;
      }

      // overload conversion operator for bool
      operator bool() {
         bool b;
         get(b);
         return b;
      }

      // overload conversion operator for std::vector<T>
      template <typename T>
      operator std::vector<T>() {
         get_data_from_odb();
         std::vector<T> v(m_num_values);
         for (int i=0 ; i<m_num_values ; i++)
            v[i] = m_data[i];
         return v;
      }

      // overload all other conversion operators
      template <typename T, typename std::enable_if<
              std::is_same<T, uint8_t>::value ||
              std::is_same<T, int8_t>::value ||
              std::is_same<T, uint16_t>::value ||
              std::is_same<T, int16_t>::value ||
              std::is_same<T, uint32_t>::value ||
              std::is_same<T, int32_t>::value ||
              std::is_same<T, float>::value ||
              std::is_same<T, double>::value
              ,T>::type* = nullptr>
      operator T() {
         return get<T>(); // forward to get<T>()
      }

      // overload stream out operator
      friend std::ostream &operator<<(std::ostream &output, odb &o) {
         std::string s = o;
         output << s;
         return output;
      };

      // overload index operator for arrays
      u_odb& operator[](int index) {
         if (index < 0 || index >= m_num_values)
            throw std::out_of_range("Index \"" + std::to_string(index) + "\" out of range for ODB key \"" +
            get_full_path() + "\"");

         m_last_index = index;
         return m_data[index];
      }

      // overload index operator for subkeys
      odb& operator[](std::string str) {
         return get_subkey(str);
      }

      odb& operator[](const char *str) {
         return get_subkey(std::string(str));
      }

      odb& get_subkey(std::string str) {
         if (m_tid != TID_KEY)
            throw std::runtime_error("ODB key \"" + get_full_path() + "\" does not have subkeys");

         int i;
         for (i=0 ; i<m_num_values ; i++)
            if (m_data[i].get_odb()->get_name() == str)
               break;
         if (i == m_num_values)
            throw std::runtime_error("ODB key \"" + get_full_path() + "\" does not contain subkey \"" + str + "\"");

         return *m_data[i].get_odb();
      }

      // get function for basic types
      template <typename T>
      T get() {
         get_data_from_odb();
         return (T)m_data[0];
      }

      // get function for basic types as a parameter
      template <typename T>
      void get(T& v) {
         get_data_from_odb();
         v = (T)m_data[0];
      }

      // get function for strings
      void get(std::string &s, bool quotes = false) {
         get_data_from_odb();

         // put value into quotes
         s = "";
         std::string sd;
         for (int i=0 ; i<m_num_values ; i++) {
            m_data[i].get(sd);
            if (quotes)
               s += "\"";
            s += sd;
            if (quotes)
               s += "\"";
            if (i < m_num_values-1)
               s += ",";
         }
      }

      // overload arithmetic operators
      int operator+(const int i) {
         int s;
         get(s);
         return s + i;
      }

      std::string print() {
         std::string s;
         s = "{\n";
         print(s, 1);
         s += "\n}";
         return s;
      }

      void print(std::string &s, int indent) {
         for (int i = 0; i < indent; i++)
            s += "   ";
         if (m_tid == TID_KEY) {
            s += "\"" + m_name + "\": {\n";
            for (int i= 0; i<m_num_values ; i++) {
               std::string v;
               m_data[i].get_odb()->print(v, indent+1);
               s += v;
               if (i < m_num_values-1)
                  s += ",\n";
               else
                  s += "\n";
            }
            for (int i = 0; i < indent; i++)
               s += "   ";
            s += "}";

         } else {
            s += "\"" + m_name + "\": ";
            if (m_num_values > 1)
               s += "[";
            std::string v;
            get(v, m_tid == TID_STRING);
            s += v;
            if (m_num_values > 1)
               s += "]";
         }
      }

      void get_hkey(std::string path, int tid, int n_values) {
         int status = db_find_key(m_hDB, 0, path.c_str(), &m_hKey);
         if (status != DB_SUCCESS) {
            if (tid > 0) {
               status = db_create_key(m_hDB, 0, path.c_str(), tid);
               if (status != DB_SUCCESS)
                  throw std::runtime_error("ODB key \"" + path + "\" cannot be created");
               status = db_find_key(m_hDB, 0, path.c_str(), &m_hKey);
               if (status != DB_SUCCESS)
                  throw std::runtime_error("ODB key \"" + path + "\" not found after creation");
               if (m_debug)
                  std::cout << "Created ODB key " + get_full_path() << std::endl;
               if (n_values > 1) {
                  char dummy[8]{};
                  status = db_set_data_index(m_hDB, m_hKey, &dummy, rpc_tid_size(tid), n_values - 1, tid);
                  if (status != DB_SUCCESS)
                     throw std::runtime_error("ODB key \"" + path + "\" cannot be resized");
               }
            } else
               throw std::runtime_error("ODB key \"" + path + "\" cannot be found");
         }
         KEY key;
         status = db_get_key(m_hDB, m_hKey, &key);
         if (status != DB_SUCCESS)
            throw std::runtime_error("db_get_key for ODB key \"" + path + "\" failed with status " + std::to_string(status));
         m_tid = key.type;
         m_num_values = key.num_values;
         if (m_debug) {
            std::cout << "Get definition for ODB key " + get_full_path() << std::endl;
         }
      }

      void get_hkey(HNDLE hkey) {
         m_hKey = hkey;
         KEY key;
         int status = db_get_key(m_hDB, m_hKey, &key);
         if (status != DB_SUCCESS)
            throw std::runtime_error("db_get_key for ODB key \"" + get_full_path() + "\" failed with status " + std::to_string(status));
         m_tid = key.type;
         m_num_values = key.num_values;
         m_name = key.name;
         if (m_debug) {
            std::cout << "Get definition for ODB key " + get_full_path() << std::endl;
         }
      }

      void init_mdata() {
         if (m_data == nullptr) {
            if (m_tid == TID_KEY) {
               std::vector<HNDLE> hlist;
               for (int i = 0;; i++) {
                  HNDLE h;
                  int status = db_enum_key(m_hDB, m_hKey, i, &h);
                  if (status != DB_SUCCESS)
                     break;
                  hlist.push_back(h);
                  m_num_values = i + 1;
               }
               m_data = new u_odb[m_num_values]{};
               for (int i = 0; i < m_num_values; i++) {
                  m_data[i].set(new odb(hlist[i]));
                  m_data[i].set_parent(this);
               }

               return;
            }

            m_data = new u_odb[m_num_values]{};
            for (int i = 0; i < m_num_values; i++)
               m_data[i].set_parent(this);
         }
      }

      void get_data_from_odb() {
         int status{}, size{};
         if (m_tid == TID_UINT8) {
            size = sizeof(uint8_t) * m_num_values;
            uint8_t *d = (uint8_t *)malloc(size);
            status = db_get_data(m_hDB, m_hKey, d, &size, m_tid);
            for (int i=0 ; i<m_num_values ; i++)
               m_data[i].set(d[i]);
            free(d);
         } else if (m_tid == TID_INT8) {
            size = sizeof(int8_t) * m_num_values;
            int8_t *d = (int8_t *)malloc(size);
            status = db_get_data(m_hDB, m_hKey, d, &size, m_tid);
            for (int i=0 ; i<m_num_values ; i++)
               m_data[i].set(d[i]);
            free(d);
         } else if (m_tid == TID_UINT16) {
            size = sizeof(uint16_t) * m_num_values;
            uint16_t *d = (uint16_t *)malloc(size);
            status = db_get_data(m_hDB, m_hKey, d, &size, m_tid);
            for (int i=0 ; i<m_num_values ; i++)
               m_data[i].set(d[i]);
            free(d);
         } else if (m_tid == TID_INT16) {
            size = sizeof(int16_t) * m_num_values;
            int16_t *d = (int16_t *)malloc(size);
            status = db_get_data(m_hDB, m_hKey, d, &size, m_tid);
            for (int i=0 ; i<m_num_values ; i++)
               m_data[i].set(d[i]);
            free(d);
         } else if (m_tid == TID_UINT32) {
            size = sizeof(uint32_t) * m_num_values;
            uint32_t *d = (uint32_t *)malloc(size);
            status = db_get_data(m_hDB, m_hKey, d, &size, m_tid);
            for (int i=0 ; i<m_num_values ; i++)
               m_data[i].set(d[i]);
            free(d);
         } else if (m_tid == TID_INT32) {
            size = sizeof(int32_t) * m_num_values;
            int32_t *d = (int32_t *)malloc(size);
            status = db_get_data(m_hDB, m_hKey, d, &size, m_tid);
            for (int i=0 ; i<m_num_values ; i++)
               m_data[i].set(d[i]);
            free(d);
         } else if (m_tid == TID_BOOL) {
            size = sizeof(BOOL) * m_num_values;
            bool *d = (bool *)malloc(size);
            status = db_get_data(m_hDB, m_hKey, d, &size, m_tid);
            for (int i=0 ; i<m_num_values ; i++)
               m_data[i].set(d[i]);
            free(d);
         } else if (m_tid == TID_FLOAT) {
            size = sizeof(float) * m_num_values;
            float *d = (float *)malloc(size);
            status = db_get_data(m_hDB, m_hKey, d, &size, m_tid);
            for (int i=0 ; i<m_num_values ; i++)
               m_data[i].set(d[i]);
            free(d);
         } else if (m_tid == TID_DOUBLE) {
            size = sizeof(double) * m_num_values;
            double *d = (double *)malloc(size);
            status = db_get_data(m_hDB, m_hKey, d, &size, m_tid);
            for (int i=0 ; i<m_num_values ; i++)
               m_data[i].set(d[i]);
            free(d);
         }  else if (m_tid == TID_STRING) {
            KEY key;
            db_get_key(m_hDB, m_hKey, &key);
            char *str = (char *)malloc(key.total_size);
            size = key.total_size;
            status = db_get_data(m_hDB, m_hKey, str, &size, m_tid);
            for (int i=0 ; i<m_num_values ; i++)
               m_data[i].set(str + i*key.item_size);
            free(str);
         } else
            throw std::runtime_error("get_data for ODB key \"" + get_full_path() +
                                     "\" failed due to unsupported type " + std::to_string(m_tid));

         if (status != DB_SUCCESS)
            throw std::runtime_error("db_get_data for ODB key \"" + get_full_path() +
                                     "\" failed with status " + std::to_string(status));
         if (m_debug) {
            std::string s;
            get(s);
            std::cout << "Get ODB key " + get_full_path() + ": " + s << std::endl;
         }
      }

      void send_data_to_odb(int index) {
         int status{};
         if (m_tid == TID_STRING) {
            KEY key;
            db_get_key(m_hDB, m_hKey, &key);
            std::string s;
            m_data[0].get(s);
            status = db_set_data_index(m_hDB, m_hKey, s.c_str(), key.item_size, index, m_tid);
            if (m_debug)
               std::cout << "Set ODB key " + get_full_path() + "[" + std::to_string(index) + "] = " + s << std:: endl;
         } else {
            u_odb u = m_data[index];
            status = db_set_data_index(m_hDB, m_hKey, &u, rpc_tid_size(m_tid), index, m_tid);
            if (m_debug) {
               std::string s;
               u.get(s);
               std::cout << "Set ODB key " + get_full_path() + "[" + std::to_string(index) + "] = " + s << std::endl;
            }
         }
         if (status != DB_SUCCESS)
            throw std::runtime_error("db_set_data for ODB key \"" + get_full_path() +
                                     "\" failed with status " + std::to_string(status));
      }

      void send_data_to_odb() {

         if (m_tid == TID_KEY)
            return;

         if (m_num_values == 1) {
            send_data_to_odb(0);
            return;
         }

         if (m_last_index != -1) {
            send_data_to_odb(m_last_index);
            return;
         }

         int status{};
         if (m_tid == TID_STRING) {
            if (is_preserve_string_size() || m_num_values > 1) {
               KEY key;
               db_get_key(m_hDB, m_hKey, &key);
               char *str = (char *)malloc(key.total_size);
               for (int i=0 ; i<m_num_values ; i++) {
                  std::string d;
                  m_data[i].get(d);
                  strncpy(str + i * key.item_size, d.c_str(), key.item_size);
               }
               status = db_set_data(m_hDB, m_hKey, str, key.total_size, m_num_values, m_tid);
               free(str);
               if (m_debug)
                  std::cout << "Set ODB key " + get_full_path() + "[0..." + std::to_string(m_num_values) + "]" << std:: endl;
            } else {
               std::string s;
               m_data[0].get(s);
               status = db_set_data(m_hDB, m_hKey, s.c_str(), s.length() + 1, 1, m_tid);
               if (m_debug)
                  std::cout << "Set ODB key " + get_full_path() + " = " + s << std:: endl;
            }
         } else {
            int size = rpc_tid_size(m_tid) * m_num_values;
            uint8_t *buffer = (uint8_t *) malloc(size);
            uint8_t *p = buffer;
            for (int i = 0; i < m_num_values; i++) {
               memcpy(p, &m_data[i], rpc_tid_size(m_tid));
               p += rpc_tid_size(m_tid);
            }
            status = db_set_data(m_hDB, m_hKey, buffer, size, m_num_values, m_tid);
            free(buffer);
            if (m_debug) {
               std::string s;
               get(s);
               std::cout << "Set ODB key " + get_full_path() + " = " + s << std::endl;
            }
         }

         if (status != DB_SUCCESS)
            throw std::runtime_error("db_set_data for ODB key \"" + get_full_path() +
            "\" failed with status " + std::to_string(status));
      }
   };

   //-----------------------------------------------

   // initialize static variables
   HNDLE odb::m_hDB = 0;
   bool odb::m_debug = false;

   //-----------------------------------------------

   //---- u_odb implementations calling functions from odb
   u_odb::~u_odb() {
      if (m_parent_odb->get_tid() == TID_STRING && m_string != nullptr)
         delete m_string;
   }

   template <typename T>
   T u_odb::get() {
      int tid = m_parent_odb->get_tid();
      if (tid == TID_UINT8)
         return (T)m_uint8;
      else if (tid == TID_INT8)
         return (T)m_int8;
      else if (tid == TID_UINT16)
         return (T)m_uint16;
      else if (tid == TID_INT16)
         return (T)m_int16;
      else if (tid == TID_UINT32)
         return (T) m_uint32;
      else if (tid == TID_INT32)
         return (T)m_int32;
      else if (tid == TID_BOOL)
         return (T)m_bool;
      else if (tid == TID_FLOAT)
         return (T)m_float;
      else if (tid == TID_DOUBLE)
         return (T)m_double;
      else
         throw std::runtime_error("Invalid type ID %s" + std::to_string(tid));
   }

   // get function for strings
   void u_odb::get(std::string &s) {
      int tid = m_parent_odb->get_tid();
      if (tid == TID_UINT8)
         s = std::to_string(m_uint8);
      else if (tid == TID_INT8)
         s = std::to_string(m_int8);
      else if (tid == TID_UINT16)
         s = std::to_string(m_uint16);
      else if (tid == TID_INT16)
         s = std::to_string(m_int16);
      else if (tid == TID_UINT32)
         s = std::to_string(m_uint32);
      else if (tid == TID_INT32)
         s = std::to_string(m_int32);
      else if (tid == TID_BOOL)
         s = std::string(m_bool ? "true" : "false");
      else if (tid == TID_FLOAT)
         s = std::to_string(m_float);
      else if (tid == TID_DOUBLE)
         s = std::to_string(m_double);
      else if (tid == TID_STRING)
         s = *m_string;
      else
         throw std::runtime_error("Invalid type ID %s" + std::to_string(tid));
   }

   //---- u_odb assignment operator overload which call db::send_data_to_odb()
   template <typename T>
   T u_odb::operator=(T v) {
      set(v);
      m_parent_odb->send_data_to_odb();
      return v;
   }

}

/*------------------------------------------------------------------*/

int main() {

   cm_connect_experiment(NULL, NULL, "test", NULL);


//   midas::odb ok("/Experiment/Security/RPC hosts/Allowed hosts");
//   std::cout << ok.print() << std::endl;
//   std::vector<std::string> v = ok;
//   v[2] = "Host2";
//   ok = v;

   // test with int
   midas::odb oi("/Experiment/ODB Timeout");
   oi.set_debug(true);

   oi = 10000;
   std::cout << oi << std::endl;
   oi = oi + 1;
   std::cout << oi << std::endl;

   // test with bool
   midas::odb ob("/Experiment/Enable core dumps");
   bool b = ob;
   std::cout << ob << std::endl;
   ob = true;
   std::cout << ob << std::endl;
   ob = b;
   std::cout << ob << std::endl;

   // test with a vector
   midas::odb oa("/Experiment/Test", TID_INT, 10);
   oa[5] = 5555;
   std::vector<int> v = oa;
   v[3] = 33333;
   oa = v;
   std::cout << oa.print() << std::endl;

   midas::odb ot("/Experiment");
   std::cout << ot["ODB timeout"] << std::endl;
   ot["ODB timeout"] = 12345;
   ot["Security"]["Enable non-localhost RPC"] = true;

   std::cout << ot.print() << std::endl;
   /*
   ot[0] = 123456;
   std::string s = ot[0];
   int i = ot[0];
   std::cout << i << std::endl;

   midas::odb on("/Experiment/Name");
   std::cout << on << std::endl;

   std::string s2 = on;
   s2 = on;
   std::cout << s2 << std::endl;
   on = "MEG";
   on = std::string("Test");
   */

   cm_disconnect_experiment();
   return 1;
}