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
      void set(char *v) {
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

      // various parameters
      bool         m_preserve_sting_size;

      // type of this object, one of TID_xxx
      int          m_tid;
      // vector containing data for this object
      u_odb*       m_data;

      std::string  m_name;
      int          m_num_values;
      HNDLE        m_hKey;

      void set_type(int t, u_odb u) {
         if (m_tid == 0)
            m_tid = t;
         else if (m_tid != t)
            throw std::runtime_error("Type change of ODB key \"" + get_full_path() + "\" not possible");
         m_data[0] = u;
         send_data_to_odb();
      }

   public:
      // Default constructor
      odb() :
         m_preserve_sting_size(true),
         m_tid{0},
         m_data{nullptr}
         {}

      // Destructor
      ~odb() {
         delete[] m_data;
      }

      // Constructor with ODB path
      odb(std::string path) : odb() {
         if (m_hDB == 0)
            cm_get_experiment_database(&m_hDB, NULL);
         if (path.find_last_of('/') == std::string::npos)
            m_name = path;
         else
            m_name = path.substr(path.find_last_of('/')+1);
         get_hkey(path);
         get_data_from_odb();
      }

      // Constuctor with ODB key
      odb(HNDLE hkey) : odb() {
         get_hkey(hkey);
         get_data_from_odb();
      }

      // Setters and Getters
      bool is_preserve_sting_size() const { return m_preserve_sting_size; }
      void set_preserve_sting_size(bool f) { m_preserve_sting_size = f; }
      int get_tid() { return m_tid; }
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

      // overload the conversion operator for std::string
      operator std::string() {
         std::string s;
         get(s); // forward to get(std::string)
         return s;
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

      // overload index operator
      u_odb& operator[](int index) {
         if (index < 0 || index >= m_num_values)
            throw std::out_of_range("Index \"" + std::to_string(index) + "\" out of range for ODB key \"" +
            get_full_path() + "\"");

         return m_data[index];
      }

      // get function for basic type
      template <typename T>
      T get() {
         return (T)m_data[0];
      }

      // get function for strings
      void get(std::string &s) {
         s = "";
         std::string sd;
         for (int i=0 ; i<m_num_values ; i++) {
            m_data[i].get(sd);
            s += sd;
            if (i < m_num_values-1)
               s += ",";
         }
      }

      void get_hkey(std::string path) {
         int status = db_find_key(m_hDB, 0, path.c_str(), &m_hKey);
         if (status != DB_SUCCESS)
            throw std::runtime_error("ODB key \"" + path + "\" not found");
         KEY key;
         status = db_get_key(m_hDB, m_hKey, &key);
         if (status != DB_SUCCESS)
            throw std::runtime_error("db_get_key for ODB key \"" + path + "\" failed with status " + std::to_string(status));
         m_tid = key.type;
         m_num_values = key.num_values;
      }

      void get_hkey(HNDLE hkey) {
         m_hKey = hkey;
         KEY key;
         int status = db_get_key(m_hDB, m_hKey, &key);
         if (status != DB_SUCCESS)
            throw std::runtime_error("db_get_key for ODB key \"" + get_full_path() + "\" failed with status " + std::to_string(status));
         m_tid = key.type;
         m_num_values = key.num_values;
      }

      void get_data_from_odb() {
         int status = 0;
         int size = 0;
         if (m_data == nullptr) {
            m_data = new u_odb[m_num_values]{};
            for (int i=0 ; i < m_num_values ; i++)
               m_data[i].set_parent(this);
         }

         if (m_tid == TID_UINT8) {
            uint8_t d;
            size = sizeof(d);
            status = db_get_data(m_hDB, m_hKey, &d, &size, m_tid);
            m_data[0].set(d);
         } else if (m_tid == TID_INT8) {
            int8_t d;
            size = sizeof(d);
            status = db_get_data(m_hDB, m_hKey, &d, &size, m_tid);
            m_data[0].set(d);
         } else if (m_tid == TID_UINT16) {
            uint16_t d;
            size = sizeof(d);
            status = db_get_data(m_hDB, m_hKey, &d, &size, m_tid);
            m_data[0].set(d);
         } else if (m_tid == TID_INT16) {
            int16_t d;
            size = sizeof(d);
            status = db_get_data(m_hDB, m_hKey, &d, &size, m_tid);
            m_data[0].set(d);
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
            float d;
            size = sizeof(d);
            status = db_get_data(m_hDB, m_hKey, &d, &size, m_tid);
            m_data[0].set(d);
         } else if (m_tid == TID_DOUBLE) {
            double d;
            size = sizeof(d);
            status = db_get_data(m_hDB, m_hKey, &d, &size, m_tid);
            m_data[0].set(d);
         }  else if (m_tid == TID_STRING) {
            KEY key;
            db_get_key(m_hDB, m_hKey, &key);
            char *str = (char *)malloc(key.total_size);
            size = key.total_size;
            status = db_get_data(m_hDB, m_hKey, str, &size, m_tid);
            m_data[0].set(str);
            free(str);
         }  else if (m_tid == TID_KEY) {
            size = sizeof(HNDLE) * m_num_values;
            HNDLE *d = (int32_t *)malloc(size);
            status = db_get_data(m_hDB, m_hKey, d, &size, m_tid);
            for (int i=0 ; i<m_num_values ; i++)
               m_data[i].set(new odb(d[i]));
            free(d);
         } else
            throw std::runtime_error("get_data for ODB key \"" + get_full_path() +
                                     "\" failed due to unsupported type " + std::to_string(m_tid));

         if (status != DB_SUCCESS)
            throw std::runtime_error("db_get_data for ODB key \"" + get_full_path() +
                                     "\" failed with status " + std::to_string(status));
      }

      void send_data_to_odb() {
         int status{}, size{};
         if (m_tid == TID_UINT8) {
            uint8_t d;
            d = m_data[0];
            status = db_set_data(m_hDB, m_hKey, &d, sizeof(d), 1, m_tid);
         } else if (m_tid == TID_INT8) {
            int8_t d;
            d = m_data[0];
            status = db_set_data(m_hDB, m_hKey, &d, sizeof(d), 1, m_tid);
         } else if (m_tid == TID_UINT16) {
            uint16_t d;
            d = m_data[0];
            status = db_set_data(m_hDB, m_hKey, &d, sizeof(d), 1, m_tid);
         } else if (m_tid == TID_INT16) {
            int16_t d;
            d = m_data[0];
            status = db_set_data(m_hDB, m_hKey, &d, sizeof(d), 1, m_tid);
         } else if (m_tid == TID_UINT32) {
            uint32_t d;
            d = m_data[0];
            status = db_set_data(m_hDB, m_hKey, &d, sizeof(d), 1, m_tid);
         } else if (m_tid == TID_INT32) {
            size = sizeof(int32_t) * m_num_values;
            int32_t *d = (int32_t *)malloc(size);
            for (int i=0 ; i<m_num_values ; i++)
               d[i] = m_data[i];
            status = db_set_data(m_hDB, m_hKey, d, size, m_num_values, m_tid);
            free(d);
         } else if (m_tid == TID_BOOL) {
            size = sizeof(bool) * m_num_values;
            bool *d = (bool *)malloc(size);
            for (int i=0 ; i<m_num_values ; i++)
               d[i] = m_data[i];
            status = db_set_data(m_hDB, m_hKey, d, size, m_num_values, m_tid);
            free(d);
         } else if (m_tid == TID_FLOAT) {
            float d;
            d = m_data[0];
            status = db_set_data(m_hDB, m_hKey, &d, sizeof(d), 1, m_tid);
         } else if (m_tid == TID_DOUBLE) {
            double d;
            d = m_data[0];
            status = db_set_data(m_hDB, m_hKey, &d, sizeof(d), 1, m_tid);
         } else if (m_tid == TID_STRING) {
            std::string d;
            m_data[0].get(d);
            if (is_preserve_sting_size()) {
               KEY key;
               db_get_key(m_hDB, m_hKey, &key);
               status = db_set_data(m_hDB, m_hKey, d.c_str(), key.total_size, 1, m_tid);
            } else
               status = db_set_data(m_hDB, m_hKey, d.c_str(), d.length()+1, 1, m_tid);
         }

         if (status != DB_SUCCESS)
            throw std::runtime_error("db_get_data for ODB key \"" + get_full_path() +
            "\" failed with status " + std::to_string(status));
      }
   };

   //-----------------------------------------------

   HNDLE odb::m_hDB = 0; // initialize static variable

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


   /*
   midas::odb oi("/Experiment/ODB Timeout");
   std::cout << oi << std::endl;
   oi = 99999;


   midas::odb ob("/Experiment/Prevent start on alarms");
   std::cout << ob << std::endl;
   ob = true;


   midas::odb ot("/Experiment/Test");
   ot[0] = 123456;
   ot[9] = 987654;
   std::string s = ot[0];
   int i = ot[0];
   std::cout << i << std::endl;
   */

   midas::odb on("/Experiment/Name");
   std::cout << on << std::endl;
   /*
   std::string s2 = on;
   s2 = on;
   std::cout << s2 << std::endl;
   on = "MEG";
   on = std::string("Test");
   */

   cm_disconnect_experiment();
   return 1;
}