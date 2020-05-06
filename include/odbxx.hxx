/********************************************************************\

  Name:         odbxx.cxx
  Created by:   Stefan Ritt

  Contents:     Object oriented interface to ODB

\********************************************************************/

#include <string>
#include <iostream>
#include <stdexcept>
#include <initializer_list>
#include <cstring>
#include <bitset>

#include "midas.h"

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

      int  m_tid;
      odb* m_parent_odb;

   public:
      u_odb() : m_string{} {};
      u_odb(odb* o) : m_string{} {};

      u_odb(uint8_t v) : m_uint8{v},m_tid{TID_UINT8} {};
      u_odb(int8_t v) : m_int8{v},m_tid{TID_INT8} {};
      u_odb(uint16_t v) : m_uint16{v},m_tid{TID_UINT16} {};
      u_odb(int16_t v) : m_int16{v},m_tid{TID_INT16} {};
      u_odb(uint32_t v) : m_uint32{v},m_tid{TID_UINT32} {};
      u_odb(int32_t v) : m_int32{v},m_tid{TID_INT32} {};
      u_odb(bool v) : m_bool{v},m_tid{TID_BOOL} {};
      u_odb(float v) : m_float{v},m_tid{TID_FLOAT} {};
      u_odb(double v) : m_double{v},m_tid{TID_DOUBLE} {};
      u_odb(std::string* v) : m_string{v},m_tid{TID_STRING} {};

      // Destructor
      ~u_odb();

      // Setters and getters
      void set_parent(odb *o) { m_parent_odb = o; }
      void set_tid(int tid) { m_tid = tid; }
      int get_tid() { return m_tid; }

      // Overload the Assignment Operators
      template <typename T>
      T operator=(T);

      // Overlaod the Conversion Operators
      template <typename T>
      inline operator T();

      template <typename T>
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
         else if (m_tid == TID_STRING)
            m_string = new std::string(std::to_string(v));
         else
            throw std::runtime_error("Invalid type ID " + std::to_string(m_tid));
      }

      void set_string(std::string s) {
         m_string = new std::string(s);
      }

      void set(odb* v) {
         if (m_tid != TID_KEY)
            throw std::runtime_error("Subkey can only be assigned to ODB key");
         m_odb = v;
      }

      void set(std::string v)  {
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
         else if (m_tid == TID_STRING)
            m_string = new std::string(v);
         else
            throw std::runtime_error("Invalid type ID " + std::to_string(m_tid));
      }

      void set(const char* v){
         set(std::string(v));
      }

      void set(char* v){
         set(std::string(v));
      }

      void add(double inc, bool push=true);
      void mult(double f, bool push=true);

      // overload the conversion operator for std::string
      operator std::string() {
         std::string s;
         get(s);
         return s;
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
      T get() {
         if (m_tid == TID_UINT8)
            return (T)m_uint8;
         else if (m_tid == TID_INT8)
            return (T)m_int8;
         else if (m_tid == TID_UINT16)
            return (T)m_uint16;
         else if (m_tid == TID_INT16)
            return (T)m_int16;
         else if (m_tid == TID_UINT32)
            return (T) m_uint32;
         else if (m_tid == TID_INT32)
            return (T)m_int32;
         else if (m_tid == TID_BOOL)
            return (T)m_bool;
         else if (m_tid == TID_FLOAT)
            return (T)m_float;
         else if (m_tid == TID_DOUBLE)
            return (T)m_double;
         else
            throw std::runtime_error("Invalid type ID %s" + std::to_string(m_tid));
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
      odb* get_odb() {
         if (m_tid != TID_KEY)
            throw std::runtime_error("odb_get() called for non-key object");
         return m_odb;
      }

      // overload stream out operator
      friend std::ostream &operator<<(std::ostream &output, u_odb &o) {
         std::string s = o;
         output << s;
         return output;
      };

   };

   //-----------------------------------------------

   enum odb_flags {
      AUTO_REFRESH_READ = 0,
      AUTO_REFRESH_WRITE,
      PRESERVE_STRING_SIZE,
      DIRTY
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

         iterator operator++() {
            ++pu_odb;
            return *this;
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
      std::function<void(midas::odb&)> m_watch_callback;

   public:

      // Default constructor
      odb() :
         m_flags{odb_flags::AUTO_REFRESH_READ | odb_flags::AUTO_REFRESH_WRITE},
         m_tid{0},
         m_data{nullptr},
         m_name{},
         m_num_values{0},
         m_last_index{-1},
         m_hKey{} {}

      // Destructor
      ~odb() {
         delete[] m_data;
      }

      static midas::odb* search_hkey(midas::odb* po, int hKey) {
         if (po->m_hKey == hKey)
            return po;
         if (po->m_tid == TID_KEY) {
            for (int i=0 ; i<po->m_num_values ; i++) {
               midas::odb *pot = search_hkey(po->m_data[i].get_odb(), hKey);
               if (pot != nullptr)
                  return pot;
            }
         }
         return nullptr;
      }

      static void watch_callback(int hDB, int hKey, int index, void *info) {
         midas::odb* po = static_cast<midas::odb *>(info);
         midas::odb* poh = search_hkey(po, hKey);
         poh->m_last_index = index;
         po->m_watch_callback(*poh);
         poh->m_last_index = -1;
      }

      // Deep copy constructor
      odb(const odb &o) : odb() {
         m_tid = o.m_tid;
         m_name = o.m_name;
         m_num_values = o.m_num_values;
         m_hKey = o.m_hKey;
         m_watch_callback = o.m_watch_callback;
         m_data = new midas::u_odb[m_num_values];
         for (int i = 0; i < m_num_values; i++) {
            m_data[i].set_tid(m_tid);
            m_data[i].set_parent(this);
            if (m_tid == TID_STRING) {
               // set_string() creates a copy of our string
               m_data[i].set_string(o.m_data[i]);
            } else if (m_tid == TID_KEY) {
               // recursive call to create a copy of the odb object
               m_data[i].set(new midas::odb(*o.m_data[i].get_odb()));
            } else {
               // simply pass basic types
               m_data[i] = o.m_data[i];
            }
         }
      }

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

      // Constructor for strings
      odb(const char *v) : odb() {
         if (v[0] == '/') {
            std::string s(v);
            pull_key(s);
            if (m_tid == TID_KEY) {
               std::vector<std::string> name;
               m_num_values = get_subkeys(name);
               delete m_data;
               m_data = new midas::u_odb[m_num_values]{};
               for (int i=0 ; i<m_num_values ; i++) {
                  std::string k(s);
                  k += "/" + name[i];
                  midas::odb* o = new midas::odb(k.c_str());
                  m_data[i].set_tid(TID_KEY);
                  m_data[i].set_parent(this);
                  m_data[i].set(o);
               }
            } else
               pull();
         } else {
            // Construct object from initializer_list
            m_num_values = 1;
            m_data = new u_odb[1]{new std::string{v}};
            m_tid = m_data[0].get_tid();
            m_data[0].set_parent(this);
         }
      }

      // Constructor with std::initializer_list
      odb(std::initializer_list<std::pair<std::string, midas::odb>> list) : odb() {
         m_tid = TID_KEY;
         m_num_values = list.size();
         m_data = new u_odb[m_num_values];
         int i = 0;
         for (auto &element: list) {
            auto o = new midas::odb(element.second);
            o->set_name(element.first);
            m_data[i].set_tid(TID_KEY);
            m_data[i].set_parent(this);
            m_data[i].set(o);
            i++;
         }
      }

      odb(std::string name, midas::odb element) : odb(element) {
         m_name = name;
         for (int i = 0; i < m_num_values; i++)
            m_data[i].set_parent(this);
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
         for (int i=0 ; i<SIZE ; i++) {
            u_odb u(arr[i]);
            m_data[i].set_tid(u.get_tid());
            m_data[i].set_parent(this);
            m_data[i].set(arr[i]);
         }
         m_tid = m_data[0].get_tid();
      }

      // Constructor with string array
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

      // Setters and Getters
      static void set_debug(bool flag) { m_debug = flag; }

      static bool get_debug() { return m_debug; }

      bool is_preserve_string_size() const { return m_flags[odb_flags::PRESERVE_STRING_SIZE]; }

      void set_preserve_string_size(bool f) { m_flags[odb_flags::PRESERVE_STRING_SIZE] = f; }

      bool is_auto_refresh_read() const { return m_flags[odb_flags::AUTO_REFRESH_READ]; }

      void set_auto_refresh_read(bool f) { m_flags[odb_flags::AUTO_REFRESH_READ] = f; }

      bool is_auto_refresh_write() const { return m_flags[odb_flags::AUTO_REFRESH_WRITE]; }

      void set_auto_refresh_write(bool f) { m_flags[odb_flags::AUTO_REFRESH_WRITE] = f; }

      bool is_dirty() const { return m_flags[odb_flags::DIRTY]; }

      void set_dirty(bool f) { m_flags[odb_flags::DIRTY] = f; }

      int get_tid() { return m_tid; }

      void set_tid(int tid) { m_tid = tid; }

      HNDLE get_hkey() { return m_hKey; }

      void set_hkey(HNDLE hKey) { m_hKey = hKey; }

      int get_num_values() { return m_num_values; }

      void set_num_values(int n) { m_num_values = n; }

      int get_last_index() { return m_last_index; }

      void set_last_index(int i) { m_last_index = i; }

      std::string get_name() { return m_name; }

      void set_name(std::string s) { m_name = s; }

      u_odb &get_mdata(int index = 0) { return m_data[index]; }

      std::string get_full_path() {
         char str[256];
         db_get_path(m_hDB, m_hKey, str, sizeof(str));
         return str;
      }

      void delete_key() {
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
      template<typename T>
      const T &operator=(const T &v) {
         if (m_num_values == 0) {
            // initialize this
            m_num_values = 1;
            if (std::is_same<T, uint8_t>::value)
               m_tid = TID_UINT8;
            if (std::is_same<T, int8_t>::value)
               m_tid = TID_INT8;
            if (std::is_same<T, uint16_t>::value)
               m_tid = TID_UINT16;
            if (std::is_same<T, int16_t>::value)
               m_tid = TID_INT16;
            if (std::is_same<T, uint32_t>::value)
               m_tid = TID_UINT32;
            if (std::is_same<T, int32_t>::value)
               m_tid = TID_INT32;
            if (std::is_same<T, bool>::value)
               m_tid = TID_BOOL;
            if (std::is_same<T, float>::value)
               m_tid = TID_FLOAT;
            if (std::is_same<T, double>::value)
               m_tid = TID_DOUBLE;

            m_data = new u_odb[1]{};
            m_data[0].set_tid(m_tid);
            m_data[0].set_parent(this);
            m_data[0].set(v);
            push();
         } else {
            for (int i = 0; i < m_num_values; i++)
               m_data[i].set(v);
            push();
         }
         return v;
      }

      // Overload the Assignment Operators for std::vector
      template<typename T>
      const std::vector<T> &operator=(const std::vector<T> &v) {

         // resize internal array if different
         if (v.size() != m_num_values) {
            if (m_tid == TID_STRING) {
               // TBD
            } else {
               auto new_array = new u_odb[v.size()]{};
               for (int i = 0; i < m_num_values && i < v.size(); i++)
                  new_array[i] = m_data[i];
               delete[] m_data;
               m_data = new_array;
               m_num_values = v.size();
               for (int i = 0; i < m_num_values; i++) {
                  m_data[i].set_tid(m_tid);
                  m_data[i].set_parent(this);
               }
            }
         }

         for (int i = 0; i < m_num_values; i++)
            m_data[i].set(v[i]);
         push();
         return v;
      }

      // Resize an ODB key
      void resize(int size) {
         if (m_tid == TID_STRING) {
            // TBD
         } else {
            auto new_array = new u_odb[size]{};
            for (int i = 0; i < m_num_values && i < size; i++)
               new_array[i] = m_data[i];
            delete[] m_data;
            m_data = new_array;
            m_num_values = size;
            for (int i = 0; i < m_num_values; i++) {
               m_data[i].set_tid(m_tid);
               m_data[i].set_parent(this);
            }
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
      template<typename T>
      operator std::vector<T>() {
         if (is_auto_refresh_read())
            pull();
         std::vector<T> v(m_num_values);
         for (int i = 0; i < m_num_values; i++)
            v[i] = m_data[i];
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
         return get<T>(); // forward to get<T>()
      }

      // overload stream out operator
      friend std::ostream &operator<<(std::ostream &output, odb &o) {
         std::string s = static_cast<std::string>(o);
         output << s;
         return output;
      };

      // overload index operator for arrays
      u_odb &operator[](int index) {
         if (index < 0 || index >= m_num_values)
            throw std::out_of_range("Index \"" + std::to_string(index) + "\" out of range for ODB key \"" +
                                    get_full_path() + "[0..." + std::to_string(m_num_values - 1) + "]\"");

         if (is_auto_refresh_read())
            pull(index);

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

      odb &get_subkey(std::string str) {
         if (m_tid != TID_KEY)
            throw std::runtime_error("ODB key \"" + get_full_path() + "\" does not have subkeys");

         std::string first = str;
         std::string tail{};
         if (str.find('/') != std::string::npos) {
            first = str.substr(0, str.find('/'));
            tail = str.substr(str.find('/') + 1);
         }

         int i;
         for (i = 0; i < m_num_values; i++)
            if (m_data[i].get_odb()->get_name() == first)
               break;
         if (i == m_num_values)
            throw std::runtime_error("ODB key \"" + get_full_path() + "\" does not contain subkey \"" + first + "\"");

         if (!tail.empty())
            return m_data[i].get_odb()->get_subkey(tail);

         return *m_data[i].get_odb();
      }

      // get function for basic types
      template<typename T>
      T get() {
         if (m_num_values > 1)
            throw std::runtime_error("ODB key \"" + get_full_path() +
                                     "[0..." + std::to_string(m_num_values - 1) +
                                     "]\" contains array. Please assign to std::vector.");
         if (is_auto_refresh_read())
            pull();
         return (T) m_data[0];
      }

      // get function for basic types as a parameter
      template<typename T>
      void get(T &v) {
         if (m_num_values > 1)
            throw std::runtime_error(
                    "ODB key \"" + get_full_path() + "\" contains array. Please assign to std::vector.");
         if (is_auto_refresh_read())
            pull();
         v = (T) m_data[0];
      }

      // get function for strings
      void get(std::string &s, bool quotes = false, bool refresh = true) {
         if (refresh && is_auto_refresh_read())
            pull();

         // put value into quotes
         s = "";
         std::string sd;
         for (int i = 0; i < m_num_values; i++) {
            m_data[i].get(sd);
            if (quotes)
               s += "\"";
            s += sd;
            if (quotes)
               s += "\"";
            if (i < m_num_values - 1)
               s += ",";
         }
      }

      // iterator support
      iterator begin() const { return iterator(m_data); }

      iterator end() const { return iterator(m_data + m_num_values); }

      // overload arithmetic operators
      template<typename T>
      T operator+(const T i) {
         if (m_num_values > 1)
            throw std::runtime_error("ODB key \"" + get_full_path() +
                                     "\" contains array which cannot be used in basic arithmetic operation.");
         T s;
         get(s);
         return s + i;
      }

      template<typename T>
      T operator-(const T i) {
         if (m_num_values > 1)
            throw std::runtime_error("ODB key \"" + get_full_path() +
                                     "\" contains array which cannot be used in basic arithmetic operation.");
         T s;
         get(s);
         return s - i;
      }

      template<typename T>
      T operator*(const T i) {
         if (m_num_values > 1)
            throw std::runtime_error("ODB key \"" + get_full_path() +
                                     "\" contains array which cannot be used in basic arithmetic operation.");
         T s;
         get(s);
         return s * i;
      }

      template<typename T>
      T operator/(const T i) {
         if (m_num_values > 1)
            throw std::runtime_error("ODB key \"" + get_full_path() +
                                     "\" contains array which cannot be used in basic arithmetic operation.");
         T s;
         get(s);
         return s / i;
      }

      odb &operator++() {
         for (int i = 0; i < m_num_values; i++)
            m_data[i].add(1, false);
         push();
         return *this;
      }

      odb operator++(int) {
         // create temporary object
         odb o(this);
         for (int i = 0; i < m_num_values; i++)
            m_data[i].add(1, false);
         push();
         return o;
      }

      odb &operator--() {
         for (int i = 0; i < m_num_values; i++)
            m_data[i].add(-1, false);
         push();
         return *this;
      }

      odb operator--(int) {
         // create temporary object
         odb o(this);
         for (int i = 0; i < m_num_values; i++)
            m_data[i].add(-1, false);
         push();
         return o;
      }

      odb &operator+=(double d) {
         for (int i = 0; i < m_num_values; i++)
            m_data[i].add(d, false);
         push();
         return *this;
      }

      odb &operator-=(double d) {
         for (int i = 0; i < m_num_values; i++)
            m_data[i].add(-d, false);
         push();
         return *this;
      }

      odb &operator*=(double d) {
         for (int i = 0; i < m_num_values; i++)
            m_data[i].mult(d, false);
         push();
         return *this;
      }

      odb &operator/=(double d) {
         if (d == 0)
            throw std::runtime_error("Division by zero");
         for (int i = 0; i < m_num_values; i++)
            m_data[i].mult(1 / d, false);
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

      std::string dump() {
         std::string s;
         s = "{\n";
         dump(s, 1);
         s += "\n}";
         return s;
      }

      // print current object with all sub-objects nicely indented
      void print(std::string &s, int indent) {
         for (int i = 0; i < indent; i++)
            s += "   ";
         if (m_tid == TID_KEY) {
            s += "\"" + m_name + "\": {\n";
            for (int i = 0; i < m_num_values; i++) {
               std::string v;
               // recursive call
               m_data[i].get_odb()->print(v, indent + 1);
               s += v;
               if (i < m_num_values - 1)
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

      // dump current object in the same way as odbedit saves as json
      void dump(std::string &s, int indent) {
         for (int i = 0; i < indent; i++)
            s += "   ";
         if (m_tid == TID_KEY) {
            s += "\"" + m_name + "\": {\n";
            for (int i = 0; i < m_num_values; i++) {
               std::string v;
               m_data[i].get_odb()->dump(v, indent + 1);
               s += v;
               if (i < m_num_values - 1)
                  s += ",\n";
               else
                  s += "\n";
            }
            for (int i = 0; i < indent; i++)
               s += "   ";
            s += "}";
         } else {
            KEY key;
            db_get_key(m_hDB, m_hKey, &key);
            s += "\"" + m_name + "/key\": ";
            s += "{ \"type\": " + std::to_string(m_tid) + ", ";
            s += "\"access_mode\": " + std::to_string(key.access_mode) + ", ";
            s += "\"last_written\": " + std::to_string(key.last_written) + "},\n";
            for (int i = 0; i < indent; i++)
               s += "   ";
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

      // initialize m_hDB, internal use only
      void init_hdb() {
         if (m_hDB == 0)
            cm_get_experiment_database(&m_hDB, nullptr);
         if (m_hDB == 0)
            throw std::runtime_error("Please call cm_connect_experiment() befor accessing the ODB");
      }

      // get number of subkeys in ODB, return number and vector of names
      int get_subkeys(std::vector<std::string>& name) {
         if (m_tid != TID_KEY)
            return 0;
         if (m_hKey == 0)
            throw std::runtime_error("get_subkeys called with invalid m_hKey for ODB key \"" + m_name + "\"");

         // count number of subkeys in ODB
         std::vector<HNDLE> hlist;
         int n = 0;
         for (int i = 0;; i++) {
            HNDLE h;
            int status = db_enum_key(m_hDB, m_hKey, i, &h);
            if (status != DB_SUCCESS)
               break;
            KEY key;
            db_get_key(m_hDB, h, &key);
            hlist.push_back(h);
            name.push_back(key.name);
            n = i + 1;
         }

         return n;
      }

      // obtain key definition from ODB and allocate local data array
      bool pull_key(std::string &path) {
         int status = db_find_key(m_hDB, 0, path.c_str(), &m_hKey);
         if (status != DB_SUCCESS)
            return false;

         KEY key;
         status = db_get_key(m_hDB, m_hKey, &key);
         if (status != DB_SUCCESS)
            throw std::runtime_error("db_get_key for ODB key \"" + path +
                                     "\" failed with status " + std::to_string(status));

         // check for correct type if given as parameter
         if (m_tid > 0 && m_tid != (int) key.type)
            throw std::runtime_error("ODB key \"" + get_full_path() +
                                     "\" has differnt type than specified");

         if (m_debug)
            std::cout << "Get definition for ODB key \"" + get_full_path() + "\"" << std::endl;

         m_tid = key.type;
         m_num_values = key.num_values;
         m_name = key.name;
         if (m_tid != TID_KEY) {
            delete m_data;
            m_data = new midas::u_odb[m_num_values]{};
            for (int i = 0; i < m_num_values; i++) {
               m_data[i].set_tid(m_tid);
               m_data[i].set_parent(this);
            }
         }

         return true;
      }

      // create key in ODB if it does not exist, otherwise check key type
      bool push_key(std::string &path, bool force = false) {
         int status = db_find_key(m_hDB, 0, path.c_str(), &m_hKey);
         if (status != DB_SUCCESS) {
            if (m_tid > 0 && m_tid < TID_LAST) {
               status = db_create_key(m_hDB, 0, path.c_str(), m_tid);
               if (status != DB_SUCCESS)
                  throw std::runtime_error("ODB key \"" + path + "\" cannot be created");
               status = db_find_key(m_hDB, 0, path.c_str(), &m_hKey);
               if (status != DB_SUCCESS)
                  throw std::runtime_error("ODB key \"" + path + "\" not found after creation");
               if (m_debug)
                  std::cout << "Created ODB key " + get_full_path() << std::endl;
            } else
               throw std::runtime_error("ODB key \"" + path + "\" cannot be found");
            return true;
         } else {
            KEY key;
            status = db_get_key(m_hDB, m_hKey, &key);
            if (status != DB_SUCCESS)
               throw std::runtime_error("db_get_key for ODB key \"" + path +
                                        "\" failed with status " + std::to_string(status));

            // check for correct type
            if (m_tid > 0 && m_tid != (int) key.type) {
               if (force) {
                  // delete and recreate key
                  status = db_delete_key(m_hDB, m_hKey, false);
                  if (status != DB_SUCCESS)
                     throw std::runtime_error("db_delete_key for ODB key \"" + path +
                                              "\" failed with status " + std::to_string(status));
                  status = db_create_key(m_hDB, 0, path.c_str(), m_tid);
                  if (status != DB_SUCCESS)
                     throw std::runtime_error("ODB key \"" + path + "\" cannot be created");
                  status = db_find_key(m_hDB, 0, path.c_str(), &m_hKey);
                  if (status != DB_SUCCESS)
                     throw std::runtime_error("ODB key \"" + path + "\" not found after creation");
                  if (m_debug)
                     std::cout << "Re-created ODB key \"" + get_full_path() << "\" with different type" << std::endl;
               } else
                  // abort
                  throw std::runtime_error("ODB key \"" + get_full_path() +
                                                   "\" has differnt type than specified");
            } else
               if (m_debug)
                  std::cout << "Validated ODB key \"" + get_full_path() + "\"" << std::endl;

            return false;
         }
      }


      // retrieve data from ODB and assign it to this object
      void pull() {
         if (m_hKey == 0)
            return; // needed to print un-connected objects

         if (m_tid == 0)
            throw std::runtime_error("Pull of invalid ODB key \"" + m_name + "\"");

         int status{};
         if (m_tid == TID_STRING) {
            KEY key;
            db_get_key(m_hDB, m_hKey, &key);
            char *str = (char *) malloc(key.total_size);
            int size = key.total_size;
            status = db_get_data(m_hDB, m_hKey, str, &size, m_tid);
            for (int i = 0; i < m_num_values; i++)
               m_data[i].set(str + i * key.item_size);
            free(str);
         } else if (m_tid == TID_KEY) {
            for (int i=0 ; i<m_num_values ; i++)
               m_data[i].get_odb()->pull();
            status = DB_SUCCESS;
         } else {
            int size = rpc_tid_size(m_tid) * m_num_values;
            void *buffer = malloc(size);
            void *p = buffer;
            status = db_get_data(m_hDB, m_hKey, p, &size, m_tid);
            for (int i = 0; i < m_num_values; i++) {
               if (m_tid == TID_UINT8)
                  m_data[i].set(*static_cast<uint8_t*>(p));
               else if (m_tid == TID_INT8)
                  m_data[i].set(*static_cast<int8_t*>(p));
               else if (m_tid == TID_UINT16)
                  m_data[i].set(*static_cast<uint16_t*>(p));
               else if (m_tid == TID_INT16)
                  m_data[i].set(*static_cast<int16_t*>(p));
               else if (m_tid == TID_UINT32)
                  m_data[i].set(*static_cast<uint32_t*>(p));
               else if (m_tid == TID_INT32)
                  m_data[i].set(*static_cast<int32_t*>(p));
               else if (m_tid == TID_BOOL)
                  m_data[i].set(*static_cast<bool*>(p));
               else if (m_tid == TID_FLOAT)
                  m_data[i].set(*static_cast<float*>(p));
               else if (m_tid == TID_DOUBLE)
                  m_data[i].set(*static_cast<double*>(p));
               else if (m_tid == TID_STRING)
                  m_data[i].set(std::string(static_cast<const char *>(p)));
               else
                  throw std::runtime_error("Invalid type ID " + std::to_string(m_tid));

               p = static_cast<char*>(p) + rpc_tid_size(m_tid);
            }
            free(buffer);
         }

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

      // retrieve individual member of array
      void pull(int index) {
         if (m_hKey == 0)
            return; // needed to print un-connected objects

         if (m_tid == 0)
            throw std::runtime_error("Pull of invalid ODB key \"" + m_name + "\"");

         int status{};
         if (m_tid == TID_STRING) {
            KEY key;
            db_get_key(m_hDB, m_hKey, &key);
            char *str = (char *) malloc(key.item_size);
            int size = key.item_size;
            status = db_get_data_index(m_hDB, m_hKey, str, &size, index, m_tid);
            m_data[index].set(str);
            free(str);
         } else if (m_tid == TID_KEY) {
            m_data[index].get_odb()->pull();
            status = DB_SUCCESS;
         } else {
            int size = rpc_tid_size(m_tid);
            void *buffer = malloc(size);
            void *p = buffer;
            status = db_get_data_index(m_hDB, m_hKey, p, &size, index, m_tid);
            if (m_tid == TID_UINT8)
               m_data[index].set(*static_cast<uint8_t*>(p));
            else if (m_tid == TID_INT8)
               m_data[index].set(*static_cast<int8_t*>(p));
            else if (m_tid == TID_UINT16)
               m_data[index].set(*static_cast<uint16_t*>(p));
            else if (m_tid == TID_INT16)
               m_data[index].set(*static_cast<int16_t*>(p));
            else if (m_tid == TID_UINT32)
               m_data[index].set(*static_cast<uint32_t*>(p));
            else if (m_tid == TID_INT32)
               m_data[index].set(*static_cast<int32_t*>(p));
            else if (m_tid == TID_BOOL)
               m_data[index].set(*static_cast<bool*>(p));
            else if (m_tid == TID_FLOAT)
               m_data[index].set(*static_cast<float*>(p));
            else if (m_tid == TID_DOUBLE)
               m_data[index].set(*static_cast<double*>(p));
            else if (m_tid == TID_STRING)
               m_data[index].set(std::string(static_cast<const char *>(p)));
            else
               throw std::runtime_error("Invalid type ID " + std::to_string(m_tid));

            free(buffer);
         }

         if (status != DB_SUCCESS)
            throw std::runtime_error("db_get_data for ODB key \"" + get_full_path() +
                                     "\" failed with status " + std::to_string(status));
         if (m_debug) {
            std::string s;
            m_data[index].get(s);
            std::cout << "Get ODB key \"" + get_full_path() + "[" +
                            std::to_string(index) + "]\": [" + s + "]" << std::endl;

         }
      }

      // push individual member of an array
      void push(int index) {
         if (m_hKey == 0)
            throw std::runtime_error("Push of un-connected ODB key \"" + m_name + "\"");

         // don't push keys
         if (m_tid == TID_KEY)
            return;

         int status{};
         if (m_tid == TID_STRING) {
            KEY key;
            db_get_key(m_hDB, m_hKey, &key);
            std::string s;
            m_data[0].get(s);
            if (m_num_values == 1) {
               int size = key.item_size;
               if (key.item_size == 0 || !is_preserve_string_size())
                  size = s.size() + 1;
               status = db_set_data(m_hDB, m_hKey, s.c_str(), size, 1, m_tid);
            } else {
               if (key.item_size == 0)
                  key.item_size = s.size() + 1;
               status = db_set_data_index(m_hDB, m_hKey, s.c_str(), key.item_size, index, m_tid);
            }
            if (m_debug) {
               if (m_num_values > 1)
                  std::cout << "Set ODB key \"" + get_full_path() + "[" + std::to_string(index) + "]\" = " + s
                            << std::endl;
               else
                  std::cout << "Set ODB key \"" + get_full_path() + "\" = " + s << std::endl;
            }
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

      // push all members of an array to the ODB
      void push() {

         // push subkeys
         if (m_tid == TID_KEY) {
            for (int i=0 ; i<m_num_values ; i++)
               m_data[i].get_odb()->push();
            return;
         }

         if (m_tid < 1 || m_tid >= TID_LAST)
            throw std::runtime_error("Invalid TID for ODB key \"" + get_full_path() + "\"");

         if (m_hKey  == 0)
            throw std::runtime_error("push for ODB key \"" + m_name +
                                     "\" not possible because of invalid key handle");

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
               if (key.item_size == 0 || key.total_size == 0) {
                  int size = 0;
                  for (int i=0 ; i<m_num_values ; i++) {
                     std::string d;
                     m_data[i].get(d);
                     if (d.size()+1 > size)
                        size = d.size()+1;
                  }
                  // round up to multiples of 32
                  size = ((size / 32) + 1) * 32;
                  key.item_size = size;
                  key.total_size = size * m_num_values;
               }
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

      // send key definitions and data with optional subkeys to certain path in ODB
      void push(std::string path, std::string name = "", bool force = false) {
         init_hdb();
         if (!name.empty())
            m_name = name;
         path += "/" + m_name;
         bool created = push_key(path, force);

         // correct wrong parent ODB from initializer_list
         for (int i=0 ; i<m_num_values ; i++)
            m_data[i].set_parent(this);

         if (m_tid == TID_KEY) {
            for (int i=0 ; i<m_num_values ; i++)
               m_data[i].get_odb()->push(get_full_path(), m_data[i].get_odb()->get_name(), force);
         } else if (created || force) {
            push();
         }
      }

      void push(std::string str, bool force = false) {
         std::string name;
         std::string path;
         if (str.find_last_of('/') == std::string::npos) {
            name = str;
            path = "/";
         } else {
            name = str.substr(str.find_last_of('/') + 1);
            path = str.substr(0, str.find_last_of('/'));
         }

         push(path, name, force);
      }

      void watch(std::function<void(midas::odb&)> f) {
         if (m_hKey == 0)
            throw std::runtime_error("watch() called for ODB key \"" + m_name +
                                     "\" which is not connected to ODB");
         m_watch_callback = f;
         db_watch(m_hDB, m_hKey, midas::odb::watch_callback, this);
      }
   };

   //-----------------------------------------------

   // initialize static variables
   HNDLE odb::m_hDB = 0;
   bool odb::m_debug = false;

   //-----------------------------------------------

   //---- u_odb implementations calling functions from odb

   u_odb::~u_odb() {
      if (m_tid == TID_STRING)
         delete m_string;
      else if (m_tid == TID_KEY)
         delete m_odb;
   }

   // get function for strings
   inline void u_odb::get(std::string &s) {
      if (m_tid == TID_UINT8)
         s = std::to_string(m_uint8);
      else if (m_tid == TID_INT8)
         s = std::to_string(m_int8);
      else if (m_tid == TID_UINT16)
         s = std::to_string(m_uint16);
      else if (m_tid == TID_INT16)
         s = std::to_string(m_int16);
      else if (m_tid == TID_UINT32)
         s = std::to_string(m_uint32);
      else if (m_tid == TID_INT32)
         s = std::to_string(m_int32);
      else if (m_tid == TID_BOOL)
         s = std::string(m_bool ? "true" : "false");
      else if (m_tid == TID_FLOAT)
         s = std::to_string(m_float);
      else if (m_tid == TID_DOUBLE)
         s = std::to_string(m_double);
      else if (m_tid == TID_STRING)
         s = *m_string;
      else if (m_tid == TID_KEY) {
         m_odb->print(s, 0);
      } else
         throw std::runtime_error("Invalid type ID " + std::to_string(m_tid));
   }

   //---- u_odb assignment and arithmetic operators overloads which call odb::push()

   template <typename T>
   inline T u_odb::operator=(T v) {
      set(v);
      m_parent_odb->push();
      return v;
   }

   // overload all standard conversion operators
   template <typename T>
   inline u_odb::operator T() {
      m_parent_odb->set_last_index(-1);
      return get<T>(); // forward to get<T>()
   }

   void u_odb::add(double inc, bool push) {
      if (m_tid == TID_UINT8)
         m_uint8 += inc;
      else if (m_tid == TID_INT8)
         m_int8 += inc;
      else if (m_tid == TID_UINT16)
         m_uint16 += inc;
      else if (m_tid == TID_INT16)
         m_int16 += inc;
      else if (m_tid == TID_UINT32)
         m_uint32 += inc;
      else if (m_tid == TID_INT32)
         m_int32 += inc;
      else if (m_tid == TID_FLOAT)
         m_float += inc;
      else if (m_tid == TID_DOUBLE)
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