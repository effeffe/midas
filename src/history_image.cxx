/********************************************************************\

  Name:         history_image.cxx
  Created by:   Stefan Ritt

  Contents:     Logger module saving images from webcams through
                network HTTP link into subdirectories. These images
                can then be retrived in the history page.

\********************************************************************/

#include <string>
#include <exception>
#include <sstream>
#include <iomanip>
#include <map>
#include <thread>

#include "midas.h"
#include "msystem.h"
#include "odbxx.hxx"

#ifdef HAVE_CURL
#include <curl/curl.h>

static size_t write_data(void *ptr, size_t size, size_t nmemb, void *stream)
{
   size_t written = fwrite(ptr, size, nmemb, (FILE *)stream);
   return written;
}

static std::vector<std::thread> _image_threads;
static bool stop_all_threads = false;

int mkpath(const char *dir, mode_t mode)
{
   struct stat sb;

   if (!dir) {
      errno = EINVAL;
      return 1;
   }

   if (!stat(dir, &sb))
      return 0;
   char *p = (char *)malloc(strlen(dir)+1);
   strncpy(p, dir, strlen(dir)+1);
   *strrchr(p, '/') = 0;
   mkpath(p, mode);
   free(p);

   return mkdir(dir, mode);
}

std::string history_dir() {
   static std::string dir;

   if (dir.empty()) {
      midas::odb l("/Logger");
      if (l.is_subkey("History dir"))
         dir = l["History dir"];
      else
         dir = l["Data dir"];
      if (dir.back() != '/')
         dir += "/";
   }
   return dir;
}

void image_thread(std::string name) {
   DWORD last_check_delete = 0;
   midas::odb o("/History/Images/"+name);

   do {
      std::this_thread::sleep_for(std::chrono::seconds(1));

      // check for old files
      if (ss_time() > last_check_delete + 60 && o["Storage hours"] > 0) {

         std::string path = history_dir();
         path += name;

         char *flist;
         int n = ss_file_find(path.c_str(), "??????_??????.*", &flist);
         for (int i=0 ; i<n ; i++) {
            char filename[MAX_STRING_LENGTH];
            strncpy(filename, flist+i*MAX_STRING_LENGTH, MAX_STRING_LENGTH);
            struct tm ti{};
            sscanf(filename, "%2d%2d%2d_%2d%2d%2d", &ti.tm_year, &ti.tm_mon,
                   &ti.tm_mday, &ti.tm_hour, &ti.tm_min, &ti.tm_sec);
            ti.tm_year += 100;
            ti.tm_mon -= 1;
            ti.tm_isdst = -1;
            time_t ft = mktime(&ti);
            if ((ss_time() - ft)/3600.0 >= o["Storage hours"]) {
               std::cout << "Delete file " << flist+i*MAX_STRING_LENGTH << " which is is " <<
                  (ss_time()-ft)/3600.0 << " hours old." << std::endl;
               int status = remove((path+"/"+filename).c_str());
               if (status)
                  cm_msg(MERROR, "image_thread", "Cannot remove file %s, status = %d", flist+i*MAX_STRING_LENGTH, status);
            }

         }
         free(flist);

         last_check_delete = ss_time();
      }

      if (!o["Enabled"])
         continue;

      if (ss_time() >= o["Last fetch"] + o["Period"]) {
         o["Last fetch"] = ss_time();
         std::string url = o["URL"];
         std::string filename = history_dir() + name;
         int status = mkpath(filename.c_str(), 0755);
         if (status)
            cm_msg(MERROR, "image_thread", "Cannot create directory \"%s\": %s", filename.c_str(), strerror(errno));

         time_t now = time(nullptr);
         tm *ltm = localtime(&now);
         std::stringstream s;
         s <<
           std::setfill('0') << std::setw(2) << ltm->tm_year - 100 <<
           std::setfill('0') << std::setw(2) << ltm->tm_mon + 1 <<
           std::setfill('0') << std::setw(2) << ltm->tm_mday <<
           "_" <<
           std::setfill('0') << std::setw(2) << ltm->tm_hour <<
           std::setfill('0') << std::setw(2) << ltm->tm_min <<
           std::setfill('0') << std::setw(2) << ltm->tm_sec;
         filename += "/" + s.str();

         if (o["Extension"] == std::string(""))
            filename += "/" + s.str() + url.substr(url.find_last_of('.'));
         else
            filename += o["Extension"];

         CURL *conn = curl_easy_init();
         curl_easy_setopt(conn, CURLOPT_URL, url.c_str());
         curl_easy_setopt(conn, CURLOPT_WRITEFUNCTION, write_data);
         curl_easy_setopt(conn, CURLOPT_VERBOSE, 0L);
         auto f = fopen(filename.c_str(), "wb");
         if (f) {
            curl_easy_setopt(conn, CURLOPT_WRITEDATA, f);
            curl_easy_setopt(conn, CURLOPT_TIMEOUT, 10L);
            int status = curl_easy_perform(conn);
            fclose(f);
            std::string error;
            if (status == CURLE_COULDNT_CONNECT) {
               error = "Cannot connect to camera \"" + name + "\" at " + url + ", please check camera power and URL";
            } else if (status != CURLE_OK) {
               error = "Error fetching image from camera \"" + name + "\", curl status " + std::to_string(status);
            } else {
               long http_code = 0;
               curl_easy_getinfo(conn, CURLINFO_RESPONSE_CODE, &http_code);
               if (http_code != 200)
                  error = "Error fetching image from camera \"" + name + "\", http error status " +
                          std::to_string(http_code);
            }
            if (!error.empty()) {
               if (ss_time() > o["Last error"] + o["Error interval (s)"]) {
                  cm_msg(MERROR, "log_image_history", "%s", error.c_str());
                  o["Last error"] = ss_time();
               }
            }

         }
         curl_easy_cleanup(conn);
      }

   } while (!stop_all_threads);
}

void stop_image_history() {
   stop_all_threads = true;
   for (auto &t : _image_threads)
      t.join();
   curl_global_cleanup();
}

void start_image_history() {

   static midas::odb h;

   curl_global_init(CURL_GLOBAL_DEFAULT);
   midas::odb::set_debug(false);

   try {
      if (!h.is_connected_odb())
         h.connect("/History/Images");
      if (h.get_num_values() == 0)
         throw std::runtime_error("");
   } catch (std::exception &e) {
      // create "Demo" image
      midas::odb::create("/History/Images/Demo", TID_KEY);
      h.connect("/History/Images");
   }

   // loop over all cameras
   for (auto &ic: h) {

      // write default values if not present (ODB has precedence)
      midas::odb c = {
              {"Name",               "Demo Camera"},
              {"Enabled",            false},
              {"URL",                "https://localhost:8000/image.jpg"},
              {"Extension",          ".jpg"},
              {"Period",             60},
              {"Last fetch",         0},
              {"Storage hours",      72},
              {"Error interval (s)", 60},
              {"Last error",         0}
      };
      c.connect(ic.get_odb().get_full_path());

      std::string name = ic.get_odb().get_name();
      _image_threads.push_back(std::thread(image_thread, name));
   }
}

#else // HAVE_CURL

// no history image logging wihtout CURL library
void start_image_history() {}
void stop_image_history() {}

#endif