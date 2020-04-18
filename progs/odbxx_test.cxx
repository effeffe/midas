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

      template <typename T>
      void set(T v);
      void set(odb* v);
      void set(std::string v);
      void set(const char* v);
      void set(char* v);
      void add(double inc, bool push=true);
      void mult(double f, bool push=true);

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

      // overlaod arithemetic operators
      u_odb& operator++(int) {
         add(1);
         return *this;
      }
      u_odb& operator++() {
         add(1);
         return *this;
      }
      u_odb& operator--(int) {
         add(-1);
         return *this;
      }
      u_odb& operator--() {
         add(-1);
         return *this;
      }
      u_odb& operator+=(double d) {
         add(d);
         return *this;
      }
      u_odb& operator-=(double d) {
         add(-d);
         return *this;
      }
      u_odb& operator*=(double d) {
         mult(d);
         return *this;
      }
      u_odb& operator/=(double d) {
         if (d == 0)
            throw std::runtime_error("Division by zero");
         mult(1/d);
         return *this;
      }

      template <typename T>
      u_odb& operator+(T v) {
         double d = *this;
         d += v;
         set(v);
         return *this;
      }
      template <typename T>
      u_odb& operator-(T v) {
         double d = *this;
         d -= v;
         set(v);
         return *this;
      }
      template <typename T>
      u_odb& operator*(T v) {
         double d = *this;
         d *= v;
         set(v);
         return *this;
      }
      template <typename T>
      u_odb& operator/(T v) {
         double d = *this;
         d /= v;
         set(v);
         return *this;
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
      bool         m_auto_refresh;

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
              m_preserve_string_size(false),
              m_auto_refresh{true},
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

      // Constructor with ODB key
      explicit odb(HNDLE hkey) : odb() {
         get_hkey(hkey);
         init_mdata();
      }

      // Constructor with ODB object
      explicit odb(odb& o) : odb() {
         odb(o.get_full_path());
      }
      // Setters and Getters
      static void set_debug(bool flag) { m_debug = flag; }
      static bool get_debug() { return m_debug; }

      bool is_preserve_string_size() const { return m_preserve_string_size; }
      void set_preserve_string_size(bool f) { m_preserve_string_size = f; }
      bool is_auto_refresh() const { return m_auto_refresh; }
      void set_auto_refresh(bool f) { m_auto_refresh = f; }
      int get_tid() { return m_tid; }
      std::string get_name() { return m_name; }

      std::string get_full_path() {
         char str[256];
         db_get_path(m_hDB, m_hKey, str, sizeof(str));
         return str;
      }

      void remove() {
         // delete key in ODB
         int status = db_delete_key(m_hDB, m_hKey, FALSE);
         if (status != DB_SUCCESS)
            throw std::runtime_error("db_delete_key for ODB key \"" + get_full_path() +
                                     "\" returnd error code " + std::to_string(status));
         // invalidate this object
         delete[] m_data;
         m_data = nullptr;
         m_num_values = 0;
         m_tid = 0;
         m_hKey = 0;
         m_name = "";
      }

      // Overload the Assignment Operators
      template <typename T>
      const T &operator=(const T &v) {
         for (int i=0 ; i<m_num_values ; i++)
            m_data[i].set(v);
         push();
         return v;
      }

      template <typename T>
      const std::vector<T> &operator=(const std::vector<T> &v) {

         // resize internal array if different
         if (v.size() != m_num_values) {
            if (m_tid == TID_STRING) {
               // TBD
            } else {
               auto new_array = new u_odb[v.size()]{};
               if (v.size() < m_num_values)
                  memcpy(new_array, m_data, v.size()*sizeof(u_odb));
               else
                  memcpy(new_array, m_data, m_num_values*sizeof(u_odb));
               delete[] m_data;
               m_data = new_array;
               m_num_values = v.size();
               for (int i = 0; i < m_num_values; i++)
                  m_data[i].set_parent(this);
            }
         }

         for (int i=0 ; i<m_num_values ; i++)
            m_data[i].set(v[i]);
         push();
         return v;
      }

      void resize(int size) {
         if (m_tid == TID_STRING) {
            // TBD
         } else {
            u_odb* new_array = new u_odb[size]{};
            if (size < m_num_values)
               memcpy(new_array, m_data, size*sizeof(u_odb));
            else
               memcpy(new_array, m_data, m_num_values*sizeof(u_odb));
            delete[] m_data;
            m_data = new_array;
            m_num_values = size;
            for (int i = 0; i < m_num_values; i++)
               m_data[i].set_parent(this);
         }
         push();
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

      // overload conversion operator for std::vector<T>
      template <typename T>
      operator std::vector<T>() {
         if (m_auto_refresh)
            pull();
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
              std::is_same<T, bool>::value ||
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
                                    get_full_path() + "[0..." + std::to_string(m_num_values-1) + "]\"");

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

         std::string first = str;
         std::string tail{""};
         if (str.find('/') != std::string::npos) {
            first = str.substr(0, str.find('/'));
            tail = str.substr(str.find('/')+1);
         }

         int i;
         for (i=0 ; i<m_num_values ; i++)
            if (m_data[i].get_odb()->get_name() == first)
               break;
         if (i == m_num_values)
            throw std::runtime_error("ODB key \"" + get_full_path() + "\" does not contain subkey \"" + first + "\"");

         if (tail != "")
            return m_data[i].get_odb()->get_subkey(tail);

         return *m_data[i].get_odb();
      }

      // get function for basic types
      template <typename T>
      T get() {
         if (m_num_values > 1)
            throw std::runtime_error("ODB key \"" + get_full_path() +
                                     "\[0..."+std::to_string(m_num_values-1) +
                                     "]\" contains array. Please assign to std::vector.");
         if (m_auto_refresh)
            pull();
         return (T)m_data[0];
      }

      // get function for basic types as a parameter
      template <typename T>
      void get(T& v) {
         if (m_num_values > 1)
            throw std::runtime_error("ODB key \"" + get_full_path() + "\" contains array. Please assign to std::vector.");
         if (m_auto_refresh)
            pull();
         v = (T)m_data[0];
      }

      // get function for strings
      void get(std::string &s, bool quotes = false, bool refresh=true) {
         if (refresh && m_auto_refresh)
            pull();

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
      template <typename T>
      T operator+(const T i) {
         if (m_num_values > 1)
            throw std::runtime_error("ODB key \"" + get_full_path() +
                                     "\" contains array which cannot be used in basic arithmetic operation.");
         T s;
         get(s);
         return s + i;
      }

      template <typename T>
      T operator-(const T i) {
         if (m_num_values > 1)
            throw std::runtime_error("ODB key \"" + get_full_path() +
                                     "\" contains array which cannot be used in basic arithmetic operation.");
         T s;
         get(s);
         return s - i;
      }

      template <typename T>
      T operator*(const T i) {
         if (m_num_values > 1)
            throw std::runtime_error("ODB key \"" + get_full_path() +
                                     "\" contains array which cannot be used in basic arithmetic operation.");
         T s;
         get(s);
         return s * i;
      }

      template <typename T>
      T operator/(const T i) {
         if (m_num_values > 1)
            throw std::runtime_error("ODB key \"" + get_full_path() +
                                     "\" contains array which cannot be used in basic arithmetic operation.");
         T s;
         get(s);
         return s / i;
      }

      odb& operator++() {
         for (int i=0 ; i<m_num_values ; i++)
            m_data[i].add(1, false);
         push();
         return *this;
      }

      odb& operator++(int) {
         for (int i=0 ; i<m_num_values ; i++)
            m_data[i].add(1, false);
         push();
         return *this;
      }

      odb& operator--() {
         for (int i=0 ; i<m_num_values ; i++)
            m_data[i].add(-1, false);
         push();
         return *this;
      }

      odb& operator--(int) {
         for (int i=0 ; i<m_num_values ; i++)
            m_data[i].add(-1, false);
         push();
         return *this;
      }

      odb& operator+=(double d) {
         for (int i=0 ; i<m_num_values ; i++)
            m_data[i].add(d, false);
         push();
         return *this;
      }

      odb& operator-=(double d) {
         for (int i=0 ; i<m_num_values ; i++)
            m_data[i].add(-d, false);
         push();
         return *this;
      }

      odb& operator*=(double d) {
         for (int i=0 ; i<m_num_values ; i++)
            m_data[i].mult(d, false);
         push();
         return *this;
      }

      odb& operator/=(double d) {
         if (d == 0)
            throw std::runtime_error("Division by zero");
         for (int i=0 ; i<m_num_values ; i++)
            m_data[i].mult(1/d, false);
         push();
         return *this;
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
            throw std::runtime_error("db_get_key for ODB key \"" + path +
                                     "\" failed with status " + std::to_string(status));

         // check for correct type if given as parameter
         if (tid > 0 && tid != key.type)
            throw std::runtime_error("ODB key \"" + get_full_path() +
                                     "\" has differnt type than specified");

         m_tid = key.type;
         m_num_values = key.num_values;
         if (m_debug) {
            std::cout << "Get definition for ODB key \"" + get_full_path() + "\"" << std::endl;
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
            std::cout << "Get definition for ODB key \"" + get_full_path() + "\"" << std::endl;
         }
      }

      void init_mdata() {
         if (m_data == nullptr) {
            if (m_tid == TID_KEY) {
               std::vector<HNDLE> hlist;
               m_num_values = 0;
               for (int i = 0;; i++) {
                  HNDLE h;
                  int status = db_enum_key(m_hDB, m_hKey, i, &h);
                  if (status != DB_SUCCESS)
                     break;
                  hlist.push_back(h);
                  m_num_values = i + 1;
               }
               if (m_num_values > 0) {
                  m_data = new u_odb[m_num_values]{};
                  for (int i = 0; i < m_num_values; i++) {
                     m_data[i].set_parent(this);
                     m_data[i].set(new odb(hlist[i]));
                  }
               }

               return;
            }

            m_data = new u_odb[m_num_values]{};
            for (int i = 0; i < m_num_values; i++)
               m_data[i].set_parent(this);
         }
      }

      // retrieve data or array of data from ODB and assign it to this object
      void pull() {
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
            char *str = (char *) malloc(key.total_size);
            size = key.total_size;
            status = db_get_data(m_hDB, m_hKey, str, &size, m_tid);
            for (int i = 0; i < m_num_values; i++)
               m_data[i].set(str + i * key.item_size);
            free(str);
         } else if (m_tid == TID_KEY) {
            delete[] m_data;
            m_data = nullptr;
            init_mdata();
            status = DB_SUCCESS;
         } else if (m_tid == 0)
            throw std::runtime_error("Invalid midas::odb object");
         else
            throw std::runtime_error("get_data for ODB key \"" + get_full_path() +
                                     "\" failed due to unsupported type " + std::to_string(m_tid));

         if (status != DB_SUCCESS)
            throw std::runtime_error("db_get_data for ODB key \"" + get_full_path() +
                                     "\" failed with status " + std::to_string(status));
         if (m_debug) {
            std::string s;
            get(s, false, false);
            if (m_num_values > 1)
               std::cout << "Get ODB key \"" + get_full_path() + "[0..." +
                            std::to_string(m_num_values - 1) + "]\": [" + s + "]" << std::endl;
            else
               std::cout << "Get ODB key \"" + get_full_path() + "\": " + s << std::endl;

         }
      }

      // push individual member of an array
      void push(int index) {
         if (m_tid == TID_KEY)
            return;
         int status{};
         if (m_tid == TID_STRING) {
            KEY key;
            db_get_key(m_hDB, m_hKey, &key);
            std::string s;
            m_data[0].get(s);
            status = db_set_data_index(m_hDB, m_hKey, s.c_str(), key.item_size, index, m_tid);
            if (m_debug)
               std::cout << "Set ODB key \"" + get_full_path() + "[" + std::to_string(index) + "]\" = " + s << std:: endl;
         } else {
            u_odb u = m_data[index];
            status = db_set_data_index(m_hDB, m_hKey, &u, rpc_tid_size(m_tid), index, m_tid);
            if (m_debug) {
               std::string s;
               u.get(s);
               if (m_num_values > 1)
                  std::cout << "Set ODB key \"" + get_full_path() + "[" + std::to_string(index) + "]\" = " + s << std::endl;
               else
                  std::cout << "Set ODB key \"" + get_full_path() + "\" = " + s << std::endl;
            }
         }
         if (status != DB_SUCCESS)
            throw std::runtime_error("db_set_data for ODB key \"" + get_full_path() +
                                     "\" failed with status " + std::to_string(status));
      }

      // push all members of an array
      void push() {
         if (m_tid == TID_KEY)
            return;

         // if index operator [] returned previously a certain index, push only this one
         if (m_last_index != -1) {
            push(m_last_index);
            m_last_index = -1;
            return;
         }

         if (m_num_values == 1) {
            push(0);
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
               if (m_debug) {
                  std::string s;
                  get(s, false, false);
                  std::cout << "Set ODB key \"" + get_full_path() +
                               "[0..." + std::to_string(m_num_values-1) + "]\" = [" + s +"]" << std::endl;
               }
            } else {
               std::string s;
               m_data[0].get(s);
               status = db_set_data(m_hDB, m_hKey, s.c_str(), s.length() + 1, 1, m_tid);
               if (m_debug)
                  std::cout << "Set ODB key \"" + get_full_path() + "\" = " + s << std:: endl;
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
               get(s, false, false);
               if (m_num_values > 1)
                  std::cout << "Set ODB key \"" + get_full_path() + "[0..." + std::to_string(m_num_values - 1) +
                                                  "]\" = [" + s + "]" << std::endl;
               else
                  std::cout << "Set ODB key \"" + get_full_path() + "\" = " + s << std::endl;
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
   inline u_odb::~u_odb() {
      if (m_parent_odb->get_tid() == TID_STRING)
         delete m_string;
   }

   template <typename T>
   inline T u_odb::get() {
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

   template <typename T>
   inline void u_odb::set(T v) {
      int tid = m_parent_odb->get_tid();
      if (tid == TID_UINT8)
         m_uint8 = v;
      else if (tid == TID_INT8)
         m_int8 = v;
      else if (tid == TID_UINT16)
         m_uint16 = v;
      else if (tid == TID_INT16)
         m_int16 = v;
      else if (tid == TID_UINT32)
         m_uint32 = v;
      else if (tid == TID_INT32)
         m_int32 = v;
      else if (tid == TID_BOOL)
         m_bool = v;
      else if (tid == TID_FLOAT)
         m_float = v;
      else if (tid == TID_DOUBLE)
         m_double = v;
      else if (tid == TID_STRING)
         m_string = new std::string(std::to_string(v));
      else
         throw std::runtime_error("Invalid type ID " + std::to_string(tid));
   }

   inline void u_odb::set(odb* v) {
      if (m_parent_odb->get_tid() != TID_KEY)
         throw std::runtime_error("Subkey can only be assigned to ODB key");
      m_odb = v;
   }

   inline void u_odb::set(std::string v) {
      int tid = m_parent_odb->get_tid();
      if (tid == TID_UINT8)
         m_uint8 = std::stoi(v);
      else if (tid == TID_INT8)
         m_int8 = std::stoi(v);
      else if (tid == TID_UINT16)
         m_uint16 = std::stoi(v);
      else if (tid == TID_INT16)
         m_int16 = std::stoi(v);
      else if (tid == TID_UINT32)
         m_uint32 = std::stoi(v);
      else if (tid == TID_INT32)
         m_int32 = std::stoi(v);
      else if (tid == TID_BOOL)
         m_bool = std::stoi(v);
      else if (tid == TID_FLOAT)
         m_float = std::stof(v);
      else if (tid == TID_DOUBLE)
         m_double = std::stof(v);
      else if (tid == TID_STRING)
         m_string = new std::string(v);
      else
         throw std::runtime_error("Invalid type ID " + std::to_string(tid));
   }

   inline void u_odb::set(const char* v) {
      set(std::string(v));
   }

   inline void u_odb::set(char* v) {
      set(std::string(v));
   }

   // get function for strings
   inline void u_odb::get(std::string &s) {
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
      else if (tid == TID_KEY) {
         s = m_odb->get_name();
      } else
         throw std::runtime_error("Invalid type ID " + std::to_string(tid));
   }

   //---- u_odb assignment and arithmetic operators overloads which call odb::push()
   template <typename T>
   inline T u_odb::operator=(T v) {
      set(v);
      m_parent_odb->push();
      return v;
   }

   void u_odb::add(double inc, bool push) {
      int tid = m_parent_odb->get_tid();
      if (tid == TID_UINT8)
         m_uint8 += inc;
      else if (tid == TID_INT8)
         m_int8 += inc;
      else if (tid == TID_UINT16)
         m_uint16 += inc;
      else if (tid == TID_INT16)
         m_int16 += inc;
      else if (tid == TID_UINT32)
         m_uint32 += inc;
      else if (tid == TID_INT32)
         m_int32 += inc;
      else if (tid == TID_FLOAT)
         m_float += inc;
      else if (tid == TID_DOUBLE)
         m_double += inc;
      else
         throw std::runtime_error("Invalid arithmetic operation for ODB key \"" +
                                  m_parent_odb->get_full_path() + "\"");
      if (push)
         m_parent_odb->push();
   }

   void u_odb::mult(double f, bool push) {
      int tid = m_parent_odb->get_tid();
      if (tid == TID_UINT8)
         m_uint8 *= f;
      else if (tid == TID_INT8)
         m_int8 *= f;
      else if (tid == TID_UINT16)
         m_uint16 *= f;
      else if (tid == TID_INT16)
         m_int16 *= f;
      else if (tid == TID_UINT32)
         m_uint32 *= f;
      else if (tid == TID_INT32)
         m_int32 *= f;
      else if (tid == TID_FLOAT)
         m_float *= f;
      else if (tid == TID_DOUBLE)
         m_double *= f;
      else
         throw std::runtime_error("Invalid operation for ODB key \"" +
                                  m_parent_odb->get_full_path() + "\"");
      if (push)
         m_parent_odb->push();
   }
}

/*------------------------------------------------------------------*/

int main() {

   cm_connect_experiment(NULL, NULL, "test", NULL);
   midas::odb::set_debug(true);

   // test with creating keys
   midas::odb oc("/Test", TID_KEY);
   midas::odb od("/Test/Number", TID_FLOAT32);
   oc.pull();
   std::cout << oc.print() << std::endl;

   // test with int
   midas::odb o2("/Experiment/ODB Timeout");
   o2 = 10000;
   std::cout << o2 << std::endl;
   o2 = o2 * 1.3;
   std::cout << o2 << std::endl;
   o2 = o2 - 1;
   std::cout << o2 << std::endl;
   o2++;
   std::cout << o2 << std::endl;

   // test with bool
   midas::odb o3("/Experiment/Enable core dumps");
   bool b = o3;
   std::cout << o3 << std::endl;
   o3 = true;
   std::cout << o3 << std::endl;
   o3 = b;
   std::cout << o3 << std::endl;

   // test with std::string
   midas::odb o4("/Experiment/Name");
   std::cout << o4 << std::endl;

   std::string s2 = o4;
   s2 = o4;
   std::cout << s2 << std::endl;
   o4 = "MEG";
   o4 = std::string("Online");

   // test with a vector
   midas::odb o5("/Experiment/Test", TID_INT, 10);
   o5[4] = 5555;
   std::vector<int> v = o5;
   v[3] = 33333;
   o5 = v;
   std::cout << o5.print() << std::endl;
   v.resize(5);
   o5 = v;
   v.resize(10);
   o5 = v;
   o5.resize(8);
   o5.resize(10);
   o5[0]++;
   o5[0] += 2.5;
   std::cout << o5.print() << std::endl;
   o5++;
   std::cout << o5.print() << std::endl;
   o5 = 10;
   std::cout << o5.print() << std::endl;
   o5 *= 13;
   std::cout << o5.print() << std::endl;
   o5 /= 13;

   // test with subkeys
   midas::odb o6("/Experiment");
   std::cout << o6["ODB timeout"] << std::endl;
   o6["ODB timeout"] = 12345;
   o6["Security"]["Enable non-localhost RPC"] = true;
   o6["Security/Enable non-localhost RPC"] = false;
   o6["Security/RPC ports/ODBEdit"] = 123;

   // creat key from other key
   midas::odb o7(o6["Security"]);
   std::cout << o7 << std::endl;

   // test auto refresh
   midas::odb o8("/Experiment/ODB Timeout");
   o8.set_auto_refresh(false);
   std::cout << o8 << std::endl;
   o8.pull();
   std::cout << o8 << std::endl;

   // print whole subtree
   std::cout << o6.print() << std::endl;

   cm_disconnect_experiment();
   return 1;
}