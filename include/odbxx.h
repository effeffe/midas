/********************************************************************\

  Name:         odbxx.cxx
  Created by:   Stefan Ritt

  Contents:     Object oriented interface to ODB

  This file provides a very object-oriented approach to interacting with the
  midas ODB. You can think of it like a "magic" map/dictionary that
  automatically sends changes you make to the ODB, and receives updates that
  others have made.

  Documentation is at https://midas.triumf.ca/MidasWiki/index.php/Odbxx

\********************************************************************/

#ifndef _ODBXX_HXX
#define _ODBXX_HXX

#include <string>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <initializer_list>
#include <cstring>
#include <bitset>
#include <functional>

#include "midas.h"
#include "mexcept.h"
// #include "mleak.h" // un-comment for memory leak debugging


/*------------------------------------------------------------------*/

namespace midas {

   class odb;

   //================================================================
   // u_odb class is a union to hold one ODB value, either a basic
   // type, a std::string or a pointer to an odb object
   //================================================================

   class u_odb {
   private:
      // union to hold data
      union {
         uint8_t m_uint8;
         int8_t m_int8;
         uint16_t m_uint16;
         int16_t m_int16;
         uint32_t m_uint32;
         int32_t m_int32;
         bool m_bool;
         float m_float;
         double m_double;
         std::string *m_string;
         odb *m_odb;
      };

      int m_tid;
      odb *m_parent_odb;

   public:
      u_odb() : m_string{}, m_tid{}, m_parent_odb{nullptr} {};

      // for shorter values, first set m_string to nullptr to initialize higher bytes to zero
      u_odb(uint8_t v) : m_tid{TID_UINT8}, m_parent_odb{nullptr} {m_string = nullptr; m_uint8 = v;};

      u_odb(int8_t v) : m_tid{TID_INT8}, m_parent_odb{nullptr} {m_string = nullptr; m_int8 = v;};

      u_odb(uint16_t v) : m_tid{TID_UINT16}, m_parent_odb{nullptr} {m_string = nullptr; m_uint16 = v;};

      u_odb(int16_t v) : m_tid{TID_INT16}, m_parent_odb{nullptr} {m_string = nullptr; m_int16 = v;};

      u_odb(uint32_t v) : m_tid{TID_UINT32}, m_parent_odb{nullptr} {m_string = nullptr; m_uint32 = v;};

      u_odb(int32_t v) : m_tid{TID_INT32}, m_parent_odb{nullptr} {m_string = nullptr; m_int32 = v;};

      u_odb(bool v) : m_tid{TID_BOOL}, m_parent_odb{nullptr} {m_string = nullptr; m_bool = v;};

      u_odb(float v) : m_float{v}, m_tid{TID_FLOAT}, m_parent_odb{nullptr} {m_string = nullptr; m_float = v;};

      u_odb(double v) : m_double{v}, m_tid{TID_DOUBLE}, m_parent_odb{nullptr} {m_string = nullptr; m_double = v;};

      u_odb(std::string *v) : m_string{v}, m_tid{TID_STRING}, m_parent_odb{nullptr} {};
      
      // Destructor
      ~u_odb();

      // Setters and getters
      void set_parent(odb *o) { m_parent_odb = o; }
      odb *get_parent() { return m_parent_odb; }

      void set_tid(int tid) { m_tid = tid; }

      int get_tid() { return m_tid; }

      // Overload the Assignment Operators
      uint8_t operator=(uint8_t v);
      int8_t operator=(int8_t v);
      uint16_t operator=(uint16_t v);
      int16_t operator=(int16_t v);
      uint32_t operator=(uint32_t v);
      int32_t operator=(int32_t v);
      bool operator=(bool v);
      float operator=(float v);
      double operator=(double v);
      const char *operator=(const char *v);
      std::string *operator=(std::string * v);
      std::string operator=(std::string v);

      // Overload the Conversion Operators
      operator uint8_t();
      operator int8_t();
      operator uint16_t();
      operator int16_t();
      operator uint32_t();
      operator int32_t();
      operator bool();
      operator float();
      operator double();
      operator std::string();
      operator const char *();
      operator midas::odb &();

      template<typename T>
      void set(T v) {
         if (m_tid == TID_UINT8)
            m_uint8 = v;
         else if (m_tid == TID_INT8)
            m_int8 = v;
         else if (m_tid == TID_UINT16)
            m_uint16 = v;
         else if (m_tid == TID_INT16)
            m_int16 = v;
         else if (m_tid == TID_UINT32)
            m_uint32 = v;
         else if (m_tid == TID_INT32)
            m_int32 = v;
         else if (m_tid == TID_BOOL)
            m_bool = v;
         else if (m_tid == TID_FLOAT)
            m_float = v;
         else if (m_tid == TID_DOUBLE)
            m_double = v;
         else if (m_tid == TID_STRING) {
            delete m_string;
            m_string = new std::string(std::to_string(v));
         } else
            mthrow("Invalid type ID " + std::to_string(m_tid));
      }

      void set_string(std::string s) {
         delete m_string;
         m_string = new std::string(s);
      }

      void set_string_size(std::string s, int size);

      void set_string_ptr(std::string *s) {
         m_string = s;
      }

      void set(odb *v) {
         if (m_tid != TID_KEY)
            mthrow("Subkey can only be assigned to ODB key");
         m_odb = v;
      }

      void set_odb(odb *v) {
         if (m_tid != TID_KEY)
            mthrow("Subkey can only be assigned to ODB key");
         m_odb = v;
      }

      void set(std::string v) {
         if (m_tid == TID_UINT8)
            m_uint8 = std::stoi(v);
         else if (m_tid == TID_INT8)
            m_int8 = std::stoi(v);
         else if (m_tid == TID_UINT16)
            m_uint16 = std::stoi(v);
         else if (m_tid == TID_INT16)
            m_int16 = std::stoi(v);
         else if (m_tid == TID_UINT32)
            m_uint32 = std::stoi(v);
         else if (m_tid == TID_INT32)
            m_int32 = std::stoi(v);
         else if (m_tid == TID_BOOL)
            m_bool = std::stoi(v);
         else if (m_tid == TID_FLOAT)
            m_float = std::stof(v);
         else if (m_tid == TID_DOUBLE)
            m_double = std::stof(v);
         else if (m_tid == TID_STRING) {
            delete m_string;
            m_string = new std::string(v);
         } else if (m_tid == TID_LINK) {
            delete m_string;
            m_string = new std::string(v);
         } else
            mthrow("Invalid type ID " + std::to_string(m_tid));
      }

      void set(const char *v) {
         set(std::string(v));
      }

      void set(char *v) {
         set(std::string(v));
      }


      void add(double inc, bool push = true);

      void mult(double f, bool push = true);

      // overlaod arithemetic operators
      u_odb &operator++(int) {
         add(1);
         return *this;
      }

      u_odb &operator++() {
         add(1);
         return *this;
      }

      u_odb &operator--(int) {
         add(-1);
         return *this;
      }

      u_odb &operator--() {
         add(-1);
         return *this;
      }

      u_odb &operator+=(double d) {
         add(d);
         return *this;
      }

      u_odb &operator-=(double d) {
         add(-d);
         return *this;
      }

      u_odb &operator*=(double d) {
         mult(d);
         return *this;
      }

      u_odb &operator/=(double d) {
         if (d == 0)
            mthrow("Division by zero");
         mult(1 / d);
         return *this;
      }

      template<typename T>
      u_odb &operator+(T v) {
         double d = *this;
         d += v;
         set(v);
         return *this;
      }

      template<typename T>
      u_odb &operator-(T v) {
         double d = *this;
         d -= v;
         set(v);
         return *this;
      }

      template<typename T>
      u_odb &operator*(T v) {
         double d = *this;
         d *= v;
         set(v);
         return *this;
      }

      template<typename T>
      u_odb &operator/(T v) {
         double d = *this;
         d /= v;
         set(v);
         return *this;
      }

      // get function for basic type
      template<typename T>
      T get() {
         if (m_tid == TID_UINT8)
            return (T) m_uint8;
         else if (m_tid == TID_INT8)
            return (T) m_int8;
         else if (m_tid == TID_UINT16)
            return (T) m_uint16;
         else if (m_tid == TID_INT16)
            return (T) m_int16;
         else if (m_tid == TID_UINT32)
            return (T) m_uint32;
         else if (m_tid == TID_INT32)
            return (T) m_int32;
         else if (m_tid == TID_BOOL)
            return (T) m_bool;
         else if (m_tid == TID_FLOAT)
            return (T) m_float;
         else if (m_tid == TID_DOUBLE)
            return (T) m_double;
         else
            mthrow("Invalid type ID %s" + std::to_string(m_tid));
      }

      // get function for string
      std::string get() {
         std::string s;
         get(s);
         return s;
      }

      // get function for strings
      void get(std::string &s);

      // get_function for keys
      odb *get_podb() {
         if (m_tid != TID_KEY)
            mthrow("odb_get() called for non-key object");
         return m_odb;
      }

      odb &get_odb() {
         if (m_tid != TID_KEY)
            mthrow("odb_get() called for non-key object");
         return *m_odb;
      }

      // overload stream out operator
      friend std::ostream &operator<<(std::ostream &output, u_odb &o) {
         std::string s = o;
         output << s;
         return output;
      };

   };

   //-----------------------------------------------

   // bit in odb::m_flags
   enum odb_flags {
      AUTO_REFRESH_READ = 0,
      AUTO_REFRESH_WRITE,
      PRESERVE_STRING_SIZE,
      AUTO_CREATE,
      AUTO_ENLARGE_ARRAY,
      DIRTY,
      DELETED
   };

   //================================================================
   // the odb object holds an ODB entry with name, type,
   // hKey and array of u_odb values
   //================================================================

   class odb {
   public:
      class iterator {
      public:
         iterator(u_odb *pu) : pu_odb(pu) {}

         // Pre-increment
         iterator operator++() {
            ++pu_odb;
            return *this;
         }

         // Post-increment
         iterator operator++(int) {
            iterator ret = *this;
            this->operator++();
            return ret;
         }

         bool operator!=(const iterator &other) const { return pu_odb != other.pu_odb; }

         u_odb &operator*() { return *pu_odb; }

      private:
         u_odb *pu_odb;
      };

   private:

      // handle to ODB, same for all instances
      static HNDLE m_hDB;
      // global debug flag for all instances
      static bool m_debug;
      // global flag indicating that we are connected to the ODB
      static bool m_connected_odb;
      // global list of ODB keys used by odb::watch
      static std::vector<midas::odb> m_watch;

      // various parameters defined in odb_flags
      std::bitset<8> m_flags;
      // type of this object, one of TID_xxx
      int m_tid;
      // vector containing data for this object
      u_odb *m_data;
      // name of ODB entry
      std::string m_name;
      // number of values of ODB entry
      int m_num_values;
      // last index accessed, needed for o[i] = x
      int m_last_index;
      // ODB handle for this key
      HNDLE m_hKey;
      // callback for watch funciton
      std::function<void(midas::odb &)> m_watch_callback;
      // parent ODB key
      midas::odb *m_parent;

      //-------------------------------------------------------------

      // static functions
      static void init_hdb();
      static midas::odb *search_hkey(midas::odb *po, int hKey);
      static void watch_callback(int hDB, int hKey, int index, void *info);
      static void unwatch_all();

      //-------------------------------------------------------------

      void set_flags_recursively(uint32_t f);
      void resize_mdata(int size);

      // get function for basic types
      template<typename T>
      T get() {
         if (m_num_values > 1)
            mthrow("ODB key \"" + get_full_path() +
                   "[0..." + std::to_string(m_num_values - 1) +
                   "]\" contains array. Please assign to std::vector.");
         if (is_auto_refresh_read())
            read();
         return (T) m_data[0];
      }

      // get function for basic types as a parameter
      template<typename T>
      void get(T &v) {
         if (m_num_values > 1)
            mthrow("ODB key \"" + get_full_path() + "\" contains array. Please assign to std::vector.");
         if (is_auto_refresh_read())
            read();
         v = (T) m_data[0];
      }

      // get function for strings
      void get(std::string &s, bool quotes = false, bool refresh = true);

      // return internal data
      u_odb &get_mdata(int index = 0) { return m_data[index]; }

      odb &get_subkey(std::string str);
      int get_subkeys(std::vector<std::string> &name);
      bool read_key(std::string &path);
      bool write_key(std::string &path, bool write_defaults);

      void set_hkey(HNDLE hKey) { m_hKey = hKey; }

      void set_flags(uint32_t f) { m_flags = f; }
      uint32_t get_flags() { return static_cast<uint32_t>(m_flags.to_ulong()); }

      bool is_deleted() const { return m_flags[odb_flags::DELETED]; }
      void set_deleted(bool f) { m_flags[odb_flags::DELETED] = f; }

      void set_tid(int tid) { m_tid = tid; }
      void set_num_values(int n) { m_num_values = n; }

      void set_name(std::string s) { m_name = s; }

      void set_parent(midas::odb *p) { m_parent = p; }
      midas::odb *get_parent() { return m_parent; }

   public:

      // Default constructor
      odb() :
              m_flags{(1 << odb_flags::AUTO_REFRESH_READ) |
                      (1 << odb_flags::AUTO_REFRESH_WRITE) |
                      (1 << odb_flags::AUTO_ENLARGE_ARRAY) |
                      (1 << odb_flags::AUTO_CREATE)},
              m_tid{0},
              m_data{nullptr},
              m_name{},
              m_num_values{0},
              m_last_index{-1},
              m_hKey{},
              m_parent{} {}

      // Destructor
      ~odb() {
         delete[] m_data;
      }

      // Deep copy constructor
      odb(const odb &o);

      // Delete shallow assignment operator
      odb operator=(odb &&o) = delete;

      // Constructor for single basic types
      template<typename T>
      odb(T v):odb() {
         m_num_values = 1;
         m_data = new u_odb[1]{v};
         m_tid = m_data[0].get_tid();
         m_data[0].set_parent(this);
      }

      // Constructor with std::initializer_list
      odb(std::initializer_list<std::pair<const char *, midas::odb>> list) : odb() {
         m_tid = TID_KEY;
         m_num_values = list.size();
         m_data = new u_odb[m_num_values];
         int i = 0;
         for (auto &element: list) {
            // check if name exists already
            for (int j=0 ; j<i ; j++) {
               if (strcasecmp(element.first, m_data[j].get_odb().get_name().c_str()) == 0) {
                  if (element.first == m_data[j].get_odb().get_name().c_str()) {
                     mthrow("ODB key with name \"" + m_data[j].get_odb().get_name() + "\" exists already");
                  } else {
                     mthrow("ODB key \"" + std::string(element.first) + "\" exists already as \"" +
                            m_data[j].get_odb().get_name() + "\" (only case differs)");
                  }
               }
            }
            auto o = new midas::odb(element.second);
            o->set_name(element.first);
            o->set_parent(this);
            m_data[i].set_tid(TID_KEY);
            m_data[i].set_parent(this);
            m_data[i].set(o);
            i++;
         }
      }

      // Constructor with basic type array
      template<typename T>
      odb(std::initializer_list<T> list) : odb() {
         m_num_values = list.size();
         m_data = new u_odb[m_num_values]{};
         int i = 0;
         for (auto &element : list) {
            u_odb u(element);
            m_data[i].set_tid(u.get_tid());
            m_data[i].set_parent(this);
            m_data[i].set(element);
            i++;
         }
         m_tid = m_data[0].get_tid();
      }

      // Constructor with basic explicit array
      template<typename T, size_t SIZE>
      odb(const std::array<T, SIZE> &arr) : odb() {
         m_num_values = SIZE;
         m_data = new u_odb[m_num_values]{};
         for (int i = 0; i < (int)SIZE; i++) {
            u_odb u(arr[i]);
            m_data[i].set_tid(u.get_tid());
            m_data[i].set_parent(this);
            m_data[i].set(arr[i]);
         }
         m_tid = m_data[0].get_tid();
      }

      // Constructor with explicit array of std::string
      template<size_t SIZE>
      odb(const std::array<std::string, SIZE> &arr) : odb() {
         m_num_values = SIZE;
         m_data = new u_odb[m_num_values]{};
         for (int i = 0; i < (int)SIZE; i++) {
            std::string * mystring = new std::string(arr[i]);
            u_odb u(mystring);
            m_data[i].set_tid(u.get_tid());
            m_data[i].set_parent(this);
            m_data[i].set(arr[i]);
         }
         m_tid = m_data[0].get_tid();
      }

      // Constructor for std::string
      odb(const std::string &str) : odb() {
         odb_from_string(str);
      }

      // Constructor for C strings
      odb(const char *v) : odb() {
         odb_from_string(std::string(v));
      }

      // Constructor with const char * array
      odb(std::initializer_list<const char *> list) : odb() {
         m_num_values = list.size();
         m_data = new u_odb[m_num_values]{};
         int i = 0;
         for (auto &element : list) {
            m_data[i].set_tid(TID_STRING);
            m_data[i].set_parent(this);
            m_data[i].set(element);
            i++;
         }
         m_tid = m_data[0].get_tid();
      }

      template<typename T>
      int detect_type(const T &v) {
         if (std::is_same<T, uint8_t>::value)
            return TID_UINT8;
         else if (std::is_same<T, int8_t>::value)
            return TID_INT8;
         else if (std::is_same<T, uint16_t>::value)
            return TID_UINT16;
         else if (std::is_same<T, int16_t>::value)
            return TID_INT16;
         else if (std::is_same<T, uint32_t>::value)
            return TID_UINT32;
         else if (std::is_same<T, int32_t>::value)
            return TID_INT32;
         else if (std::is_same<T, bool>::value)
            return TID_BOOL;
         else if (std::is_same<T, float>::value)
            return TID_FLOAT;
         else if (std::is_same<T, double>::value)
            return TID_DOUBLE;
         else
            return TID_STRING;
      }

      // Overload the Assignment Operators
      template<typename T>
      const T &operator=(const T &v) {
         if (m_num_values == 0) {
            // initialize this
            m_num_values = 1;
            m_tid = detect_type(v);
            m_data = new u_odb[1]{};
            m_data[0].set_tid(m_tid);
            m_data[0].set_parent(this);
            m_data[0].set(v);
            if (this->is_auto_refresh_write())
               write();
         } else {
            for (int i = 0; i < m_num_values; i++)
               m_data[i].set(v);

            if (this->is_auto_refresh_write())
               write();
         }
         return v;
      }

      // Overload the Assignment Operators for std::vector
      template<typename T>
      const std::vector<T> &operator=(const std::vector<T> &v) {

         if (m_num_values == 0) {
            // initialize this
            m_num_values = v.size();
            m_tid = detect_type(v[0]);
            m_data = new u_odb[m_num_values]{};
            for (int i = 0; i < m_num_values; i++) {
               m_data[i].set_tid(m_tid);
               m_data[i].set_parent(this);
            }

         } else {

            // resize internal array if different
            if ((int)v.size() != m_num_values) {
               resize_mdata(v.size());
            }
         }

         for (int i = 0; i < m_num_values; i++)
            m_data[i].set(v[i]);

         if (this->is_auto_refresh_write())
            write();

         return v;
      }

      // Overload the Assignment Operators for std::array
      template<typename T, size_t SIZE>
      const std::array<T, SIZE> &operator=(const std::array<T, SIZE> &arr) {

         if (m_num_values == 0) {
            // initialize this
            m_num_values = SIZE;
            m_tid = detect_type(arr[0]);
            m_data = new u_odb[m_num_values]{};
            for (int i = 0; i < m_num_values; i++) {
               m_data[i].set_tid(m_tid);
               m_data[i].set_parent(this);
            }

         } else {

            // resize internal array if different
            if (SIZE != m_num_values) {
               resize_mdata(SIZE);
            }
         }

         for (int i = 0; i < m_num_values; i++)
            m_data[i].set(arr[i]);

         if (this->is_auto_refresh_write())
            write();
         return arr;
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
      template<typename T>
      operator std::vector<T>() {
         if (is_auto_refresh_read())
            read();
         std::vector<T> v(m_num_values);
         for (int i = 0; i < m_num_values; i++)
            v[i] = m_data[i];
         return v;
      }

      operator std::vector<std::string>() {
         if (is_auto_refresh_read())
            read();
         std::vector<std::string> v(m_num_values);
         for (int i = 0; i < m_num_values; i++)
            v[i] = m_data[i].get();
         return v;
      }

      // overload all other conversion operators
      template<typename T, typename std::enable_if<
              std::is_same<T, uint8_t>::value ||
              std::is_same<T, int8_t>::value ||
              std::is_same<T, uint16_t>::value ||
              std::is_same<T, int16_t>::value ||
              std::is_same<T, uint32_t>::value ||
              std::is_same<T, int32_t>::value ||
              std::is_same<T, bool>::value ||
              std::is_same<T, float>::value ||
              std::is_same<T, double>::value, T>::type * = nullptr>
      operator T() {
         if (m_tid == 0)
            mthrow("Cannot return un-initialized object \"" + m_name + "\"");
         return get<T>(); // forward to get<T>()
      }

      // overload stream out operator
      friend std::ostream &operator<<(std::ostream &output, odb &o) {
         std::string s;
         if (o.m_tid == TID_KEY)
            o.print(s, 0);
         else
            o.get(s);
         output << s;
         return output;
      };

      // overload index operator for arrays
      u_odb &operator[](int index) {
         if (index < 0)
            throw std::out_of_range("Index \"" + std::to_string(index) + "\" out of range for ODB key \"" + get_full_path() + "[0..." + std::to_string(m_num_values - 1) + "]\"");

         if (index == 0 && m_num_values == 0) {
            // initialize this
            m_num_values = 1;
            m_tid = 0;
            m_data = new u_odb[1]{};
            m_data[0].set_tid(m_tid);
            m_data[0].set_parent(this);
            m_last_index = 0;
            return m_data[0];
         } else if (index >= m_num_values) {
            if (is_auto_enlarge_array()) {
               resize_mdata(index+1);
               if (this->is_auto_refresh_write())
                  write(index, 0);
            } else {
               throw std::out_of_range("Index \"" + std::to_string(index) + "\" out of range for ODB key \"" + get_full_path() + "[0..." + std::to_string(m_num_values - 1) + "]\", please consider set_auto_enlarge_array(true)");
            }
         }

         if (is_auto_refresh_read())
            read(index);

         m_last_index = index;
         return m_data[index];
      }

      // overload index operator for subkeys
      odb &operator[](std::string &str) {
         return get_subkey(str);
      }

      odb &operator[](const char *str) {
         return get_subkey(std::string(str));
      }

      // overload the call operator
      template <typename T>
      odb& operator()(T v) {
         if (m_tid == 0) {
            if (m_num_values == 0) {
               // initialize this
               m_num_values = 1;
               m_tid = detect_type(v);
               m_data = new u_odb[1]{};
               m_data[0].set_tid(m_tid);
               m_data[0].set_parent(this);
               m_data[0].set(v);
               if (this->is_auto_refresh_write())
                  write();
            } else {
               for (int i = 0; i < m_num_values; i++)
                  m_data[i].set(v);
               if (this->is_auto_refresh_write())
                  write();
            }
         }
         return *this;
      }

      // indexed access, internal use only
      int get_last_index() { return m_last_index; }
      void set_last_index(int i) { m_last_index = i; }

      // iterator support
      iterator begin() const { return iterator(m_data); }
      iterator end() const { return iterator(m_data + m_num_values); }

      // overload arithmetic operators
      template<typename T>
      T operator+(T i) {
         if (m_num_values > 1)
            mthrow("ODB key \"" + get_full_path() +
                   "\" contains array which cannot be used in basic arithmetic operation.");
         if (std::is_same<T, midas::odb>::value) {
            if (is_auto_refresh_read()) {
               read();
               i.read();
            }
            // adding two midas::odb objects is best done in double
            double s1 = static_cast<double>(m_data[0]);
            double s2 = static_cast<double>(i.m_data[0]);
            return s1 + s2;
         } else {
            if (is_auto_refresh_read())
               read();
            T s = (T) m_data[0];
            return s + i;
         }
      }

      template<typename T>
      T operator-(T i) {
         if (m_num_values > 1)
            mthrow("ODB key \"" + get_full_path() +
                   "\" contains array which cannot be used in basic arithmetic operation.");
         if (std::is_same<T, midas::odb>::value) {
            if (is_auto_refresh_read()) {
               read();
               i.read();
            }
            // subtracting two midas::odb objects is best done in double
            double s1 = static_cast<double>(m_data[0]);
            double s2 = static_cast<double>(i.m_data[0]);
            return s1 - s2;
         } else {
            if (is_auto_refresh_read())
               read();
            T s = (T) m_data[0];
            return s - i;
         }
      }

      template<typename T>
      T operator*(const T i) {
         if (m_num_values > 1)
            mthrow("ODB key \"" + get_full_path() +
                   "\" contains array which cannot be used in basic arithmetic operation.");
         if (is_auto_refresh_read())
            read();
         T s = (T) m_data[0];
         return s * i;
      }

      template<typename T>
      T operator/(const T i) {
         if (m_num_values > 1)
            mthrow("ODB key \"" + get_full_path() +
                   "\" contains array which cannot be used in basic arithmetic operation.");
         if (is_auto_refresh_read())
            read();
         T s = (T) m_data[0];
         return s / i;
      }

      odb &operator++() {
         if (is_auto_refresh_read())
            read();
         for (int i = 0; i < m_num_values; i++)
            m_data[i].add(1, false);
         if (this->is_auto_refresh_write())
            write();
         return *this;
      }

      odb operator++(int) {
         // create temporary object
         odb o(this);
         if (is_auto_refresh_read())
            read();
         for (int i = 0; i < m_num_values; i++)
            m_data[i].add(1, false);
         if (this->is_auto_refresh_write())
            write();
         return o;
      }

      odb &operator--() {
         if (is_auto_refresh_read())
            read();
         for (int i = 0; i < m_num_values; i++)
            m_data[i].add(-1, false);
         if (this->is_auto_refresh_write())
            write();
         return *this;
      }

      odb operator--(int) {
         // create temporary object
         odb o(this);
         if (is_auto_refresh_read())
            read();
         for (int i = 0; i < m_num_values; i++)
            m_data[i].add(-1, false);
         if (this->is_auto_refresh_write())
            write();
         return o;
      }

      odb &operator+=(double d) {
         if (is_auto_refresh_read())
            read();
         for (int i = 0; i < m_num_values; i++)
            m_data[i].add(d, false);
         if (this->is_auto_refresh_write())
            write();
         return *this;
      }

      odb &operator-=(double d) {
         if (is_auto_refresh_read())
            read();
         for (int i = 0; i < m_num_values; i++)
            m_data[i].add(-d, false);
         if (this->is_auto_refresh_write())
            write();
         return *this;
      }

      odb &operator*=(double d) {
         if (is_auto_refresh_read())
            read();
         for (int i = 0; i < m_num_values; i++)
            m_data[i].mult(d, false);
         if (this->is_auto_refresh_write())
            write();
         return *this;
      }

      odb &operator/=(double d) {
         if (is_auto_refresh_read())
            read();
         if (d == 0)
            mthrow("Division by zero");
         for (int i = 0; i < m_num_values; i++)
            m_data[i].mult(1 / d, false);
         if (this->is_auto_refresh_write())
            write();
         return *this;
      }

      // overload comparison operators
      template<typename T>
      friend bool operator==(const midas::odb &o, const T &d);
      template<typename T>
      friend bool operator==(const T &d, const midas::odb &o);
      template<typename T>
      friend bool operator!=(const midas::odb &o, const T &d);
      template<typename T>
      friend bool operator!=(const T &d, const midas::odb &o);
      template<typename T>
      friend bool operator<(const midas::odb &o, const T &d);
      template<typename T>
      friend bool operator<(const T &d, const midas::odb &o);
      template<typename T>
      friend bool operator<=(const midas::odb &o, const T &d);
      template<typename T>
      friend bool operator<=(const T &d, const midas::odb &o);
      template<typename T>
      friend bool operator>(const midas::odb &o, const T &d);
      template<typename T>
      friend bool operator>(const T &d, const midas::odb &o);
      template<typename T>
      friend bool operator>=(const midas::odb &o, const T &d);
      template<typename T>
      friend bool operator>=(const T &d, const midas::odb &o);

      // Setters and Getters
      bool is_preserve_string_size() const { return m_flags[odb_flags::PRESERVE_STRING_SIZE]; }
      void set_preserve_string_size(bool f) {
         m_flags[odb_flags::PRESERVE_STRING_SIZE] = f;
         set_flags_recursively(get_flags());
      }

      bool is_auto_refresh_read() const { return m_flags[odb_flags::AUTO_REFRESH_READ]; }
      void set_auto_refresh_read(bool f) {
         m_flags[odb_flags::AUTO_REFRESH_READ] = f;
         set_flags_recursively(get_flags());
      }

      bool is_auto_refresh_write() const { return m_flags[odb_flags::AUTO_REFRESH_WRITE]; }
      void set_auto_refresh_write(bool f) {
         m_flags[odb_flags::AUTO_REFRESH_WRITE] = f;
         set_flags_recursively(get_flags());
      }

      bool is_dirty() const { return m_flags[odb_flags::DIRTY]; }
      void set_dirty(bool f) { m_flags[odb_flags::DIRTY] = f; }

      bool is_auto_create() const { return m_flags[odb_flags::AUTO_CREATE]; }
      void set_auto_create(bool f) {
         m_flags[odb_flags::AUTO_CREATE] = f;
         set_flags_recursively(get_flags());
      }

      bool is_auto_enlarge_array() const { return m_flags[odb_flags::AUTO_ENLARGE_ARRAY]; }
      void set_auto_enlarge_array(bool f) {
         m_flags[odb_flags::AUTO_ENLARGE_ARRAY] = f;
         set_flags_recursively(get_flags());
      }

      // Static functions
      static void set_debug(bool flag) { m_debug = flag; }
      static bool get_debug() { return m_debug; }
      static int create(const char *name, int type = TID_KEY);
      static bool exists(const std::string &name);
      static int delete_key(const std::string &name);

      void odb_from_string(const std::string &s);
      void connect(const std::string &path, const std::string &name, bool write_defaults, bool delete_keys_not_in_defaults = false);
      void connect(std::string str, bool write_defaults = false, bool delete_keys_not_in_defaults = false);
      void connect_and_fix_structure(std::string path);
      static bool is_connected_odb() { return m_connected_odb; }

      void read();
      void read(int index);
      void write(int str_size = 0);
      void write(int index, int str_size);
      std::string print();
      std::string dump();
      void print(std::string &s, int indent);
      void dump(std::string &s, int indent);
      void delete_key();
      int size();
      void resize(int size);
      void watch(std::function<void(midas::odb &)> f);
      void unwatch();

      bool is_subkey(std::string str);
      HNDLE get_hkey() { return m_hKey; }
      std::string get_full_path();
      std::string get_parent_path();
      int get_tid() { return m_tid; }
      int get_num_values() { return m_num_values; }
      std::string get_name() { return m_name; }

      void fix_order(std::vector<std::string> target_subkey_order);
   };

   //---- midas::odb friend functions -------------------------------

   // overload comparison operators
   template<typename T>
   bool operator==(const midas::odb &o, const T &d) {
      // the operator needs a "const midas::odb" reference,
      // so we have to make a non-const copy
      T v;
      midas::odb oc(o);
      oc.get(v);
      return v == d;
   }

   template<typename T>
   bool operator==(const T &d, const midas::odb &o) {
      T v;
      midas::odb oc(o);
      oc.get(v);
      return d == v;
   }

   template<typename T>
   bool operator!=(const midas::odb &o, const T &d) {
      T v;
      midas::odb oc(o);
      oc.get(v);
      return v != d;
   }

   template<typename T>
   bool operator!=(const T &d, const midas::odb &o) {
      T v;
      midas::odb oc(o);
      oc.get(v);
      return d != v;
   }

   template<typename T>
   bool operator<(const midas::odb &o, const T &d) {
      T v;
      midas::odb oc(o);
      oc.get(v);
      return v < d;
   }

   template<typename T>
   bool operator<(const T &d, const midas::odb &o) {
      T v;
      midas::odb oc(o);
      oc.get(v);
      return d < v;
   }

   template<typename T>
   bool operator<=(const midas::odb &o, const T &d) {
      T v;
      midas::odb oc(o);
      oc.get(v);
      return v <= d;
   }

   template<typename T>
   bool operator<=(const T &d, const midas::odb &o) {
      T v;
      midas::odb oc(o);
      oc.get(v);
      return d <= v;
   }

   template<typename T>
   bool operator>(const midas::odb &o, const T &d) {
      T v;
      midas::odb oc(o);
      oc.get(v);
      return v > d;
   }

   template<typename T>
   bool operator>(const T &d, const midas::odb &o) {
      T v;
      midas::odb oc(o);
      oc.get(v);
      return d > v;
   }

   template<typename T>
   bool operator>=(const midas::odb &o, const T &d) {
      T v;
      midas::odb oc(o);
      oc.get(v);
      return v >= d;
   }

   template<typename T>
   bool operator>=(const T &d, const midas::odb &o) {
      T v;
      midas::odb oc(o);
      oc.get(v);
      return d >= v;
   }

} // namespace midas


#endif // _ODBXX_HXX
