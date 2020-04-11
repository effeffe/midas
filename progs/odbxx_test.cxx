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

struct u_odb {
   union {
      uint8_t      m_uint8;
      int8_t       m_int8;
      uint16_t     m_uint16;
      int16_t      m_int16;
      uint32_t     m_uint32;
      int32_t      m_int32;
      float        m_float;
      double       m_double;
      std::string* m_string;
   };
};

namespace midas {

   class odb {
   private:

      // handle to ODB, same for all instances
      static HNDLE m_hDB;


      // various parameters
      bool         m_preserve_sting_size;

      // type of this object, one of TID_xxx
      int          m_tid;

      // vector containing data for this object
      std::vector<u_odb>  m_data;

      std::string  m_path;
      HNDLE        m_hKey;
      KEY          m_key;

      void set_type(int t, u_odb u) {
         if (m_tid == 0)
            m_tid = t;
         else if (m_tid != t)
            throw std::runtime_error("Type change of ODB key \"" + m_path + "\" not possible");
         set_data();
      }

   public:
      // Default constructor
      odb() :
         m_preserve_sting_size(true),
         m_tid(0)
         {
         }

      // Destructor
      ~odb() {
         if (m_tid == TID_STRING)
            for (auto &s : m_data)
               if (s.m_string != nullptr)
                  delete s.m_string;
      }

      // Constructor with ODB path
      odb(std::string path) : odb() {
         if (m_hDB == 0)
            cm_get_experiment_database(&m_hDB, NULL);
         m_path = path;
         get_key();
         get_data();
      }

      // Setters and Getters
      bool is_preserve_sting_size() const { return m_preserve_sting_size; }
      void set_preserve_sting_size(bool f) { m_preserve_sting_size = f; }

      // Overload the Assignment Operators
      const uint8_t &operator=(const uint8_t &v) {
         u_odb u;
         u.m_uint8 = v;
         set_type(TID_UINT8, u);
         return v;
      }
      const int8_t &operator=(const int8_t &v) {
         u_odb u;
         u.m_int8 = v;
         set_type(TID_INT8, u);
         return v;
      }
      const uint16_t &operator=(const uint16_t &v) {
         u_odb u;
         u.m_uint16 = v;
         set_type(TID_UINT16, u);
         return v;
      }
      const int16_t &operator=(const int16_t &v) {
         u_odb u;
         u.m_int16 = v;
         set_type(TID_INT16, u);
         return v;
      }
      const uint32_t &operator=(const uint32_t &v) {
         u_odb u;
         u.m_uint32 = v;
         set_type(TID_UINT32, u);
         return v;
      }
      const int32_t &operator=(const int32_t &v) {
         u_odb u;
         u.m_int32 = v;
         set_type(TID_INT32, u);
         return v;
      }
      const float &operator=(const float &v) {
         u_odb u;
         u.m_float = v;
         set_type(TID_FLOAT, u);
         return v;
      }
      const double &operator=(const double &v) {
         u_odb u;
         u.m_double = v;
         set_type(TID_DOUBLE, u);
         return v;
      }
      const std::string &operator=(const std::string &v) {
         u_odb u;
         u.m_string = new std::string(v);
         set_type(TID_STRING, u);
         return v;
      }

      // overload the conversion operator for std::string
      operator std::string() {
         std::string s;
         get(s); // forward to get(std::string)
         return s;
      }

      // overload all other conversion operators
      template <typename T>
      operator T() {
         return get<T>(); // forward to get<T>()
      }

      // overload stream out operator
      friend std::ostream &operator<<(std::ostream &output, odb &o) {
         std::string s = o;
         output << s;
         return output;
      };

      // get function for basic type
      template <typename T>
      T get() {
         if (m_tid == TID_UINT8)
            return (T)m_data[0].m_uint8;
         else if (m_tid == TID_INT8)
            return (T)m_data[0].m_int8;
         else if (m_tid == TID_UINT16)
            return (T)m_data[0].m_uint16;
         else if (m_tid == TID_INT16)
            return (T)m_data[0].m_int16;
         else if (m_tid == TID_UINT32)
            return (T) m_data[0].m_uint32;
         else if (m_tid == TID_INT32)
            return (T)m_data[0].m_int32;
         else if (m_tid == TID_FLOAT)
            return (T)m_data[0].m_float;
         else if (m_tid == TID_DOUBLE)
            return (T)m_data[0].m_double;
         else
            throw std::runtime_error("Invalid type ID %s" + std::to_string(m_tid));
      }

      // get function for strings
      void get(std::string &s) {
         if (m_tid == TID_UINT8)
            s = std::to_string(m_data[0].m_uint8);
         else if (m_tid == TID_INT8)
            s = std::to_string(m_data[0].m_int8);
         else if (m_tid == TID_UINT16)
            s = std::to_string(m_data[0].m_uint16);
         else if (m_tid == TID_INT16)
            s = std::to_string(m_data[0].m_int16);
         else if (m_tid == TID_UINT32)
            s = std::to_string(m_data[0].m_uint32);
         else if (m_tid == TID_INT32)
            s = std::to_string(m_data[0].m_int32);
         else if (m_tid == TID_FLOAT)
            s = std::to_string(m_data[0].m_float);
         else if (m_tid == TID_DOUBLE)
            s = std::to_string(m_data[0].m_double);
         else if (m_tid == TID_STRING)
            s = *m_data[0].m_string;
         else
            throw std::runtime_error("Invalid type ID %s" + std::to_string(m_tid));
      }

      void get_key() {
         int status = db_find_key(m_hDB, 0, m_path.c_str(), &m_hKey);
         if (status != DB_SUCCESS)
            throw std::runtime_error("ODB key \"" + m_path + "\" not found");
         status = db_get_key(m_hDB, m_hKey, &m_key);
         if (status != DB_SUCCESS)
            throw std::runtime_error("db_get_key for ODB key \"" + m_path + "\" failed with status " + std::to_string(status));
         m_tid = m_key.type;
      }

      void get_data() {
         int status = 0;
         int size = 0;
         if (m_data.empty()) {
            u_odb u;
            u.m_string = nullptr;
            m_data.push_back(u);
         }
         if (m_tid == TID_UINT8) {
            size = sizeof(uint8_t);
            status = db_get_data(m_hDB, m_hKey, &m_data[0].m_uint8, &size, m_tid);
         } else if (m_tid == TID_INT8) {
            size = sizeof(int8_t);
            status = db_get_data(m_hDB, m_hKey, &m_data[0].m_int8, &size, m_tid);
         } else if (m_tid == TID_UINT16) {
            size = sizeof(uint16_t);
            status = db_get_data(m_hDB, m_hKey, &m_data[0].m_uint16, &size, m_tid);
         } else if (m_tid == TID_INT16) {
            size = sizeof(int16_t);
            status = db_get_data(m_hDB, m_hKey, &m_data[0].m_int16, &size, m_tid);
         } else if (m_tid == TID_UINT32) {
            size = sizeof(uint32_t);
            status = db_get_data(m_hDB, m_hKey, &m_data[0].m_uint32, &size, m_tid);
         } else if (m_tid == TID_INT32) {
            size = sizeof(int32_t);
            status = db_get_data(m_hDB, m_hKey, &m_data[0].m_int32, &size, m_tid);
         } else if (m_tid == TID_FLOAT) {
            size = sizeof(float);
            status = db_get_data(m_hDB, m_hKey, &m_data[0].m_float, &size, m_tid);
         } else if (m_tid == TID_DOUBLE) {
            size = sizeof(double);
            status = db_get_data(m_hDB, m_hKey, &m_data[0].m_double, &size, m_tid);
         }  else if (m_tid == TID_STRING) {
            char *str = (char *)malloc(m_key.total_size);
            size = m_key.total_size;
            status = db_get_data(m_hDB, m_hKey, str, &size, m_tid);
            if (m_data[0].m_string != nullptr)
               delete m_data[0].m_string;
            m_data[0].m_string = new std::string(str);
            free(str);
         } else
            throw std::runtime_error("get_data for ODB key \"" + m_path +
            "\" failed due to unsupported type " + std::to_string(m_tid));

         if (status != DB_SUCCESS)
            throw std::runtime_error("db_get_data for ODB key \"" + m_path +
            "\" failed with status " + std::to_string(status));
      }

      void set_data() {
         int status = 0;
         if (m_tid == TID_UINT8)
            status = db_set_data(m_hDB, m_hKey, &m_data[0].m_uint8, sizeof(uint8_t), 1, m_tid);
         else if (m_tid == TID_INT8)
            status = db_set_data(m_hDB, m_hKey, &m_data[0].m_int8, sizeof(int8_t), 1, m_tid);
         else if (m_tid == TID_UINT16)
            status = db_set_data(m_hDB, m_hKey, &m_data[0].m_uint16, sizeof(uint16_t), 1, m_tid);
         else if (m_tid == TID_INT16)
            status = db_set_data(m_hDB, m_hKey, &m_data[0].m_int16, sizeof(uint16_t), 1, m_tid);
         else if (m_tid == TID_UINT32)
            status = db_set_data(m_hDB, m_hKey, &m_data[0].m_uint32, sizeof(uint32_t), 1, m_tid);
         else if (m_tid == TID_INT32)
            status = db_set_data(m_hDB, m_hKey, &m_data[0].m_int32, sizeof(int32_t), 1, m_tid);
         else if (m_tid == TID_FLOAT)
            status = db_set_data(m_hDB, m_hKey, &m_data[0].m_float, sizeof(float), 1, m_tid);
         else if (m_tid == TID_DOUBLE)
            status = db_set_data(m_hDB, m_hKey, &m_data[0].m_double, sizeof(double),1,  m_tid);
         else if (m_tid == TID_STRING) {
            if (is_preserve_sting_size())
               status = db_set_data(m_hDB, m_hKey, m_data[0].m_string->c_str(), m_key.total_size, 1, m_tid);
            else
               status = db_set_data(m_hDB, m_hKey, m_data[0].m_string->c_str(), m_data[0].m_string->length()+1, 1, m_tid);
         }

         if (status != DB_SUCCESS)
            throw std::runtime_error("db_get_data for ODB key \"" + m_path + "\" failed with status " + std::to_string(status));
      }
   };

   HNDLE odb::m_hDB = 0; // initialize static variable
}

/*------------------------------------------------------------------*/

int main() {

   cm_connect_experiment(NULL, NULL, "test", NULL);

   midas::odb ot("/Experiment/ODB Timeout");
   std::cout << ot << std::endl;
   ot = 123456;

   midas::odb o("/Experiment/Name");
   std::string s = o;
   std::cout << s << std::endl;

   o = "MEG";

   cm_disconnect_experiment();
   return 1;
}