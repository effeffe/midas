/********************************************************************\

  Name:         odbxx.cxx
  Created by:   Stefan Ritt

  Contents:     Object oriented interface to ODB implementation file

\********************************************************************/

#include <string>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <initializer_list>
#include <cstring>
#include <bitset>
#include <functional>

#include "midas.h"
#include "odbxx.h"
#include "mexcept.h"
// #include "mleak.hxx" // un-comment for memory leak debugging

/*------------------------------------------------------------------*/

namespace midas {

   //----------------------------------------------------------------

   // initialize static variables
   HNDLE odb::m_hDB = 0;
   bool odb::m_debug = false;
   bool odb::m_connected_odb = false;
   std::vector<midas::odb *> m_watchlist = {};

   // static functions ----------------------------------------------

   // initialize m_hDB, internal use only
   void odb::init_hdb() {
      if (m_hDB == 0)
         cm_get_experiment_database(&m_hDB, nullptr);
      if (m_hDB == 0)
         mthrow("Please call cm_connect_experiment() befor accessing the ODB");
      m_connected_odb = true;
   }

   // search for a key with a specific hKey, needed for callbacks
   midas::odb *odb::search_hkey(midas::odb *po, int hKey) {
      if (po->m_hKey == hKey)
         return po;
      if (po->m_tid == TID_KEY) {
         for (int i = 0; i < po->m_num_values; i++) {
            midas::odb *pot = search_hkey(po->m_data[i].get_podb(), hKey);
            if (pot != nullptr)
               return pot;
         }
      }
      return nullptr;
   }

   // check if a key exists in the ODB
   bool odb::exists(std::string name) {
      init_hdb();
      if (!odb::is_connected_odb())
         return false;
      HNDLE hkey;
      return db_find_key(m_hDB, 0, name.c_str(), &hkey) == DB_SUCCESS;
   }

   // global callback function for db_watch()
   void odb::watch_callback(int hDB, int hKey, int index, void *info) {
      midas::odb *po = static_cast<midas::odb *>(info);
      if (po->m_data == nullptr)
         mthrow("Callback received for a midas::odb object which went out of scope");
      midas::odb *poh = search_hkey(po, hKey);
      poh->m_last_index = index;
      po->m_watch_callback(*poh);
      poh->m_last_index = -1;
   }

   // create ODB key
   int odb::create(const char *name, int type) {
      init_hdb();
      int status = -1;
      if (is_connected_odb())
         status = db_create_key(m_hDB, 0, name, type);
      if (status != DB_SUCCESS && status != DB_KEY_EXIST)
         mthrow("Cannot create key " + std::string(name) + ", db_create_key() status = " + std::to_string(status));
      return status;
   }

   // private functions ---------------------------------------------

   void odb::set_flags_recursively(uint32_t f) {
      m_flags = f;
      if (m_tid == TID_KEY) {
         for (int i = 0; i < m_num_values; i++)
            m_data[i].get_odb().set_flags_recursively(f);
      }
   }

   // resize internal m_data array, keeping old values
   void odb::resize_mdata(int size) {
      auto new_array = new u_odb[size]{};
      int i;
      for (i = 0; i < m_num_values && i < size; i++) {
         new_array[i] = m_data[i];
         if (m_tid == TID_KEY)
            m_data[i].set_odb(nullptr); // move odb*
         if (m_tid == TID_STRING || m_tid == TID_LINK)
            m_data[i].set_string_ptr(nullptr); // move std::string*
      }
      for (; i < size; i++) {
         if (m_tid == TID_STRING || m_tid == TID_LINK)
            new_array[i].set_string(""); // allocates new string
      }
      delete[] m_data;
      m_data = new_array;
      m_num_values = size;
      for (int i = 0; i < m_num_values; i++) {
         m_data[i].set_tid(m_tid);
         m_data[i].set_parent(this);
      }
   }

   // get function for strings
   void odb::get(std::string &s, bool quotes, bool refresh) {
      if (refresh && is_auto_refresh_read())
         read();

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

   // public functions ----------------------------------------------

   // Deep copy constructor
   odb::odb(const odb &o) : odb() {
      m_tid = o.m_tid;
      m_name = o.m_name;
      m_num_values = o.m_num_values;
      m_hKey = o.m_hKey;
      m_watch_callback = o.m_watch_callback;
      m_data = new midas::u_odb[m_num_values]{};
      for (int i = 0; i < m_num_values; i++) {
         m_data[i].set_tid(m_tid);
         m_data[i].set_parent(this);
         if (m_tid == TID_STRING || m_tid == TID_LINK) {
            // set_string() creates a copy of our string
            m_data[i].set_string(o.m_data[i]);
         } else if (m_tid == TID_KEY) {
            // recursive call to create a copy of the odb object
            midas::odb *po = o.m_data[i].get_podb();
            midas::odb *pc = new midas::odb(*po);
            pc->set_parent(this);
            m_data[i].set(pc);
         } else {
            // simply pass basic types
            m_data[i] = o.m_data[i];
         }
      }
   }

   // Constructor for strings
   odb::odb(const char *v) : odb() {
      if (v[0] == '/') {
         std::string s(v);
         if (!read_key(s))
            mthrow("ODB key \"" + s + "\" not found in ODB");

         if (m_tid == TID_KEY) {
            std::vector<std::string> name;
            m_num_values = get_subkeys(name);
            delete[] m_data;
            m_data = new midas::u_odb[m_num_values]{};
            for (int i = 0; i < m_num_values; i++) {
               std::string k(s);
               k += "/" + name[i];
               midas::odb *o = new midas::odb(k.c_str());
               o->set_parent(this);
               m_data[i].set_tid(TID_KEY);
               m_data[i].set_parent(this);
               m_data[i].set(o);
            }
         } else
            read();
      } else {
         // Construct object from initializer_list
         m_num_values = 1;
         m_data = new u_odb[1]{new std::string{v}};
         m_tid = m_data[0].get_tid();
         m_data[0].set_parent(this);
      }
   }

   // return full path from ODB
   std::string odb::get_full_path() {
      if (m_parent)
         return m_parent->get_full_path() + "/" + m_name;

      if (!is_connected_odb())
         return m_name;

      char str[256];
      db_get_path(m_hDB, m_hKey, str, sizeof(str));
      return str;
   }

   // Resize an ODB key
   void odb::resize(int size) {
      resize_mdata(size);
      write();
   }

   std::string odb::print() {
      std::string s;
      s = "{\n";
      print(s, 1);
      s += "\n}";
      return s;
   }

   std::string odb::dump() {
      std::string s;
      s = "{\n";
      dump(s, 1);
      s += "\n}";
      return s;
   }

   // print current object with all sub-objects nicely indented
   void odb::print(std::string &s, int indent) {
      for (int i = 0; i < indent; i++)
         s += "   ";
      if (m_tid == TID_KEY) {
         s += "\"" + m_name + "\": {\n";
         for (int i = 0; i < m_num_values; i++) {
            std::string v;
            // recursive call
            m_data[i].get_odb().print(v, indent + 1);
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
         get(v, m_tid == TID_STRING || m_tid == TID_LINK);
         if (m_tid == TID_LINK)
            s += " -> ";
         s += v;
         if (m_num_values > 1)
            s += "]";
      }
   }

   // dump current object in the same way as odbedit saves as json
   void odb::dump(std::string &s, int indent) {
      for (int i = 0; i < indent; i++)
         s += "   ";
      if (m_tid == TID_KEY) {
         s += "\"" + m_name + "\": {\n";
         for (int i = 0; i < m_num_values; i++) {
            std::string v;
            m_data[i].get_odb().dump(v, indent + 1);
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
         db_get_link(m_hDB, m_hKey, &key);
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
         get(v, m_tid == TID_STRING || m_tid == TID_LINK);
         s += v;
         if (m_num_values > 1)
            s += "]";
      }
   }

   // check if key contains a certain subkey
   bool odb::is_subkey(std::string str) {
      if (m_tid != TID_KEY)
         return false;

      std::string first = str;
      std::string tail{};
      if (str.find('/') != std::string::npos) {
         first = str.substr(0, str.find('/'));
         tail = str.substr(str.find('/') + 1);
      }

      int i;
      for (i = 0; i < m_num_values; i++)
         if (m_data[i].get_odb().get_name() == first)
            break;
      if (i == m_num_values)
         return false;

      if (!tail.empty())
         return m_data[i].get_odb().is_subkey(tail);

      return true;
   }

   odb &odb::get_subkey(std::string str) {
      if (m_tid == 0) {
         if (is_auto_create()) {
            m_tid = TID_KEY;
            int status = db_create_key(m_hDB, 0, m_name.c_str(), m_tid);
            if (status != DB_SUCCESS && status != DB_CREATED && status != DB_KEY_EXIST)
               mthrow("Cannot create ODB key \"" + m_name + "\", status" + std::to_string(status));
            db_find_link(m_hDB, 0, m_name.c_str(), &m_hKey);
            if (m_debug)
               std::cout << "Created ODB key " + get_full_path() << std::endl;
            // strip path from name
            if (m_name.find_last_of('/') != std::string::npos)
               m_name = m_name.substr(m_name.find_last_of('/') + 1);
         } else
            mthrow("Invalid key \"" + m_name + "\" does not have subkeys");

      }
      if (m_tid != TID_KEY)
         mthrow("ODB key \"" + get_full_path() + "\" does not have subkeys");

      std::string first = str;
      std::string tail{};
      if (str.find('/') != std::string::npos) {
         first = str.substr(0, str.find('/'));
         tail = str.substr(str.find('/') + 1);
      }

      int i;
      for (i = 0; i < m_num_values; i++)
         if (m_data[i].get_odb().get_name() == first)
            break;
      if (i == m_num_values) {
         if (is_auto_create()) {
            if (m_num_values == 0) {
               m_num_values = 1;
               m_data = new u_odb[1]{};
               i = 0;
            } else {
               // resize array
               resize_mdata(m_num_values + 1);
               i = m_num_values - 1;
            }
            midas::odb *o = new midas::odb();
            m_data[i].set_tid(TID_KEY);
            m_data[i].set_parent(this);
            o->set_name(get_full_path() + "/" + str);
            o->set_tid(0); // tid is currently undefined
            o->set_flags(get_flags());
            o->set_parent(this);
            m_data[i].set(o);
         } else
            mthrow("ODB key \"" + get_full_path() + "\" does not contain subkey \"" + first + "\"");
      }
      if (!tail.empty())
         return m_data[i].get_odb().get_subkey(tail);

      return *m_data[i].get_podb();
   }

   // get number of subkeys in ODB, return number and vector of names
   int odb::get_subkeys(std::vector<std::string> &name) {
      if (m_tid != TID_KEY)
         return 0;
      if (m_hKey == 0)
         mthrow("get_subkeys called with invalid m_hKey for ODB key \"" + m_name + "\"");

      // count number of subkeys in ODB
      std::vector<HNDLE> hlist;
      int n = 0;
      for (int i = 0;; i++) {
         HNDLE h;
         int status = db_enum_link(m_hDB, m_hKey, i, &h);
         if (status != DB_SUCCESS)
            break;
         KEY key;
         db_get_link(m_hDB, h, &key);
         hlist.push_back(h);
         name.push_back(key.name);
         n = i + 1;
      }

      return n;
   }

   // obtain key definition from ODB and allocate local data array
   bool odb::read_key(std::string &path) {
      init_hdb();

      int status = db_find_link(m_hDB, 0, path.c_str(), &m_hKey);
      if (status != DB_SUCCESS)
         return false;

      KEY key;
      status = db_get_link(m_hDB, m_hKey, &key);
      if (status != DB_SUCCESS)
         mthrow("db_get_link for ODB key \"" + path +
                "\" failed with status " + std::to_string(status));

      // check for correct type if given as parameter
      if (m_tid > 0 && m_tid != (int) key.type)
         mthrow("ODB key \"" + get_full_path() +
                "\" has differnt type than specified");

      if (m_debug)
         std::cout << "Get definition for ODB key \"" + get_full_path() + "\"" << std::endl;

      m_tid = key.type;
      m_name = key.name;
      if (m_tid == TID_KEY) {

         // merge ODB keys with local keys
         for (int i = 0; i < m_num_values; i++) {
            std::string k(path);
            k += "/" + m_data[i].get_odb().get_name();
            HNDLE h;
            status = db_find_link(m_hDB, 0, k.c_str(), &h);
            if (status != DB_SUCCESS) {
               // if key does not exist in ODB write it
               m_data[i].get_odb().write_key(k, true);
               m_data[i].get_odb().write();
            } else {
               // check key type
               KEY key;
               status = db_get_link(m_hDB, h, &key);
               if (status != DB_SUCCESS)
                  mthrow("db_get_link for ODB key \"" + get_full_path() +
                         "\" failed with status " + std::to_string(status));
               if (m_data[i].get_odb().get_tid() != (int)key.type) {
                  // write key if different
                  m_data[i].get_odb().write_key(k, true);
                  m_data[i].get_odb().write();
               }
            }
         }

         // read back everything from ODB
         std::vector<std::string> name;
         m_num_values = get_subkeys(name);
         delete[] m_data;
         m_data = new midas::u_odb[m_num_values]{};
         for (int i = 0; i < m_num_values; i++) {
            std::string k(path);
            k += "/" + name[i];
            midas::odb *o = new midas::odb(k.c_str());
            o->set_parent(this);
            m_data[i].set_tid(TID_KEY);
            m_data[i].set_parent(this);
            m_data[i].set(o);
         }
      } else  {
         m_num_values = key.num_values;
         delete[] m_data;
         m_data = new midas::u_odb[m_num_values]{};
         for (int i = 0; i < m_num_values; i++) {
            m_data[i].set_tid(m_tid);
            m_data[i].set_parent(this);
         }
      }

      return true;
   }

   // create key in ODB if it does not exist, otherwise check key type
   bool odb::write_key(std::string &path, bool force_write) {
      int status = db_find_link(m_hDB, 0, path.c_str(), &m_hKey);
      if (status != DB_SUCCESS) {
         if (m_tid == 0) // auto-create subdir
            m_tid = TID_KEY;
         if (m_tid > 0 && m_tid < TID_LAST) {
            status = db_create_key(m_hDB, 0, path.c_str(), m_tid);
            if (status != DB_SUCCESS)
               mthrow("ODB key \"" + path + "\" cannot be created");
            status = db_find_link(m_hDB, 0, path.c_str(), &m_hKey);
            if (status != DB_SUCCESS)
               mthrow("ODB key \"" + path + "\" not found after creation");
            if (m_debug)
               std::cout << "Created ODB key " + get_full_path() << std::endl;
         } else
            mthrow("ODB key \"" + path + "\" cannot be found");
         return true;
      } else {
         KEY key;
         status = db_get_link(m_hDB, m_hKey, &key);
         if (status != DB_SUCCESS)
            mthrow("db_get_link for ODB key \"" + path +
                   "\" failed with status " + std::to_string(status));
         if (m_tid == 0)
            m_tid = key.type;

         // check for correct type
         if (m_tid > 0 && m_tid != (int) key.type) {
            if (force_write) {
               // delete and recreate key
               status = db_delete_key(m_hDB, m_hKey, false);
               if (status != DB_SUCCESS)
                  mthrow("db_delete_key for ODB key \"" + path +
                         "\" failed with status " + std::to_string(status));
               status = db_create_key(m_hDB, 0, path.c_str(), m_tid);
               if (status != DB_SUCCESS)
                  mthrow("ODB key \"" + path + "\" cannot be created");
               status = db_find_link(m_hDB, 0, path.c_str(), &m_hKey);
               if (status != DB_SUCCESS)
                  mthrow("ODB key \"" + path + "\" not found after creation");
               if (m_debug)
                  std::cout << "Re-created ODB key \"" + get_full_path() << "\" with different type" << std::endl;
            } else
               // abort
               mthrow("ODB key \"" + get_full_path() +
                      "\" has differnt type than specified");
         } else if (m_debug)
            std::cout << "Validated ODB key \"" + get_full_path() + "\"" << std::endl;

         return false;
      }
   }


   // retrieve data from ODB and assign it to this object
   void odb::read() {
      if (!is_connected_odb())
         return;

      // check if deleted
      if (is_deleted())
         mthrow("ODB key \"" + m_name + "\" cannot be pulled because it has been deleted");

      if (m_hKey == 0)
         return; // needed to print un-connected objects

      if (m_tid == 0)
         mthrow("Read of invalid ODB key \"" + m_name + "\"");

      int status{};
      if (m_tid == TID_STRING || m_tid == TID_LINK) {
         KEY key;
         db_get_link(m_hDB, m_hKey, &key);
         char *str = (char *) malloc(key.total_size);
         int size = key.total_size;
         if (m_tid == TID_LINK)
            status = db_get_link_data(m_hDB, m_hKey, str, &size, m_tid);
         else
            status = db_get_data(m_hDB, m_hKey, str, &size, m_tid);
         for (int i = 0; i < m_num_values; i++)
            m_data[i].set(str + i * key.item_size);
         free(str);
      } else if (m_tid == TID_KEY) {
         std::vector<std::string> name;
         int n = get_subkeys(name);
         if (n != m_num_values) {
            // if subdirs have changed, rebuild it
            delete[] m_data;
            m_num_values = n;
            m_data = new midas::u_odb[m_num_values]{};
            for (int i = 0; i < m_num_values; i++) {
               std::string k(get_full_path());
               k += "/" + name[i];
               midas::odb *o = new midas::odb(k.c_str());
               o->set_parent(this);
               m_data[i].set_tid(TID_KEY);
               m_data[i].set_parent(this);
               m_data[i].set(o);
            }
         }
         for (int i = 0; i < m_num_values; i++)
            m_data[i].get_odb().read();
         status = DB_SUCCESS;
      } else {
         // resize local array if number of values has changed
         KEY key;
         status = db_get_link(m_hDB, m_hKey, &key);
         if (key.num_values != m_num_values) {
            delete[] m_data;
            m_num_values = key.num_values;
            m_data = new midas::u_odb[m_num_values]{};
            for (int i = 0; i < m_num_values; i++) {
               m_data[i].set_tid(m_tid);
               m_data[i].set_parent(this);
            }
         }

         int size = rpc_tid_size(m_tid) * m_num_values;
         void *buffer = malloc(size);
         void *p = buffer;
         status = db_get_data(m_hDB, m_hKey, p, &size, m_tid);
         for (int i = 0; i < m_num_values; i++) {
            if (m_tid == TID_UINT8)
               m_data[i].set(*static_cast<uint8_t *>(p));
            else if (m_tid == TID_INT8)
               m_data[i].set(*static_cast<int8_t *>(p));
            else if (m_tid == TID_UINT16)
               m_data[i].set(*static_cast<uint16_t *>(p));
            else if (m_tid == TID_INT16)
               m_data[i].set(*static_cast<int16_t *>(p));
            else if (m_tid == TID_UINT32)
               m_data[i].set(*static_cast<uint32_t *>(p));
            else if (m_tid == TID_INT32)
               m_data[i].set(*static_cast<int32_t *>(p));
            else if (m_tid == TID_BOOL)
               m_data[i].set(*static_cast<bool *>(p));
            else if (m_tid == TID_FLOAT)
               m_data[i].set(*static_cast<float *>(p));
            else if (m_tid == TID_DOUBLE)
               m_data[i].set(*static_cast<double *>(p));
            else if (m_tid == TID_STRING)
               m_data[i].set(std::string(static_cast<const char *>(p)));
            else if (m_tid == TID_LINK)
               m_data[i].set(std::string(static_cast<const char *>(p)));
            else
               mthrow("Invalid type ID " + std::to_string(m_tid));

            p = static_cast<char *>(p) + rpc_tid_size(m_tid);
         }
         free(buffer);
      }

      if (status != DB_SUCCESS)
         mthrow("db_get_data for ODB key \"" + get_full_path() +
                "\" failed with status " + std::to_string(status));
      if (m_debug) {
         if (m_tid == TID_KEY) {
            std::cout << "Get ODB key \"" + get_full_path() + "[0..." +
                         std::to_string(m_num_values - 1) + "]\"" << std::endl;
         } else {
            std::string s;
            get(s, false, false);
            if (m_num_values > 1)
               std::cout << "Get ODB key \"" + get_full_path() + "[0..." +
                            std::to_string(m_num_values - 1) + "]\": [" + s + "]" << std::endl;
            else
               std::cout << "Get ODB key \"" + get_full_path() + "\": " + s << std::endl;
         }
      }
   }

   // retrieve individual member of array
   void odb::read(int index) {
      if (!is_connected_odb())
         return;

      if (m_hKey == 0)
         return; // needed to print un-connected objects

      if (m_tid == 0)
         mthrow("Pull of invalid ODB key \"" + m_name + "\"");

      int status{};
      if (m_tid == TID_STRING || m_tid == TID_LINK) {
         KEY key;
         db_get_link(m_hDB, m_hKey, &key);
         char *str = (char *) malloc(key.item_size);
         int size = key.item_size;
         status = db_get_data_index(m_hDB, m_hKey, str, &size, index, m_tid);
         m_data[index].set(str);
         free(str);
      } else if (m_tid == TID_KEY) {
         m_data[index].get_odb().read();
         status = DB_SUCCESS;
      } else {
         int size = rpc_tid_size(m_tid);
         void *buffer = malloc(size);
         void *p = buffer;
         status = db_get_data_index(m_hDB, m_hKey, p, &size, index, m_tid);
         if (m_tid == TID_UINT8)
            m_data[index].set(*static_cast<uint8_t *>(p));
         else if (m_tid == TID_INT8)
            m_data[index].set(*static_cast<int8_t *>(p));
         else if (m_tid == TID_UINT16)
            m_data[index].set(*static_cast<uint16_t *>(p));
         else if (m_tid == TID_INT16)
            m_data[index].set(*static_cast<int16_t *>(p));
         else if (m_tid == TID_UINT32)
            m_data[index].set(*static_cast<uint32_t *>(p));
         else if (m_tid == TID_INT32)
            m_data[index].set(*static_cast<int32_t *>(p));
         else if (m_tid == TID_BOOL)
            m_data[index].set(*static_cast<bool *>(p));
         else if (m_tid == TID_FLOAT)
            m_data[index].set(*static_cast<float *>(p));
         else if (m_tid == TID_DOUBLE)
            m_data[index].set(*static_cast<double *>(p));
         else if (m_tid == TID_STRING)
            m_data[index].set(std::string(static_cast<const char *>(p)));
         else if (m_tid == TID_LINK)
            m_data[index].set(std::string(static_cast<const char *>(p)));
         else
            mthrow("Invalid type ID " + std::to_string(m_tid));

         free(buffer);
      }

      if (status != DB_SUCCESS)
         mthrow("db_get_data for ODB key \"" + get_full_path() +
                "\" failed with status " + std::to_string(status));
      if (m_debug) {
         std::string s;
         m_data[index].get(s);
         std::cout << "Get ODB key \"" + get_full_path() + "[" +
                      std::to_string(index) + "]\": [" + s + "]" << std::endl;

      }
   }

   // push individual member of an array
   void odb::write(int index) {
      if (!is_connected_odb())
         return;

      if (m_hKey == 0) {
         if (is_auto_create()) {
            int status = db_create_key(m_hDB, 0, m_name.c_str(), m_tid);
            if (status != DB_SUCCESS && status != DB_CREATED && status != DB_KEY_EXIST)
               mthrow("Cannot create ODB key \"" + m_name + "\", status" + std::to_string(status));
            db_find_link(m_hDB, 0, m_name.c_str(), &m_hKey);
            if (m_debug)
               std::cout << "Created ODB key " + get_full_path() << std::endl;
            // strip path from name
            if (m_name.find_last_of('/') != std::string::npos)
               m_name = m_name.substr(m_name.find_last_of('/') + 1);
         } else
            mthrow("Write of un-connected ODB key \"" + m_name + "\" not possible");
      }

      // don't write keys
      if (m_tid == TID_KEY)
         return;

      int status{};
      if (m_tid == TID_STRING || m_tid == TID_LINK) {
         KEY key;
         db_get_link(m_hDB, m_hKey, &key);
         std::string s;
         m_data[index].get(s);
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
         if (m_tid == TID_BOOL) {
            BOOL b = static_cast<bool>(u); // "bool" is only 1 Byte, BOOL is 4 Bytes
            status = db_set_data_index(m_hDB, m_hKey, &b, rpc_tid_size(m_tid), index, m_tid);
         } else {
            status = db_set_data_index(m_hDB, m_hKey, &u, rpc_tid_size(m_tid), index, m_tid);
         }
         if (m_debug) {
            std::string s;
            u.get(s);
            if (m_num_values > 1)
               std::cout << "Set ODB key \"" + get_full_path() + "[" + std::to_string(index) + "]\" = " + s
                         << std::endl;
            else
               std::cout << "Set ODB key \"" + get_full_path() + "\" = " + s << std::endl;
         }
      }
      if (status != DB_SUCCESS)
         mthrow("db_set_data_index for ODB key \"" + get_full_path() +
                "\" failed with status " + std::to_string(status));
   }

   // write all members of an array to the ODB
   void odb::write() {

      // check if deleted
      if (is_deleted())
         mthrow("ODB key \"" + m_name + "\" cannot be written because it has been deleted");

      // write subkeys
      if (m_tid == TID_KEY) {
         for (int i = 0; i < m_num_values; i++)
            m_data[i].get_odb().write();
         return;
      }

      if (m_tid < 1 || m_tid >= TID_LAST)
         mthrow("Invalid TID for ODB key \"" + get_full_path() + "\"");

      if (m_hKey == 0 && !is_auto_create())
         mthrow("Writing ODB key \"" + m_name +
                "\" is not possible because of invalid key handle");

      // if index operator [] returned previously a certain index, write only this one
      if (m_last_index != -1) {
         write(m_last_index);
         m_last_index = -1;
         return;
      }

      if (m_num_values == 1) {
         write(0);
         return;
      }

      if (m_hKey == 0) {
         if (is_auto_create()) {
            int status = db_create_key(m_hDB, 0, m_name.c_str(), m_tid);
            if (status != DB_SUCCESS && status != DB_CREATED && status != DB_KEY_EXIST)
               mthrow("Cannot create ODB key \"" + m_name + "\", status" + std::to_string(status));
            db_find_link(m_hDB, 0, m_name.c_str(), &m_hKey);
            if (m_debug)
               std::cout << "Created ODB key " + get_full_path() << std::endl;
            // strip path from name
            if (m_name.find_last_of('/') != std::string::npos)
               m_name = m_name.substr(m_name.find_last_of('/') + 1);
         } else
            mthrow("Write of un-connected ODB key \"" + m_name + "\" not possible");
      }

      int status{};
      if (m_tid == TID_STRING || m_tid == TID_LINK) {
         if (is_preserve_string_size() || m_num_values > 1) {
            KEY key;
            db_get_link(m_hDB, m_hKey, &key);
            if (key.item_size == 0 || key.total_size == 0) {
               int size = 1;
               for (int i = 0; i < m_num_values; i++) {
                  std::string d;
                  m_data[i].get(d);
                  if ((int) d.size() + 1 > size)
                     size = d.size() + 1;
               }
               // round up to multiples of 32
               size = (((size - 1) / 32) + 1) * 32;
               key.item_size = size;
               key.total_size = size * m_num_values;
            }
            char *str = (char *) malloc(key.item_size * m_num_values);
            for (int i = 0; i < m_num_values; i++) {
               std::string d;
               m_data[i].get(d);
               strncpy(str + i * key.item_size, d.c_str(), key.item_size);
            }
            status = db_set_data(m_hDB, m_hKey, str, key.item_size * m_num_values, m_num_values, m_tid);
            free(str);
            if (m_debug) {
               std::string s;
               get(s, true, false);
               std::cout << "Set ODB key \"" + get_full_path() +
                            "[0..." + std::to_string(m_num_values - 1) + "]\" = [" + s + "]" << std::endl;
            }
         } else {
            std::string s;
            m_data[0].get(s);
            status = db_set_data(m_hDB, m_hKey, s.c_str(), s.length() + 1, 1, m_tid);
            if (m_debug)
               std::cout << "Set ODB key \"" + get_full_path() + "\" = " + s << std::endl;
         }
      } else {
         int size = rpc_tid_size(m_tid) * m_num_values;
         uint8_t *buffer = (uint8_t *) malloc(size);
         uint8_t *p = buffer;
         for (int i = 0; i < m_num_values; i++) {
            if (m_tid == TID_BOOL) {
               // bool has 1 Byte, BOOL has 4 Bytes
               BOOL b = static_cast<bool>(m_data[i]);
               memcpy(p, &b, rpc_tid_size(m_tid));
            } else {
               memcpy(p, &m_data[i], rpc_tid_size(m_tid));
            }
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
         mthrow("db_set_data for ODB key \"" + get_full_path() +
                "\" failed with status " + std::to_string(status));
   }

   // write function with separated path and key name
   void odb::connect(std::string path, std::string name, bool write_defaults) {
      init_hdb();

      if (!name.empty())
         m_name = name;
      path += "/" + m_name;

      HNDLE hKey;
      int status = db_find_link(m_hDB, 0, path.c_str(), &hKey);
      bool key_exists = (status == DB_SUCCESS);
      bool created = false;

      if (!key_exists || write_defaults)
         created = write_key(path, write_defaults);
      else
         read_key(path);

      // correct wrong parent ODB from initializer_list
      for (int i = 0; i < m_num_values; i++)
         m_data[i].set_parent(this);

      if (m_tid == TID_KEY) {
         for (int i = 0; i < m_num_values; i++)
            m_data[i].get_odb().connect(get_full_path(), m_data[i].get_odb().get_name(), write_defaults);
      } else if (created || write_defaults) {
         write();
      } else
         read();
   }

   // send key definitions and data with optional subkeys to certain path in ODB
   void odb::connect(std::string str, bool write_defaults) {
      if (str[0] != '/')
         mthrow("connect(\"" + str + "\"): path must start with leading \"/\"");

      std::string name;
      std::string path;
      if (str.find_last_of('/') == std::string::npos) {
         name = str;
         path = "";
      } else {
         name = str.substr(str.find_last_of('/') + 1);
         path = str.substr(0, str.find_last_of('/'));
      }

      connect(path, name, write_defaults);
   }

   void odb::delete_key() {
      init_hdb();

      // keep the name for debugging
      m_name = get_full_path();

      // delete key in ODB
      int status = db_delete_key(m_hDB, m_hKey, FALSE);
      if (status != DB_SUCCESS && status != DB_INVALID_HANDLE)
         mthrow("db_delete_key for ODB key \"" + get_full_path() +
                "\" returnd error code " + std::to_string(status));

      if (m_debug)
         std::cout << "Deleted ODB key \"" + m_name + "\"" << std::endl;

      // invalidate this object
      delete[] m_data;
      m_data = nullptr;
      m_num_values = 0;
      m_tid = 0;
      m_hKey = 0;

      // set flag that this object has been deleted
      set_deleted(true);
   }

   void odb::watch(std::function<void(midas::odb &)> f) {
      if (m_hKey == 0)
         mthrow("watch() called for ODB key \"" + m_name +
                "\" which is not connected to ODB");

      // create a deep copy of current object in case it
      // goes out of scope
      midas::odb* ow = new midas::odb(*this);

      ow->m_watch_callback = f;
      db_watch(m_hDB, m_hKey, midas::odb::watch_callback, ow);

      // put object into watchlist
      m_watchlist.push_back(ow);
   }

   void odb::unwatch()
   {
      for (int i=0 ; i<(int)m_watchlist.size() ; i++) {
         if (m_watchlist[i]->get_hkey() == this->get_hkey()) {
            db_unwatch(m_hDB, m_watchlist[i]->get_hkey());
            delete m_watchlist[i];
            m_watchlist.erase(m_watchlist.begin() + i);
            i--;
         }
      }
   }

   void odb::unwatch_all()
   {
      for (int i=0 ; i<(int)m_watchlist.size() ; i++) {
         db_unwatch(m_hDB, m_watchlist[i]->get_hkey());
         delete m_watchlist[i];
      }
      m_watchlist.clear();
   }

   //-----------------------------------------------

   //---- u_odb implementations calling functions from odb

   u_odb::~u_odb() {
      if (m_tid == TID_STRING || m_tid == TID_LINK)
         delete m_string;
      else if (m_tid == TID_KEY)
         delete m_odb;
   }

   // get function for strings
   void u_odb::get(std::string &s) {
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
      else if (m_tid == TID_LINK)
         s = *m_string;
      else if (m_tid == TID_KEY) {
         m_odb->print(s, 0);
      } else
         mthrow("Invalid type ID " + std::to_string(m_tid));
   }

   //---- u_odb assignment and arithmetic operators overloads which call odb::write()

   // overload assignment operators
   uint8_t u_odb::operator=(uint8_t v) {
      set(v);
      if (m_parent_odb)
         m_parent_odb->write();
      return v;
   }
   int8_t u_odb::operator=(int8_t v) {
      set(v);
      if (m_parent_odb)
         m_parent_odb->write();
      return v;
   }
   uint16_t u_odb::operator=(uint16_t v) {
      set(v);
      if (m_parent_odb)
         m_parent_odb->write();
      return v;
   }
   int16_t u_odb::operator=(int16_t v) {
      set(v);
      if (m_parent_odb)
         m_parent_odb->write();
      return v;
   }
   uint32_t u_odb::operator=(uint32_t v) {
      set(v);
      if (m_parent_odb)
         m_parent_odb->write();
      return v;
   }
   int32_t u_odb::operator=(int32_t v) {
      set(v);
      if (m_parent_odb)
         m_parent_odb->write();
      return v;
   }
   bool u_odb::operator=(bool v) {
      set(v);
      if (m_parent_odb)
         m_parent_odb->write();
      return v;
   }
   float u_odb::operator=(float v) {
      set(v);
      if (m_parent_odb)
         m_parent_odb->write();
      return v;
   }
   double u_odb::operator=(double v) {
      set(v);
      if (m_parent_odb)
         m_parent_odb->write();
      return v;
   }
   const char * u_odb::operator=(const char * v) {
      set(v);
      if (m_parent_odb)
         m_parent_odb->write();
      return v;
   }

   std::string * u_odb::operator=(std::string * v){
       set(*v);
       if (m_parent_odb)
          m_parent_odb->write();
       return v;

   }

   // overload all standard conversion operators
   u_odb::operator uint8_t() {
      m_parent_odb->set_last_index(-1);
      return get<uint8_t>();
   }
   u_odb::operator int8_t() {
         m_parent_odb->set_last_index(-1);
      return get<int8_t>();
   }
   u_odb::operator uint16_t() {
         m_parent_odb->set_last_index(-1);
      return get<uint16_t>();
   }
   u_odb::operator int16_t() {
         m_parent_odb->set_last_index(-1);
      return get<int16_t>();
   }
   u_odb::operator uint32_t() {
      m_parent_odb->set_last_index(-1);
      return get<uint32_t>();
   }
   u_odb::operator int32_t() {
      m_parent_odb->set_last_index(-1);
      return get<int32_t>();
   }
   u_odb::operator bool() {
      m_parent_odb->set_last_index(-1);
      return get<bool>();
   }
   u_odb::operator float() {
      m_parent_odb->set_last_index(-1);
      return get<float>();
   }
   u_odb::operator double() {
      m_parent_odb->set_last_index(-1);
      return get<double>();
   }
   u_odb::operator std::string() {
      m_parent_odb->set_last_index(-1);
      std::string s;
      get(s);
      return s;
   }
   u_odb::operator const char *() {
      m_parent_odb->set_last_index(-1);
      if (m_tid != TID_STRING && m_tid != TID_LINK)
         mthrow("Only ODB string keys can be converted to \"const char *\"");
      return m_string->c_str();
   }
   u_odb::operator midas::odb&() {
      m_parent_odb->set_last_index(-1);
      if (m_tid != TID_KEY)
         mthrow("Only ODB directories can be converted to \"midas::odb &\"");
      return *m_odb;
   }


   void u_odb::add(double inc, bool write) {
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
         m_float += static_cast<float>(inc);
      else if (m_tid == TID_DOUBLE)
         m_double += inc;
      else
         mthrow("Invalid arithmetic operation for ODB key \"" +
                m_parent_odb->get_full_path() + "\"");
      if (write)
         m_parent_odb->write();
   }

   void u_odb::mult(double f, bool write) {
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
         mthrow("Invalid operation for ODB key \"" +
                m_parent_odb->get_full_path() + "\"");
      if (write)
         m_parent_odb->write();
   }

}; // namespace midas
